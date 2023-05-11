// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#include <linux/bitfield.h>
#include <linux/crc32c.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/module.h>

#include "fw.h"
#include "iaf_drv.h"
#include "ops.h"
#include "mbdb.h"
#include "port.h"
#include "routing_engine.h"

/*
 * How many times to check FW_VERSION for completion of firmware initialization
 * before declaring failure (there is a 20 ms delay before each check).
 */
#define MAX_20MS_DELAYS (300)

#define MODULE_TYPE_CSS_GENERIC	(6)
#define HEADER_VER_RSA		(0x00010000)
#define MODULE_VENDOR_NAME	GENMASK(15, 0)
#define MODULE_VENDOR_INTEL	(0x8086)
#define MODULE_ID_DEBUG_SIGNED	BIT(31)
#define KEY_SIZE_SHA384		(96)
#define MODULUS_SIZE_SHA384	(96)
#define EXPONENT_SIZE_SHA384	(1)
#define CSS_DATE_YEAR		GENMASK(31, 16)
#define CSS_DATE_MONTH		GENMASK(15, 8)
#define CSS_DATE_DAY		GENMASK(7, 0)
#define PSC_TIME_HOUR		GENMASK(31, 16)
#define PSC_TIME_MINUTE		GENMASK(15, 8)
#define PSC_TIME_SECOND		GENMASK(7, 0)

struct fw_css_header {
	u32 module_type;	/* always 0x00000006 */
	u32 header_len;		/* always 0x000000e1 */
	u32 header_ver;		/* always 0x00010000 */
	u32 module_id;		/* bit 31 = debug signed */
	u32 module_vendor;	/* always 0x00008086 */
	u32 date;		/* format 0xYYYYMMDD */
	u32 size;		/* size in dwords */
	u32 key_size;		/* typical 0x00000060, must be valid */
	u32 modulus_size;	/* typical 0x00000060, must be valid */
	u32 exponent_size;	/* typical 0x00000001, don't care */
	u32 reserved[22];
	u8 modulus[384];
	u8 exponent[4];
	u8 signature[384];
};

#define FW_MAX_SEGMENTS	(3)

#define BASE_LOAD_ADDRESS	(0xaffe0000)

struct fw_segment {
	u32 start;
	u32 end;
};

struct fw_module_header {
	u32 init_addr;
	struct fw_segment segment[FW_MAX_SEGMENTS];
	u32 fw_version;
	u32 fw_build;
};

#define PSCBIN_MAGIC_NUMBER (0x42435350)
#define MAX_INIS (32 * 2)

enum {
	PSCBIN_VERSION_RAW_V1 = 0,
	PSCBIN_VERSION_V2
};

enum {
	FORM_FACTOR_PCI = 0,
	FORM_FACTOR_OAM,
	FORM_FACTOR_A21,
	FORM_FACTOR_END
};

/*
 * FW image
 */
static DEFINE_MUTEX(fw_image_lock);

static const char *fw_image = "i915/pvc_iaf_ver1.bin";

static int fw_image_set(const char *val, const struct kernel_param *kp)
{
	int err;

	mutex_lock(&fw_image_lock);
	err = param_set_charp(val, kp);
	mutex_unlock(&fw_image_lock);

	return err;
}

static struct kernel_param_ops fw_image_ops = {
	.set = fw_image_set,
	.get = param_get_charp,
};

module_param_cb(fw_image, &fw_image_ops, &fw_image, 0600);
MODULE_PARM_DESC(fw_image, "Retrieve FW image from specified FW file");

/*
 * Index/size in psc_data.data[] array, in u8s
 */
struct psc_item {
	u32 idx;
	u32 size;
};

/*
 * magic == PSCBIN_MAGIC_NUMBER
 * psc_format_version defines format of this structure
 * form_factor is the type of the product (PCI, OAM, A21)
 * cfg_version should only increment to prevent unintentional downgrades
 * date is UTC generation date in BCD (YYYYMMDD)
 * time is UTC generation time in BCD (HHMMSS)
 * data_size is size of data[]
 * brand_name is provided by the OAM
 * product_name shouldn't change when a new PSCBIN is burned
 * ini_bin is binary ini to provide to FW (multiple entries can refer to data)
 * ini_name identifies ini_bin file used (multiple entries can refer to data)
 * crc_data should be the last item and protects all previous items in data[].
 * csc_data.size == 4 for CRC32C
 * crc32c_hdr protects all other header fields
 */
struct psc_data {
	u32 magic;
	u32 psc_format_version;
	u32 form_factor;
	u32 cfg_version;
	u32 date;
	u32 time;
	u32 data_size;
	struct psc_item brand_name;
	struct psc_item product_name;
	struct psc_item comment;
	struct psc_item ini_name[MAX_INIS];
	struct psc_item ini_bin[MAX_INIS];
	struct psc_item crc_data;
	u32 crc32c_hdr;
	u8 data[];
};

/*
 * PSC-related parameters
 */

/* Protects PSC file override parameter */
static DEFINE_MUTEX(psc_file_override_lock);
static const char *psc_file_override;

static int psc_file_override_set(const char *val, const struct kernel_param *kp)
{
	int err;

	mutex_lock(&psc_file_override_lock);
	err = param_set_charp(val, kp);
	mutex_unlock(&psc_file_override_lock);

	return err;
}

static struct kernel_param_ops psc_file_override_ops = {
	.set = psc_file_override_set,
	.get = param_get_charp,
};

module_param_cb(psc_file_override, &psc_file_override_ops, &psc_file_override,
		0600);
MODULE_PARM_DESC(psc_file_override,
		 "Retrieve PSC data from specified FW file instead of MEI");

static unsigned short mei_timeout = 5 * 60;
module_param(mei_timeout, ushort, 0400);
MODULE_PARM_DESC(mei_timeout,
		 "Seconds to wait for MEI before warning (default: 5 minutes)");

/*
 * Workqueue and timer support functions
 */
static void wrk_continue_load_and_init_dev(struct work_struct *work);
static void wrk_load_and_init_subdev(struct work_struct *work);
static void queue_load_and_init_all_subdevs(struct fdev *dev);
static void continuation_timeout(struct timer_list *timer);

static void report_fw_info(struct fsubdev *sd,
			   struct mbdb_op_fw_version_rsp *fw_version,
			   const char *when)
{
	sd_info(sd, "ROM/Firmware Version Info: (%s)\n", when);
	sd_info(sd, "    MBox Version:  %d\n", fw_version->mbox_version);
	sd_info(sd, "    Environment:   %s%s\n",
		(fw_version->environment & FW_VERSION_ENV_BIT) ? "run-time" :
								 "bootloader",
		(fw_version->environment & FW_VERSION_INIT_BIT) ? ", ready" :
								  "");
	sd_info(sd, "    FW Version:    %s\n", fw_version->fw_version_string);
	sd_info(sd, "    OPs supported: 0x%llx%016llx%016llx%016llx\n",
		fw_version->supported_opcodes[3],
		fw_version->supported_opcodes[2],
		fw_version->supported_opcodes[1],
		fw_version->supported_opcodes[0]);
}

/*
 * Here temporarily to indicate FW has new capabilities, so that we know that
 * the driver is unaware of them.
 *
 */
static void report_extra_opcodes(struct fsubdev *sd,
				 struct mbdb_op_fw_version_rsp *fw_version,
				 u64 needed[4])
{
	u64 extra[4];
	int i;

	for (i = 0; i < 4; ++i)
		extra[i] = fw_version->supported_opcodes[i] & ~needed[i];

	sd_info(sd, "  Extra OPs found: 0x%llx%016llx%016llx%016llx\n",
		extra[3], extra[2], extra[1], extra[0]);
}

/* convert BCD-coded unsigned number to decimal */
static u32 decode_bcd(u32 bcd_value)
{
	u32 value = 0;
	u32 place;
	u32 digit;

	for (place = 1; bcd_value; bcd_value >>= 4, place *= 10) {
		digit = bcd_value & 0xf;
		if (digit > 9)
			return -EINVAL;
		value += digit * place;
	}

	return value;
}

/* Leap year every 4 years unless year is divisible by 100 and not 400 */
static bool leap_year(u32 year)
{
	if (year % 4 != 0)
		return false;
	else if ((year % 100 == 0) || (year % 400 != 0))
		return false;
	else
		return true;
}

/* Verify that date is of form 0xYYYYMMDD */
static bool valid_bcd_date(u32 date)
{
	u32 month;
	u32 year;
	u32 day;

	year = decode_bcd(FIELD_GET(CSS_DATE_YEAR, date));
	month = decode_bcd(FIELD_GET(CSS_DATE_MONTH, date));
	day = decode_bcd(FIELD_GET(CSS_DATE_DAY, date));

	if (day < 1 || year < 2019)
		return false;

	switch (month) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		if (day > 31)
			return false;
		break;

	case 4:
	case 6:
	case 9:
	case 11:
		if (day > 30)
			return false;
		break;

	case 2:
		if (day > 29 || (day == 29 && !leap_year(year)))
			return false;
		break;

	default:
		return false;
	}

	return true;
}

/* Leap seconds occur at the end of the UTC day */
static bool leap_second(u32 hour, u32 minute, u32 second)
{
	return hour == 23 && minute == 59 && second == 60;
}

/* Verify that date is of form 0xHHMMSS */
static bool valid_bcd_time(u32 time)
{
	u32 second;
	u32 minute;
	u32 hour;

	hour = decode_bcd(FIELD_GET(PSC_TIME_HOUR, time));
	minute = decode_bcd(FIELD_GET(PSC_TIME_MINUTE, time));
	second = decode_bcd(FIELD_GET(PSC_TIME_SECOND, time));

	return hour < 24 && minute < 60 && (second < 60 ||
					    leap_second(hour, minute, second));
}

static int verify_fw(struct fdev *dev)
{
	struct fw_module_header *mod_hdr;
	struct fw_css_header *css_hdr;
	u8 *segment_data_end;
	u8 *segment_data;
	const u8 *data;
	size_t size;
	int i;

	dev_dbg(fdev_dev(dev), "Checking firmware header\n");

	data = dev->fw->data;
	size = dev->fw->size;

	css_hdr = (struct fw_css_header *)data;
	mod_hdr = (struct fw_module_header *)(css_hdr + 1);

	segment_data = (u8 *)(mod_hdr + 1);
	segment_data_end = segment_data;

	if (data + size < segment_data) {
		dev_err(fdev_dev(dev), "Incomplete firmware header\n");
		return -EINVAL;
	}

	if (css_hdr->module_type != MODULE_TYPE_CSS_GENERIC ||
	    css_hdr->header_ver != HEADER_VER_RSA ||
	    FIELD_GET(MODULE_VENDOR_NAME, css_hdr->module_vendor) !=
	     MODULE_VENDOR_INTEL) {
		dev_err(fdev_dev(dev),
			"Illegal firmware type/version/vendor: %d/0x%x/0x%x\n",
			css_hdr->module_type, css_hdr->header_ver,
			css_hdr->module_vendor);
		return -EINVAL;
	}

	for (i = 0; i < FW_MAX_SEGMENTS; ++i)
		if (mod_hdr->segment[i].end > mod_hdr->segment[i].start)
			segment_data_end += (mod_hdr->segment[i].end -
					     mod_hdr->segment[i].start);

	if (data + size < segment_data_end) {
		dev_err(fdev_dev(dev), "Incomplete fw data\n");
		return -EINVAL;
	}

	if (css_hdr->header_len * sizeof(u32) != sizeof(*css_hdr) ||
	    data + css_hdr->size * sizeof(u32) != segment_data_end ||
	    css_hdr->key_size != KEY_SIZE_SHA384 ||
	    css_hdr->modulus_size != MODULUS_SIZE_SHA384 ||
	    css_hdr->exponent_size != EXPONENT_SIZE_SHA384)
		dev_warn(fdev_dev(dev),
			 "Mismatched size information in fw header\n");

	dev_info(fdev_dev(dev), "Firmware available, dated: %08x\n",
		 css_hdr->date);

	if (!valid_bcd_date(css_hdr->date))
		dev_warn(fdev_dev(dev),
			 "Invalid date format in firmware header\n");

	if (css_hdr->module_id & MODULE_ID_DEBUG_SIGNED)
		dev_info(fdev_dev(dev), "Firmware is debug signed\n");

	return 0;
}

static int prepare_to_initialize_subdevs(struct fdev *dev)
{
	int err;
	int i;

	INIT_WORK(&dev->psc_work, wrk_continue_load_and_init_dev);
	timer_setup(&dev->continuation_timer, continuation_timeout, 0);

	atomic_set(&dev->fwinit_refcnt, dev->pd->sd_cnt);

	for (i = 0; i < dev->pd->sd_cnt; ++i)
		INIT_WORK(&dev->sd[i].fw_work, wrk_load_and_init_subdev);

	mutex_lock(&fw_image_lock);
	dev->fw_name = fw_image;
	mutex_unlock(&fw_image_lock);

	dev_dbg(fdev_dev(dev), "Reading firmware file: %s\n", dev->fw_name);

	err = request_firmware(&dev->fw, dev->fw_name, fdev_dev(dev));
	if (err) {
		dev_err(fdev_dev(dev), "Could not open fw file\n");
		goto end;
	}

	err = verify_fw(dev);

end:
	return err;
}

static int copy_fw_to_device(struct fsubdev *sd, const u8 *data, size_t size)
{
	struct fw_module_header *mod_hdr;
	struct fw_css_header *css_hdr;
	u32 current_load_address;
	u8 *segment_data;
	size_t len;
	int err;
	int i;

	sd_info(sd, "Downloading firmware\n");

	/* verify_fw() ensures that this data is all in the FW image */

	css_hdr = (struct fw_css_header *)data;
	mod_hdr = (struct fw_module_header *)(css_hdr + 1);
	segment_data = (u8 *)(mod_hdr + 1);

	current_load_address = BASE_LOAD_ADDRESS;

	sd_dbg(sd, "CSR_RAW_WR address=0x%08x len=%lu\n", current_load_address,
	       sizeof(*css_hdr));

	err = ops_mem_posted_wr(sd, current_load_address, (u8 *)css_hdr,
				sizeof(*css_hdr));
	if (err) {
		sd_err(sd, "Could not write CSS hdr\n");
		return err;
	}

	current_load_address += sizeof(*css_hdr);

	sd_dbg(sd, "CSR_RAW_WR address=0x%08x len=%lu\n", current_load_address,
	       sizeof(*mod_hdr));

	err = ops_mem_posted_wr(sd, current_load_address, (u8 *)mod_hdr,
				sizeof(*mod_hdr));
	if (err) {
		sd_err(sd, "Could not write module hdr\n");
		return err;
	}

	current_load_address += sizeof(*mod_hdr);
	for (i = 0; i < FW_MAX_SEGMENTS; ++i)
		if (mod_hdr->segment[i].end > mod_hdr->segment[i].start) {
			len = (mod_hdr->segment[i].end -
			       mod_hdr->segment[i].start);

			sd_dbg(sd, "CSR_RAW_WR address=0x%08x len=%lu\n",
			       mod_hdr->segment[i].start, len);

			err = ops_mem_posted_wr(sd, mod_hdr->segment[i].start,
						segment_data, len);
			if (err) {
				sd_err(sd, "Could not write segment %d\n", i);
				return err;
			}
			segment_data += len;
		}

	return err;
}

static u64 load_fw_opcodes[4] = {
	/* bits 0-63 */
	(BIT_ULL_MASK(MBOX_OP_CODE_FW_VERSION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CSR_RAW_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CSR_RAW_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_FW_START)),
	/* bits 128-255 */
	0, 0, 0
};

static bool has_all_opcodes(u64 supported[4], u64 needed[4])
{
	int i;

	for (i = 0; i < 4; ++i)
		if ((supported[i] & needed[i]) != needed[i])
			return false;

	return true;
}

static bool has_extra_opcodes(u64 supported[4], u64 needed[4])
{
	int i;

	for (i = 0; i < 4; ++i)
		if (supported[i] & ~needed[i])
			return true;

	return false;
}

static int load_fw(struct fsubdev *sd)
{
	struct mbdb_op_fw_version_rsp fw_version;
	int err;

	/*
	 * Make sure mailbox seq_nos are synced between KMD and ANR
	 * If a good response is received, the driver will check that it is
	 * communicating with the bootrom and take appropriate actions if not
	 */
	err = ops_fw_version(sd, &fw_version, NULL, NULL);
	if (err == MBOX_RSP_STATUS_SEQ_NO_ERROR)
		err = ops_fw_version(sd, &fw_version, NULL, NULL);

	if (err) {
		sd_err(sd, "unable to query firmware version\n");
		goto load_failed;
	}

	report_fw_info(sd, &fw_version, "at boot");

	if (fw_version.environment & FW_VERSION_ENV_BIT) {
		int cnt;

		/*
		 * Found runtime FW, expecting boot loader
		 */

		sd_info(sd, "runtime firmware detected at boot\n");

		if (!(fw_version.supported_opcodes
		      [BIT_ULL_WORD(MBOX_OP_CODE_RESET)] &
		      BIT_ULL_MASK(MBOX_OP_CODE_RESET))) {
			sd_err(sd, "firmware RESET opcode not supported\n");
			err = -EPERM;
			goto load_failed;
		}

		sd_info(sd, "resetting to boot loader\n");

		err = ops_reset(sd, NULL, NULL, false);
		if (err)
			goto load_failed;

		cnt = 1;
		do {
			msleep(20);
			err = ops_fw_version(sd, &fw_version, NULL, NULL);
		} while (!err &&
			 fw_version.environment & FW_VERSION_ENV_BIT &&
			 cnt++ < MAX_20MS_DELAYS);

		if (err) {
			sd_err(sd, "error checking FW version\n");
			goto load_failed;
		}

		report_fw_info(sd, &fw_version, "after RESET");
	}

	if (fw_version.environment & FW_VERSION_ENV_BIT) {
		sd_err(sd, "unable to reset to boot loader\n");
		goto load_failed;
	}

	if (!has_all_opcodes(fw_version.supported_opcodes, load_fw_opcodes)) {
		sd_err(sd, "FW LOAD opcodes not supported\n");
		err = -EPERM;
		goto load_failed;
	}

	if (has_extra_opcodes(fw_version.supported_opcodes, load_fw_opcodes))
		report_extra_opcodes(sd, &fw_version, load_fw_opcodes);

	/*
	 * This shouldn't be possible
	 */
	if (WARN_ON(!sd->fdev->fw)) {
		err = -ENOENT;
		goto load_failed;
	}

	err = copy_fw_to_device(sd, sd->fdev->fw->data, sd->fdev->fw->size);
	if (err) {
		sd_err(sd, "error copying firmware to device\n");
		goto load_failed;
	}

	return 0;

load_failed:

	sd_err(sd, "could not load firmware\n");
	return err;
}

static u64 initial_runtime_opcodes[4] = {
	/* bits 0-63 */
	(BIT_ULL_MASK(MBOX_OP_CODE_FW_VERSION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CSR_RAW_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CSR_RAW_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_WALLOC) |
	 BIT_ULL_MASK(MBOX_OP_CODE_FREE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_TABLE_LOAD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_LOADED_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_NODE_GUID_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LINK_STATE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_MAJOR_PHYSICAL_STATE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_CSR_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_CSR_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_RESET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_ASIC_TEMP_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_PORT_TYPE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_POWER_ALLOCATED_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_POWER_USED_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_ARRAY_ADDRESS_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_MAX_DETECTED_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_NOTIFICATION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ACKNOWLEDGE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULTED_FIRST) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_FAULTED) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_PRESENT) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_READ) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_WRITE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATES_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_BEACON_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_BEACON_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_NOTIFICATION
		      ) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ACKNOWLEDGE
		      ) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_SHUTDOWN_ALL_PORTS) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_PORT_TABLE_FIELD_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_NOOP) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CALL_ROUTINE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SYSREG_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SYSREG_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_PORT_TABLE_FIELD_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_TABLES_ADDR_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_TASK_START) |
	 BIT_ULL_MASK(MBOX_OP_CODE_UTIL_PROC_CALL_ADDRESS_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FORCE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_TRACE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PACKET_BUFFER_INFO) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SWITCHINFO_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SWITCHINFO_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTINFO_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTINFO_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTSTATEINFO_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTSTATEINFO_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_RPIPE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_RPIPE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CLEAR_PORT_STATUS) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CLEAR_PORT_ERRORINFO) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_GET)),
	/* bits 64-127 */
	(BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_NOTIFICATION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ACKNOWLEDGE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_NOTIFICATION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ACKNOWLEDGE)),
	/* bits 128-255 */
	0, 0
};

static int start_fw(struct fsubdev *sd)
{
	struct mbdb_op_fw_version_rsp fw_version;
	int err;

	sd_info(sd, "Starting firmware\n");

	err = ops_fw_start(sd, NULL, NULL);
	if (err)
		goto start_failed;

	err = ops_fw_version(sd, &fw_version, NULL, NULL);
	if (err)
		goto start_failed;

	report_fw_info(sd, &fw_version, "after firmware start");

	if (!has_all_opcodes(fw_version.supported_opcodes,
			     initial_runtime_opcodes)) {
		sd_err(sd, "Required FW opcodes not supported\n");
		err = -EPERM;
		goto start_failed;
	}

	if (has_extra_opcodes(fw_version.supported_opcodes,
			      initial_runtime_opcodes)) {
		report_extra_opcodes(sd, &fw_version, initial_runtime_opcodes);
	}

	return 0;

start_failed:
	sd_err(sd, "could not start firmware\n");
	return err;
}

static int copy_ini_to_device(struct fsubdev *sd, const u8 *data, size_t size)
{
	u32 *dword = (u32 *)data;
	u32 *end = (u32 *)(data + size);
	u32 cnt;
	u32 *crc;
	int err;
	struct mbdb_op_ini_table_load_req ini;
	u32 result;

	sd_info(sd, "Loading INI file, size=%ld\n", size);

	while (dword + 2 < end) {
		ini.header1 = *dword++;
		ini.header2 = *dword++;

		sd_dbg(sd,
		       "HDR1/HDR2=0x%08x/0x%08x cnt=%d, left=%ld, index=%d, type=%d\n",
		       ini.header1, ini.header2, (ini.header1 >> 16) & 0xfff,
		       end - dword, ini.header1 & 0x3f, ini.header1 >> 28);

		if (ini.header1 != ~ini.header2) {
			sd_err(sd, "Invalid ini hdr\n");
			return -EINVAL;
		}
		cnt = (ini.header1 >> 16) & 0xfff;
		crc = dword + cnt;
		if (crc >= end) {
			sd_err(sd, "Incomplete ini block\n");
			return -EINVAL;
		}

		sd_dbg(sd, "WALLOC dwords=0x%x\n", cnt);
		err = ops_walloc(sd, cnt, &ini.address, NULL, NULL);
		if (err)
			return err;

		sd_dbg(sd, "CSR_RAW_WR address=ADDR len=%lu\n",
		       cnt * sizeof(u32));

		err = ops_mem_posted_wr(sd, ini.address, (u8 *)dword,
					cnt * sizeof(u32));
		if (err)
			return err;

		sd_dbg(sd, "INI_TABLE_LOAD header=0x%08x crc=0x%08x\n",
		       ini.header1, *crc);

		ini.crc = *crc;

		err = ops_ini_table_load(sd, &ini, &result, NULL, NULL);
		if (err)
			return err;

		sd_dbg(sd, "ini_load returned 0x%x\n", result);

		dword = (crc + 1);
	}

	sd_info(sd, "INI_LOADED_SET\n");
	err = ops_ini_loaded_set(sd, NULL, NULL, false);
	if (err)
		return err;

	sd_dbg(sd, "ini load complete\n");

	return err;
}

static bool valid_ini_file(const u8 *data, size_t size)
{
	/* Verify that ini file consists of a set of valid sections */
	return true;
}

static int validate_psc(struct fdev *dev)
{
	const struct psc_data *psc = (const struct psc_data *)dev->psc_data;
	u32 *data_crc;
	u32 crc;
	u8 i;

	if (dev->psc_size < sizeof(*psc) || !psc) {
		dev_err(fdev_dev(dev), "No PSC header found\n");
		return -ENODATA;
	}

	if (psc->magic != PSCBIN_MAGIC_NUMBER) {
		dev_err(fdev_dev(dev), "Invalid PSC header found\n");
		return -ENOENT;
	}

	if (psc->psc_format_version != PSCBIN_VERSION_V2) {
		dev_err(fdev_dev(dev), "Unsupported PSC version %u detected\n",
			psc->psc_format_version);
		return -ENOENT;
	}

	if (psc->data_size + sizeof(struct psc_data) < dev->psc_size) {
		dev_err(fdev_dev(dev), "PSC data incomplete\n");
		return -ENOENT;
	}

	/* Check header CRC */
	crc = crc32c(0, psc, offsetof(struct psc_data, crc32c_hdr));
	if (crc != psc->crc32c_hdr) {
		dev_err(fdev_dev(dev), "PSC header CRC mismatch\n");
		return -ENOENT;
	}

	/* Check data CRC */
	if (psc->crc_data.size != sizeof(u32) ||
	    psc->crc_data.idx + psc->crc_data.size > psc->data_size) {
		dev_err(fdev_dev(dev), "PSC data CRC not found\n");
		return -ENOENT;
	}

	data_crc = (u32 *)&psc->data[psc->crc_data.idx];
	crc = crc32c(0, psc->data, psc->crc_data.idx);
	if (crc != *data_crc) {
		dev_err(fdev_dev(dev), "PSC data CRC mismatch\n");
		return -ENOENT;
	}

	/*
	 * validate PSC fields
	 */

	if (psc->form_factor >= FORM_FACTOR_END) {
		dev_err(fdev_dev(dev), "PSC invalid form factor specified\n");
		return -ENOENT;
	}

	if (!valid_bcd_date(psc->date))
		dev_warn(fdev_dev(dev), "PSC invalid date format\n");

	if (!valid_bcd_time(psc->time))
		dev_warn(fdev_dev(dev), "PSC invalid time format\n");

	dev_info(fdev_dev(dev),
		 "PSC version %u : %04x/%02x/%02x %02x:%02x:%02x\n",
		 psc->cfg_version, psc->date >> 16, psc->date >> 8 & 0xff,
		 psc->date & 0xff, psc->time >> 16, psc->time >> 8 & 0xff,
		 psc->time & 0xff);

	/*
	 * possibly should display brand/product/comment here, though it
	 * requires dealing with a non-null-terminated string
	 */

	if (psc->brand_name.idx + psc->brand_name.size > psc->data_size)
		dev_warn(fdev_dev(dev), "PSC invalid brand data\n");

	if (psc->product_name.idx + psc->product_name.size > psc->data_size)
		dev_warn(fdev_dev(dev), "PSC invalid product data\n");

	if (psc->comment.idx + psc->comment.size > psc->data_size)
		dev_warn(fdev_dev(dev), "PSC invalid comment data\n");

	for (i = 0; i < dev->pd->sd_cnt; ++i) {
		u8 n = dev->pd->socket_id + i;
		const u8 *data;
		size_t size;

		if (psc->ini_name[n].idx + psc->ini_name[n].size >
		    psc->data_size)
			dev_warn(fdev_dev(dev), "PSC invalid ini name %u\n", n);

		data = &psc->data[psc->ini_bin[n].idx];
		size = psc->ini_bin[n].size;

		if (!size) {
			dev_err(fdev_dev(dev), "PSC missing inibin[%u]", n);
			return -ENODATA;
		}

		if (psc->ini_bin[n].idx + size > psc->data_size) {
			dev_err(fdev_dev(dev),
				"PSC inibin[%u] outside of PSC data", n);
			return -EFBIG;
		}

		if (!valid_ini_file(data, size)) {
			dev_err(fdev_dev(dev), "PSC inibin[%u] invalid", n);
			return -ENOENT;
		}
	}

	return 0;
}

static int extract_ini(const void *psc_data, size_t psc_size, int socket,
		       int subdev, const u8 **ini_data, size_t *ini_size)
{
	const struct psc_data *psc = (const struct psc_data *)psc_data;
	int ini_no = socket + subdev;

	*ini_size = psc->ini_bin[ini_no].size;
	*ini_data = &psc->data[psc->ini_bin[ini_no].idx];

	/* Should have been checked during earlier validation */
	if (psc->ini_bin[ini_no].idx + psc->ini_bin[ini_no].size > psc_size)
		return -EFBIG;

	return 0;
}

static u64 final_runtime_opcodes[4] = {
	/* bits 0-63 */
	(BIT_ULL_MASK(MBOX_OP_CODE_FW_VERSION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CSR_RAW_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CSR_RAW_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_WALLOC) |
	 BIT_ULL_MASK(MBOX_OP_CODE_FREE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_TABLE_LOAD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_LOADED_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_NODE_GUID_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LINK_STATE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_MAJOR_PHYSICAL_STATE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_CSR_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_CSR_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_RESET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_ASIC_TEMP_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_PORT_TYPE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_POWER_ALLOCATED_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_POWER_USED_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_ARRAY_ADDRESS_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_TEMPERATURE_MAX_DETECTED_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_NOTIFICATION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_ACKNOWLEDGE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FAULTED_FIRST) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_FAULTED) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_PRESENT) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_READ) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_WRITE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATES_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_BEACON_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_BEACON_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_NOTIFICATION
		      ) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_ACKNOWLEDGE
		      ) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_SHUTDOWN_ALL_PORTS) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_PORT_TABLE_FIELD_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_NOOP) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CALL_ROUTINE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SYSREG_RD) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SYSREG_WR) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_SYSTEM_TABLE_FIELD_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_PORT_TABLE_FIELD_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_INI_TABLES_ADDR_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_TASK_START) |
	 BIT_ULL_MASK(MBOX_OP_CODE_UTIL_PROC_CALL_ADDRESS_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_QSFP_MGR_FORCE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_DIAGNOSTIC_MODE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_TRACE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PACKET_BUFFER_INFO) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SWITCHINFO_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_SWITCHINFO_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTINFO_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTINFO_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTSTATEINFO_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_PORTSTATEINFO_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_RPIPE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_RPIPE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CLEAR_PORT_STATUS) |
	 BIT_ULL_MASK(MBOX_OP_CODE_CLEAR_PORT_ERRORINFO) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_GET)),
	/* bits 64-127 */
	(BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_NOTIFICATION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_ACKNOWLEDGE) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_GET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ENABLE_SET) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_NOTIFICATION) |
	 BIT_ULL_MASK(MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_ACKNOWLEDGE)),
	/* bits 128-255 */
	0, 0
};

static int init_fw(struct fsubdev *sd)
{
	struct mbdb_op_fw_version_rsp fw_version;
	int idx = sd - sd->fdev->sd;
	const u8 *data = NULL;
	size_t size = 0;
	int cnt;
	int err;

	err = extract_ini(sd->fdev->psc_data, sd->fdev->psc_size,
			  sd->fdev->pd->socket_id, idx, &data, &size);

	if (err)
		goto init_failed;

	if (!data || !size)
		goto no_data;

	err = copy_ini_to_device(sd, data, size);
	if (err)
		goto init_failed;

	/*
	 * FW documentation indicates to wait 20 ms, then verify that the INIT
	 * bit reported by FW_VERSION is set. Allow for slightly longer delays
	 * in case the FW is somehow busy.
	 */
	cnt = 1;
	do {
		msleep(20);
		err = ops_fw_version(sd, &fw_version, NULL, NULL);
	} while (!err &&
		 !(fw_version.environment & FW_VERSION_INIT_BIT) &&
		 cnt++ < MAX_20MS_DELAYS);

	if (err)
		goto init_failed;

	sd_info(sd, "FW init took at least %d ms\n", 20 * cnt);

	if (!(fw_version.environment & FW_VERSION_INIT_BIT)) {
		sd_err(sd, "FW never finished initializing\n");
		err = -EBUSY;
		goto init_failed;
	}

	report_fw_info(sd, &fw_version, "after firmware init");

	if (!has_all_opcodes(fw_version.supported_opcodes,
			     final_runtime_opcodes)) {
		sd_err(sd, "Required FW opcodes not supported\n");
		err = -EPERM;
		goto init_failed;
	}

	if (has_extra_opcodes(fw_version.supported_opcodes,
			      final_runtime_opcodes)) {
		report_extra_opcodes(sd, &fw_version, final_runtime_opcodes);
	}

	err = ops_node_guid_get(sd, &sd->guid, NULL, NULL);
	if (err)
		goto init_failed;

	sd_info(sd, "sd %d: guid 0x%016llx\n", idx, sd->guid);

	return 0;

no_data:
	err = -ENOENT;

init_failed:
	sd_err(sd, "could not initialize firmware\n");
	return err;
}

static int request_pscdata_from_fw(struct fdev *dev, const char *filename)
{
	int err;

	err = request_firmware(&dev->psc_as_fw, filename, fdev_dev(dev));
	if (err)
		goto end;

	dev->psc_data = dev->psc_as_fw->data;
	dev->psc_size = dev->psc_as_fw->size;

end:
	return err;
}

/* PSC must be at least as large as header and less than 1 MB */
#define MIN_PSC_SIZE sizeof(struct psc_data)
#define MAX_PSC_SIZE (1024 * 1024)

static int request_pscdata_from_nvmem(struct fdev *dev)
{
	return request_pscdata_from_fw(dev, "default_iaf.pscbin");
}

static void release_pscdata(struct fdev *dev)
{
	if (dev->psc_as_fw)
		release_firmware(dev->psc_as_fw);

	dev->psc_data = NULL;
	dev->psc_size = 0;
}

static void load_and_init_subdev(struct fsubdev *sd)
{
	struct fdev *dev = sd->fdev;
	int err;

	err = load_fw(sd);
	if (err)
		goto cleanup;

	err = start_fw(sd);
	if (err)
		goto cleanup;

	err = init_fw(sd);
	if (err)
		goto cleanup;

	initialize_fports(sd);

cleanup:

	if (atomic_dec_and_test(&dev->fwinit_refcnt)) {
		release_pscdata(dev);
		release_firmware(dev->fw);
	}
}

/*
 * psc_file_override_lock is always locked across this call
 */
static int complete_load_and_init_dev(struct fdev *dev)
{
	int err;

	dev_dbg(fdev_dev(dev), "Reading provisioned psc data\n");

	if (psc_file_override)
		err = request_pscdata_from_fw(dev, psc_file_override);
	else
		err = request_pscdata_from_nvmem(dev);

	if (err)
		goto end;

	err = validate_psc(dev);

	if (err)
		goto end;

	queue_load_and_init_all_subdevs(dev);

end:
	return err;
}

static void continuation_timeout(struct timer_list *timer)
{
	struct fdev *dev = container_of(timer, struct fdev, continuation_timer);

	if (dev->mei_bind_continuation)
		dev_warn(fdev_dev(dev),
			 "No MEI driver registered after %u seconds\n",
			 mei_timeout);
}

static int continue_load_and_init_dev(struct fdev *dev)
{
	int err = 0;

	mutex_lock(&psc_file_override_lock);

	err = complete_load_and_init_dev(dev);

	mutex_unlock(&psc_file_override_lock);

	return err;
}

int load_and_init_fw(struct fdev *dev)
{
	int err;

	err = prepare_to_initialize_subdevs(dev);
	if (err)
		goto end;

	err = continue_load_and_init_dev(dev);

end:
	return err;
}

void cancel_any_outstanding_fw_initializations(struct fdev *dev)
{
	int i;

	if (atomic_read(&dev->fwinit_refcnt))
		for (i = 0; i < dev->pd->sd_cnt; ++i)
			cancel_work_sync(&dev->sd[i].fw_work);
}

/*
 * Workqueue support functions
 */

static void queue_load_and_init_all_subdevs(struct fdev *dev)
{
	int i;

	for (i = 0; i < dev->pd->sd_cnt; ++i)
		queue_work(system_unbound_wq, &dev->sd[i].fw_work);
}

static void wrk_load_and_init_subdev(struct work_struct *work)
{
	load_and_init_subdev(container_of(work, struct fsubdev, fw_work));
}

static void wrk_continue_load_and_init_dev(struct work_struct *work)
{
	continue_load_and_init_dev(container_of(work, struct fdev, psc_work));
}
