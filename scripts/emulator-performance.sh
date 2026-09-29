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
# Development target only; production consent state/authority are never used.
# Before first use, prepare fresh basic state with emulator-scenario.sh basic,
# stop its socket/service, then use --capture to freeze the seed without grants
# for performance-scope. Preserve old fixture state before fresh basic setup.
action=${1:?--capture or an attempt ID required}
binary=${2:-/usr/libexec/consent/tests/consent-performance-isolated}
seed=/opt/var/lib/consent-perf-template-build32-tool2
state=/opt/var/lib/consent-test
authority=/opt/var/lib/consent-test-authority
control=/opt/var/lib/consent-test-control
runtime=/tmp/consent-test
[ "$(id -u)" = 0 ]
systemctl stop consentd-isolated.socket consentd-isolated.service
if [ "$action" = --capture ]; then
  [ ! -e "$seed" ] && [ ! -L "$seed" ]
  mkdir -m700 "$seed"
  for name in consent-test consent-test-authority consent-test-control; do
    [ -d "/opt/var/lib/$name" ] && [ ! -L "/opt/var/lib/$name" ]
    cp -a "/opt/var/lib/$name" "$seed/$name"
  done
  cp "$runtime/roles.conf" "$seed/roles.base.conf"
  chmod 600 "$seed/roles.base.conf"
  find "$seed" -type f -exec sha256sum {} +
  exit 0
fi
case "$action" in *[!a-zA-Z0-9_-]*|'') exit 2;; esac
case "$binary" in /usr/libexec/consent/tests/consent-performance*) ;; *) exit 2;; esac
[ -f "$binary" ] && [ ! -L "$binary" ]
[ "$(stat -c '%u:%g:%a' "$binary")" = 0:0:755 ]
attempt=/opt/var/lib/consent-perf-attempt-$action
[ ! -e "$attempt" ] && [ ! -L "$attempt" ]
mkdir -m700 "$attempt"
for name in consent-test consent-test-authority consent-test-control; do
  [ -d "$seed/$name" ] && [ ! -L "$seed/$name" ]
  [ -d "/opt/var/lib/$name" ] && [ ! -L "/opt/var/lib/$name" ]
  mv "/opt/var/lib/$name" "$attempt/$name"
  cp -a "$seed/$name" "/opt/var/lib/$name"
done
find "$state" "$authority" "$control" -type f -exec sha256sum {} +
# Only the isolated runtime authority changes. Bind the exact installed actor.
[ ! -L "$runtime/roles.conf" ]
cp "$runtime/roles.conf" "$attempt/roles.before.conf"
# The frozen recipe supplies a base runtime policy without performance actors.
# Capture that base only once so failed attempts cannot add duplicate rules.
base_roles=$seed/roles.base.conf
[ -f "$base_roles" ] && [ ! -L "$base_roles" ]
cp "$base_roles" "$runtime/roles.conf"
cat >> "$runtime/roles.conf" <<ROLE
[identity performance]
uid=0
executable=$binary
label=System
roles=installer;argo;checker;ui;admin;session;holder;
subjects=demo.subject;
profiles=demo.profile;
enforcers=scenario;
packages=demo.package;
ROLE
chown root:security_fw "$runtime/roles.conf"
chmod 640 "$runtime/roles.conf"
restore() {
  systemctl stop consentd-isolated.socket consentd-isolated.service || true
  cp "$base_roles" "$runtime/roles.conf"
}
trap restore EXIT
systemctl start consentd-isolated.socket consentd-isolated.service
sha256sum "$binary" /usr/libexec/consent/tests/consentd-test \
  /usr/lib64/libconsent.so.0.1.0 /usr/bin/consentd
"$binary" "$(cat "$control/generation")"
