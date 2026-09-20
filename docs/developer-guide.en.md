# consent developer guide

This guide describes the implementation in this repository. The original
[CEP](CEP_Consent_Framework.md) remains a design proposal. Implementation and
verification are being integrated; the presence of an API or test source does
not establish that every CEP acceptance criterion has passed.

## Build environment

Use CMake 3.12 or later, Python 3, C11, C++17 and pkg-config packages `glib-2.0`, `gio-2.0`,
`gio-unix-2.0`, `sqlite3`, `libsystemd`, `pkgmgr-info`, `capi-base-common`, and
native Tizen `parcel`.
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
| `consentd` | `/usr/bin/consentd`, `/usr/sbin/consent-installation-authority`, `/usr/sbin/consent-storage-prepare`, systemd units, empty identity policy |
| `consent-tests` | C exercisers and isolated tests under the configured libexec directory |

Implementation and test sources live under `src/`. Build settings are split by
component. The daemon alone owns `/opt/var/lib/consentd`, mode 0700, and `consent.db`.
Tizen links `/var` to `/opt/var`; the canonical path satisfies strict state-path
validation without accepting symlinks. The root preparation helper owns directory
creation and migration; neither systemd StateDirectory nor RPM directory attributes
may recursively change ownership before validation.
Systemd owns `/run/.consentd.sock`; the daemon requires the inherited listener.
IPC uses native Tizen Parcel with a bounded four-byte length prefix.
The RPM installs both sockets.target.wants/consentd.socket and the AMD-style
basic.target.wants/consentd.service symlink. Boot startup and socket activation
share the same inherited listener.

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

The service uses the existing platform account `security_fw` (observed UID/GID402),
with `CAP_SYS_PTRACE` as its only bounded/ambient capability and NoNewPrivileges.
The literal account `security` was absent; no new account is created. The account
name is resolved by the platform, never replaced with an assumed numeric UID.
The root-only ExecStartPre preparation helper is a separate process. The socket
remains mode0660, root:`system_share`.
This group grants transport access only. The daemon authenticates roles from
kernel credentials and the root-owned `/etc/consent/roles.conf`. The shipped
policy authorizes no identities. A platform integrator must provision exact
executables, kernel security labels, roles and delegated contexts.

The production client verifies the fixed `/run/.consentd.sock` path, root ownership
and unchanged socket device/inode across connection. It accepts the inherited
systemd endpoint only when kernel peer credentials identify PID 1/UID 0 and the
configured `System::Privileged` security label. `/run` may be root:`system_share`
group-writable on this target; other writable parent paths remain rejected.
A socket owned by a normal process does not authenticate as the service.

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
sdb -s "$CONSENT_DEVICE" shell 'rpm -Uvh --replacepkgs --replacefiles /tmp/consent-0.1.0-1.x86_64.rpm /tmp/consentd-0.1.0-1.x86_64.rpm /tmp/consent-devel-0.1.0-1.x86_64.rpm /tmp/consent-tests-0.1.0-1.x86_64.rpm'
sdb -s "$CONSENT_DEVICE" shell 'systemctl status consentd.socket'
sdb -s "$CONSENT_DEVICE" shell 'journalctl -u consentd.service -n 80 --no-pager'
```

Use the actual release and RPM paths printed by GBS. The verified repeated
development installation uses `--replacepkgs --replacefiles` for these exact
consent RPMs without changing their version number.

Stopping `consentd.service` alone permits reactivation. Stop both the socket
and service for maintenance. Never remove the systemd endpoint manually.

## API integration contract

Public C API headers live only in `src/consent/inc/`; private C++ headers stay
outside that directory. The installed header remains
`/usr/include/consent/consent.h`, included as `<consent.h>` by consumers using
pkg-config. It includes `<tizen.h>`, so `consent.pc` declares the public
`capi-base-common` dependency and supplies its compiler/linker requirements.

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
After a holder restarts, call `consent_cleanup_get_pending()` with the explicit
subject/profile and `reconcile=1` to discover outstanding cleanup from its
previous process instance. Acknowledge actual deletion with
`consent_data_release()` using that same context. This reconciliation gives
cleanup access only. See the [storage guide](storage-design.en.md) for total DB
loss and incomplete-holder reconciliation limits.

## Public error values and upgrades

Use `consent_error_e` names rather than copied numbers. Standard errors use
Tizen aliases. The original eight standard values remain unchanged:

| Public name | Value |
|---|---|
| `CONSENT_ERROR_NONE` | `0` |
| `CONSENT_ERROR_INVALID_PARAMETER` | `-EINVAL` (`-22`) |
| `CONSENT_ERROR_OUT_OF_MEMORY` | `-ENOMEM` (`-12`) |
| `CONSENT_ERROR_PERMISSION_DENIED` | `-EACCES` (`-13`) |
| `CONSENT_ERROR_BUSY` | `-EBUSY` (`-16`) |
| `CONSENT_ERROR_NOT_FOUND` | `-ENOENT` (`-2`) |
| `CONSENT_ERROR_TIMEOUT` | `-ETIMEDOUT` (`-110`) |
| `CONSENT_ERROR_DISCONNECTED` | `-ENOTCONN` (`-107`) |

`CONSENT_ERROR_WOULD_DEADLOCK` now uses the standard `-EDEADLK` value.
Additional public aliases are `CONSENT_ERROR_STALE` (`-ESTALE`),
`CONSENT_ERROR_TOO_LARGE` (`-E2BIG`), `CONSENT_ERROR_NO_SPACE` (`-ENOSPC`),
`CONSENT_ERROR_INVALID_OPERATION` (`-ENOSYS`) and `CONSENT_ERROR_IO` (`-EIO`).

Consent-specific errors occupy the module-local Tizen range. This does not
claim a platform-wide module allocation:

| Public name | Value |
|---|---|
| `CONSENT_ERROR_PROTOCOL` | `TIZEN_ERROR_MIN_MODULE_ERROR + 0` |
| `CONSENT_ERROR_OUTCOME_UNKNOWN` | `TIZEN_ERROR_MIN_MODULE_ERROR + 1` |
| `CONSENT_ERROR_SESSION_INACTIVE` | `TIZEN_ERROR_MIN_MODULE_ERROR + 2` |
| `CONSENT_ERROR_SESSION_CLOSED` | `TIZEN_ERROR_MIN_MODULE_ERROR + 3` |
| `CONSENT_ERROR_CONFLICT` | `TIZEN_ERROR_MIN_MODULE_ERROR + 4` |
| `CONSENT_ERROR_STORAGE` | `TIZEN_ERROR_MIN_MODULE_ERROR + 5` |

This corrects unpublished v0.1 error numbers. Rebuild and upgrade `consentd`,
`libconsent` and every consumer together. Mixing earlier `-200x` error values
with the new values is unsupported. Native Parcel framing and field layout
are unchanged; the numeric status contract changed. A nonzero status always
blocks protected execution, regardless of the accompanying decision.

## Parameter fields

The public ABI uses opaque `consent_params_t` builders and string fields.
`consent_params_set()` copies UTF-8 input; integer setters encode decimal values.
Protocol identity fields and underscore-prefixed internal fields are reserved.

| Operation | Required fields and interpretation |
|---|---|
| register/update | `operation_id`, `expected_generation`, `definition`, `enforcer`, positive `policy_version` and `text_revision`, `level` 0–3, comma-separated `modes`, `default_locale`, `message.<locale>.title` and `.body`; optional typed template schema and locale aliases below; package/app are separate API arguments |
| unregister | Package name as its own argument, plus params containing retry-stable `operation_id` and current `expected_generation`; no app ID |
| request | `subject`, `profile`, stable `client_request_id`, `operation_id`, requirements; optional `session` and its `generation`; `deadline_ms` is independent of local wait timeout |
| check | `subject`, `profile`, requirements; mode is QUERY or AUTHORIZE; AUTHORIZE also requires `operation_id` and `step_id` |
| requirement | `consent_params_add_requirement()` appends definition, operation, exact scope, purpose and recipient; at most 16 requirements; typed definitions also require matching `rN.policy_version` |
| session open | `subject`, `profile`; lifecycle is CONNECTION_BOUND or RESUMABLE_CONVERSATION; bounded timeout values are validated by the daemon |
| session transition | `subject`, `profile`, `session`, current `generation`; resume also requires the rotating `resume_token` |

Scopes compare exactly in this version. The UI must display the registered
message and actual scope/purpose/recipient together. Typed message parameters
are bound to validated request fields; a displayed query interval and a
post-acquisition retention interval are separate values. Level 3 permits ONCE
only. Increment `policy_version` for a changed policy meaning.
`text_revision` is monotone per definition ID, including after reinstall or a
policy version increase. Changes to `default_locale`, the registered message
map or the locale alias map require a strictly higher text revision; identical
maps may retain the same revision.

An approval response re-evaluates the full current AND of requirements without
consuming ONCE grants. For example, A was already allowed while B awaited a
choice. If A expires, is revoked or is consumed by another operation before the
user approves B, the request ends as `INVALIDATED`, with current per-condition
results (A `CONSENT_REQUIRED`, B `ALLOWED`). The explicit, still-valid B grant is
retained and remains unused. This does not authorize the combined operation.
The old request is terminal and cannot reopen its prompt automatically; an
authenticated requester must initiate a new request with fresh request/operation
IDs if further approval is needed. Actual protected work still requires
`AUTHORIZE` against all conditions.

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

## Localized approval text

The A-15 increment adds a bounded, typed `{name}` template contract while
preserving existing literal messages. Frozen build15 passed GBS and the isolated
emulator C API scenarios; the [verification record](verification.en.md) identifies
the tested snapshot and limits. This formatter supports plain text and canonical decimal
integers. It does not implement ICU syntax, plural rules, dates, or localized
number formatting.

Registration defines each variable's type, limits and trusted source. A
variable is derived from its validated requirement or retention metadata, not
from a caller-supplied display string. For example, an integer scope `30` can
represent 30 days of past query coverage; `retention_ms` is a distinct interval
for retaining acquired results, measured in milliseconds. Formatting performs
no unit conversion. An omitted `retention_ms` uses the policy's actual default
of zero, which must satisfy the declared bounds. A missing string source is
invalid; an explicitly supplied empty string is allowed by the string schema.
The UI displays scope, purpose, recipient, sensitivity and
allowed modes alongside the complete registered sentence.

Use the existing params builder to register the following additional fields:

| Field | Contract |
|---|---|
| `template_version` | `1` for typed messages |
| `parameter.<name>.type` | `integer` or `string` |
| `parameter.<name>.source` | Integer: `scope` or `retention_ms`; string: `scope`, `purpose`, `recipient` or `operation` |
| `parameter.<name>.min`, `.max` | Required inclusive signed 64-bit bounds for an integer |
| `parameter.<name>.max_bytes` | Required UTF-8 byte limit from 1 to 512 for a string |
| `locale_fallback.<requested>` | Direct target locale with both title and body registered |

For example, register `parameter.days.type=integer`, `.source=scope`,
`.min=1`, `.max=365`, and body `Read the last {days} days?`, together with
`template_version=1` and a title. A request with `r0.scope=30` and the current
`r0.policy_version` then supplies the value `30` from the authorization context.
Typed requests and checks reject a missing or stale policy version. Do not send `display_args` or
`r0.arg*` fields; the daemon rejects caller-supplied display arguments.
Changing parameter types, bounds or sources requires a new policy version.

At most eight variables are allowed, each named by a 1–32 character ASCII
identifier. Every typed locale needs title and body; the union of their
placeholders must exactly match the declared variable names. Each template
is limited to 4,096 UTF-8 bytes and each rendered field to 8,192 bytes.
Integers must use canonical decimal form: `30` is valid, `030`, `+30`,
`30.0`, overflow and out-of-range values are rejected. Invalid registration,
missing translations or argument errors never imply approval.

The UI explicitly sets `template_version=1` and its requested `locale` when
fetching typed prompts. The response's top-level `locale` is the requested
locale; each `rN.locale` identifies the selected registered translation.
Use `consent_prompt_format(result, index, "title", &text)` or the same call
with `"body"` for a returned requirement. On success, `text` is an allocated
UTF-8 string owned by the caller; release it with `free()`. On failure, the
output is NULL and the UI must not proceed as if formatting succeeded. The
formatter processes the template in one pass and never interprets substituted
text as another template or markup. Render the result as plain text.
Invalid prompt fields, indices or arguments return
`CONSENT_ERROR_INVALID_PARAMETER`; allocation failure returns
`CONSENT_ERROR_OUT_OF_MEMORY`.

```c
#include <consent.h>
#include <stdio.h>
#include <stdlib.h>

int print_prompt_body(const consent_result_t *prompt, unsigned int index) {
  char *text = NULL;
  int status = consent_prompt_format(prompt, index, "body", &text);
  if (status != 0)
    return status;
  puts(text);
  free(text);
  return 0;
}
```

Locale selection first uses an exact registered translation, then an explicit
alias directly to a registered locale. The supported `ko-KR` to `ko` and
`en-US`/`en-GB` to `en` fallbacks follow, then the registered default locale.
Aliases cannot chain or form cycles. Script and region subtags are not
generically stripped. Registration rejects an alias whose source already has
a complete registered translation. A typed approval response must echo the top-level
requested `locale` and current `prompt_token`. Refetch after a locale change; an old
token cannot authorize the newly displayed prompt. Changes to aliases require
a new text revision and invalidate pending prompts.

## Recovery and verification

The root-only installation authority manages `installations.conf` through
`begin`, `attach`, `commit`, and `remove` commands. A begin operation rotates the
generation and fences the old app list; attach records each package app; commit
activates the completed list. Use `absent` only for a never-recorded package.
Each command also takes a unique operation ID and expected generation; retry
the same operation after an uncertain result. Register only after the real
package installation and authority commit have succeeded. For removal,
mark the installation removed in the authority first, then unregister consent
definitions using the same expected generation. The tombstone prevents new
authorization while package-wide cleanup proceeds. Reinstallation uses a new generation. The utility is an
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

`scripts/emulator-scenario.sh` provides the isolated emulator phases. `basic`
requires fresh test installation state; it does not silently delete previous
results. Later phases cover persistence, running/stopped DB deletion, corrupt DB
recovery, stale DB replacement and same-UID role rejection. Run only on the
selected development emulator as root with the `System` security label.
`endpoint-fixture` is a separate manual endpoint-authentication fixture excluded
from CTest because it uses the actual `/run/.consentd.sock` path. Follow the
[validation evidence](verification.en.md) setup before using it.

Keep shutdown evidence separate for incomplete IPC input, a request waiting for
UI, and DB work that is actually pending. The manual `wire-scenario --shutdown-wait` fixture proves closure of a connection
holding a partial header;
the supervisor also checks normal service exit and the `database-drained` log.
That alone does not prove a pending DB job was completed. A request awaiting UI
holds neither a DB transaction nor a worker, so its disconnect/restart behavior
is a separate scenario. See the recorded results for the exact tested boundary.

The separate `consentd-shutdown-test` executable supports a controlled pending-DB
scenario. It uses the normal isolated daemon configuration and adds only the
`repository_shutdown_interposer.cc` test helper; production `consentd` and the
ordinary `consentd-test` do not contain that helper. The test controller prepares
daemon-account-owned mode0700 `/tmp/consent-shutdown-gate` and a mode0600 FIFO named
`shutdown-db-release`, keeping it open for reading and writing. A revoke that
actually changes a grant pauses before COMMIT and creates mode-0600
`shutdown-db-ready` containing `pid=N state=before-commit`. Match that PID to the
test daemon, initiate service shutdown, then send the single byte `C` through
the FIFO within five seconds. Require the supervisor's normal-exit and
`database-drained` checks and inspect the committed revocation only after the
daemon stops. A gate error or timeout fails the transaction; reaching the ready
marker alone is not a successful drain test. This describes the test protocol;
executed PASS claims belong to the snapshot-specific verification record.

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

GBS builds the native Parcel client, daemon, C exercisers and installation
authority with GCC 14.2 and `-Werror`. Package checks cover client contracts,
repository policies/recovery, injected storage failures, process-kill boundaries
and IDL generation. Build-root tests are separate from emulator integration.
The [validation evidence](verification.en.md) records each tested snapshot,
executed results and remaining acceptance gaps. Mocked identities do not prove
product identity integration; real Installer, argo and UI policy integration
remains necessary.

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
