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
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
[ "$(id -u)" = 0 ]
tpk=/usr/share/consent/poc/org.tizen.consentui-0.1.0.tpk
state=/opt/var/lib/consent-poc-package

for directory in / /usr /usr/share /usr/share/consent /usr/share/consent/poc \
                 /opt /opt/var /opt/var/lib; do
  [ -d "$directory" ] && [ ! -L "$directory" ]
  [ "$(stat -c %u "$directory")" = 0 ]
  mode=$(stat -c %a "$directory")
  [ $((0$mode & 0022)) -eq 0 ]
done
if [ ! -e "$state" ] && [ ! -L "$state" ]; then
  mkdir -m 0700 "$state"
fi
[ -d "$state" ] && [ ! -L "$state" ]
[ "$(stat -c '%u:%g:%a' "$state")" = 0:0:700 ]
for file in "$state/lock" "$state/installed.sha256"; do
  if [ -e "$file" ] || [ -L "$file" ]; then
    [ -f "$file" ] && [ ! -L "$file" ]
    [ "$(stat -c '%u:%g:%a:%h' "$file")" = 0:0:600:1 ]
  fi
done
exec 9>"$state/lock"
flock -n 9
[ -f "$tpk" ] && [ ! -L "$tpk" ]
[ "$(stat -c '%u:%g:%a:%h' "$tpk")" = 0:0:644:1 ]
sum=$(sha256sum "$tpk")
digest=${sum%% *}
[ "${#digest}" -eq 64 ]

registered() {
  app=$(pkginfo --app org.tizen.consentui 9<&-) || return 1
  package=$(pkginfo --pkg org.tizen.consentui 9<&-) || return 1
  printf '%s\n' "$app" | grep -Fxq 'Package: org.tizen.consentui' || return 1
  printf '%s\n' "$package" | grep -Fxq 'Version: 0.1.0' || return 1
}

if [ -f "$state/installed.sha256" ] &&
   [ "$(cat "$state/installed.sha256")" = "$digest" ] && registered; then
  echo 'PASS consent PoC UI package already registered'
  exit 0
fi

# The systemd unit supplies the real platform installer context. This registers
# only the TPK; consent identities, definitions and daemon activation are an
# explicit later setup operation. Image RPM installation never executes it.
pkgcmd -i -t tpk -p "$tpk" --global 9<&-
registered
temporary=$(mktemp "$state/.installed.XXXXXX")
trap 'rm -f "$temporary"' EXIT HUP INT TERM
printf '%s\n' "$digest" > "$temporary"
chmod 0600 "$temporary"
mv -f "$temporary" "$state/installed.sha256"
trap - EXIT HUP INT TERM
echo 'PASS consent PoC UI package registered globally; consent setup remains explicit'
