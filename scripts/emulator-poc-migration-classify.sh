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
[ "$(id -u)" = 0 ]
helper=/usr/libexec/consent/tests/consent-storage-prepare-poc-fixture
state=/opt/var/lib/consent-poc-classify-state
authority=/opt/var/lib/consent-poc-classify-authority
control=/opt/var/lib/consent-poc-classify-control
unit=/run/systemd/system/consentd-poc-classify-test.service
service=consentd-poc-classify-test.service
evidence=/opt/var/lib/consent-poc-classify-evidence-$$
for path in "$state" "$authority" "$control" "$unit" "$evidence"; do
  [ ! -e "$path" ] && [ ! -L "$path" ]
done
mkdir -m 700 "$evidence" "$control"
cleanup() {
  result=$?
  trap - EXIT
  systemctl stop "$service" >/dev/null 2>&1 || true
  if [ "$(systemctl show "$service" -p MainPID --value)" != 0 ]; then
    echo 'FAIL classification daemon still runs; paths retained' >&2
    exit 1
  fi
  for path in "$state" "$authority" "$control"; do
    if [ -e "$path" ] || [ -L "$path" ]; then
      mv "$path" "$evidence/" || exit 1
    fi
  done
  if [ -e "$unit" ]; then
    cp "$unit" "$evidence/" || exit 1
    rm "$unit" || exit 1
  fi
  systemctl daemon-reload
  echo "classification fixture evidence=$evidence"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cat > "$unit" <<SERVICE
[Unit]
Description=Consent POC missing-state classification fixture
[Service]
Type=simple
User=security_fw
Group=security_fw
SmackProcessLabel=System
ExecStartPre=+$helper
ExecStart=/bin/false
Restart=no
SERVICE
systemctl daemon-reload
mkdir -m 750 "$authority"
chown root:security_fw "$authority"
printf '[authority]\nschema=1\n' > "$authority/installations.conf"
: > "$authority/installations.lock"
: > "$authority/lifecycle.lock"
chown root:security_fw "$authority/installations.conf" \
  "$authority/installations.lock" "$authority/lifecycle.lock"
chmod 640 "$authority/installations.conf" "$authority/lifecycle.lock"
chmod 600 "$authority/installations.lock"
source_before=$(stat -c '%d:%i:%u:%g:%a:%s' \
  /opt/var/lib/consent-poc-state \
  /opt/var/lib/consent-poc-state/consent.db \
  /opt/var/lib/consent-poc-state/definitions.registry \
  /opt/var/lib/consent-poc-authority \
  /opt/var/lib/consent-poc-authority/installations.conf \
  /opt/var/lib/consent-poc-authority/bootstrap.receipt)
source_hash_before=$(sha256sum \
  /opt/var/lib/consent-poc-state/consent.db \
  /opt/var/lib/consent-poc-state/definitions.registry \
  /opt/var/lib/consent-poc-authority/installations.conf \
  /opt/var/lib/consent-poc-authority/bootstrap.receipt)
fixture_before=$(stat -c '%n %d:%i:%u:%g:%a:%s' \
  "$authority/installations.conf" "$authority/installations.lock" \
  "$authority/lifecycle.lock")
fixture_hash_before=$(sha256sum "$authority/installations.conf" \
  "$authority/installations.lock" "$authority/lifecycle.lock")
printf '%s\n' "$source_before" > "$evidence/source-before.txt"
printf '%s\n' "$source_hash_before" > "$evidence/source-hash-before.txt"
printf '%s\n' "$fixture_before" > "$evidence/fixture-before.txt"
printf '%s\n' "$fixture_hash_before" > "$evidence/fixture-hash-before.txt"
[ ! -e "$state" ]
systemd-run --quiet --wait --pipe \
  --unit="consent-poc-classify-migrate-$$" \
  -p SmackProcessLabel=System::Privileged \
  "$helper" --migrate-fixture > "$evidence/migrate.txt" 2>&1
grep -Fx 'phase=RECOVERY_REQUIRED' "$authority/bootstrap.receipt"
[ ! -e "$state" ]
receipt_before=$(stat -c '%d:%i:%u:%g:%a:%s' \
  "$authority/bootstrap.receipt")
receipt_hash_before=$(sha256sum "$authority/bootstrap.receipt")
printf '%s\n' "$receipt_before" > "$evidence/receipt-before.txt"
printf '%s\n' "$receipt_hash_before" > "$evidence/receipt-hash-before.txt"
echo 'PASS absent-state POC fixture classified RECOVERY_REQUIRED'
dlogutil -d STDERR_consent-storage-prepare-poc-fixture:V '*:S' \
  > "$evidence/dlog-before.txt"
if systemctl start "$service" > "$evidence/start.txt" 2>&1; then
  echo 'FAIL RECOVERY_REQUIRED service admitted' >&2
  exit 1
fi
[ "$(systemctl show "$service" -p MainPID --value)" = 0 ]
[ "$(systemctl show "$service" -p Result --value)" = exit-code ]
[ ! -e "$state" ]
journalctl -u "$service" --no-pager > "$evidence/journal.txt"
systemctl show "$service" -p ExecStartPre \
  > "$evidence/exec-start-pre.txt"
grep -F 'code=exited' "$evidence/exec-start-pre.txt" \
  | grep -F 'status=1' > "$evidence/failed-preflight.txt"
pre_pid=$(sed -n 's/.*pid=\([0-9][0-9]*\).*/\1/p' \
  "$evidence/failed-preflight.txt")
[ -n "$pre_pid" ]
dlogutil -d STDERR_consent-storage-prepare-poc-fixture:V '*:S' \
  > "$evidence/dlog-after.txt"
before_lines=$(wc -l < "$evidence/dlog-before.txt")
after_lines=$(wc -l < "$evidence/dlog-after.txt")
[ "$after_lines" -gt "$before_lines" ]
head -n "$before_lines" "$evidence/dlog-after.txt" \
  | cmp -s "$evidence/dlog-before.txt" -
tail -n "+$((before_lines + 1))" "$evidence/dlog-after.txt" \
  | grep -F "($pre_pid):" \
  | grep -F 'bootstrap receipt requires recovery before state creation' \
  > "$evidence/expected-refusal.txt"
echo 'PASS ExecStartPre rejected before creating POC fixture state'
if systemd-run --quiet --wait --pipe \
  --unit="consent-poc-classify-repeat-$$" \
  -p SmackProcessLabel=System::Privileged \
  "$helper" --migrate-fixture > "$evidence/repeat.txt" 2>&1; then
  echo 'FAIL RECOVERY_REQUIRED receipt reclassified' >&2
  exit 1
fi
grep -Fx 'phase=RECOVERY_REQUIRED' "$authority/bootstrap.receipt"
[ ! -e "$state" ]
[ "$receipt_before" = "$(stat -c '%d:%i:%u:%g:%a:%s' \
  "$authority/bootstrap.receipt")" ]
[ "$receipt_hash_before" = "$(sha256sum \
  "$authority/bootstrap.receipt")" ]
[ "$fixture_before" = "$(stat -c '%n %d:%i:%u:%g:%a:%s' \
  "$authority/installations.conf" "$authority/installations.lock" \
  "$authority/lifecycle.lock")" ]
[ "$fixture_hash_before" = "$(sha256sum \
  "$authority/installations.conf" "$authority/installations.lock" \
  "$authority/lifecycle.lock")" ]
[ "$source_before" = "$(stat -c '%d:%i:%u:%g:%a:%s' \
  /opt/var/lib/consent-poc-state \
  /opt/var/lib/consent-poc-state/consent.db \
  /opt/var/lib/consent-poc-state/definitions.registry \
  /opt/var/lib/consent-poc-authority \
  /opt/var/lib/consent-poc-authority/installations.conf \
  /opt/var/lib/consent-poc-authority/bootstrap.receipt)" ]
[ "$source_hash_before" = "$(sha256sum \
  /opt/var/lib/consent-poc-state/consent.db \
  /opt/var/lib/consent-poc-state/definitions.registry \
  /opt/var/lib/consent-poc-authority/installations.conf \
  /opt/var/lib/consent-poc-authority/bootstrap.receipt)" ]
[ "$(systemctl show consentd-poc.service -p ActiveState --value)" = \
  inactive ]
[ "$(systemctl is-enabled consentd-poc.service)" = disabled ]
echo 'PASS retry refused, authority bytes/inodes and real POC stores preserved'
