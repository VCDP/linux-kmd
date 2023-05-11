# Define the kmod package name here.
%define kmod_name ukmd

%{!?_ver: %define _ver 4.19.112}
%{!?_rever: %define _rever 20506}

# If _kversion isn't defined on the rpmbuild line, define it here.
%{!?_kversion: %define _kversion 4.19.112 }

Name:    kmod-%{kmod_name}
Version: %{_ver}
Release: %{_rever}%{?dist}
Group:   System Environment/Kernel
License: GPL
Summary: %{kmod_name} kernel module(s)
URL:     http://www.intel.com/

BuildRequires: perl
BuildRequires: redhat-rpm-config
ExclusiveArch: x86_64

# Sources.
# aaaaSource0:  %{kmod_name}-%{version}.tar.gz
Source0:  %{name}-%{version}.tar.gz
Source1:  dg1_guc_49.0.1.bin
Source2:  dg1_huc_7.9.5.bin
Source3:  dg1_dmc_ver2_02.bin

# Magic hidden here.
# %{expand:%     (sh %{SOURCE2} rpmtemplate %{kmod_name} %{_kversion} "")}

# Disable the building of the debug package(s).
%define debug_package %{nil}

%description
This package provides the %{kmod_name} kernel module(s).
It is built to depend upon the specific ABI provided by a range of releases
of the same variant of the Linux kernel and not on any one specific build.

%prep
%setup -q

%build
pwd

cd orig/drivers/gpu/drm
%{__make} -C /lib/modules/%{_kversion}/build ARCH=x86 modules M=$PWD
cp -f Module.symvers ../../../../Module_centos7.4.symvers
cd -

%{__make} KLIB=/lib/modules/%{_kversion}/ usedefconfig
%{__make} KLIB=/lib/modules/%{_kversion}/ -j`nproc`

%install
%{__install} -d %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp orig/drivers/gpu/drm/drm.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp compat/drm_ukmd_compat.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/drm_ukmd.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/drm_ukmd_kms_helper.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/drm_ukmd_panel_orientation_quirks.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/i915/i915.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/i915/i915_spi.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
%{__install} -d %{buildroot}%{_sysconfdir}/depmod.d/
echo "override drm * extra/ukmd" > %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_ukmd_compat * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_ukmd * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_ukmd_kms_helper * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_panel_orientation_quirks * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override i915 * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override i915_spi * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
%{__mkdir_p} %{buildroot}/lib/firmware/i915
%{__install} -m0644 %{SOURCE1} %{buildroot}/lib/firmware/i915
%{__install} -m0644 %{SOURCE2} %{buildroot}/lib/firmware/i915
%{__install} -m0644 %{SOURCE3} %{buildroot}/lib/firmware/i915


%files
%{_sysconfdir}/depmod.d/kmod-ukmd.conf
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_compat.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_kms_helper.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_panel_orientation_quirks.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/i915.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/i915_spi.ko
/lib/firmware/i915/dg1_guc_49.0.1.bin
/lib/firmware/i915/dg1_huc_7.9.5.bin
/lib/firmware/i915/dg1_dmc_ver2_02.bin

%clean
%{__rm} -rf %{buildroot}

%post
# do what kmod will do
echo "Install working. This may take some time ..."
for kv in `ls /lib/modules | grep %{_kversion} `; do
    if [ %{_kversion} != ${kv} ]; then
        mkdir -p /lib/modules/${kv}/extra/ukmd/

        rm -f /lib/modules/${kv}/extra/ukmd/drm.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/drm.ko /lib/modules/${kv}/extra/ukmd/drm.ko

        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd.ko /lib/modules/${kv}/extra/ukmd/drm_ukmd.ko

        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd_compat.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_compat.ko /lib/modules/${kv}/extra/ukmd/drm_ukmd_compat.ko

        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd_kms_helper.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_kms_helper.ko /lib/modules/${kv}/extra/ukmd/drm_ukmd_kms_helper.ko

        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd_panel_orientation_quirks.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_panel_orientation_quirks.ko /lib/modules/${kv}/extra/ukmd/drm_ukmd_panel_orientation_quirks.ko

        rm -f /lib/modules/${kv}/extra/ukmd/i915.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/i915.ko /lib/modules/${kv}/extra/ukmd/i915.ko

        rm -f /lib/modules/${kv}/extra/ukmd/i915_spi.ko
        ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/i915_spi.ko /lib/modules/${kv}/extra/ukmd/i915_spi.ko
    fi

    if [ -e "/boot/initramfs-${kv}.img" ]; then
        depmod -a ${kv}
        mkinitrd --force /boot/initramfs-${kv}.img ${kv}
    fi
done

%postun
# do what kmod will do
echo "Uninstall working. This may take some time ..."
for kv in `ls /lib/modules | grep %{_kversion}`; do
    if [ %{_kversion} != ${kv} ]; then
        rm -f /lib/modules/${kv}/extra/ukmd/drm.ko
        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd.ko
        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd_compat.ko
        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd_kms_helper.ko
        rm -f /lib/modules/${kv}/extra/ukmd/drm_ukmd_panel_orientation_quirks.ko
        rm -f /lib/modules/${kv}/extra/ukmd/i915.ko
        rm -f /lib/modules/${kv}/extra/ukmd/i915_spi.ko
    fi
    if [ -e "/boot/initramfs-${kv}.img" ]; then
        depmod -a ${kv}
        mkinitrd --force /boot/initramfs-${kv}.img ${kv}
    fi
done
