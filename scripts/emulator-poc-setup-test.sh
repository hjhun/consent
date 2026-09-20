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
# Development-only failure fixture for the PoC setup override lifecycle.
set -eu
[ "$(id -u)" = 0 ]
setup=${1:?absolute setup script path required}
case "$setup" in /*) ;; *) exit 2;; esac
[ -f "$setup" ] && [ ! -L "$setup" ]
[ "$(stat -c '%u:%h' "$setup")" = 0:1 ]
mode=$(stat -c %a "$setup")
[ $((0$mode & 0022)) -eq 0 ]
drop=/run/systemd/system/consentd-poc.socket.d
service_drop=/run/systemd/system/consentd-poc.service.d
[ ! -e "$drop" ] && [ ! -L "$drop" ]
[ ! -e "$service_drop" ] && [ ! -L "$service_drop" ]
control=/opt/var/lib/consent-poc-control
[ -d "$control" ] && [ ! -L "$control" ]
[ "$(stat -c '%u:%g:%a' "$control")" = 0:0:700 ]
systemctl stop consentd-poc.socket consentd-poc.service
mkdir -m 0755 "$drop"
marker_dir=$(mktemp -d "$control/probe-failure.XXXXXX")
cleanup() {
  result=$?
  trap - EXIT HUP INT TERM
  systemctl stop consentd-poc.socket consentd-poc.service || result=1
  rm -f "$drop/fixture-fail.conf" "$drop/fail-start.sh" || result=1
  rm -f "$marker_dir/executed" || result=1
  rmdir "$marker_dir" || result=1
  rmdir "$drop" || result=1
  systemctl daemon-reload || result=1
  exit "$result"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
cat > "$drop/fail-start.sh" <<EOF
#!/bin/sh
set -eu
umask 077
printf '%s\n' injected-socket-start-failure > '$marker_dir/executed'
exit 1
EOF
chmod 0600 "$drop/fail-start.sh"
cat > "$drop/fixture-fail.conf" <<EOF
[Socket]
ExecStartPre=/bin/sh $drop/fail-start.sh
EOF
systemctl daemon-reload
if /bin/sh "$setup" probe-start; then
  echo 'FAIL injected socket startup unexpectedly succeeded' >&2
  exit 1
fi
[ -f "$marker_dir/executed" ] && [ ! -L "$marker_dir/executed" ]
[ "$(stat -c '%u:%g:%a:%h' "$marker_dir/executed")" = 0:0:600:1 ]
[ "$(cat "$marker_dir/executed")" = injected-socket-start-failure ]
[ ! -e "$service_drop" ] && [ ! -L "$service_drop" ]
[ "$(systemctl show -p MainPID --value consentd-poc.service)" = 0 ]
[ "$(systemctl show -p Type --value consentd-poc.service)" = notify ]
[ -z "$(systemctl show -p DropInPaths --value consentd-poc.service)" ]
state=$(systemctl show -p ActiveState --value consentd-poc.socket)
[ "$state" = inactive ] || [ "$state" = failed ]
echo 'PASS executed injected socket-start failure; probe cleanup stopped units and removed owned observe override'
