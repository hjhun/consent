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
# Run as root with SmackProcessLabel=System on a selected DEVELOPMENT emulator.
# All mutable data belongs to this isolated test; production config is untouched.
phase=${1:-basic}
state=/opt/var/lib/consent-test
runtime=/tmp/consent-test
tools=/usr/libexec/consent/tests
scenario=$tools/consent-scenario-isolated
authority=$tools/consent-installation-authority-isolated
[ "$(id -u)" = 0 ]
[ ! -L "$runtime" ] && [ ! -L "$state" ]
mkdir -p "$runtime" "$state"
chown root:root "$runtime" "$state"
chmod 700 "$runtime" "$state"
cat > "$runtime/roles.conf" <<ROLES
[policy]
version=1
[identity scenario]
uid=0
executable=$scenario
label=System
roles=installer;argo;checker;ui;admin;session;holder;
subjects=demo.subject;
profiles=demo.profile;
enforcers=scenario;
packages=demo.package;
[identity cli]
uid=0
executable=$tools/consent-api-test-isolated
label=System
roles=installer;checker;
subjects=demo.subject;
profiles=demo.profile;
enforcers=scenario;
packages=demo.package;
ROLES
chmod 600 "$runtime/roles.conf"
cat > /run/systemd/system/consentd-isolated.socket <<'SOCKET'
[Unit]
Description=Consent isolated integration socket
[Socket]
ListenStream=/tmp/consent-test/consent.sock
SocketMode=0600
SmackLabel=System
Service=consentd-isolated.service
RemoveOnStop=yes
SOCKET
cat > /run/systemd/system/consentd-isolated.service <<SERVICE
[Unit]
Description=Consent isolated integration service
Requires=consentd-isolated.socket
After=consentd-isolated.socket
[Service]
Type=notify
ExecStart=$tools/consentd-test
SmackProcessLabel=System
UMask=0077
TimeoutStartSec=15
TimeoutStopSec=10
SERVICE
systemctl daemon-reload
systemctl reset-failed consentd-isolated.service || true
systemctl start consentd-isolated.socket
case "$phase" in
  basic)
    # Fresh state only: preserve previous evidence instead of implicitly wiping it.
    [ ! -e "$state/generation" ]
    generation=$($authority begin demo.package begin-1 absent)
    $authority attach demo.package demo.app attach-1 "$generation"
    $authority attach demo.package demo.app2 attach-2 "$generation"
    $authority commit demo.package commit-1 "$generation"
    printf '%s\n' "$generation" > "$state/generation"
    "$scenario" basic "$generation"
    systemctl stop consentd-isolated.socket consentd-isolated.service
    cp "$state/consent.db" "$state/approved-snapshot.db"
    ;;
  persistent)
    "$scenario" persistent "$(cat "$state/generation")"
    ;;
  running-delete)
    systemctl start consentd-isolated.service
    systemctl is-active consentd-isolated.service
    rm -f "$state/consent.db"
    "$scenario" recovered "$(cat "$state/generation")"
    ;;
  stopped-delete)
    systemctl stop consentd-isolated.socket consentd-isolated.service
    rm -f "$state/consent.db"
    systemctl start consentd-isolated.socket
    "$scenario" recovered "$(cat "$state/generation")"
    ;;
  stale-replace)
    systemctl stop consentd-isolated.socket consentd-isolated.service
    cp "$state/approved-snapshot.db" "$state/replacement.db"
    mv "$state/replacement.db" "$state/consent.db"
    systemctl start consentd-isolated.socket
    "$scenario" recovered "$(cat "$state/generation")"
    ;;
  corrupt)
    systemctl stop consentd-isolated.socket consentd-isolated.service
    printf 'deliberately corrupt isolated database\n' > "$state/consent.db"
    systemctl start consentd-isolated.socket
    "$scenario" recovered "$(cat "$state/generation")"
    ;;
  unauthorized)
    # Same UID and security label, different unregistered executable path/inode.
    cp "$tools/consent-api-test-isolated" "$runtime/untrusted-client"
    chmod 755 "$runtime/untrusted-client"
    rejection_before=$(journalctl -u consentd-isolated.service --no-pager -o cat | \
      grep -c 'role=rejected' || true)
    if "$runtime/untrusted-client" check subject=demo.subject profile=demo.profile \
        count=1 r0.definition=demo.read r0.operation=read r0.scope=persistent-scope \
        r0.purpose=answer r0.holder=scenario; then
      echo 'FAIL unregistered executable accepted' >&2
      exit 1
    fi
    rejection_after=$(journalctl -u consentd-isolated.service --no-pager -o cat | \
      grep -c 'role=rejected' || true)
    [ "$rejection_after" -gt "$rejection_before" ]
    journalctl -u consentd-isolated.service --no-pager -o cat -n 12
    echo 'PASS same-UID unregistered executable rejected'
    ;;
  *) echo "Unknown phase: $phase" >&2; exit 2 ;;
esac
if [ "$phase" != basic ]; then
  systemctl is-active consentd-isolated.service
fi
# Daemon owns the live DB. Perform SQLite readback only with the service stopped.
systemctl stop consentd-isolated.socket consentd-isolated.service
integrity=$(sqlite3 "$state/consent.db" 'PRAGMA integrity_check;')
[ "$integrity" = ok ]
[ "$(sqlite3 "$state/consent.db" 'PRAGMA user_version;')" = 1 ]
[ "$(sqlite3 "$state/consent.db" 'SELECT count(*) FROM meta WHERE key IN ("revision","registry_revision","cleanup_unknown");')" = 3 ]
[ "$(sqlite3 "$state/consent.db" 'SELECT CAST(value AS INTEGER) FROM meta WHERE key="registry_revision";')" -ge 2 ]
echo 'PASS integrity_check=ok schema=1 expected_metadata_present'
echo "PASS emulator phase=$phase"
