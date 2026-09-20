#!/bin/sh
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
# Explicit development-emulator preparation. Production state is never used.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
[ "$(id -u)" = 0 ]
phase=${1:-}
tools=/usr/libexec/consent/poc
control=/opt/var/lib/consent-poc-control
runtime=/opt/var/lib/consent-poc-runtime
config=/etc/consent-poc
package=org.tizen.consentui
for path in /opt /opt/var /opt/var/lib /etc "$config"; do
  [ -d "$path" ] && [ ! -L "$path" ]
  [ "$(stat -c %u "$path")" = 0 ]
  mode=$(stat -c %a "$path")
  [ $((0$mode & 0022)) -eq 0 ]
done
if [ ! -e "$control" ] && [ ! -L "$control" ]; then mkdir -m 0700 "$control"; fi
[ -d "$control" ] && [ ! -L "$control" ]
[ "$(stat -c '%u:%g:%a' "$control")" = 0:0:700 ]
prepare_runtime() {
  # Both callers stop the socket and service first. Only this protected leaf
  # receives the read/traverse label; DAC still prevents unprivileged writes.
  [ "$(systemctl show -p MainPID --value consentd-poc.service)" = 0 ]
  socket_state=$(systemctl show -p ActiveState --value consentd-poc.socket)
  [ "$socket_state" = inactive ] || [ "$socket_state" = failed ]
  if [ ! -e "$runtime" ] && [ ! -L "$runtime" ]; then mkdir -m 0755 "$runtime"; fi
  [ -d "$runtime" ] && [ ! -L "$runtime" ]
  [ "$(stat -c '%u:%g:%a' "$runtime")" = 0:0:755 ]
  chsmack -a _ "$runtime"
  [ "$(chsmack "$runtime")" = "$runtime access=\"_\"" ]
}
cleanup_probe_start() {
  result=$?
  trap - EXIT HUP INT TERM
  systemctl stop consentd-poc.socket consentd-poc.service || result=1
  rm -f /run/systemd/system/consentd-poc.service.d/observe.conf || result=1
  rmdir /run/systemd/system/consentd-poc.service.d || result=1
  systemctl daemon-reload || result=1
  exit "$result"
}
case "$phase" in
  probe-start)
    systemctl stop consentd-poc.socket consentd-poc.service
    prepare_runtime
    date +%s > "$control/probe-start-time"
    [ ! -e /run/systemd/system/consentd-poc.service.d ]
    mkdir -m 0755 /run/systemd/system/consentd-poc.service.d
    trap cleanup_probe_start EXIT
    trap 'exit 1' HUP INT TERM
    cat > /run/systemd/system/consentd-poc.service.d/observe.conf <<EOF
[Service]
Type=simple
ExecStart=
ExecStart=$tools/consent-poc-peer-probe
Restart=no
EOF
    systemctl daemon-reload
    systemctl start consentd-poc.socket
    trap - EXIT HUP INT TERM
    echo 'READY socket peer observation; launch the UI within the activated probe timeout'
    ;;
  probe-stop)
    # A short-lived probe can exit before journald resolves its cgroup/unit.
    # The inherited stream identifier remains available for the bounded run.
    since=$(cat "$control/probe-start-time")
    case "$since" in ''|*[!0-9]*) exit 1;; esac
    journalctl SYSLOG_IDENTIFIER=consentd-poc --since "@$since" -n 80 \
      --no-pager -o cat > "$control/peer-observation.log"
    systemctl stop consentd-poc.socket consentd-poc.service
    rm /run/systemd/system/consentd-poc.service.d/observe.conf
    rmdir /run/systemd/system/consentd-poc.service.d
    systemctl daemon-reload
    cat "$control/peer-observation.log"
    ;;
  configure)
    # The diagnostic must be fully removed before a real daemon can be READY.
    [ ! -e /run/systemd/system/consentd-poc.service.d ]
    [ ! -L /run/systemd/system/consentd-poc.service.d ]
    [ -z "$(systemctl show -p DropInPaths --value consentd-poc.service)" ]
    [ "$(systemctl show -p FragmentPath --value consentd-poc.service)" = /usr/lib/systemd/system/consentd-poc.service ]
    [ "$(systemctl show -p Type --value consentd-poc.service)" = notify ]
    start=$(systemctl show -p ExecStart --value consentd-poc.service)
    printf '%s\n' "$start" | grep -F "path=$tools/consentd-poc ; argv[]=$tools/consentd-poc ;" > /dev/null
    [ "$#" = 1 ] || [ "$#" = 3 ]
    operation=${2:-poc-ui-install-1}
    expected=${3:-absent}
    case "$operation" in ''|*[!a-zA-Z0-9_.-]*) exit 2;; esac
    case "$expected" in ''|*[!a-zA-Z0-9_.-]*) exit 2;; esac
    [ "${#operation}" -le 100 ] && [ "${#expected}" -le 128 ]
    # The operator must first preserve this exact package label observed by the
    # kernel probe at this socket. A process label or guessed prefix is not enough.
    grep -Fq '"label":"User::Pkg::org.tizen.consentui"' "$control/peer-observation.log"
    app=$(pkginfo --app "$package")
    printf '%s\n' "$app" | grep -Fx "Package: $package" > /dev/null
    printf '%s\n' "$app" | grep -Fx 'Exec: /opt/usr/globalapps/org.tizen.consentui/bin/ConsentUI.dll' > /dev/null
    apps=$(pkginfo --list "$package")
    [ "$(printf '%s\n' "$apps" | grep -c '^Appid: ')" = 1 ]
    printf '%s\n' "$apps" | grep -Fx "Appid: $package" > /dev/null
    app_uid=$(id -u owner)
    [ "$(id -g owner)" = "$(getent group users | cut -d: -f3)" ]
    # Capture installed payload ownership before any UI rule is provisioned.
    stat -c '%u:%g:%a:%d:%i %n' /usr/bin/dotnet-hydra-loader \
      /opt/usr/globalapps /opt/usr/globalapps/org.tizen.consentui \
      /opt/usr/globalapps/org.tizen.consentui/bin \
      /opt/usr/globalapps/org.tizen.consentui/bin/ConsentUI.dll > "$control/payload-identity.log"
    pkg_uid=$(getent passwd tizenglobalapp | cut -d: -f3)
    [ -n "$pkg_uid" ] && [ "$pkg_uid" != "$app_uid" ]
    case " $(id -G owner) " in *' 0 '*) exit 1;; esac
    for path in /opt /opt/usr /opt/usr/globalapps \
        /opt/usr/globalapps/org.tizen.consentui \
        /opt/usr/globalapps/org.tizen.consentui/bin \
        /opt/usr/globalapps/org.tizen.consentui/bin/ConsentUI.dll; do
      [ ! -L "$path" ]
      uid=$(stat -c %u "$path")
      [ "$uid" = 0 ] || [ "$uid" = "$pkg_uid" ]
      [ "$(stat -c %g "$path")" = 0 ]
      mode=$(stat -c %a "$path")
      [ $((0$mode & 0002)) -eq 0 ]
    done
    systemctl stop consentd-poc.socket consentd-poc.service
    prepare_runtime
    temporary=$(mktemp "$config/.roles.XXXXXX")
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    printf '[policy]\nversion=1\n' > "$temporary"
    for role in installer argo cm ce holder; do
      roles=$role
      case "$role" in argo) roles='argo;session';; cm|ce) roles=checker;; esac
      cat >> "$temporary" <<EOF
[identity mock-$role]
uid=0
executable=$tools/consent-mock-$role
label=System
roles=$roles;
subjects=owner;
profiles=default;
enforcers=mock-ce;
packages=$package;
EOF
    done
    cat >> "$temporary" <<EOF
[identity consent-ui]
uid=$app_uid
executable=/usr/bin/dotnet-hydra-loader
label=User::Pkg::org.tizen.consentui
roles=ui;
subjects=owner;
profiles=default;
EOF
    chmod 0644 "$temporary"
    chsmack -a System "$temporary"
    mv "$temporary" "$config/roles.conf"
    trap - EXIT HUP INT TERM
    systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
      "$tools/consent-storage-prepare-poc"
    # Stable explicit installation transaction. Repeated setup never invents a
    # different installation generation or silently overwrites another owner.
    authority="$tools/consent-installation-authority-poc"
    generation=$(systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
      "$authority" begin "$package" "$operation" "$expected")
    systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
      "$authority" attach "$package" "$package" "$operation-attach" "$generation"
    systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
      "$authority" commit "$package" "$operation-commit" "$generation"
    printf '%s\n' "$generation" > "$control/generation"
    mkdir -p "$control/fixtures"
    for input in /usr/share/consent/poc/fixtures/*.ini; do
      sed "s/@INSTALL_GENERATION@/$generation/g" "$input" > "$control/fixtures/${input##*/}"
    done
    systemctl start consentd-poc.socket consentd-poc.service
    [ "$(systemctl show -p Type --value consentd-poc.service)" = notify ]
    pid=$(systemctl show -p MainPID --value consentd-poc.service)
    case "$pid" in ''|0|*[!0-9]*) exit 1;; esac
    [ "$(readlink "/proc/$pid/exe")" = "$tools/consentd-poc" ]
    [ "$(stat -c '%u:%a' "$runtime")" = 0:755 ]
    [ "$(stat -c '%u:%G:%a' "$runtime/consent.sock")" = 0:users:660 ]
    systemctl is-active consentd-poc.service
    echo 'READY explicit PoC roles and generation; definitions still require the installer C API'
    ;;
  stop)
    systemctl stop consentd-poc.socket consentd-poc.service
    [ "$(systemctl show -p MainPID --value consentd-poc.service)" = 0 ]
    echo 'PASS PoC daemon stopped; preserved state and package are available for review'
    ;;
  *) echo 'Usage: emulator-poc-setup.sh probe-start|probe-stop|configure [INSTALL_OPERATION EXPECTED_GENERATION]|stop' >&2; exit 2;;
esac
