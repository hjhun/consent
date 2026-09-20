# consent IPC protocol version 1

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
TTL by the active session deadlines. It caps lifetime at one
second and the cache at 64 entries per client with LRU eviction. The full request context is
compared, except transport ID, request ID, operation ID and deadline. A cache
hit returns `source=CACHE` and omits the remote `request_id`. QUERY and AUTHORIZE
always contact the daemon. The cache key includes session and generation. Sharing cache storage across
multiple client handles remains future work.

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
`session_state`, `data_register`, `data_derived`, `data_release`, and `cleanup`.
Detailed policy and mandatory-field rules are enforced in the repository and
identity adapter; a field's presence is not proof of identity or authority.

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
