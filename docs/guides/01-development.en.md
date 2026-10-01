# Guide 01: Build and deploy Consent

<a id="guide-01-consent-developer-guide"></a>

[한국어](01-development.ko.md)

Build the packages for a selected development emulator, install the exact RPMs
from that build, and check service startup. For approval API usage, continue to
[API 01](api/01-start.en.md). For a temporary native UI test that restores the
original installation, use [Guide 17](17-native-ui-smoke.en.md).

## 1. Prepare the SDK and select the emulator

Use CMake 3.12 or later, Python 3, C11, C++17 and pkg-config packages
`glib-2.0`, `gio-2.0`,
`gio-unix-2.0`, `sqlite3`, `libsystemd`, `pkgmgr-info`, `capi-base-common`, and
native Tizen `parcel`.
The reference emulator
inspected on 2026-09-20 has x86_64, GLib 2.80.5, SQLite 3.50.2 and systemd 244.
Host-only builds supplement GBS validation.

Discover the current device and select its serial explicitly:

```sh
sdb devices
CONSENT_DEVICE='SELECTED_EMULATOR'
sdb -s "$CONSENT_DEVICE" shell 'uname -m; systemctl --version'
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root
```

The profile above is the local profile verified during initial development;
adapt it to the installed SDK. `--include-all` includes uncommitted sources;
no commit is required. GBS may require local privilege to initialize its chroot.
Do not publish repository credentials or the full GBS configuration in logs.

The default RPM build also enables the isolated .NET PoC. Its additional
BuildRequires are `dotnet-build-tools` 8.0.421, `csapi-tizenfx-nuget`
14.0.0.19364 and `capi-appfw-app-control`. GBS compiles and development-signs
the TPKs using its own SDK and offline NuGet inputs. See
[Guide 08](08-consent-ui-poc.en.md) for the separate package and setup contract.

## 2. Select and install matching packages

Use the exact RPM paths printed by the successful GBS build. Confirm each
package name, version, architecture and dependency before installing. These
commands are portable examples, not a claim of a new target run.

```sh
CONSENT_RUNTIME_RPM=/path/to/consent-VERSION-RELEASE.x86_64.rpm
CONSENT_DAEMON_RPM=/path/to/consentd-VERSION-RELEASE.x86_64.rpm
CONSENT_TESTS_RPM=/path/to/consent-tests-VERSION-RELEASE.x86_64.rpm
rpm -qp --qf '%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n' \
  "$CONSENT_RUNTIME_RPM" "$CONSENT_DAEMON_RPM" "$CONSENT_TESTS_RPM"
sdb -s "$CONSENT_DEVICE" root on
for rpm_file in "$CONSENT_RUNTIME_RPM" "$CONSENT_DAEMON_RPM" \
    "$CONSENT_TESTS_RPM"; do
  sdb -s "$CONSENT_DEVICE" push "$rpm_file" /tmp/
done
runtime_name=$(basename "$CONSENT_RUNTIME_RPM")
daemon_name=$(basename "$CONSENT_DAEMON_RPM")
tests_name=$(basename "$CONSENT_TESTS_RPM")
sdb -s "$CONSENT_DEVICE" shell \
  "systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System::Privileged /bin/sh -c \
  'rpm -Uvh /tmp/$runtime_name /tmp/$daemon_name /tmp/$tests_name; \
  install_status=\$?; echo CONSENT_INSTALL_EXIT=\$install_status; \
  exit \$install_status'"
sdb -s "$CONSENT_DEVICE" shell 'systemctl status consentd.socket'
sdb -s "$CONSENT_DEVICE" shell \
  'journalctl -u consentd.service -n 80 --no-pager'
```

Run this from the host after selecting SDB root. The root transient uses the
verified System::Privileged installation context. Require CONSENT_INSTALL_EXIT=0
and the remote unit's successful exit; host SDB exit 0 alone is insufficient.
If target policy does not permit that context, stop and diagnose it rather than
bypassing dependencies or labels.

Expected: RPM reports successful installation; the socket is listening; service
startup has no storage, listener or identity-configuration error. The shipped
role policy authorizes no application. Successful service startup therefore
does not mean the first API call is permitted.

Install a matching `consent-devel` RPM to compile consumers. Resolve
dependencies
with the configured SDK/repository; do not use `--nodeps` or upgrade an
unrelated
platform library merely to force this installation. Same-version replacement
needs separate verification of owned files; the command above is a normal
upgrade and intentionally names only the selected packages.

## 3. Build the first API consumer

On the target or in a compatible SDK with installed headers/libraries, save
the complete query program from [API 01](api/01-start.en.md) as `query.c`:

```sh
cc -std=c11 -Wall -Wextra query.c -o consent-query \
  $(pkg-config --cflags --libs consent)
```

The program needs a separately enrolled checker identity. QUERY reports current
satisfaction only. It never opens UI or reads data. Continue to
[request and authorization](api/03-request-and-check.en.md) for actual access.

## Troubleshooting

Missing pkg-config dependencies on the host do not establish an SDK problem;
use the configured GBS profile. Socket access errors require checking DAC,
SMACK and role policy independently. Read credential/role logs without adding
raw scope, conversation content or credentials. A daemon without exactly one
valid activation listener must fail startup; direct bind is not a fallback.


The compatible `consent.h` umbrella includes self-contained feature headers:
`consent_common.h`, `consent_client.h`, `consent_params.h`, `consent_result.h`,
`consent_registration.h`, `consent_request.h`, `consent_prompt.h`,
`consent_session.h` and `consent_data.h`. Each compiles independently as C or
C++.
C wrappers are split by those functions; only private Guard/Call/Submit helpers
are shared, and transport/cache ownership remains in `client.cc`.
The explicit registration-only offline constructor is documented in
[offline registration](04-offline-registration.en.md); ordinary clients never
fall
back to it after a connection failure.
## Packages and installed files

| Package | Contents |
|---|---|
| `consent` | Versioned public shared library |
| `consent-devel` | Public C headers, linker symlink, `consent.pc`, bilingual guides |
| `consentd` | `/usr/bin/consentd`, `/usr/sbin/consent-installation-authority`, `/usr/sbin/consent-storage-prepare`, systemd units, empty identity policy |
| `consent-tests` | C exercisers, four C integration examples and isolated tests under libexec |
| `consent-poc` | Separate .NET UI/negative TPKs, strict client, daemon and five participant mocks |

Implementation sources live under `src/`; test sources live under `tests/`.
Build settings are split by
component. The daemon alone owns `/opt/var/lib/consentd`, mode 0700, and
`consent.db`.
Tizen links `/var` to `/opt/var`; the canonical path satisfies strict state-path
validation without accepting symlinks. The root preparation helper owns
directory
creation and migration; neither systemd StateDirectory nor RPM directory
attributes
may recursively change ownership before validation.
Systemd owns `/run/.consentd.sock`; the daemon requires the inherited listener.
IPC uses native Tizen Parcel with a bounded four-byte length prefix.
The RPM installs both sockets.target.wants/consentd.socket and the AMD-style
basic.target.wants/consentd.service symlink. Boot startup and socket activation
share the same inherited listener.

## Identity and deployment

The service uses the existing platform account `security_fw` (observed
UID/GID402),
with `CAP_SYS_PTRACE` as its only bounded/ambient capability and
NoNewPrivileges.
The literal account `security` was absent; no new account is created. The
account
name is resolved by the platform, never replaced with an assumed numeric UID.
The root-only ExecStartPre preparation helper is a separate process. The socket
remains mode0660, root:`system_share`.
This group grants transport access only. The daemon authenticates roles from
kernel credentials and the root-owned `/etc/consent/roles.conf`. The shipped
policy authorizes no identities. A platform integrator must provision exact
executables, kernel security labels, roles and delegated contexts.

The production client verifies the fixed `/run/.consentd.sock` path, root
ownership
and unchanged socket device/inode across connection. It accepts the inherited
systemd endpoint only when kernel peer credentials identify PID 1/UID 0 and the
configured `System::Privileged` security label. `/run` may be
root:`system_share`
group-writable on this target; other writable parent paths remain rejected.
A socket owned by a normal process does not authenticate as the service.

Each `[identity NAME]` keyfile section specifies `uid`, `executable`, `label`,
and semicolon-separated `roles`, `subjects`, `profiles`, `enforcers`, and
`packages`. `enforcers` lists delegated enforcement identities; `packages`
limits Installer package management. Subject and profile wildcards are rejected.
Do not copy test identities into production policy. Current trusted identity
validation and role names are defined by `src/consentd/identity.cc`.

Stop both consentd.socket and consentd.service for maintenance; stopping the
service alone permits socket reactivation. Never unlink the systemd endpoint.
## Reference: generated protocol

`src/protocol/consent.idl.json` defines the wire records. The build runs the
standard-library-only `src/tools/parcel_codegen.py` compiler and produces
`generated/consent_wire.hh` in the build directory. `Field` and `Envelope` are
native `tizen_base::Parcelable` subclasses with `WriteToParcel`,
`ReadFromParcel`
and `Valid` methods. Generated code uses the bounded native Parcel helpers in
`src/common/parcel_codec.hh`; Python is not a runtime dependency.

The supported IDL types are `u32`, `i32`, `u64`, UTF-8 `string` with
`max_bytes`,
an earlier declared record, and `array` of an earlier record with `max_count`.
Strings may also have `min_bytes`; integer fields may have a checked `default`.
Field order is wire order. Recursive/forward references, duplicate names/JSON
keys, unknown constraints, reserved identifiers and unbounded fields fail
generation. License metadata must contain the full Apache-2.0 notice. The
compiler bounds transitive nesting, record layout, wire size and allocation;
array decoding checks remaining wire bytes before resizing storage. The
compiler preserves an existing output after invalid input and
does not rewrite unchanged output. Extend the wire version before changing
incompatible field layouts.

```sh
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json --check
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json /tmp/consent_wire.hh
python3 tests/idl_codegen_test.py
```

<a id="build-environment"></a>

[Build environment](api/01-start.en.md)

<a id="native-parcel-idl"></a>

[Native Parcel IDL](05-idl.en.md)

<a id="api-integration-contract"></a>

[API integration contract](02-c-api.en.md)

<a id="public-error-values-and-upgrades"></a>

[Public error values and upgrades](api/04-results-and-callbacks.en.md)

<a id="parameter-fields"></a>

[Parameter fields](api/03-request-and-check.en.md)

<a id="localized-approval-text"></a>

[Localized approval text](api/02-registration.en.md)

<a id="recovery-and-verification"></a>

[Recovery and verification](09-storage-maintenance.en.md)

---

[Continue](api/01-start.en.md) · [Reading paths](../README.md)
