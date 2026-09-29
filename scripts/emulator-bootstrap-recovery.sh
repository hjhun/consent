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
# Dedicated development-emulator paths; production state is never touched.
[ "$(id -u)" = 0 ]
service_user=security_fw
tools=/usr/libexec/consent/tests
helper=$tools/consent-storage-prepare-bootstrap
daemon=$tools/consentd-bootstrap-test
state=/opt/var/lib/consent-bootstrap-test
authority=/opt/var/lib/consent-bootstrap-authority
runtime=/tmp/consent-bootstrap-test
service=consentd-bootstrap-test.service
socket=consentd-bootstrap-test.socket
unit=/run/systemd/system/$service
socket_unit=/run/systemd/system/$socket
evidence=/opt/var/lib/consent-bootstrap-evidence-$$
for path in "$state" "$authority" "$runtime" "$unit" "$socket_unit" \
  "$evidence"; do
  [ ! -e "$path" ] && [ ! -L "$path" ]
done
mkdir -m 700 "$evidence"
mkdir -m 700 "$runtime"
chown "$service_user":"$service_user" "$runtime"
cleanup() {
  result=$?
  trap - EXIT
  if ! systemctl stop "$socket" "$service"; then
    echo "FAIL bootstrap units could not stop; evidence retained" >&2
    exit 1
  fi
  if [ "$(systemctl show "$service" -p MainPID --value)" != 0 ]; then
    echo "FAIL bootstrap daemon remains live; paths retained" >&2
    exit 1
  fi
  for name in state authority runtime; do
    case "$name" in
      state) path=$state ;;
      authority) path=$authority ;;
      runtime) path=$runtime ;;
    esac
    if [ -e "$path" ] || [ -L "$path" ]; then
      mv "$path" "$evidence/final-$name" || exit 1
    fi
  done
  for path in "$unit" "$socket_unit"; do
    if [ -e "$path" ]; then
      cp "$path" "$evidence/" || exit 1
      rm "$path" || exit 1
    fi
  done
  systemctl daemon-reload
  echo "bootstrap fixture evidence=$evidence"
  exit "$result"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cat > "$socket_unit" <<SOCKET
[Unit]
Description=Consent bootstrap fixture socket
[Socket]
ListenStream=$runtime/consent.sock
SocketMode=0600
Service=$service
RemoveOnStop=yes
SOCKET
write_service() {
  executable=$1
  cat > "$unit" <<SERVICE
[Unit]
Description=Consent bootstrap fixture daemon
Requires=$socket
After=$socket
[Service]
Type=notify
NotifyAccess=main
User=$service_user
Group=$service_user
AmbientCapabilities=CAP_SYS_PTRACE
CapabilityBoundingSet=CAP_SYS_PTRACE
NoNewPrivileges=yes
ExecStartPre=+$helper
ExecStart=$executable
ExecStartPost=+$helper --complete
SmackProcessLabel=System
Sockets=$socket
UMask=0077
Restart=no
TimeoutStartSec=15
SERVICE
  systemctl daemon-reload
}
write_service "$daemon"
"$helper" --first-install
systemctl start "$socket" "$service"
grep -Fx 'phase=INITIALIZED' "$authority/bootstrap.receipt"
[ -f "$state/definitions.registry" ]
[ -f "$state/consent.db" ]
first_invocation=$(systemctl show "$service" -p InvocationID --value)
[ -n "$first_invocation" ]
echo "PASS fresh root claim, manager-bound daemon READY, post acknowledgement"
systemctl stop "$socket" "$service"
before=$(sha256sum "$state/definitions.registry" "$state/consent.db" \
  "$authority/bootstrap.receipt")
if "$helper" --begin aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
  > "$evidence/no-source.txt" 2>&1; then
  echo 'FAIL missing desired source accepted' >&2
  exit 1
fi
[ "$before" = "$(sha256sum "$state/definitions.registry" \
  "$state/consent.db" "$authority/bootstrap.receipt")" ]
echo 'PASS production begin missing source has no pair/receipt mutation'
mv "$state/consent.db" "$evidence/lost-db"
systemctl start "$socket" "$service"
systemctl stop "$socket" "$service"
unknown=$(sqlite3 -readonly "$state/consent.db" \
  "SELECT value FROM meta WHERE key='cleanup_unknown';")
[ "$unknown" = 1 ]
echo 'PASS trusted registry DB-only recovery keeps cleanup_unknown=1'
before=$(sha256sum "$state/definitions.registry" "$state/consent.db")
mv "$authority/bootstrap.receipt" "$evidence/lost-receipt"
if systemctl start "$socket" "$service" \
  > "$evidence/lost-receipt-start.txt" 2>&1; then
  echo 'FAIL receipt-only loss admitted' >&2
  exit 1
fi
systemctl stop "$socket" "$service"
[ "$before" = "$(sha256sum "$state/definitions.registry" \
  "$state/consent.db")" ]
[ ! -e "$authority/bootstrap.receipt" ]
echo 'PASS receipt-only loss blocks restart without pair mutation'
mv "$state" "$evidence/initialized-state"
mv "$authority" "$evidence/initialized-authority"
write_service /bin/false
"$helper" --first-install
if systemctl start "$socket" "$service" \
  > "$evidence/pre-db-crash.txt" 2>&1; then
  echo 'FAIL deliberate pre-DB crash succeeded' >&2
  exit 1
fi
systemctl stop "$socket" "$service"
grep -Fx 'phase=FRESH_CLAIMED' "$authority/bootstrap.receipt"
[ ! -e "$state/definitions.registry" ]
[ ! -e "$state/consent.db" ]
claimed=$(sed -n 's/^invocation=//p' "$authority/bootstrap.receipt")
[ -n "$claimed" ] && [ "$claimed" != "$first_invocation" ]
echo "PASS first DB creation pre-crash claimed invocation=$claimed"
cat > "$runtime/forge.py" <<'PYTHON'
import os
import socket
import sys

daemon, path, invocation = sys.argv[1:]
listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
listener.bind(path)
listener.listen(1)
os.dup2(listener.fileno(), 3)
os.set_inheritable(3, True)
os.environ['LISTEN_PID'] = str(os.getpid())
os.environ['LISTEN_FDS'] = '1'
os.environ['INVOCATION_ID'] = invocation
print('forged_activation_pid=' + str(os.getpid()), flush=True)
os.execv(daemon, [daemon])
PYTHON
chmod 644 "$runtime/forge.py"
[ ! -e "$runtime/consent.sock" ]
if systemd-run --quiet --wait --pipe \
  --unit="consent-bootstrap-forged-$$" \
  -p User="$service_user" -p Group="$service_user" \
  -p SmackProcessLabel=System /usr/bin/python3 "$runtime/forge.py" \
  "$daemon" "$runtime/consent.sock" "$claimed" \
  > "$evidence/forged-activation.txt" 2>&1; then
  echo 'FAIL forged unit and inherited activation FD admitted' >&2
  exit 1
fi
forged_pid=$(sed -n 's/^forged_activation_pid=//p' \
  "$evidence/forged-activation.txt")
[ -n "$forged_pid" ]
dlogutil -d CONSENTD:V '*:S' | grep "($forged_pid)" | \
  grep -F 'event=bootstrap-rejected reason=systemd unit' \
  > "$evidence/forged-manager-rejection.txt"
[ ! -e "$state/definitions.registry" ]
[ ! -e "$state/consent.db" ]
echo 'PASS foreign unit with copied env and inherited FD rejected'
rm -f "$runtime/consent.sock"
write_service "$daemon"
systemctl reset-failed "$service" || true
if systemctl start "$socket" "$service" \
  > "$evidence/new-invocation-start.txt" 2>&1; then
  echo 'FAIL claimed bootstrap reused on new invocation' >&2
  exit 1
fi
second_invocation=$(systemctl show "$service" -p InvocationID --value)
[ -n "$second_invocation" ] && [ "$second_invocation" != "$claimed" ]
[ ! -e "$state/definitions.registry" ]
[ ! -e "$state/consent.db" ]
echo "PASS new invocation=$second_invocation cannot reuse FRESH_CLAIMED"
