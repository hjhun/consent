Name:       consent
Summary:    Tizen user consent framework
Version:    0.1.0
Release:    1
Group:      Application Framework/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1001: %{name}.manifest
%bcond_without poc
BuildRequires: cmake
BuildRequires: gcc-c++
BuildRequires: python3-base
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(gio-unix-2.0)
BuildRequires: pkgconfig(sqlite3)
BuildRequires: pkgconfig(libsystemd)
BuildRequires: pkgconfig(pkgmgr-info)
BuildRequires: pkgconfig(parcel)
BuildRequires: pkgconfig(capi-base-common)
%if %{with poc}
BuildRequires: dotnet-build-tools = 8.0.421
BuildRequires: csapi-tizenfx-nuget = 14.0.0.19364
BuildRequires: pkgconfig(capi-appfw-app-control)
%endif
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
Public C API library for the Tizen consent service.

%package -n consentd
Summary:    Tizen user consent daemon
Group:      Application Framework/Daemons
Requires:   systemd
Requires(pre): /usr/bin/systemctl
Requires(post): /usr/bin/systemctl
Requires(preun): /usr/bin/systemctl
Requires(postun): /usr/bin/systemctl

%description -n consentd
Socket-activated consent policy, approval and metadata service. The installed
identity policy denies all privileged operations until the platform integrator
provisions trusted service identities.

%package devel
Summary:    Tizen consent development headers
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
Public C headers and pkg-config metadata for consent.

%package tests
Summary:    Tizen consent executable tests
Group:      Development/Testing
Requires:   %{name} = %{version}-%{release}
Requires:   python3-base

%description tests
C API exercisers and isolated unit tests. This package does not grant test
identities access to the production daemon.

%if %{with poc}
%package poc
Summary:    Isolated consent development UI and scenarios
Group:      Development/Testing
Requires:   dotnet-launcher >= 8.0
Requires:   coreclr >= 8.0
Requires:   corefx-managed >= 8.0
Requires:   corefx-native >= 8.0
Requires:   csapi-tizenfx-full >= 14.0
Requires:   /usr/bin/pkgcmd
Requires:   /usr/bin/pkginfo
Requires:   /usr/bin/flock
Requires:   /usr/bin/sha256sum
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires(pre): /usr/bin/systemctl
Requires(post): /usr/bin/systemctl
Requires(preun): /usr/bin/systemctl
Requires(postun): /usr/bin/systemctl

%description poc
Development-only .NET approval UI compiled and signed inside GBS, a separate
native client library and daemon, and explicit scenario setup tools. The UI
TPK is registered globally on a running target or at first boot. Consent role
provisioning and the isolated daemon remain disabled until explicit setup.
%endif

%prep
%setup -q
cp %{SOURCE1001} .

%build
%cmake . -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
         -DCMAKE_INSTALL_LIBEXECDIR=%{_libexecdir} \
         -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
         -DCMAKE_INSTALL_DOCDIR=%{_docdir}/%{name} \
         -DUNITDIR=%{_unitdir} \
         -DBUILD_TESTING=ON \
         -DCONSENT_BUILD_TOOLS=ON \
         -DCONSENT_BUILD_TEST_DAEMON=ON \
         -DCONSENT_BUILD_POC=%{?with_poc:ON}%{!?with_poc:OFF} \
         -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
%__make %{?_smp_mflags}

%install
%make_install
install -d -m 0755 %{buildroot}%{_unitdir}/sockets.target.wants
ln -s ../consentd.socket %{buildroot}%{_unitdir}/sockets.target.wants/consentd.socket
install -d -m 0755 %{buildroot}%{_unitdir}/basic.target.wants
ln -s ../consentd.service %{buildroot}%{_unitdir}/basic.target.wants/consentd.service
%if %{with poc}
install -d -m 0755 %{buildroot}%{_unitdir}/multi-user.target.wants
ln -s ../consent-poc-register.service %{buildroot}%{_unitdir}/multi-user.target.wants/consent-poc-register.service
%endif

%check
ctest --output-on-failure

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%pre -n consentd
if [ "$1" -gt 1 ] && [ -d /run/systemd/system ]; then
  systemctl stop consentd.socket consentd.service || exit 1
fi

%post -n consentd
if [ -d /run/systemd/system ]; then
  systemctl daemon-reload
  systemctl start consentd.socket consentd.service || exit 1
fi

%preun -n consentd
if [ "$1" -eq 0 ] && [ -d /run/systemd/system ]; then
  systemctl stop consentd.socket consentd.service
fi

%postun -n consentd
if [ -d /run/systemd/system ]; then
  systemctl daemon-reload
fi

%if %{with poc}
%pre poc
if [ "$1" -gt 1 ] && [ -d /run/systemd/system ] &&
   [ "$(stat -Lc '%%d:%%i' /)" = "$(stat -Lc '%%d:%%i' /proc/1/root 2>/dev/null)" ]; then
  systemctl stop consent-poc-register.service consentd-poc.socket consentd-poc.service || exit 1
fi

%post poc
/sbin/ldconfig
if [ -d /run/systemd/system ] && [ "$(cat /proc/1/comm 2>/dev/null)" = systemd ] &&
   [ "$(stat -Lc '%%d:%%i' /)" = "$(stat -Lc '%%d:%%i' /proc/1/root 2>/dev/null)" ]; then
  systemctl daemon-reload || exit 1
  systemctl start consent-poc-register.service || exit 1
fi

%preun poc
if [ "$1" -eq 0 ] && [ -d /run/systemd/system ] &&
   [ "$(stat -Lc '%%d:%%i' /)" = "$(stat -Lc '%%d:%%i' /proc/1/root 2>/dev/null)" ]; then
  systemctl stop consent-poc-register.service consentd-poc.socket consentd-poc.service || exit 1
fi

%postun poc
/sbin/ldconfig
if [ -d /run/systemd/system ] &&
   [ "$(stat -Lc '%%d:%%i' /)" = "$(stat -Lc '%%d:%%i' /proc/1/root 2>/dev/null)" ]; then
  systemctl daemon-reload || exit 1
fi
%endif

%files
%defattr(-,root,root,-)
%manifest %{name}.manifest
%license LICENSE
%{_libdir}/libconsent.so.*

%files -n consentd
%defattr(-,root,root,-)
%manifest %{name}.manifest
%license LICENSE
%{_bindir}/consentd
%{_sbindir}/consent-installation-authority
%{_sbindir}/consent-storage-prepare
%{_unitdir}/consentd.service
%{_unitdir}/consentd.socket
%{_unitdir}/sockets.target.wants/consentd.socket
%{_unitdir}/basic.target.wants/consentd.service
%dir %{_sysconfdir}/consent
%config(noreplace) %{_sysconfdir}/consent/roles.conf

%files devel
%defattr(-,root,root,-)
%manifest %{name}.manifest
%{_libdir}/libconsent.so
%{_libdir}/pkgconfig/consent.pc
%{_includedir}/consent
%{_docdir}/consent

%files tests
%defattr(-,root,root,-)
%manifest %{name}.manifest
%{_libexecdir}/consent/tests
%{_libexecdir}/consent/examples

%if %{with poc}
%files poc
%defattr(-,root,root,-)
%manifest %{name}.manifest
%license LICENSE
%{_libdir}/libconsent-poc.so*
%{_libexecdir}/consent/poc
%{_datadir}/consent/poc
%{_unitdir}/consentd-poc.service
%{_unitdir}/consentd-poc.socket
%{_unitdir}/consent-poc-register.service
%{_unitdir}/multi-user.target.wants/consent-poc-register.service
%dir %{_sysconfdir}/consent-poc
%config(noreplace) %{_sysconfdir}/consent-poc/roles.conf
%endif
