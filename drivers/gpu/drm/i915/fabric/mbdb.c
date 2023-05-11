// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 *
 */

#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/io.h>

#include "iaf_drv.h"
#include "io.h"
#include "ops.h"
#include "mbdb.h"
#include "trace.h"
#include "port.h"
#include "debugfs.h"

/* Mailbox private structures */

#define CP_ADDR_MBDB_INT_STATUS_UNMASKED (CP_ADDR_MBDB_BASE + 0x00)
#define CP_ADDR_MBDB_INT_ENABLE_MASK     (CP_ADDR_MBDB_BASE + 0x08)
#define CP_ADDR_MBDB_INT_STATUS_MASKED   (CP_ADDR_MBDB_BASE + 0x10)
#define CP_ADDR_MBDB_INT_ACK             (CP_ADDR_MBDB_BASE + 0x18)
#define CP_ADDR_MBDB_INT_PARTNER_SET     (CP_ADDR_MBDB_BASE + 0x20)
#define CP_ADDR_MBDB_INT_STATUS_GPSB     (CP_ADDR_MBDB_BASE + 0x28)
#define CP_ADDR_MBDB_DB0_STATUS          (CP_ADDR_MBDB_BASE + 0x40)
#define CP_ADDR_MBDB_DB0_DEC             (CP_ADDR_MBDB_BASE + 0x48)
#define CP_ADDR_MBDB_DB0_RESET           (CP_ADDR_MBDB_BASE + 0x50)
#define CP_ADDR_MBDB_DB1_STATUS          (CP_ADDR_MBDB_BASE + 0x60)
#define CP_ADDR_MBDB_DB1_DEC             (CP_ADDR_MBDB_BASE + 0x68)
#define CP_ADDR_MBDB_DB1_RESET           (CP_ADDR_MBDB_BASE + 0x60)
#define CP_ADDR_MBDB_DB2_STATUS          (CP_ADDR_MBDB_BASE + 0x80)
#define CP_ADDR_MBDB_DB2_INC             (CP_ADDR_MBDB_BASE + 0x88)
#define CP_ADDR_MBDB_DB3_STATUS          (CP_ADDR_MBDB_BASE + 0xA0)
#define CP_ADDR_MBDB_DB3_INC             (CP_ADDR_MBDB_BASE + 0xA8)
#define CP_ADDR_MBDB_SEM0_ACQUIRE        (CP_ADDR_MBDB_BASE + 0xC0)
#define CP_ADDR_MBDB_SEM0_RELEASE        (CP_ADDR_MBDB_BASE + 0xC8)
#define CP_ADDR_MBDB_SEM1_ACQUIRE        (CP_ADDR_MBDB_BASE + 0xD0)
#define CP_ADDR_MBDB_SEM1_RELEASE        (CP_ADDR_MBDB_BASE + 0xD8)
#define CP_ADDR_MBDB_SEM2_ACQUIRE        (CP_ADDR_MBDB_BASE + 0xE0)
#define CP_ADDR_MBDB_SEM2_RELEASE        (CP_ADDR_MBDB_BASE + 0xE8)
#define CP_ADDR_MBDB_SEM3_ACQUIRE        (CP_ADDR_MBDB_BASE + 0xF0)
#define CP_ADDR_MBDB_SEM3_RELEASE        (CP_ADDR_MBDB_BASE + 0xF8)
#define CP_ADDR_MBDB_MISC_SHARED         (CP_ADDR_MBDB_BASE + 0x100)
#define CP_ADDR_MBDB_GP_DATA             (CP_ADDR_MBDB_BASE + 0x1000)

#define S_INT_STATUS_DB0_INT       0
#define S_INT_STATUS_DB1_INT       1
#define S_INT_STATUS_SEM0_ACQUIRED 2
#define S_INT_STATUS_SEM1_ACQUIRED 3
#define S_INT_STATUS_SEM2_ACQUIRED 4
#define S_INT_STATUS_SEM3_ACQUIRED 5
#define S_INT_STATUS_INBOX_FULL    6
#define S_INT_STATUS_OUTBOX_EMPTY  7

#define INBOX_FULL_MASK   (BIT_ULL(S_INT_STATUS_INBOX_FULL))
#define OUTBOX_EMPTY_MASK (BIT_ULL(S_INT_STATUS_OUTBOX_EMPTY))

#define TIMEOUT (10 * HZ)

#define MAX_U64_WIDTH 20

/*
 * Polling mode will most likely be removed in the future. Currently it is
 * for debug purposes only
 */
#define INCLUDE_POLLING

#ifdef INCLUDE_POLLING
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#endif

enum mbdb_counters {
	MBDB_COUNTERS_FIRST,
	POSTED_REQUESTS = MBDB_COUNTERS_FIRST,
	NON_POSTED_REQUESTS,
	RECEIVED_REQUESTS,
	TIMEDOUT_REQUESTS,
	NON_ERROR_RESPONSES,
	ERROR_RESPONSES,
	UNMATCHED_RESPONSES,
	TIMEDOUT_RESPONSES,
	MBDB_COUNTERS_MAX
};

struct mbdb {
	struct fsubdev *sd;
	bool stopping;
	atomic_t pending_new_seq;

	u64 __iomem *int_status_unmasked_addr;
	u64 __iomem *int_status_masked_addr;
	u64 __iomem *int_ack_addr;
	u64 __iomem *int_partner_set_addr;
	u64 __iomem *int_enable_mask_addr;
	u64 __iomem *gp_outbox_data_addr;
	u64 __iomem *gp_outbox_cw_addr;
	u64 __iomem *gp_outbox_param_addr;
	u64 __iomem *gp_inbox_data_addr;
	u64 __iomem *gp_inbox_cw_addr;
	u64 __iomem *gp_inbox_param_addr;

	/* Protect outbox access */
	struct semaphore outbox_sem;
	struct completion outbox_empty;
	u8 outbox_seqno;

	struct work_struct inbox_full;
	u8 inbox_seqno;
	u32 inbox_tid;

	struct list_head ibox_list;
	/* Protect inbox list access */
	struct mutex inbox_list_mutex;

	/* Protect irq access */
	spinlock_t irq_spinlock;
	unsigned long flags;
	u64 int_enables;

	/* Protect pci bar space access */
	struct mutex ack_mutex;
	/* Protect pci bar space access */
	struct mutex partner_set_mutex;

	u64 counters[MBDB_COUNTERS_MAX];

#ifdef INCLUDE_POLLING
	/* Protect inbox access */
	struct mutex inbox_mutex;
	struct timer_list inbox_timer;
	atomic_t ibox_waiters;
#endif
};

#define MAILBOX_COUNTERS_FILE_NAME "mailbox_counters"

static const char * const mbdb_counter_names[] = {
	"posted requests     : ",
	"non posted requests : ",
	"received requests   : ",
	"timedout requests   : ",
	"non error responses : ",
	"error responses     : ",
	"unmatched responses : ",
	"timedout responses  : ",
};

#ifdef INCLUDE_POLLING

#define MBDB_SLOW_POLL_TIMEOUT (HZ)
#define MBDB_FAST_POLL_TIMEOUT 5

static bool mbdb_polling_mode = true;

static void mbdb_disable_interrupts(struct mbdb *mbdb, u64 intr_mask);
static void mbdb_enable_interrupts(struct mbdb *mbdb, u64 intr_mask);

static void inbox_timer_fn(struct timer_list *timer)
{
	if (mbdb_polling_mode) {
		struct mbdb *mbdb = from_timer(mbdb, timer, inbox_timer);

		queue_work(system_unbound_wq, &mbdb->inbox_full);
		mod_timer(&mbdb->inbox_timer, jiffies + MBDB_SLOW_POLL_TIMEOUT);
	}
}

static void log_sd_mbdb_access_mode(struct fsubdev *sd)
{
	if (mbdb_polling_mode) {
		sd_info(sd, "/***************************/\n");
		sd_info(sd, "/*                         */\n");
		sd_info(sd, "/* Mailbox Polling Enabled */\n");
		sd_info(sd, "/*                         */\n");
		sd_info(sd, "/***************************/\n");
	} else {
		sd_info(sd, "/******************************/\n");
		sd_info(sd, "/*                            */\n");
		sd_info(sd, "/* Mailbox Interrupts Enabled */\n");
		sd_info(sd, "/*                            */\n");
		sd_info(sd, "/******************************/\n");
	}
}

static int mbdb_wait_outbox_empty(struct mbdb *mbdb)
{
	int wait_time;
	int timeout;

	for (wait_time = 0; wait_time < TIMEOUT;
	     wait_time += MBDB_FAST_POLL_TIMEOUT - timeout) {
		if (mbdb->stopping)
			return -ENODEV;

		if (readq(mbdb->int_status_unmasked_addr) & OUTBOX_EMPTY_MASK)
			return 0;

		timeout =
			schedule_timeout_interruptible(MBDB_FAST_POLL_TIMEOUT);
	}

	return -ETIMEDOUT;
}

module_param(mbdb_polling_mode, bool, 0400);
MODULE_PARM_DESC(mbdb_polling_mode,
		 "Use polling instead of interrupts to access Mailbox (default: Y)");
#endif

static ssize_t mbdb_display_counters(struct file *fp, char __user *user_buffer,
				     size_t count, loff_t *position)
{
	struct fsubdev *sd = fp->private_data;
	u64 *mailbox_counter_values = sd->mbdb->counters;
	int buf_size = sizeof(mbdb_counter_names) +
		       (MAX_U64_WIDTH * MBDB_COUNTERS_MAX);
	int buf_offset = 0;
	char *buf;
	enum mbdb_counters i;
	int ret;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return 0;

	for (i = MBDB_COUNTERS_FIRST; i < MBDB_COUNTERS_MAX; i++)
		buf_offset += snprintf(buf + buf_offset, buf_size - buf_offset,
				       "%s%llu\n", mbdb_counter_names[i],
				       mailbox_counter_values[i]);

	ret = simple_read_from_buffer(user_buffer, count, position, buf,
				      buf_offset);

	kfree(buf);

	return ret;
}

static const struct file_operations mbdb_counter_fops = {
	.open = simple_open,
	.read = mbdb_display_counters,
	.llseek = default_llseek,
};

static void mbdb_init_irq_lock(struct mbdb *mbdb)
{
	spin_lock_init(&mbdb->irq_spinlock);
}

static void mbdb_destroy_irq_lock(struct mbdb *mbdb)
{
}

static void mbdb_acquire_irq_lock(struct mbdb *mbdb)
{
	spin_lock_irqsave(&mbdb->irq_spinlock, mbdb->flags);
}

static void mbdb_release_irq_lock(struct mbdb *mbdb)
{
	spin_unlock_irqrestore(&mbdb->irq_spinlock, mbdb->flags);
}

static void mbdb_int_ack_wr(struct mbdb *mbdb, u64 shift)
{
	mutex_lock(&mbdb->ack_mutex);
	writeq(BIT_ULL(shift), mbdb->int_ack_addr);
	mutex_unlock(&mbdb->ack_mutex);
}

static void mbdb_int_partner_set_wr(struct mbdb *mbdb, u64 shift)
{
	mutex_lock(&mbdb->partner_set_mutex);
	writeq(BIT_ULL(shift), mbdb->int_partner_set_addr);
	mutex_unlock(&mbdb->partner_set_mutex);
}

static void mbdb_enable_interrupts(struct mbdb *mbdb, u64 intr_mask)
{
	mbdb_acquire_irq_lock(mbdb);

	if (~mbdb->int_enables & intr_mask) {
		mbdb->int_enables |= intr_mask;
		writeq(mbdb->int_enables, mbdb->int_enable_mask_addr);
	}

	mbdb_release_irq_lock(mbdb);
}

static void mbdb_disable_interrupts(struct mbdb *mbdb, u64 intr_mask)
{
	mbdb_acquire_irq_lock(mbdb);

	if (mbdb->int_enables & intr_mask) {
		mbdb->int_enables &= ~intr_mask;
		writeq(mbdb->int_enables, mbdb->int_enable_mask_addr);
	}

	mbdb_release_irq_lock(mbdb);
}

irqreturn_t mbdb_handle_irq(struct fsubdev *sd)
{
	struct mbdb *mbdb = sd->mbdb;
	u64 contrib;

	if (!mbdb)
		return IRQ_NONE;

	contrib = readq(mbdb->int_status_masked_addr);

	/*
	 * This warrants some further investigation: If this indicates an
	 * interrupt for THIS IAF, returning IRQ_HANDLED might be more
	 * resilient, though that probably should never happen anyway.
	 */
	if (!contrib)
		return IRQ_NONE;

	mbdb_disable_interrupts(mbdb, contrib);

	if (contrib & INBOX_FULL_MASK)
		queue_work(system_unbound_wq, &mbdb->inbox_full);

	if (contrib & OUTBOX_EMPTY_MASK)
		complete(&mbdb->outbox_empty);

	return IRQ_HANDLED;
}

static void mbdb_ibox_cb_fn(struct work_struct *work)
{
	struct mbdb_ibox *ibox = container_of(work, struct mbdb_ibox, work);
	u8 rsp_status = mbdb_mbox_rsp_status(ibox->cw);

	ibox->cb(ibox->response, ibox->rsp_len, ibox->context, rsp_status);

	mbdb_ibox_release(ibox);
}

static void mbdb_ibox_handle_unmatched(struct fsubdev *sd,
				       enum mbdb_msg_type msg_type, u32 tid,
				       u8 op_code, u8 rsp_status)
{
	switch (op_code) {
	case MBOX_OP_CODE_LINK_MGR_PORT_STATE_CHANGE_TRAP_NOTIFICATION:
		port_state_change_trap_handler(sd);
		break;

	case MBOX_OP_CODE_LINK_MGR_PORT_LWD_TRAP_NOTIFICATION:
		port_link_width_degrade_trap_handler(sd);
		break;

	case MBOX_OP_CODE_LINK_MGR_PORT_LQI_TRAP_NOTIFICATION:
		port_link_quality_indicator_trap_handler(sd);
		break;

	case MBOX_OP_CODE_QSFP_MGR_FAULT_TRAP_NOTIFICATION:
		port_qsfp_fault_trap_handler(sd);
		break;

	/*
	 * Not implemented yet:
	 *
	 * case MBOX_OP_CODE_QSFP_MGR_PRESENCE_TRAP_NOTIFICATION:
	 *	port_qsfp_presence_trap_handler(sd);
	 *	break;
	 */

	default:
		if (msg_type == MBOX_RESPONSE)
			sd_warn(sd, "NOT found op_code=%u tid=%u rsp_status=%u\n",
				op_code, tid, rsp_status);
		else
			sd_err(sd, "Received MBOX request with opCode=%u tid=%u rsp_status=%u\n",
			       op_code, tid, rsp_status);
		break;
	}
}

/**
 * mbdb_seq_reset - Resets sequence numbers on a successful bootloader<->fw
 * transition, and unblocks the outbox.
 * @mbdb: The mailbox to operate on.
 * @cw: The inbound control word in flight
 */
static void mbdb_seq_reset(struct mbdb *mbdb, u64 cw)
{
	/* This only applies to FW_START and RESET responses */

	if (mbdb_mbox_op_code(cw) != MBOX_OP_CODE_RESET &&
	    mbdb_mbox_op_code(cw) != MBOX_OP_CODE_FW_START)
		return;

	if (mbdb_mbox_rsp_status(cw) == MBOX_RSP_STATUS_OK) {
		mbdb->inbox_seqno = 0;
		mbdb->outbox_seqno = 0;
	}

	if (atomic_cmpxchg(&mbdb->pending_new_seq, 1, 0))
		up(&mbdb->outbox_sem);
}

static struct mbdb_ibox *mbdb_find_ibox(struct mbdb *mbdb, u32 tid, u8 op_code)
{
	struct mbdb_ibox *ibox;
	bool found = false;

	/*
	 * Obtain the mutex that protects our ibox related data structures
	 * so that our lists stay consistent
	 */
	mutex_lock(&mbdb->inbox_list_mutex);

	/*
	 * see if we can find the associated ibox in the list for this
	 * op_code and tid
	 */

	list_for_each_entry(ibox, &mbdb->ibox_list, ibox_list_link) {
		if (ibox->tid == tid && ibox->op_code == op_code) {
			found = true;
			/* Remove this from the list */
			list_del_init(&ibox->ibox_list_link);
			break;
		}
	}

	/* done checking/updating our lists */
	mutex_unlock(&mbdb->inbox_list_mutex);

	return found ? ibox : NULL;
}

static void mbdb_inbox_empty(struct mbdb *mbdb)
{
/* tell our partner it can put a new message in its outbox */

#ifdef INCLUDE_POLLING
	if (!mbdb->stopping && !mbdb_polling_mode)
#else
	if (!mbdb->stopping)
#endif
		mbdb_enable_interrupts(mbdb, INBOX_FULL_MASK);

	mbdb_int_partner_set_wr(mbdb, S_INT_STATUS_OUTBOX_EMPTY);

#ifdef INCLUDE_POLLING
	if (!mbdb->stopping && mbdb_polling_mode)
		queue_work(system_unbound_wq, &mbdb->inbox_full);

	mutex_unlock(&mbdb->inbox_mutex);
#endif
}

static void mbdb_inbox_full_fn(struct work_struct *inbox_full)
{
	struct mbdb *mbdb = container_of(inbox_full, struct mbdb, inbox_full);
	struct fsubdev *sd = mbdb->sd;
	u8 op_code;
	u8 seq_no;
	bool seq_no_error;
	u8 rsp_status;
	u32 tid;
	u64 cw;
	enum mbdb_msg_type msg_type;
	struct mbdb_ibox *ibox = NULL;

#ifdef INCLUDE_POLLING
	mutex_lock(&mbdb->inbox_mutex);

	if (mbdb_polling_mode)
		if (!(readq(mbdb->int_status_unmasked_addr) &
		      INBOX_FULL_MASK)) {
			if (atomic_read(&mbdb->ibox_waiters))
				queue_work(system_unbound_wq,
					   &mbdb->inbox_full);
			mutex_unlock(&mbdb->inbox_mutex);
			return;
		}
#endif

	/* Get the control word info */
	cw = readq(mbdb->gp_inbox_cw_addr);

	if (cw == ~0ULL) {
		sd_err(sd, "PCI register read timeout has occurred\n");
		mbdb_inbox_empty(mbdb);
		return;
	}

	/* acknowlege the inbox full */
	mbdb_int_ack_wr(mbdb, S_INT_STATUS_INBOX_FULL);

	/* fetch information about the message */
	op_code = mbdb_mbox_op_code(cw);
	rsp_status = mbdb_mbox_rsp_status(cw);
	tid = mbdb_mbox_tid(cw);
	msg_type = mbdb_mbox_msg_type(cw);
	seq_no = mbdb_mbox_seq_no(cw);
	seq_no_error = mbdb_mbox_seqno_error(seq_no, mbdb->inbox_seqno);

	/* make sure we don't have a sequence number mismatch */
	if (seq_no_error)
		sd_info(sd, "Synchronizing recv sequence number\n");

	trace_iaf_mbx_in(sd->fdev->pd->index, sd_index(sd), op_code,
			 mbdb->inbox_seqno, seq_no, rsp_status, tid, cw);

	/* update our expected sequence number of the next received message */
	mbdb->inbox_seqno = mbdb_mbox_seqno_next(seq_no);

	if (msg_type == MBOX_RESPONSE)
		ibox = mbdb_find_ibox(mbdb, tid, op_code);

	if (!ibox) {
		if (msg_type == MBOX_RESPONSE)
			mbdb->counters[UNMATCHED_RESPONSES]++;
		else
			mbdb->counters[RECEIVED_REQUESTS]++;
		mbdb_ibox_handle_unmatched(sd, msg_type, tid, op_code,
					   rsp_status);
		mbdb_inbox_empty(mbdb);
		return;
	}

	/* transfer the h/w data into the virtual ibox */
	if (rsp_status == MBOX_RSP_STATUS_OK) {
		if (seq_no_error)
			mbdb->counters[ERROR_RESPONSES]++;
		else
			mbdb->counters[NON_ERROR_RESPONSES]++;
		if (ibox->rsp_len)
			io_read(mbdb->gp_inbox_param_addr, ibox->response,
				ibox->rsp_len);
	} else {
		mbdb->counters[ERROR_RESPONSES]++;
		if (rsp_status == MBOX_RSP_STATUS_SEQ_NO_ERROR)
			sd_info(sd, "Synchronizing xmit sequence number\n");
		else
			sd_err(sd, "Errored MBOX control word: 0x%016llx\n",
			       cw);
	}

	ibox->cw = cw;
	ibox->rsp_status =
		seq_no_error ? MBOX_RSP_STATUS_SEQ_NO_ERROR : rsp_status;

	mbdb_seq_reset(mbdb, cw);

	if (ibox->cb)
		queue_work(system_unbound_wq, &ibox->work);
	else
		complete(&ibox->ibox_full);

	mbdb_inbox_empty(mbdb);
}

static void mbdb_init(struct mbdb *mbdb)
{
	mbdb_int_partner_set_wr(mbdb, S_INT_STATUS_OUTBOX_EMPTY);
#ifdef INCLUDE_POLLING
	if (!mbdb_polling_mode)
#endif
		mbdb_enable_interrupts(mbdb, INBOX_FULL_MASK);
}

u8 mbdb_outbox_seqno(struct fsubdev *sd)
{
	u8 result;

	result = sd->mbdb->outbox_seqno;
	sd->mbdb->outbox_seqno = mbdb_mbox_seqno_next(result);
	return result;
}

static int mbdb_outbox_is_empty(struct mbdb *mbdb)
{
	if (mbdb->stopping)
		return -ENODEV;

	if (readq(mbdb->int_status_unmasked_addr) & OUTBOX_EMPTY_MASK)
		return 0;

#ifdef INCLUDE_POLLING
	if (mbdb_polling_mode) {
		int ret = mbdb_wait_outbox_empty(mbdb);

		if (ret == -ETIMEDOUT) {
			mbdb->counters[TIMEDOUT_REQUESTS]++;
			sd_err(mbdb->sd, "OUTBOX timed out\n");
		}

		return ret;
	}
#endif

	mbdb_enable_interrupts(mbdb, OUTBOX_EMPTY_MASK);

	if (wait_for_completion_timeout(&mbdb->outbox_empty, TIMEOUT)) {
		if (mbdb->stopping)
			return -ENODEV;

		reinit_completion(&mbdb->outbox_empty);
		return 0;
	}

	if (mbdb->stopping)
		return -ENODEV;

	if (readq(mbdb->int_status_unmasked_addr) & OUTBOX_EMPTY_MASK)
		return 0;

	mbdb->counters[TIMEDOUT_REQUESTS]++;
	sd_err(mbdb->sd, "OUTBOX timed out\n");

	return -ETIMEDOUT;
}

struct mbox_msg __iomem *mbdb_outbox_acquire(struct fsubdev *sd, u64 *cw)
{
	struct mbdb *mbdb = sd->mbdb;
	int ret;

	if  (!mbdb)
		return IOMEM_ERR_PTR(-EINVAL);

	ret = down_killable(&mbdb->outbox_sem);
	if (ret)
		return IOMEM_ERR_PTR(ret);

	if (mbdb->stopping) {
		up(&mbdb->outbox_sem);
		return IOMEM_ERR_PTR(-ENODEV);
	}

	if (mbdb_mbox_is_posted(*cw) == MBOX_NO_RESPONSE_REQUESTED)
		++mbdb->counters[POSTED_REQUESTS];
	else
		++mbdb->counters[NON_POSTED_REQUESTS];

	ret = mbdb_outbox_is_empty(mbdb);
	if (ret < 0) {
		up(&mbdb->outbox_sem);
		return IOMEM_ERR_PTR(ret);
	}

	mbdb_int_ack_wr(mbdb, S_INT_STATUS_OUTBOX_EMPTY);

	/* Add a sequence number to the control word */
	*cw |= (mbdb_outbox_seqno(sd) & MBOX_CW_SEQ_NO_MASK) <<
	       MBOX_CW_SEQ_NO_SHIFT;

	/* For posted RESET, reset sequences immediately */
	if (mbdb_mbox_op_code(*cw) == MBOX_OP_CODE_RESET &&
	    mbdb_mbox_is_posted(*cw) == MBOX_NO_RESPONSE_REQUESTED) {
		mbdb->inbox_seqno = 0;
		mbdb->outbox_seqno = 0;
	}

	/* For FW_START and posted RESET, lock outbox */
	if (mbdb_mbox_op_code(*cw) == MBOX_OP_CODE_FW_START ||
	    (mbdb_mbox_op_code(*cw) == MBOX_OP_CODE_RESET &&
	     mbdb_mbox_is_posted(*cw) == MBOX_RESPONSE_REQUESTED))
		atomic_set(&mbdb->pending_new_seq, 1);

	return (struct mbox_msg __iomem *)mbdb->gp_outbox_data_addr;
}

/**
 * mbdb_outbox_release - Releases the outbox.
 * @sd: The subdevice to operate on.
 *
 * Will not release the outbox lock if the operation issued was a firmware
 * start.  Instead the outbox will remain blocked until the fw start operation
 * resolves to prevent sequence number desyncs.
 */
void mbdb_outbox_release(struct fsubdev *sd)
{
	struct mbdb *mbdb = sd->mbdb;

	if (!mbdb)
		return;

	mbdb_int_partner_set_wr(mbdb, S_INT_STATUS_INBOX_FULL);

	if (!atomic_read(&mbdb->pending_new_seq))
		up(&mbdb->outbox_sem);
}

struct mbdb_ibox *mbdb_ibox_acquire(struct fsubdev *sd, u8 op_code,
				    void *response, u32 rsp_len, mbox_cb cb,
				       void *context, bool posted)
{
	struct mbdb *mbdb = sd->mbdb;
	struct mbdb_ibox *ibox;

	if (!mbdb)
		return ERR_PTR(-EINVAL);

	if (posted)
		return NULL;

	ibox = kzalloc(sizeof(*ibox), GFP_ATOMIC);
	if (!ibox)
		return ERR_PTR(-ENOMEM);

	ibox->mbdb = mbdb;
	ibox->op_code = op_code;

	INIT_LIST_HEAD(&ibox->ibox_list_link);
	ibox->tid = mbdb->inbox_tid++;
	ibox->response = response;
	ibox->rsp_len = response ? rsp_len : 0;

	if (cb) {
		INIT_WORK(&ibox->work, mbdb_ibox_cb_fn);
		ibox->cb = cb;
		ibox->context = context;
	} else {
		init_completion(&ibox->ibox_full);
	}

	mutex_lock(&mbdb->inbox_list_mutex);
	list_add_tail(&ibox->ibox_list_link, &mbdb->ibox_list);
	mutex_unlock(&mbdb->inbox_list_mutex);

	return ibox;
}

void mbdb_ibox_release(struct mbdb_ibox *ibox)
{
	kfree(ibox);
}

int mbdb_ibox_wait(struct mbdb_ibox *ibox)
{
	struct mbdb *mbdb = ibox->mbdb;
	int ret;

	if (mbdb->stopping)
		return -ENODEV;

#ifdef INCLUDE_POLLING
	mutex_lock(&mbdb->inbox_mutex);

	if (mbdb_polling_mode) {
		atomic_inc(&mbdb->ibox_waiters);
		queue_work(system_unbound_wq, &mbdb->inbox_full);
	}

	mutex_unlock(&mbdb->inbox_mutex);
#endif

	ret = wait_for_completion_timeout(&ibox->ibox_full, TIMEOUT);
	if (!ret) {
		mutex_lock(&mbdb->inbox_list_mutex);
		list_del_init(&ibox->ibox_list_link);
		mutex_unlock(&mbdb->inbox_list_mutex);
		mbdb->counters[TIMEDOUT_RESPONSES]++;
		sd_err(mbdb->sd, "INBOX timed out: opcode %u tid 0x%08x\n",
		       ibox->op_code, ibox->tid);

		if (ibox->op_code == MBOX_OP_CODE_FW_START ||
		    ibox->op_code == MBOX_OP_CODE_RESET)
			if (atomic_cmpxchg(&mbdb->pending_new_seq, 1, 0))
				up(&mbdb->outbox_sem);

		ret = -ETIMEDOUT;
	}

#ifdef INCLUDE_POLLING
	mutex_lock(&mbdb->inbox_mutex);

	if (mbdb_polling_mode) {
		if (atomic_read(&mbdb->ibox_waiters))
			atomic_dec(&mbdb->ibox_waiters);
		queue_work(system_unbound_wq, &mbdb->inbox_full);
	}

	mutex_unlock(&mbdb->inbox_mutex);
#endif

	return ret;
}

static void mbdb_ibox_drain(struct mbdb *mbdb)
{
	struct mbdb_ibox *ibox;
	struct mbdb_ibox *tmp;

	mutex_lock(&mbdb->inbox_list_mutex);

	list_for_each_entry_safe(ibox, tmp, &mbdb->ibox_list, ibox_list_link) {
		list_del_init(&ibox->ibox_list_link);
		mbdb_ibox_release(ibox);
	}

	mutex_unlock(&mbdb->inbox_list_mutex);
}

void destroy_mbdb(struct fsubdev *sd)
{
	struct mbdb *mbdb = sd->mbdb;

	if (!mbdb)
		return;

	mbdb->stopping = true;

#ifdef INCLUDE_POLLING
	mutex_lock(&mbdb->inbox_mutex);
	if (mbdb_polling_mode) {
		del_timer_sync(&mbdb->inbox_timer);
	} else {
#endif
		mbdb_disable_interrupts(mbdb, mbdb->int_enables);
		complete(&mbdb->outbox_empty);
#ifdef INCLUDE_POLLING
	}
	mutex_unlock(&mbdb->inbox_mutex);
#endif

	flush_work(&mbdb->inbox_full);
	mbdb_ibox_drain(mbdb);
	mbdb_destroy_irq_lock(mbdb);
	mutex_destroy(&mbdb->partner_set_mutex);
	mutex_destroy(&mbdb->ack_mutex);
	mutex_destroy(&mbdb->inbox_list_mutex);
#ifdef INCLUDE_POLLING
	mutex_destroy(&mbdb->inbox_mutex);
#endif

	sd->mbdb = NULL;
	kfree(mbdb);
}

static void mbdb_set_mem_addresses(struct mbdb *mbdb)
{
	void __iomem *csr_base = mbdb->sd->csr_base;

	mbdb->int_status_unmasked_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_INT_STATUS_UNMASKED);

	mbdb->int_status_masked_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_INT_STATUS_MASKED);

	mbdb->int_ack_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_INT_ACK);

	mbdb->int_partner_set_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_INT_PARTNER_SET);

	mbdb->int_enable_mask_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_INT_ENABLE_MASK);

	mbdb->gp_outbox_data_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_GP_DATA);

	mbdb->gp_outbox_cw_addr = mbdb->gp_outbox_data_addr;

	mbdb->gp_outbox_param_addr = mbdb->gp_outbox_data_addr + 1;

	mbdb->gp_inbox_data_addr = (u64 __iomem *)
		(csr_base + CP_ADDR_MBDB_GP_DATA + MBOX_SIZE_IN_BYTES);

	mbdb->gp_inbox_cw_addr = mbdb->gp_inbox_data_addr;

	mbdb->gp_inbox_param_addr = mbdb->gp_inbox_data_addr + 1;
}

int create_mbdb(struct fsubdev *sd, struct dentry *sd_dir_node)
{
	struct mbdb *mbdb;

	mbdb = kzalloc(sizeof(*sd->mbdb), GFP_ATOMIC);
	if (!mbdb)
		return -ENOMEM;

	sd->mbdb = mbdb;
	mbdb->sd = sd;
	mbdb->stopping = false;
	atomic_set(&mbdb->pending_new_seq, 0);

	mbdb_set_mem_addresses(mbdb);

	init_completion(&mbdb->outbox_empty);

	INIT_LIST_HEAD(&mbdb->ibox_list);

	INIT_WORK(&mbdb->inbox_full, mbdb_inbox_full_fn);

	sema_init(&mbdb->outbox_sem, 1);
#ifdef INCLUDE_POLLING
	mutex_init(&mbdb->inbox_mutex);
#endif
	mutex_init(&mbdb->inbox_list_mutex);
	mutex_init(&mbdb->ack_mutex);
	mutex_init(&mbdb->partner_set_mutex);

	mbdb_init_irq_lock(mbdb);

	mbdb->int_enables = 0ull;
	mbdb_init(mbdb);

#ifdef INCLUDE_POLLING
	atomic_set(&mbdb->ibox_waiters, 0);

	if (mbdb_polling_mode) {
		timer_setup(&mbdb->inbox_timer, inbox_timer_fn, 0);
		add_timer(&mbdb->inbox_timer);
	}

	log_sd_mbdb_access_mode(mbdb->sd);
#endif
	debugfs_add_file_node(MAILBOX_COUNTERS_FILE_NAME, 0400, sd_dir_node,
			      sd, &mbdb_counter_fops);

	return 0;
}
