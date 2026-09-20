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
daemon_binary=$tools/consentd-test
observer=
drain_open=0
if [ "$phase" = db-shutdown ]; then
  daemon_binary=$tools/consentd-shutdown-test
fi
cleanup_fixture() {
  if [ "$drain_open" = 1 ]; then
    printf X >&3 || true
    exec 3>&-
    drain_open=0
  fi
  if [ -n "$observer" ]; then
    systemctl stop "$observer" || true
    rm -f "/run/systemd/system/$observer"
  fi
  if [ "$phase" = db-shutdown ]; then
    systemctl stop consentd-isolated.socket consentd-isolated.service || true
    sed -i 's@/consentd-shutdown-test@/consentd-test@' /run/systemd/system/consentd-isolated.service
    rm -f "$runtime/shutdown-db-ready" "$runtime/shutdown-db-release"
  fi
  systemctl daemon-reload
}
if [ "$phase" = shutdown ] || [ "$phase" = db-shutdown ]; then
  trap cleanup_fixture EXIT
fi
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
[identity cache-scenario]
uid=0
executable=$tools/consent-cache-scenario-isolated
label=System
roles=installer;argo;checker;ui;admin;session;holder;
subjects=demo.subject;
profiles=demo.profile;
enforcers=cache-scenario;
packages=demo.package;
[identity wire-scenario]
uid=0
executable=$tools/wire-scenario
label=System
roles=checker;
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
ExecStart=$daemon_binary
SmackProcessLabel=System
UMask=0077
TimeoutStartSec=15
TimeoutStopSec=10
SERVICE
if [ "$phase" = db-shutdown ]; then
  # Loading another ExecStart does not replace an already running daemon.
  systemctl stop consentd-isolated.socket consentd-isolated.service
fi
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
  cache)
    "$tools/consent-cache-scenario-isolated" "$(cat "$state/generation")"
    ;;
  ui-reevaluate)
    "$scenario" ui-reevaluate "$(cat "$state/generation")"
    ;;
  races)
    "$scenario" races "$(cat "$state/generation")"
    ;;
  wire)
    systemctl start consentd-isolated.service
    daemon_pid=$(systemctl show consentd-isolated.service -p MainPID --value)
    quota_before=$(dlogutil -d STDERR_consentd-test:V '*:S' | \
      grep "$daemon_pid)" | grep -c 'reason=uid-connection-limit' || true)
    pressure_before=$(dlogutil -d STDERR_consentd-test:V '*:S' | \
      grep "$daemon_pid)" | grep -Ec 'reason=(output-limit|write-timeout)' || true)
    "$tools/wire-scenario"
    [ "$(systemctl show consentd-isolated.service -p MainPID --value)" = "$daemon_pid" ]
    dlogutil -d STDERR_consentd-test:V '*:S' | grep "$daemon_pid)" > "$runtime/wire-daemon.log"
    quota_after=$(grep -c 'reason=uid-connection-limit' "$runtime/wire-daemon.log" || true)
    pressure_after=$(grep -Ec 'reason=(output-limit|write-timeout)' "$runtime/wire-daemon.log" || true)
    [ "$((quota_after - quota_before))" = 4 ]
    [ "$pressure_after" -gt "$pressure_before" ]
    echo "PASS wire same daemon PID=$daemon_pid quota_rejections=4 output-pressure-reason-confirmed"
    ;;
  shutdown)
    systemctl start consentd-isolated.service
    daemon_pid=$(systemctl show consentd-isolated.service -p MainPID --value)
    rm -f "$runtime/shutdown-ready"
    wait_unit=consent-partial-$$
    systemd-run --unit="$wait_unit" -p SmackProcessLabel=System \
      -p StandardOutput=journal -p StandardError=journal \
      "$tools/wire-scenario" --shutdown-wait
    attempt=0
    while [ ! -e "$runtime/shutdown-ready" ] && [ "$attempt" -lt 40 ]; do
      sleep 0.05
      attempt=$((attempt + 1))
    done
    [ -e "$runtime/shutdown-ready" ]
    waiter_pid=$(systemctl show "$wait_unit" -p MainPID --value)
    [ "$waiter_pid" -gt 0 ]
    # Retain both unit objects after stop so systemd cannot replace exit
    # evidence with the defaults of a newly loaded, garbage-collected unit.
    observer=consent-observer-$$.target
    cat > "/run/systemd/system/$observer" <<OBSERVER
[Unit]
Description=Consent test exit status observer
DefaultDependencies=no
Wants=consentd-isolated.service $wait_unit.service
OBSERVER
    systemctl daemon-reload
    systemctl start "$observer"
    systemctl stop consentd-isolated.socket consentd-isolated.service
    [ "$(systemctl show consentd-isolated.service -p MainPID --value)" = 0 ]
    [ "$(systemctl show consentd-isolated.service -p Result --value)" = success ]
    [ "$(systemctl show consentd-isolated.service -p ExecMainCode --value)" = 1 ]
    [ "$(systemctl show consentd-isolated.service -p ExecMainStatus --value)" = 0 ]
    attempt=0
    while [ "$(systemctl show "$wait_unit" -p MainPID --value)" != 0 ] && [ "$attempt" -lt 20 ]; do
      sleep 0.05
      attempt=$((attempt + 1))
    done
    [ "$(systemctl show "$wait_unit" -p MainPID --value)" = 0 ]
    [ "$(systemctl show "$wait_unit" -p Result --value)" = success ]
    [ "$(systemctl show "$wait_unit" -p ExecMainCode --value)" = 1 ]
    [ "$(systemctl show "$wait_unit" -p ExecMainStatus --value)" = 0 ]
    dlogutil -d STDERR_consentd-test:V '*:S' | grep "$daemon_pid)" | grep 'stage=database-drained'
    dlogutil -d STDERR_consentd-test:V '*:S' | grep "$daemon_pid)" | \
      grep "pid=$waiter_pid .*reason=daemon-shutdown pending_input_bytes=2"
    journalctl -u "$wait_unit" --no-pager -o cat -n 8
    systemctl stop "$observer"
    rm -f "/run/systemd/system/$observer"
    observer=
    systemctl daemon-reload
    echo "PASS partial-I/O shutdown daemon PID=$daemon_pid normal exit and database-drained"
    ;;
  db-shutdown)
    rm -f "$runtime/shutdown-db-ready" "$runtime/shutdown-db-release"
    mkfifo -m 600 "$runtime/shutdown-db-release"
    exec 3<> "$runtime/shutdown-db-release"
    drain_open=1
    scope=shutdown-scope-$$
    generation=$(cat "$state/generation")
    "$scenario" shutdown-seed "$generation" "$scope"
    daemon_pid=$(systemctl show consentd-isolated.service -p MainPID --value)
    [ "$daemon_pid" -gt 0 ]
    revoke_unit=consent-revoke-$$
    systemd-run --unit="$revoke_unit" -p SmackProcessLabel=System \
      -p StandardOutput=journal -p StandardError=journal \
      "$scenario" shutdown-revoke "$generation" "$scope"
    attempt=0
    while [ ! -e "$runtime/shutdown-db-ready" ] && [ "$attempt" -lt 40 ]; do
      sleep 0.05
      attempt=$((attempt + 1))
    done
    [ "$(cat "$runtime/shutdown-db-ready")" = "pid=$daemon_pid state=before-commit" ]
    observer=consent-db-observer-$$.target
    cat > "/run/systemd/system/$observer" <<OBSERVER
[Unit]
Description=Consent pending DB shutdown observer
DefaultDependencies=no
Wants=consentd-isolated.service $revoke_unit.service
OBSERVER
    systemctl daemon-reload
    systemctl start "$observer"
    systemctl kill --kill-who=main --signal=SIGTERM consentd-isolated.service
    attempt=0
    while [ "$attempt" -lt 40 ]; do
      dlogutil -d STDERR_consentd-shutdown-test:V '*:S' | grep "$daemon_pid)" > "$runtime/db-drain.log" || true
      if grep -q 'stage=stop-admission' "$runtime/db-drain.log"; then break; fi
      sleep 0.05
      attempt=$((attempt + 1))
    done
    grep 'stage=stop-admission' "$runtime/db-drain.log"
    if grep -q 'stage=database-drained' "$runtime/db-drain.log"; then
      echo 'FAIL drained before release' >&2
      exit 1
    fi
    [ "$(systemctl show consentd-isolated.service -p MainPID --value)" = "$daemon_pid" ]
    kill -0 "$daemon_pid"
    printf 'PASS accepted revoke is gated before COMMIT; same PID=%s stopped admission before release\n' "$daemon_pid"
    printf C >&3
    exec 3>&-
    drain_open=0
    attempt=0
    while [ "$(systemctl show consentd-isolated.service -p MainPID --value)" != 0 ] && [ "$attempt" -lt 60 ]; do
      sleep 0.05
      attempt=$((attempt + 1))
    done
    for unit in consentd-isolated.service "$revoke_unit"; do
      [ "$(systemctl show "$unit" -p MainPID --value)" = 0 ]
      [ "$(systemctl show "$unit" -p Result --value)" = success ]
      [ "$(systemctl show "$unit" -p ExecMainCode --value)" = 1 ]
      [ "$(systemctl show "$unit" -p ExecMainStatus --value)" = 0 ]
    done
    dlogutil -d STDERR_consentd-shutdown-test:V '*:S' | grep "$daemon_pid)" | grep 'stage=database-drained'
    journalctl -u "$revoke_unit" --no-pager -o cat -n 8
    systemctl stop consentd-isolated.socket consentd-isolated.service "$observer"
    rm -f "/run/systemd/system/$observer"
    observer=
    sed -i 's@/consentd-shutdown-test@/consentd-test@' /run/systemd/system/consentd-isolated.service
    systemctl daemon-reload
    systemctl start consentd-isolated.socket
    "$scenario" shutdown-result "$generation" "$scope"
    echo "PASS accepted DB revoke drained on SIGTERM; ordinary daemon restart confirms durable revocation"
    ;;
  holder-restart)
    "$scenario" holder-seed "$(cat "$state/generation")"
    "$scenario" holder-reconcile "$(cat "$state/generation")"
    ;;
  installation)
    old=$(cat "$state/generation")
    $authority remove demo.package uninstall-old "$old"
    "$tools/consent-api-test-isolated" unregister demo.package \
      operation_id=uninstall-old expected_generation="$old"
    for definition in demo.read demo.other; do
      "$tools/consent-api-test-isolated" check subject=demo.subject profile=demo.profile \
        count=1 r0.definition="$definition" r0.operation=read r0.scope=persistent-scope \
        r0.purpose=answer r0.holder=scenario --expect-decision=DENIED
    done
    generation=$($authority begin demo.package reinstall-new "$old")
    $authority attach demo.package demo.app reinstall-attach-1 "$generation"
    $authority attach demo.package demo.app2 reinstall-attach-2 "$generation"
    $authority commit demo.package reinstall-commit "$generation"
    printf '%s\n' "$generation" > "$state/generation"
    "$scenario" reinstalled "$generation"
    "$tools/consent-api-test-isolated" unregister demo.package \
      operation_id=uninstall-old expected_generation="$old" --expect-status=-116
    [ "$($authority remove demo.package uninstall-old "$old")" = "$old" ]
    "$scenario" recovered "$generation"
    echo 'PASS package removal, generation rotation and stale uninstall retry'
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
    cp "$tools/consent-api-test-isolated" "$tools/consent-untrusted-client"
    chmod 755 "$tools/consent-untrusted-client"
    rejection_before=$(dlogutil -d STDERR_consentd-test:V '*:S' | \
      grep -c 'role=rejected' || true)
    if "$tools/consent-untrusted-client" check subject=demo.subject profile=demo.profile \
        count=1 r0.definition=demo.read r0.operation=read r0.scope=persistent-scope \
        r0.purpose=answer r0.holder=scenario; then
      echo 'FAIL unregistered executable accepted' >&2
      exit 1
    fi
    rejection_after=$(dlogutil -d STDERR_consentd-test:V '*:S' | \
      grep -c 'role=rejected' || true)
    [ "$rejection_after" -gt "$rejection_before" ]
    dlogutil -d STDERR_consentd-test:V '*:S' | tail -12
    rm "$tools/consent-untrusted-client"
    echo 'PASS same-UID unregistered executable rejected'
    ;;
  *) echo "Unknown phase: $phase" >&2; exit 2 ;;
esac
if [ "$phase" != basic ] && [ "$phase" != shutdown ]; then
  systemctl is-active consentd-isolated.service
fi
# Daemon owns the live DB. Perform SQLite readback only with the service stopped.
systemctl stop consentd-isolated.socket consentd-isolated.service
integrity=$(sqlite3 "$state/consent.db" 'PRAGMA integrity_check;')
[ "$integrity" = ok ]
[ "$(sqlite3 "$state/consent.db" 'PRAGMA user_version;')" = 2 ]
[ "$(sqlite3 "$state/consent.db" 'SELECT count(*) FROM meta WHERE key IN ("revision","registry_revision","cleanup_unknown");')" = 3 ]
[ "$(sqlite3 "$state/consent.db" 'SELECT CAST(value AS INTEGER) FROM meta WHERE key="registry_revision";')" -ge 2 ]
echo 'PASS integrity_check=ok schema=2 expected_metadata_present'
echo "PASS emulator phase=$phase"
