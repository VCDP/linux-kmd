# Define the kmod package name here.
%define kmod_name ukmd

%{!?_ver: %define _ver 5.14.0-427}
%{!?_rever: %define _rever 54018}

# If _kversion isn't defined on the rpmbuild line, define it here.
%{!?_kversion: %define _kversion 5.14.0-427}

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
Source0:  %{name}-%{version}.tar.gz
Source1:  dg1_guc_49.0.1.bin
Source2:  dg1_huc_7.9.9.bin
Source3:  dg1_dmc_ver2_02.bin

# Disable the building of the debug package(s).
%define debug_package %{nil}

# Skip strip for signature
%define __brp_strip /bin/true
%define __brp_strip_comment_note /bin/true
%define __brp_strip_lto /bin/true
%define __brp_strip_static_archive /bin/true

%description
This package provides the %{kmod_name} kernel module(s).
It is built to depend upon the specific ABI provided by a range of releases
of the same variant of the Linux kernel and not on any one specific build.

%prep
%setup -q

%build
pwd

%{__make} %{?_smp_mflags} KLIB=/lib/modules/%{_kversion}/ usedefconfig
%{__make} %{?_smp_mflags} KLIB=/lib/modules/%{_kversion}/

%install
mkdir -p %{_builddir}/signing
cp -r signing/* %{_builddir}/signing/
chmod -R 755 %{_builddir}/signing/
%{__install} -d %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp compat/signed/drm_ukmd_compat.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/signed/drm.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/signed/drm_kms_helper.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/signed/drm_shmem_helper.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/i915/signed/i915.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/gpu/drm/i915/signed/i915_spi.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/misc/mei/signed/mei.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/misc/mei/signed/mei-me.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
cp drivers/misc/mei/signed/mei-gsc.ko %{buildroot}/lib/modules/%{_kversion}/extra/%{kmod_name}/
%{__install} -d %{buildroot}%{_sysconfdir}/depmod.d/
echo "override drm * extra/ukmd" > %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_ukmd_compat * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_kms_helper * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override drm_shmem_helper * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override i915 * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override i915_spi * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override mei * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override mei-me * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
echo "override mei-gsc * extra/ukmd" >> %{buildroot}%{_sysconfdir}/depmod.d/kmod-ukmd.conf
%{__mkdir_p} %{buildroot}/lib/firmware/i915
%{__install} -m0644 %{SOURCE1} %{buildroot}/lib/firmware/i915
%{__install} -m0644 %{SOURCE2} %{buildroot}/lib/firmware/i915
%{__install} -m0644 %{SOURCE3} %{buildroot}/lib/firmware/i915


%files
%{_sysconfdir}/depmod.d/kmod-ukmd.conf
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_ukmd_compat.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_kms_helper.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/drm_shmem_helper.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/i915.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/i915_spi.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/mei.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/mei-me.ko
/lib/modules/%{_kversion}/extra/%{kmod_name}/mei-gsc.ko
/lib/firmware/i915/dg1_guc_49.0.1.bin
/lib/firmware/i915/dg1_huc_7.9.9.bin
/lib/firmware/i915/dg1_dmc_ver2_02.bin

%clean
%{__rm} -rf %{buildroot}

%post
# do what kmod will do
echo "Install working. This may take some time ..."
for kv in `ls /lib/modules | grep %{_kversion}`; do
    if [ %{_kversion} != ${kv} ]; then
        mkdir -p /lib/modules/${kv}/extra/ukmd/

        for ko in `ls /lib/modules/${kv}/extra/ukmd/`; do
                rm -rf /lib/modules/${kv}/extra/ukmd/${ko}
                ln -s /lib/modules/%{_kversion}/extra/%{kmod_name}/${ko} /lib/modules/${kv}/extra/ukmd/${ko}
        done
    fi

    if [ -e "/boot/initramfs-${kv}.img" ]; then
        depmod -a ${kv}
         dracut --force /boot/initramfs-${kv}.img ${kv}
    fi
done

%postun
# do what kmod will do
echo "Uninstall working. This may take some time ..."
for kv in `ls /lib/modules | grep %{_kversion}`; do
    if [ %{_kversion} != ${kv} ]; then
        rm -f /lib/modules/${kv}/extra/ukmd
    fi
    rm -rf /lib/modules/%{_kversion}/extra/%{kmod_name}
    if [ -e "/boot/initramfs-${kv}.img" ]; then
        depmod -a ${kv}
        dracut --force /boot/initramfs-${kv}.img ${kv}
    fi
done
