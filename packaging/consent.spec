# Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

Name:       consent
Summary:    Tizen user consent framework
Version:    0.1.0
Release:    1
Group:      Application Framework/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1001: %{name}.manifest
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
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
Public C API library for the Tizen consent service.

%package -n consentd
Summary:    Tizen user consent daemon
Group:      Application Framework/Daemons
Requires:   systemd
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

%description tests
C API exercisers and isolated unit tests. This package does not grant test
identities access to the production daemon.

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
         -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
%__make %{?_smp_mflags}

%install
%make_install
install -d -m 0700 %{buildroot}/opt/var/lib/consentd
install -d -m 0755 %{buildroot}%{_unitdir}/sockets.target.wants
ln -s ../consentd.socket %{buildroot}%{_unitdir}/sockets.target.wants/consentd.socket

%check
ctest --output-on-failure

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%post -n consentd
if [ -d /run/systemd/system ]; then
  systemctl daemon-reload
  systemctl start consentd.socket
  if [ "$1" -gt 1 ]; then
    systemctl try-restart consentd.service
  fi
fi

%preun -n consentd
if [ "$1" -eq 0 ] && [ -d /run/systemd/system ]; then
  systemctl stop consentd.socket consentd.service
fi

%postun -n consentd
if [ -d /run/systemd/system ]; then
  systemctl daemon-reload
fi

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
%{_unitdir}/consentd.service
%{_unitdir}/consentd.socket
%{_unitdir}/sockets.target.wants/consentd.socket
%dir %{_sysconfdir}/consent
%config(noreplace) %{_sysconfdir}/consent/roles.conf
%attr(0700,root,root) %dir /opt/var/lib/consentd

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
