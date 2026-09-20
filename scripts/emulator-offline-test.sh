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
# The ordinary isolated harness must have prepared its role/socket units first.
[ "$(id -u)" = 0 ]
tools=/usr/libexec/consent/tests
api=$tools/consent-api-test-isolated
authority=$tools/consent-installation-authority-isolated
state=/opt/var/lib/consent-test
inventory=/opt/var/lib/consent-test-authority
fixture=/opt/var/lib/consent-offline-evidence-$$
image=$fixture/image
service=consentd-isolated.service
socket=consentd-isolated.socket
logging_directory=/run/systemd/system/consentd-isolated.service.d
logging_dropin=$logging_directory/zz-consent-offline-journal-$$.conf
logging_owned=0
logging_directory_created=0
[ ! -e "$fixture" ] && [ ! -L "$fixture" ]
mkdir -m 700 "$fixture"
mkdir -m 700 "$image"
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
reset_failure() {
  reset_state=$(systemctl show "$service" -p ActiveState --value 9<&-) || return 1
  case "$reset_state" in
    inactive) ;; # Inactive units may already have been garbage-collected.
    failed) systemctl reset-failed "$service" 9<&- ;;
    *) echo "FAIL unexpected state before startup: $reset_state" >&2; return 1 ;;
  esac
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
restore_logging() {
  if [ "$logging_owned" = 1 ]; then
    rm -f "$logging_dropin" || return 1
    systemctl daemon-reload 9<&- || return 1
    logging_owned=0
    if [ "$logging_directory_created" = 1 ]; then
      # Never remove another test's files if it added a drop-in concurrently.
      rmdir "$logging_directory" 2>/dev/null || true
    fi
  fi
}
cleanup() {
  saved_status=$?
  trap - EXIT
  trap '' HUP INT TERM
  if ! stop; then
    echo "FAIL could not confirm daemon/socket stopped; stores left in place, backups retained at $fixture" >&2
    restore_logging || echo "FAIL could not restore isolated logging: $logging_dropin" >&2
    exec 9<&-
    exit 1
  fi
  if [ "$swap_started" = 1 ]; then
    if ! restore_store "$state" "$fixture/original-state" "$fixture/result-state" "$original_state_present" ||
       ! restore_store "$inventory" "$fixture/original-authority" "$fixture/result-authority" "$original_inventory_present"; then
      echo "FAIL store restoration incomplete; retained evidence/backups at $fixture" >&2
      restore_logging || echo "FAIL could not restore isolated logging: $logging_dropin" >&2
      exec 9<&-
      exit 1
    fi
  fi
  if ! restore_logging; then
    echo "FAIL isolated logging restoration incomplete: $logging_dropin" >&2
    exec 9<&-
    exit 1
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
# Tizen isolated units may route stderr only to dlog. This unique test-only
# override makes per-attempt journal cursors authoritative and is removed on
# every controlled exit before any original service is restarted.
! exists "$logging_dropin"
if exists "$logging_directory"; then
  [ -d "$logging_directory" ] && [ ! -L "$logging_directory" ]
else
  logging_directory_created=1
  mkdir -m 755 "$logging_directory"
fi
logging_owned=1
cat > "$logging_dropin" <<'EOF'
[Service]
StandardOutput=journal
StandardError=journal
EOF
systemctl daemon-reload 9<&-
[ "$(systemctl show "$service" -p StandardOutput --value 9<&-)" = journal ]
[ "$(systemctl show "$service" -p StandardError --value 9<&-)" = journal ]
stage() {
  stage_root=$1 stage_package=$2 stage_app=$3 stage_generation=$4
  stage_operation=$5 stage_definition=$6 stage_policy=$7 stage_text=$8
  shift 8
  "$api" 9<&- register "$stage_package" "$stage_app" --offline-image-root="$stage_root" \
    operation_id="$stage_operation" expected_generation="$stage_generation" \
    definition="$stage_definition" enforcer=scenario policy_version="$stage_policy" text_revision="$stage_text" \
    level=1 modes=PERSISTENT default_locale=en message.en.title=Allow message.en.body=Read \
    "$@"
}
query() {
  definition=$1 decision=$2
  shift 2
  "$api" 9<&- check subject=demo.subject profile=demo.profile count=1 \
    r0.definition="$definition" r0.operation=read r0.scope=offline-scope \
    r0.purpose=answer r0.holder=scenario --expect-decision="$decision" "$@"
}
assert_db() {
  stop
  [ "$(sqlite3 -readonly "$state/consent.db" 'PRAGMA integrity_check;')" = ok ]
  [ "$(sqlite3 -readonly "$state/consent.db" 'SELECT count(*) FROM grants;')" = 0 ]
  echo 'PASS offline import creates no user approvals; integrity=ok'
}
# This CLI provisions installation identity only; every definition is staged by
# the public consent_register API in the separately linked C executable.
generation=$("$authority" 9<&- --image-root "$image" begin demo.package offline-begin absent)
"$authority" 9<&- --image-root "$image" attach demo.package demo.app offline-app1 "$generation"
"$authority" 9<&- --image-root "$image" attach demo.package demo.app2 offline-app2 "$generation"
"$authority" 9<&- --image-root "$image" commit demo.package offline-commit "$generation"
other=$("$authority" 9<&- --image-root "$image" begin offline.other other-begin absent)
"$authority" 9<&- --image-root "$image" attach offline.other offline.otherapp other-app "$other"
"$authority" 9<&- --image-root "$image" commit offline.other other-commit "$other"
# Deliberately create the newest revision first; import order must be numeric.
stage "$image" demo.package demo.app "$generation" offline-high demo.offline 2 2 --repeat=2
stage "$image" demo.package demo.app "$generation" offline-low demo.offline 1 1
stage "$image" demo.package demo.app2 "$generation" offline-second demo.offline.second 1 1
stage "$image" offline.other offline.otherapp "$other" offline-other other.offline 1 1
stage "$image" demo.package demo.app stale-generation offline-stale demo.offline.stale 1 1
stage "$image" demo.package demo.app "$generation" offline-high demo.changed 2 2 \
  --expect-status=-2147483644
"$api" 9<&- check --offline-image-root="$image" --expect-status=-38
"$api" 9<&- update demo.package demo.app --offline-image-root="$image" --expect-status=-38
[ ! -e "$image$state" ]
[ ! -e /tmp/consent-test/consent.sock ]
echo 'PASS socket-absent public C consent_register STAGED, exact retry/conflict, unsupported methods, no DB'
# Simulate publishing the built image into this isolated target's fixed paths.
# Preserve both previous test stores verbatim and restore them on every exit.
# Set the phase before either rename; cleanup discovers completed moves from
# backup existence even if a signal arrives between mv and the next command.
swap_started=1
if [ "$original_state_present" = 1 ]; then mv "$state" "$fixture/original-state"; fi
mv "$inventory" "$fixture/original-authority"
mv "$image$inventory" "$inventory"
# Startup must reject invalid authority before creating any policy database.
# The helper may prepare/label the state directory but never parses this file.
cp "$inventory/installations.conf" "$fixture/valid-installations.conf"
reject_authority_startup() {
  invalid_kind=$1
  cursor_output=$(journalctl -b -u "$service" -n 1 --show-cursor --no-pager -o cat 9<&-)
  journal_cursor=$(printf '%s\n' "$cursor_output" | sed -n 's/^-- cursor: //p')
  [ -n "$journal_cursor" ]
  authority_inode=$(stat -c '%d:%i' "$inventory/installations.conf")
  reset_failure
  if start > "$fixture/start-$invalid_kind.txt" 2>&1; then
    echo "FAIL daemon started with $invalid_kind authority" >&2
    exit 1
  fi
  # Cancel Restart=on-failure before restoring bytes or inspecting the database.
  stop
  [ ! -e "$state/consent.db" ] && [ ! -L "$state/consent.db" ]
  journalctl -b -u "$service" --after-cursor="$journal_cursor" --no-pager -o cat 9<&- \
    > "$fixture/journal-$invalid_kind.txt"
  grep -F 'event=database-open-failed reason=offline-authority-preflight status=-22' \
    "$fixture/journal-$invalid_kind.txt"
  [ "$(stat -c '%d:%i' "$inventory/installations.conf")" = "$authority_inode" ]
  prepared_metadata=$(stat -c '%d:%i:%u:%g:%a' "$inventory/installations.conf")
  # Overwrite only bytes through the same inode. Keep the helper-normalized
  # ownership, permissions and System SMACK xattr intact.
  cat "$fixture/valid-installations.conf" > "$inventory/installations.conf"
  [ "$(stat -c '%d:%i:%u:%g:%a' "$inventory/installations.conf")" = "$prepared_metadata" ]
  expected_hash=$(sha256sum "$fixture/valid-installations.conf")
  actual_hash=$(sha256sum "$inventory/installations.conf")
  [ "${expected_hash%% *}" = "${actual_hash%% *}" ]
  reset_failure
  echo "PASS $invalid_kind authority rejected before database creation; valid bytes restored"
}
printf '[malformed\n' > "$inventory/installations.conf"
reject_authority_startup malformed
printf '[authority]\nschema=2\n' > "$inventory/installations.conf"
reject_authority_startup unsupported-schema
start
query demo.offline CONSENT_REQUIRED r0.policy_version=2
query demo.offline.second CONSENT_REQUIRED
query other.offline CONSENT_REQUIRED
query demo.offline.stale DENIED
# The live daemon holds SH lifecycle. An explicit offline handle must not turn
# a running target into a second writer; existing metadata is unchanged.
metadata_before=$(stat -c '%u:%g:%a' "$inventory/lifecycle.lock")
"$api" 9<&- register demo.package demo.app --offline-image-root=/ --expect-status=-16
if "$authority" 9<&- --image-root / begin demo.package busy-operation "$generation"; then
  echo 'FAIL live authority accepted offline writer' >&2
  exit 1
fi
[ "$(stat -c '%u:%g:%a' "$inventory/lifecycle.lock")" = "$metadata_before" ]
assert_db
start
query demo.offline CONSENT_REQUIRED r0.policy_version=2
query other.offline CONSENT_REQUIRED
assert_db
# Definitions and their dedup ledger survive DB loss; approvals cannot appear.
rm "$state/consent.db"
start
query demo.offline CONSENT_REQUIRED r0.policy_version=2
query other.offline CONSENT_REQUIRED
assert_db
# An unseen old seed cannot revive a same-generation definition after removal.
start
"$api" 9<&- unregister demo.package operation_id=offline-remove expected_generation="$generation"
stop
"$api" 9<&- register demo.package demo.app --offline-image-root=/ \
  operation_id=offline-unseen expected_generation="$generation" definition=demo.offline \
  enforcer=scenario policy_version=2 text_revision=2 level=1 modes=PERSISTENT \
  default_locale=en message.en.title=Allow message.en.body=Read
start
query demo.offline DENIED
query demo.offline.second DENIED
query other.offline CONSENT_REQUIRED
assert_db
# A new real installation generation permits new registration, while immutable
# old-generation records stay inactive and the other package remains valid.
next=$("$authority" 9<&- --image-root / begin demo.package offline-reinstall "$generation")
"$authority" 9<&- --image-root / attach demo.package demo.app offline-reinstall-app1 "$next"
"$authority" 9<&- --image-root / attach demo.package demo.app2 offline-reinstall-app2 "$next"
"$authority" 9<&- --image-root / commit demo.package offline-reinstall-commit "$next"
stage / demo.package demo.app "$next" offline-new-install demo.offline 2 2
start
query demo.offline CONSENT_REQUIRED r0.policy_version=2
query demo.offline.second DENIED
query other.offline CONSENT_REQUIRED
assert_db
start
query demo.offline CONSENT_REQUIRED r0.policy_version=2
query other.offline CONSENT_REQUIRED
assert_db
echo 'PASS deterministic revisions, restart/DB-loss dedup, unregister tombstone, stale generation and unrelated package'
