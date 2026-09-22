# Design 03: consent IPC protocol version 1

This document describes the implemented wire format in
`src/common/message.hh`, using the native Tizen `parcel` library from
`platform/core/base/bundle`. `src/protocol/consent.idl.json` is the schema;
the deterministic compiler generates `consent_wire.hh` with native
`Parcelable` Field and Envelope classes. CEP v0.4 JSON examples describe
domain objects; its wire protocol is Parcel. JSON and GVariant are not used
as an encapsulated payload.

## Framing and encoding

Each AF_UNIX/SOCK_STREAM message contains a four-byte unsigned **big-endian**
body length followed by that many native Parcel bytes. The body must be
nonempty and no larger than 65,536 bytes. Senders and receivers handle partial
headers, partial bodies and multiple frames per read.

Both readers and writers call `Parcel::SetByteOrder(true)`. All typed integers
and string lengths are big-endian on every host architecture. A string uses
`WriteString`: a u32 byte count including the final NUL, followed by UTF-8
bytes and that NUL. The envelope has this exact field order:

| Native Parcel field | Type / bound |
|---|---|
| version | u32, exactly 1 |
| kind | u32: request=1, reply=2, event=3 |
| correlation | u64: 1..INT64_MAX for requests/replies, 0 for events |
| method | UTF-8 string, 1..128 bytes excluding NUL |
| status | i32: nonpositive reply status, zero for request/event |
| field count | u32, at most 256; overall Message at most 256 fields |
| repeated key/value | key string <=128, value string <=8192 bytes |

The encoder orders fields lexicographically. Receivers reject duplicate fields,
reserved envelope metadata inside the field array, invalid UTF-8, embedded NUL,
missing final NUL, oversized counts, truncated data and trailing bytes. Keys
are nonempty ASCII characters from `A-Z a-z 0-9 _ . -`. Requirement lists have
at most 16 entries. Numeric API values represented as strings reject trailing
characters and signed 64-bit overflow.

The generated reader uses checked Parcel primitive reads. Before reading a
string, it verifies the length prefix, configured maximum and remaining body,
then validates the borrowed span and only then constructs the string. It does
not call `Parcel::ReadString()` on untrusted data. `ReadParcelable()` alone is
not a success proof; the caller also checks generated validation and complete
body consumption.

This complete hexadecimal regression vector includes the four-byte header.
It represents `v=1, id=1, method=hello` with no extra fields:

```text
00000022
00000001 00000001 0000000000000001
00000006 68656c6c6f00
00000000 00000000
```

The first line is body size 34. The next lines are version/kind/correlation,
method string, status and field count. Whitespace is only for readability.

## Envelope and correlation

Every client request has `v=1`, a positive decimal `id` unique on the
connection, and a `method`. All reply frames repeat `v` and `id` and carry
`status`, `epoch` and `revision`. The status is `0` on successful processing or
a negative API error code. Decision strings are independent of API errors:
`ALLOWED`, `DENIED`, `CONSENT_REQUIRED`, `PENDING`, `CANCELLED`, `EXPIRED`, and
`INVALIDATED`. A nonzero status never permits a protected action.

The server obtains caller identity from the accepted socket and protected
platform policy. No client-provided `role`, PID, UID or private
underscore-prefixed identity field is authoritative. The public params
builder rejects these reserved fields. The server separately validates its
input; library validation is not an authorization boundary.

A `request` reply with `decision=PENDING` includes `request_id`. The client I/O
thread periodically sends `result` for this request, using a new transport
correlation ID. It releases the server worker and DB executor between polls.
A final decision completes the original local operation exactly once. A
`result` API explicitly requested by the caller may return `PENDING` directly.
The stable caller `client_request_id` and operation identifiers survive loss
of a transport reply; the transport `id` is not a persistent idempotency key.

## Invalidation and cache contract

The daemon broadcasts `v=1, method=event, event=invalidate, epoch=...,
revision=...` after a committed change. Events are ordered ahead of that
change's reply on each connection. The client clears its entire cache on any
event or changed epoch/revision. Disconnect also clears and disables it.

A request response can opt in to caching using `cacheable=1` and
`cache_ttl_ms`. The client accepts this only for ALLOWED requests with a nonempty epoch
and valid revision. Requests carrying a session also require the response to
confirm exactly the requested session and generation; the daemon bounds their
TTL by the active session deadlines. The client conservatively anchors that
lifetime at local operation admission, subtracting all time spent awaiting the
response; a delayed response never restarts a session's remaining lifetime.
It caps lifetime at one second and the cache at 64 entries per client with LRU eviction. The full request context is
compared, except transport ID, request ID, operation ID and deadline. A cache
hit returns `source=CACHE` and omits the remote `request_id`. QUERY and AUTHORIZE
always contact the daemon. The cache key includes session and generation. Sharing cache storage across
multiple client handles remains future work.

## Production endpoint authentication

The production library uses the compiled `/run/.consentd.sock` endpoint.
It verifies root-owned parent directories and a root-owned, non-world-writable
socket before and after connection, including the same device and inode.
The only group-writable parent exception is the exact `/run` directory owned
by `root:system_share` on Tizen. No other writable-parent exception is made.

Filesystem observations alone do not eliminate pathname races. The connected
socket must also report kernel SO_PEERCRED PID 1 and UID 0, its exact original
AF_UNIX bound pathname `/run/.consentd.sock` with the expected sockaddr length
and terminal NUL, and SO_PEERSEC `System::Privileged`. This label was observed
on the emulator's systemd-owned production listener. Missing credentials or
label, a direct root server, a differently labeled endpoint, and a renamed
unrelated systemd socket all fail verification. Production has no environment
switch to disable these checks. The separately compiled isolated test client
has a different endpoint and cannot change production policy.

## Operations and parameter representation

Public C params are extensible string dictionaries. A requirement list uses
`count`, `r0.definition`, `r0.operation`, `r0.scope`, `r0.purpose`,
`r0.recipient`, then `r1.*`, and so on. Registration uses `package`, `app`,
`definition`, `enforcer`, `operation_id`, `expected_generation`,
`policy_version`, `text_revision`, `level`, `modes`, `default_locale`,
`message.<locale>.title`, `message.<locale>.body`, and retention metadata.

`consent_register(client, package, app, params)` copies both explicit identity
fields. `consent_unregister(client, package, params)` removes by package and
requires no app ID; its params carry stable `operation_id` and
`expected_generation` so a delayed uninstall cannot remove a later install.
The trusted installation generation comes from daemon-side authority.

Request and check use `subject`, `profile`, optional `session`/`generation`,
`client_request_id`, `operation_id`, `step_id`, `deadline_ms`, and requirements.
Check `mode` is `QUERY` or `AUTHORIZE`. Server methods are `hello`, `register`,
`unregister`, `request`, `result`, `cancel`, `check`, `prompt`, `respond`,
`revoke`, `session_open`, `session_suspend`, `session_resume`, `session_close`,
`session_state`, `data_register`, `data_derived`, `data_release`, `cleanup`,
and `cleanup_list`.
Detailed policy and mandatory-field rules are enforced in the repository and
identity adapter; a field's presence is not proof of identity or authority.

`consent_cleanup_get_pending()` maps to `cleanup_list`. An authenticated holder
must supply `subject` and `profile`; results contain at most 48 pending/failed
cleanup entries in `count` and `aN.artifact`, `aN.session`, `aN.state`, and
`aN.error`. Normally only the current process instance is visible. With
`reconcile=1`, the same stable holder identity can discover blocked artifacts
from previous instances. It may acknowledge actual deletion through
`consent_data_release()` with `artifact`, `success`, `reconcile=1`, and the same
subject/profile. Discovery and reconciliation never grant data-use permission,
transfer ownership, or permit registration under an old instance.

## Typed localized prompts

The A-15 increment adds named plain-text templates without changing the Parcel
envelope version. Frozen build15 passed GBS and the isolated emulator C API
scenarios; see the paired verification record for snapshot evidence and limits. Existing literal
definitions retain their compatibility path.

The schema remains a field dictionary inside the existing Envelope:

| Registration field | Value |
|---|---|
| `template_version` | Exactly `1` |
| `parameter.<name>.type` | `integer` or `string` |
| `parameter.<name>.source` | Integer: `scope` or `retention_ms`; string: `scope`, `purpose`, `recipient`, `operation` |
| `parameter.<name>.min`, `parameter.<name>.max` | Required inclusive signed 64-bit limits for integer values |
| `parameter.<name>.max_bytes` | Required 1–512 byte maximum for UTF-8 string values |
| `locale_fallback.<requested>` | Direct target with registered title and body |

Variable names match `[A-Za-z_][A-Za-z0-9_]{0,31}`; at most eight are allowed.
Every typed locale has both title and body, whose combined placeholder names
exactly equal the declared variables. A template is nonempty and bounded to
4,096 UTF-8 bytes. Integer schema bounds and values use canonical signed
decimal form; leading zeros, a plus sign, negative zero, fractions and
overflow are rejected. There is no brace-escaping syntax. Invalid braces,
unknown variables/properties and undeclared parameters fail validation.

The request carries the ordinary `rN.scope`, `.purpose`, `.recipient` and
`.operation` fields. For typed definitions, both request and check require
the current `rN.policy_version`; missing or stale versions fail with `-ESTALE`.
The request does not carry independent formatting arguments.
`display_args`, `display_args.*`, `rN.display_args`, `rN.display_args.*` and
`rN.arg*` are rejected.
The daemon appends `rN.template_version=1`, `rN.arg_count`, and the bounded
`rN.argM.name`, `.type`, `.value` list to each typed prompt row. Arguments are
ordered by variable name; callers should match the explicit names. This list
is validated data for the UI formatter, never a replacement authorization
context. The whole prompt must still fit the existing frame and field limits;
oversized combined prompts fail rather than dropping requirements or arguments.
The repository limits prompt content to 240 fields and reserves envelope/token
space within the 65,536-byte frame budget before issuing a new prompt token.

Registration validates the complete templates, variable schema and explicit
locale aliases together. A variable has a declared type, bound and source in
validated request context or retention metadata. Arbitrary display-only values
cannot replace that source. A query scope such as integer `30` and retention
metadata `retention_ms` represent different concepts and units; the formatter
does not convert between them. Absent `retention_ms` uses the policy's real
default zero, which must satisfy the declared integer bounds. Missing string
source fields are rejected; explicitly present empty strings are accepted by
the string schema. Template syntax is a whole sentence with named
`{name}` substitutions. Formatting is a single pass, so substituted braces
are literal display data. Output is plain UTF-8 text, with canonical decimal
integers; ICU expressions, plural/date rules and localized numeric formatting
are outside this contract.

For typed definitions, `prompt` requires explicit `template_version=1` and
the UI's requested `locale`. Typed responses return top-level
`template_version=1` and the original requested `locale`, while each
`rN.locale` identifies the selected translation. An exact registered translation
is selected first. Aliases point directly to a complete registered locale;
chains, cycles and aliases shadowing a complete source translation are invalid.
If neither an exact translation nor an alias is available, the supported
`ko-KR` to `ko` and `en-US`/`en-GB` to `en` fallbacks apply, then the definition's
default. Other script or region subtags are not automatically removed.

The UI completes each returned title/body using
`consent_prompt_format(result, requirement_index, field, &text)`, with field
equal to `"title"` or `"body"`. A successful result transfers a malloc-allocated
UTF-8 string to the caller, which releases it using `free()`. Failure leaves
the output NULL. Each rendered field is bounded to 8,192 bytes. The formatter
validates both template fields and their complete argument set before rendering
the selected field. It is local and does not make IPC calls or grant
authorization. The UI renders its output as plain text alongside the prompt's
actual scope, purpose, recipient, sensitivity and allowed modes.

A typed `respond` must echo the top-level requested `locale` and fresh `prompt_token`.
Locale changes require refetching the prompt and replace the previous token.
The token remains bound to the request, displayed definition revisions and UI
process instance. Text revisions are monotone per definition ID, even across
reinstallation or a policy-version increase. A changed `default_locale`,
registered message map or locale alias map requires a strictly higher
`text_revision`; identical maps may keep the same revision. These text changes
invalidate pending prompts. Changing the meaning of a variable or its source
requires a policy revision. Formatting or locale negotiation errors do not
create a grant or authorize an operation.

Missing UI capability, invalid definitions or invalid bound arguments return
`-EINVAL`. A mismatched locale or obsolete prompt token returns `-EACCES`.
Exceeding the combined prompt budget or rendered-text bound returns `-E2BIG`.
The local C formatter returns `CONSENT_ERROR_INVALID_PARAMETER` for invalid
fields, indices or schema and `CONSENT_ERROR_OUT_OF_MEMORY` for allocation
failure, with a NULL output in either case.

## Callback and resource contract

The client uses one bounded I/O thread per handle, with a maximum of 16 live
handles per process and 64 outstanding operations per handle, including
completed callbacks waiting for dispatch. Outgoing queued data is capped at
256 KiB. A partial incoming frame has a five-second completion limit.

Client creation captures the creating thread and its GLib thread-default
context, or the context explicitly supplied to
`consent_client_create_with_context()`. That same thread calls async APIs,
iterates the context and destroys the client. It must not iterate a nested
main loop during an async API. Callbacks are attached as idle sources and
never invoked inline; no library mutex is held while a callback runs.

Async success means local acceptance; remote processing errors arrive through
the callback. Async inputs and callback bookkeeping are allocated before
acceptance. Allocation failure before acceptance leaves no callback, I/O
operation or user-data reference behind. A callback owns its result only for
the duration of that invocation; use `consent_result_clone()` to retain it.
`consent_async_detach()` suppresses that local callback and does not cancel a
remote request. Use `consent_cancel_request()` for remote cancellation.

SYNC waits on a condition independently of callback dispatch. Calling it while
owning the callback context returns `CONSENT_ERROR_WOULD_DEADLOCK`.
A local timeout does not mean denial or remote cancellation. Transmission
without a known receipt can yield `CONSENT_ERROR_OUTCOME_UNKNOWN`; retry or
lookup with the same stable request ID. Async operations have a five-minute
local maximum; the daemon separately enforces the request deadline.

Destroy suppresses pending callbacks, wakes synchronous waiters and joins the
I/O thread. It is supported from a callback. The caller coordinates destruction
against other threads using the raw handle. Inherited handles after fork are
rejected before touching inherited mutexes; queued parent callbacks are also
suppressed in a child. A child creates a new handle, with a fresh process
admission count. Identity changes similarly require discarding and recreating
a handle.

## Verification scope

`src/tests/client-test.cc` exercises fragmented wire data, Parcel golden bytes, UTF-8, duplicate
keys, truncated/trailing data, bounded string/array lengths, numeric overflow, queued ALLOWED/DENIED and PENDING polling, local cache
hits, invalidation, local timeout, detach, queue bounds, callback destruction,
allocation fault injection, and fork isolation with 16 parent handles.
The fake server is a transport fixture, not evidence of platform role checks,
SQLite recovery or real approval UI behavior. Production and isolated C API
executables are separately linked; no environment variable changes the
production endpoint or disables its endpoint ownership checks.

Host sanitizer runs separate fork coverage from LeakSanitizer: run
`ASAN_OPTIONS=detect_leaks=0 client-test` for the complete ASAN/UBSAN suite,
and `ASAN_OPTIONS=detect_leaks=1 client-test --skip-fork` for leak checking.
LeakSanitizer enabled during the multithreaded fork stress hung inside the
child's fresh client creation on the host. The normal GBS test includes fork;
`--skip-fork` is a test-executable option only, not a library configuration.

## Feature selection approval version 1

The `hello` reply advertises `approval_version=1`. A new client refuses to
send an opt-in call to a daemon that did not advertise this capability. Merely
having an `approval_version` field also disables request cache lookup and
storage, including malformed versions. The daemon returns `cacheable=0` for
these evaluations. Legacy requests keep their existing behavior.

An opt-in request/check carries `approval_version=1`,
`request_kind=PREAPPROVAL|TASK`, `selection_id`, `selection_revision`,
`selection_digest`, a common `grant_mode=ONCE|SESSION|TIMED`, and `duration_ms`
only for TIMED (100–3,600,000). Every row includes `feature_id`,
`feature_revision` and explicit `policy_version`, including literal definitions.
Identifiers are 1–128 ASCII letters/digits/`_`/`-`/`.`; revisions are positive
canonical decimal int64 values. Version 1 chooses one period for the entire
batch, not independent row periods. SESSION requires an active session and its
current generation. Feature metadata never becomes a wildcard grant key.

PREAPPROVAL checks that existing grants cover the selected period: any usable
grant covers ONCE, PERSISTENT covers any period, SESSION requires the same
session, and TIMED must expire no earlier than the server's fixed admission
instant plus duration. That target is stored only after deduplication and is
not part of the caller fingerprint. Newly approved TIMED grants expire duration
after the single response instant. TASK uses any currently valid exact grant
first, granting the selected period only for missing conditions. Scope matching
remains exact; no subset inference or provider substitution occurs.

For the selection digest, form a sorted string map with the following keys:
`approval_version`, `request_kind`, `selection_id`, `selection_revision`,
`grant_mode`, optional `duration_ms`, `subject`, `profile`, `session`,
`generation`, `count`; then each `rN.definition`, `rN.policy_version`,
`rN.scope`, `rN.operation`, `rN.purpose`, `rN.recipient`, `rN.holder`,
`rN.feature_id`, `rN.feature_revision`, for N from 0 to count-1. Absent session,
generation and row values encode as empty strings. Exclude `selection_digest`
and all transport, operation, step, request, deadline and private fields.
Sort keys by ASCII byte order. Start with UTF-8 bytes `consent-selection-v1\n`
(the last byte is LF); append each key/value as decimal **UTF-8 byte length**,
colon, bytes, decimal value byte length, colon, bytes. There is no separator
between entries. SHA-256 is encoded as 64 lowercase hexadecimal characters.
The complete operation fingerprint separately binds stable IDs and the original
request context; a changed selection under an existing ID is a conflict.

Digest example: approval version 1, PREAPPROVAL, settings-1/revision1,
TIMED/1800000, owner/default, empty session/generation, count1, and r0 values
`calendar.read`, `1`, `30`, `read`, `answer`, `local`, `holder`, `calendar`, `1`
in the row-field order above produce `112eebdb4fcd0b7f0a3bebeb488d95be216259cfcaf36d0cd4da26382b9c9bdf`.

A UI sends `approval_version=1` alongside its typed-template capability.
The prompt retains the complete stored AND but returns only missing rows with
compact `count`, `total_count`, and `rN.original_index`. Provider package/app
come from the trusted definition. The token records exactly those original
indices, the selected period and selection context, plus locale. Respond must
echo that context unchanged. A formerly satisfied hidden condition is never
silently regranted, and concurrent approval of a displayed condition does not
create another ONCE grant. The whole AND is reevaluated with the same coverage
rule. If another request satisfies all displayed rows, get_prompt returns a
normal terminal `ALLOWED` or `INVALIDATED` result without a token; the UI closes
without issuing DENIED. Private fields beginning with `_` never appear in
public request-result responses. Old UI capability, malformed input or failed
render budget checks cannot replace the current token.

The 16-row limit is logical, not a promise that every 16-row prompt fits.
Combined prompts still have a stricter 240-field / 64 KiB budget and bounded
rendered text. Short literal v1 prompts fit up to 11 rows; 12 or 16 missing
rows exceed the field budget. Failure is explicit E2BIG before token mutation;
there is no partial approval or implicit splitting. The PoC catalog keeps its
complete selected vector within these bounds.
