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
set -eu
# Selected DEVELOPMENT emulator only. Run as root with SmackProcessLabel=System.
# Uses the actual production adapter and unchanged default-deny roles. Both
# original stores are moved intact and restored on exit; no package is installed.
[ "$(id -u)" = 0 ]
state=/opt/var/lib/consentd
inventory=/opt/var/lib/consent-authority
fixture=/opt/var/lib/consent-offline-platform-$$
api=/usr/libexec/consent/tests/consent-api-test
authority=/usr/sbin/consent-installation-authority
# Verify these exact apps on this selected image instead of guessing identity.
calendar_info=$(pkginfo --app org.tizen.calendar)
camera_info=$(pkginfo --app attach-panel-camera)
printf '%s\n' "$calendar_info" | grep -Fx 'Package: org.tizen.calendar'
printf '%s\n' "$camera_info" | grep -Fx 'Package: attach-panel-camera'
[ ! -e "$fixture" ] && [ ! -L "$fixture" ]
mkdir -m 700 "$fixture"
service=consentd.service
socket=consentd.socket
original_state_present=0
original_inventory_present=0
swap_started=0
original_service=$(systemctl show "$service" -p ActiveState --value)
original_socket=$(systemctl show "$socket" -p ActiveState --value)
case "$original_service:$original_socket" in
  active:active|active:inactive|inactive:active|inactive:inactive|failed:active|failed:inactive) ;;
  *) echo 'FAIL units are transitioning; retry after they settle' >&2; exit 1 ;;
esac
exists() { [ -e "$1" ] || [ -L "$1" ]; }
stop() {
  systemctl stop "$socket" "$service" 9<&- || return 1
  [ "$(systemctl show "$service" -p MainPID --value 9<&-)" = 0 ] || return 1
  case "$(systemctl show "$service" -p ActiveState --value 9<&-)" in
    inactive|failed) ;;
    *) return 1 ;;
  esac
  [ "$(systemctl show "$socket" -p ActiveState --value 9<&-)" = inactive ]
}
start() { systemctl start "$socket" "$service" 9<&-; }
restore_store() {
  restore_path=$1 restore_backup=$2 restore_result=$3 restore_present=$4
  if exists "$restore_backup"; then
    if exists "$restore_path"; then
      ! exists "$restore_result" || return 1
      mv "$restore_path" "$restore_result" || return 1
    fi
    mv "$restore_backup" "$restore_path" || return 1
  elif [ "$restore_present" = 0 ] && exists "$restore_path"; then
    ! exists "$restore_result" || return 1
    mv "$restore_path" "$restore_result" || return 1
  fi
}
cleanup() {
  saved_status=$?
  trap - EXIT
  trap '' HUP INT TERM
  if ! stop; then
    echo "FAIL could not confirm daemon/socket stopped; stores left in place, backups retained at $fixture" >&2
    exec 9<&-
    exit 1
  fi
  if [ "$swap_started" = 1 ]; then
    if ! restore_store "$state" "$fixture/original-state" "$fixture/result-state" "$original_state_present" ||
       ! restore_store "$inventory" "$fixture/original-authority" "$fixture/result-authority" "$original_inventory_present"; then
      echo "FAIL store restoration incomplete; retained evidence/backups at $fixture" >&2
      exec 9<&-
      exit 1
    fi
  fi
  # The lock follows the original inode through both renames. Release it only
  # after restoration, before ExecStartPre and the original daemon reacquire it.
  exec 9<&-
  if [ "$original_socket" = active ]; then
    systemctl start "$socket" || exit 1
  fi
  if [ "$original_service" = active ]; then
    systemctl start "$service" || exit 1
  fi
  echo "Offline fixture evidence retained: $fixture; original stores and active units restored"
  exit "$saved_status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
stop
# Existing prepared authority is required. Holding its EX lock excludes an
# Installer writer even while the original directory is moved into the fixture.
[ -d "$inventory" ] && [ ! -L "$inventory" ]
[ -f "$inventory/lifecycle.lock" ] && [ ! -L "$inventory/lifecycle.lock" ]
[ "$(stat -c '%u:%h' "$inventory/lifecycle.lock")" = 0:1 ]
exec 9< "$inventory/lifecycle.lock"
flock -n -x 9
if exists "$state"; then
  [ -d "$state" ] && [ ! -L "$state" ]
  original_state_present=1
fi
original_inventory_present=1
swap_started=1
if [ "$original_state_present" = 1 ]; then mv "$state" "$fixture/original-state"; fi
mv "$inventory" "$fixture/original-authority"
printf '%s\n' "$calendar_info" > "$fixture/pkginfo-calendar.txt"
printf '%s\n' "$camera_info" > "$fixture/pkginfo-camera.txt"
generation=$("$authority" 9<&- --image-root / begin org.tizen.calendar platform-begin absent)
"$authority" 9<&- --image-root / attach org.tizen.calendar org.tizen.calendar platform-app "$generation"
# pkginfo above confirms that this pre-existing app is installed. This is an
# explicit development provisioning transaction, not an Installer-hook claim.
"$authority" 9<&- --image-root / commit org.tizen.calendar platform-commit "$generation"
wrong=$("$authority" 9<&- --image-root / begin consent.offline.wrong wrong-begin absent)
"$authority" 9<&- --image-root / attach consent.offline.wrong attach-panel-camera wrong-app "$wrong"
"$authority" 9<&- --image-root / commit consent.offline.wrong wrong-commit "$wrong"
stage() {
  "$api" 9<&- register "$1" "$2" --offline-image-root=/ \
    expected_generation="$3" operation_id="$4" definition="$5" \
    enforcer=unconfigured.product.enforcer policy_version=1 text_revision=1 \
    level=1 modes=PERSISTENT default_locale=en message.en.title=Allow message.en.body=Read
}
stage org.tizen.calendar org.tizen.calendar "$generation" platform-definition consent.offline.platform
stage consent.offline.wrong attach-panel-camera "$wrong" wrong-definition consent.offline.wrong
stage org.tizen.calendar org.tizen.calendar stale-generation stale-definition consent.offline.stale
[ ! -e "$state/consent.db" ]
[ ! -e /run/.consentd.sock ]
start
pid=$(systemctl show consentd.service -p MainPID --value)
[ "$pid" -gt 0 ]
[ "$(systemctl show consentd.service -p User --value)" = security_fw ]
# The root staging privilege must not turn into a live caller-role bypass.
"$api" check > "$fixture/live-role-probe.txt" 2>&1 9<&- &
probe_pid=$!
if wait "$probe_pid"; then
  echo 'FAIL root offline writer gained a live role' >&2
  exit 1
else
  probe_exit=$?
fi
[ "$probe_exit" = 1 ]
cat "$fixture/live-role-probe.txt"
probe_status=$(sed -n 's/^status=//p' "$fixture/live-role-probe.txt")
case "$probe_status" in
  -107|-2147483647) ;; # Pre-hello close: disconnected or already-sent outcome unknown.
  *) echo "FAIL unexpected identity-rejection status: $probe_status" >&2; exit 1 ;;
esac
if grep -q '^iteration=' "$fixture/live-role-probe.txt"; then
  echo 'FAIL untrusted peer passed client initialization' >&2
  exit 1
fi
rejection_seen=0
attempt=0
while [ "$attempt" -lt 30 ]; do
  journalctl -b -u "$service" _PID="$pid" --no-pager -o cat 9<&- > "$fixture/live-role-journal.txt"
  if grep -F " pid=$probe_pid uid=0 " "$fixture/live-role-journal.txt" |
      grep -F 'reason=no matching live trusted identity '; then
    rejection_seen=1
    break
  fi
  attempt=$((attempt + 1))
  sleep 0.1 9<&-
done
[ "$rejection_seen" = 1 ]
printf 'PASS live identity rejection: daemon_pid=%s client_pid=%s observed_status=%s\n' "$pid" "$probe_pid" "$probe_status"

stop
[ "$(sqlite3 -readonly "$state/consent.db" 'PRAGMA integrity_check;')" = ok ]
[ "$(sqlite3 -readonly "$state/consent.db" "SELECT count(*) FROM definitions WHERE id='consent.offline.platform' AND package='org.tizen.calendar' AND app='org.tizen.calendar' AND active=1;")" = 1 ]
[ "$(sqlite3 -readonly "$state/consent.db" "SELECT count(*) FROM definitions WHERE id IN ('consent.offline.wrong','consent.offline.stale');")" = 0 ]
[ "$(sqlite3 -readonly "$state/consent.db" 'SELECT count(*) FROM grants;')" = 0 ]
echo 'PASS production public offline C registration imports actual pkgmgr app/package only; mismatch/stale stay inactive; no grants; live roles still deny'
