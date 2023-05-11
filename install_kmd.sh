TARGET_KERNEL_VERSION=`uname -r`
set -e
if [ ! -d /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/ ]; then
	mkdir -p /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/
fi

cd orig
#patch -p1< ../dualdrm/4.19.112_support_dual_drm.patch
cd -

cd orig/drivers/gpu/drm
make -C /lib/modules/${TARGET_KERNEL_VERSION}/build  ARCH=x86 modules M=$PWD -I$PWD
cd -

cp -f ./orig/drivers/gpu/drm/drm.ko /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/drm.ko
cp -f ./orig/drivers/gpu/drm/Module.symvers ./Module_centos7.4.symvers

make usedefconfig
make -j4
cp -f ./compat/drm_ukmd_compat.ko /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/
cp -f ./drivers/gpu/drm/drm_ukmd.ko /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/
cp -f ./drivers/gpu/drm/drm_ukmd_kms_helper.ko /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/
cp -f ./drivers/gpu/drm/drm_ukmd_panel_orientation_quirks.ko /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/
cp -f ./drivers/gpu/drm/i915/i915.ko /lib/modules/${TARGET_KERNEL_VERSION}/extra/ukmd/

depmod -a ${TARGET_KERNEL_VERSION}

echo -e $ECHO_PREFIX_INFO "Calling mkinitrd upon ${TARGET_KERNEL_VERSION} kernel..."
mkinitrd --force /boot/initramfs-"${TARGET_KERNEL_VERSION}".img ${TARGET_KERNEL_VERSION}
