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
runtime=/opt/var/lib/consent-feature-runtime
control=/opt/var/lib/consent-poc-control
tools=/usr/libexec/consent/poc
roles=/etc/consent-poc/roles.conf
for path in /opt /opt/var /opt/var/lib /etc /etc/consent-poc "$control"; do
  [ -d "$path" ] && [ ! -L "$path" ]
  [ "$(stat -c %u "$path")" = 0 ]
  mode=$(stat -c %a "$path")
  [ $((0$mode & 0022)) -eq 0 ]
done
[ -f "$roles" ] && [ ! -L "$roles" ]
[ "$(stat -c '%u:%h' "$roles")" = 0:1 ]
restore_roles() {
  [ -f "$control/feature-original-roles.conf" ] && [ ! -L "$control/feature-original-roles.conf" ] || return 1
  [ "$(stat -c '%u:%h:%a' "$control/feature-original-roles.conf")" = 0:1:600 ] || return 1
  systemctl stop consentd-poc.socket consentd-poc.service || return 1
  [ "$(systemctl show -p MainPID --value consentd-poc.service)" = 0 ] || return 1
  cp "$control/feature-original-roles.conf" "$roles" || return 1
  chmod 0644 "$roles" || return 1
  chsmack -a System "$roles" || return 1
  systemctl start consentd-poc.socket consentd-poc.service || return 1
  rm "$control/feature-original-roles.conf" || return 1
}
rollback_start() {
  result=$?
  trap - EXIT HUP INT TERM
  # Never restore role configuration while either consumer remains active.
  if systemctl stop consent-feature-poc.socket consent-feature-poc.service &&
      [ "$(systemctl show -p MainPID --value consent-feature-poc.service)" = 0 ]; then
    restore_roles || result=1
  else
    result=1
  fi
  exit "$result"
}
case "$phase" in
  start)
    # This opt-in fixture assumes the exact observed package/UI roles were
    # provisioned with emulator-poc-setup first. No production role is touched.
    grep -Fx 'label=User::Pkg::org.tizen.consentui' "$roles" > /dev/null
    [ -z "$(systemctl show -p DropInPaths --value consent-feature-poc.service)" ]
    [ -z "$(systemctl show -p DropInPaths --value consent-feature-poc.socket)" ]
    systemctl stop consent-feature-poc.socket consent-feature-poc.service
    [ "$(systemctl show -p MainPID --value consent-feature-poc.service)" = 0 ]
    if [ ! -e "$runtime" ] && [ ! -L "$runtime" ]; then mkdir -m 0755 "$runtime"; fi
    [ -d "$runtime" ] && [ ! -L "$runtime" ]
    [ "$(stat -c '%u:%g:%a' "$runtime")" = 0:0:755 ]
    chsmack -a _ "$runtime"
    [ "$(chsmack "$runtime")" = "$runtime access=\"_\"" ]
    # Preserve the PoC role configuration for explicit cleanup. Refuse to
    # overwrite a prior unfinished feature run's backup.
    [ ! -e "$control/feature-original-roles.conf" ]
    [ ! -L "$control/feature-original-roles.conf" ]
    cp "$roles" "$control/feature-original-roles.conf"
    chmod 0600 "$control/feature-original-roles.conf"
    trap rollback_start EXIT
    trap 'exit 1' HUP INT TERM
    systemctl stop consentd-poc.socket consentd-poc.service
    /usr/bin/python3 - "$roles" <<'PYTHON'
import configparser,os,stat,sys
path=sys.argv[1]
info=os.lstat(path)
assert stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and not(info.st_mode&0o022)
p=configparser.ConfigParser(interpolation=None)
p.read(path)
for role in ('cm','ce'):
 section='identity mock-'+role
 assert p[section]['executable']=='/usr/libexec/consent/poc/consent-mock-'+role
 assert p[section]['roles']=='checker;'
 p[section]['enforcers']='mock-'+role+';'
with open(path,'w') as output:
 p.write(output)
 output.flush();os.fsync(output.fileno())
PYTHON
    chsmack -a System "$roles"
    systemctl start consentd-poc.socket consentd-poc.service
    generation=$(cat "$control/generation")
    case "$generation" in ''|*[!a-zA-Z0-9_.-]*) exit 1;; esac
    systemd-run --quiet --wait --pipe -p SmackProcessLabel=System \
      "$tools/consent-mock-installer" feature-register "$generation"
    systemctl start consent-feature-poc.socket consent-feature-poc.service
    pid=$(systemctl show -p MainPID --value consent-feature-poc.service)
    case "$pid" in ''|0|*[!0-9]*) exit 1;; esac
    [ "$(readlink "/proc/$pid/exe")" = "$tools/consent-mock-argo" ]
    [ "$(stat -c '%u:%G:%a' "$runtime/argo.sock")" = 0:users:660 ]
    systemctl is-active consent-feature-poc.service
    trap - EXIT HUP INT TERM
    echo 'READY isolated feature settings; all features initially unselected'
    ;;
  stop)
    systemctl stop consent-feature-poc.socket consent-feature-poc.service
    [ "$(systemctl show -p MainPID --value consent-feature-poc.service)" = 0 ]
    if [ -e "$control/feature-original-roles.conf" ] || [ -L "$control/feature-original-roles.conf" ]; then
      restore_roles
    fi
    echo 'PASS feature coordinator stopped and PoC roles restored'
    ;;
  *) echo 'Usage: emulator-feature-setup.sh start|stop' >&2; exit 2;;
esac
