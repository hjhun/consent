# consent developer guide

This guide describes the implementation in this repository. The original
[CEP](CEP_Consent_Framework.md) remains a design proposal. Implementation and
verification are being integrated; the presence of an API or test source does
not establish that every CEP acceptance criterion has passed.

## Build environment

Use CMake 3.12 or later, Python 3, C11, C++17 and pkg-config packages `glib-2.0`, `gio-2.0`,
`gio-unix-2.0`, `sqlite3`, `libsystemd`, `pkgmgr-info`, and native Tizen `parcel`.
The reference emulator
inspected on 2026-09-20 has x86_64, GLib 2.80.5, SQLite 3.50.2 and systemd 244.
Host-only builds supplement GBS validation.

Discover the current device and select its serial explicitly:

```sh
sdb devices
CONSENT_DEVICE=<selected-development-emulator>
sdb -s "$CONSENT_DEVICE" shell 'uname -m; systemctl --version'
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root
```

The profile above is the local profile verified during initial development;
adapt it to the installed SDK. `--include-all` includes uncommitted sources;
no commit is required. GBS may require local privilege to initialize its chroot.
Do not publish repository credentials or the full GBS configuration in logs.

## Packages and installed files

| Package | Contents |
|---|---|
| `consent` | Versioned public shared library |
| `consent-devel` | Public C headers, linker symlink, `consent.pc`, bilingual guides |
| `consentd` | `/usr/bin/consentd`, `/usr/sbin/consent-installation-authority`, systemd units, empty identity policy |
| `consent-tests` | C exercisers and isolated tests under the configured libexec directory |

Implementation and test sources live under `src/`. Build settings are split by
component. The daemon alone owns `/opt/var/lib/consentd`, mode 0700, and `consent.db`.
Tizen links `/var` to `/opt/var`; the canonical path satisfies strict state-path
validation without accepting symlinks. systemd `StateDirectory=consentd` creates
the same location on this target.
Systemd owns `/run/.consentd.sock`; the daemon requires the inherited listener.
IPC uses native Tizen Parcel with a bounded four-byte length prefix.
Only the socket is enabled for boot activation.

## Native Parcel IDL

`src/protocol/consent.idl.json` defines the wire records. The build runs the
standard-library-only `src/tools/parcel_codegen.py` compiler and produces
`generated/consent_wire.hh` in the build directory. `Field` and `Envelope` are
native `tizen_base::Parcelable` subclasses with `WriteToParcel`, `ReadFromParcel`
and `Valid` methods. Generated code uses the bounded native Parcel helpers in
`src/common/parcel_codec.hh`; Python is not a runtime dependency.

The supported IDL types are `u32`, `i32`, `u64`, UTF-8 `string` with `max_bytes`,
an earlier declared record, and `array` of an earlier record with `max_count`.
Strings may also have `min_bytes`; integer fields may have a checked `default`.
Field order is wire order. Recursive/forward references, duplicate names/JSON
keys, unknown constraints, reserved identifiers and unbounded fields fail
generation. License metadata must contain the full Apache-2.0 notice. The
compiler bounds transitive nesting, record layout, wire size and allocation;
array decoding checks remaining wire bytes before resizing storage. The compiler preserves an existing output after invalid input and
does not rewrite unchanged output. Extend the wire version before changing
incompatible field layouts.

```sh
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json --check
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json /tmp/consent_wire.hh
python3 src/tests/idl_codegen_test.py
```

## Identity and deployment

The initial service runs as root to validate cross-UID process identity and
protect the recovery registry. The socket is mode 0660, root:`system_share`.
This group grants transport access only. The daemon authenticates roles from
kernel credentials and the root-owned `/etc/consent/roles.conf`. The shipped
policy authorizes no identities. A platform integrator must provision exact
executables, kernel security labels, roles and delegated contexts.

Each `[identity NAME]` keyfile section specifies `uid`, `executable`, `label`,
and semicolon-separated `roles`, `subjects`, `profiles`, `enforcers`, and
`packages`. `enforcers` lists delegated enforcement identities; `packages`
limits Installer package management. Subject and profile wildcards are rejected.
Do not copy test identities into production policy. Current trusted identity
validation and role names are defined by `src/consentd/identity.cc`.

Install only RPMs built for the selected device. After installation:

```sh
CONSENT_RPM_DIR=/var/tmp/consent-gbs-root/local/repos/tizen_10_1_emulator/x86_64/RPMS
sdb -s "$CONSENT_DEVICE" root on
for package in consent consentd consent-devel consent-tests; do
  sdb -s "$CONSENT_DEVICE" push "$CONSENT_RPM_DIR/$package-0.1.0-1.x86_64.rpm" /tmp/
done
sdb -s "$CONSENT_DEVICE" shell 'rpm -Uvh --replacepkgs /tmp/consent-0.1.0-1.x86_64.rpm /tmp/consentd-0.1.0-1.x86_64.rpm /tmp/consent-devel-0.1.0-1.x86_64.rpm /tmp/consent-tests-0.1.0-1.x86_64.rpm'
sdb -s "$CONSENT_DEVICE" shell 'systemctl status consentd.socket'
sdb -s "$CONSENT_DEVICE" shell 'journalctl -u consentd.service -n 80 --no-pager'
```

Use the actual release and RPM paths printed by GBS. `--replacepkgs` permits
reinstalling this development version without changing its version number.

Stopping `consentd.service` alone permits reactivation. Stop both the socket
and service for maintenance. Never remove the systemd endpoint manually.

## API integration contract

Compile C consumers against installed metadata:

```sh
cc consumer.c -o consumer $(pkg-config --cflags --libs consent)
```

`consent_register()` receives the package name and app ID separately;
`consent_unregister()` removes all definitions belonging to a package.
Caller strings do not prove ownership. Request is restricted to authenticated
argo; check performs no UI interaction. Protected execution uses authoritative
AUTHORIZE checking, with atomic one-time consumption and retry identifiers.

Asynchronous success means acceptance. Final success or failure arrives through
the registered dispatcher callback, including immediate decisions. Inputs are
copied before return. Callback results are borrowed during the callback; use
the public clone/free contract when retaining a result. Call asynchronous APIs
on the dispatcher owner thread without nested loop iteration. Never treat a
request cache as permission to execute a protected operation.

Sessions describe conversations, independently of transport connections.
Artifacts and data-use permits store control metadata, never conversation
bodies. Holders enforce actual data lifetime and acknowledge cleanup. A close
response means further use is blocked; it does not prove physical deletion.

## Parameter fields

The public ABI uses opaque `consent_params_t` builders and string fields.
`consent_params_set()` copies UTF-8 input; integer setters encode decimal values.
Protocol identity fields and underscore-prefixed internal fields are reserved.

| Operation | Required fields and interpretation |
|---|---|
| register/update | `operation_id`, `expected_generation`, `definition`, `enforcer`, positive `policy_version` and `text_revision`, `level` 0–3, comma-separated `modes`, `default_locale`, `message.<locale>.title` and `.body`; package/app are separate API arguments |
| unregister | Package name as its own argument, plus params containing retry-stable `operation_id` and current `expected_generation`; no app ID |
| request | `subject`, `profile`, stable `client_request_id`, `operation_id`, requirements; optional `session` and its `generation`; `deadline_ms` is independent of local wait timeout |
| check | `subject`, `profile`, requirements; mode is QUERY or AUTHORIZE; AUTHORIZE also requires `operation_id` and `step_id` |
| requirement | `consent_params_add_requirement()` appends definition, operation, exact scope, purpose and recipient; at most 16 requirements |
| session open | `subject`, `profile`; lifecycle is CONNECTION_BOUND or RESUMABLE_CONVERSATION; bounded timeout values are validated by the daemon |
| session transition | `subject`, `profile`, `session`, current `generation`; resume also requires the rotating `resume_token` |

Scopes compare exactly in this version. The UI must display the registered
message and actual scope/purpose/recipient together. Message placeholders are
rejected until a typed formatting schema is available. Level 3 permits ONCE
only. Increment `policy_version` for a changed policy meaning; increment
`text_revision` for revised translations.

For example, an authenticated enforcement service can make an advisory check
with the following C API sequence. Every nonzero status blocks execution.

```c
#include <consent.h>

int query_calendar(consent_client_h client) {
  consent_params_t *params = NULL;
  consent_result_t *result = NULL;
  int status = consent_params_create(&params);
  if (status != 0)
    return status;
  status = consent_params_set(params, "subject", "org.example.agent");
  if (status == 0)
    status = consent_params_set(params, "profile", "owner");
  if (status == 0)
    status = consent_params_add_requirement(params, "calendar.read", "read",
        "today", "answer-calendar", "");
  if (status == 0)
    status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
  if (status == 0)
    status = consent_check(client, params, 2000, &result);
  /* Inspect consent_result_get_decision(result) only when status == 0.
   * QUERY is advisory: perform AUTHORIZE before accessing protected data. */
  consent_result_free(result);
  consent_params_free(params);
  return status;
}
```

## Recovery and verification

The root-only installation authority manages `installations.conf` through
`begin`, `attach`, `commit`, and `remove` commands. A begin operation rotates the
generation and fences the old app list; attach records each package app; commit
activates the completed list. Use `absent` only for a never-recorded package.
Each command also takes a unique operation ID and expected generation; retry
the same operation after an uncertain result. Register only after the real
package installation and authority commit have succeeded. For removal,
unregister consent definitions with the current generation before marking the
authority removed. Reinstallation uses a new generation. The utility is an
Installer integration surface, not evidence that platform hooks are installed.

The C API exerciser accepts `METHOD [PACKAGE [APP]] key=value ...`, `--async`,
`--timeout-ms=N`, `--repeat=N`, `--expect-status=N`, and
`--expect-decision=VALUE`. Mismatches return a nonzero exit status. For example,
after provisioning a trusted checker with the matching delegated context:

```sh
/usr/libexec/consent/tests/consent-api-test check \
  subject=org.example.agent profile=owner count=1 \
  r0.definition=calendar.read r0.operation=read r0.scope=today \
  r0.purpose=answer-calendar --async --expect-decision=CONSENT_REQUIRED
```

The `-isolated` tool links a separate static test client. `consentd-test` uses
the same role checks with socket/config paths under `/tmp/consent-test` and
persistent state under `/opt/var/lib/consent-test`; only its installation inventory
adapter is substituted.
The production binaries have no runtime switch to enable that adapter. The
client unit test uses a mock transport peer and therefore tests the client
contract, not daemon policy or platform identity.

Keep process kills, orderly reboots and abrupt emulator power interruption as
separate test scenarios. Forced DB deletion must target only an isolated test
state or the selected development emulator's consent state. Do not remove any
other platform DB. Lost approvals require fresh approval; installed definitions
need a separately trusted reconstruction source. Permission, storage-full and
I/O failures must remain errors rather than silently wiping the DB.

The initial storage implementation uses one serialized SQLite connection,
`journal_mode=DELETE`, `synchronous=EXTRA`, foreign keys, and a 100 ms busy
timeout. The rollback journal belongs to SQLite; do not delete it separately
during recovery. A protected `definitions.registry` preserves definitions and
package-removal tombstones independently of the DB. An additional Installer
generation registry and platform `pkgmgr-info` bind definitions to current
installations. Reconstruction restores definitions only, never user approvals.
Missing or replaced live DB files retire the old handle and create a new epoch.
Ordinary restarts invalidate pending requests and sessions while preserving
eligible persistent grants. Holder cleanup remains outstanding until evidence
of deletion is received.

GBS has compiled the native Parcel client, daemon, C exercisers and installation
authority with GCC 14.2 and `-Werror`. The sixth build passed all four CTest suites:
client contracts, repository policies/recovery, injected storage faults and IDL
generation. These build-root tests are separate from emulator integration.
Current device results and remaining acceptance gaps belong in the paired
validation evidence documents; mocked identities do not prove product identity
integration. Real Installer, argo and UI policy integration remains necessary.

The daemon marks eligible PERSISTENT/SESSION grants cacheable for at most 500 ms
and reconciles installation generations during its timer work. Each client
handle keeps at most 64 request-cache entries; SESSION entries require the
server-confirmed session and generation, and cannot outlive the supplied TTL or
session deadline. The cache is not shared across handles. Events, epoch changes
and disconnects invalidate entries. QUERY and AUTHORIZE always check the daemon.
The real approval UI, Installer lifecycle hooks and product policy need
integration; a protocol exerciser is not a production UI.

## Troubleshooting

Missing pkg-config dependencies on the host do not establish an SDK problem;
use the configured GBS profile. Socket access errors require checking DAC,
SMACK and role policy independently. Read credential/role logs without adding
raw scope, conversation content or credentials. A daemon without exactly one
valid activation listener must fail startup; direct bind is not a fallback.
