# API 04: Handle results and asynchronous callbacks

[한국어](04-results-and-callbacks.ko.md)

Use ASYNC when Argo must remain responsive while a user decides. The callback
result is borrowed; clone it before storing it. The complete executable is
[request.c](../../../src/examples/request.c), including its bounded watchdog,
remote cancellation and shutdown.

The complete request.c builds operation=read inputs. Its callback/dispatcher
ownership applies to the API fragment below, but it is not the summary tool's
operation=execute executable. The exact summary Argo input is implemented by
[Guide 13's smoke actor](../../../tests/smoke/argo.c). Never change operation
while assuming the same approval covers it.


## Before you start

Use an authenticated Argo process and the input from
[API 03](03-request-and-check.en.md). Link `consent glib-2.0`. Create an
explicit
GLib context and client on one thread, and iterate that context only after
submission returns. A separate authenticated UI handles the prompt.

## 1. Prepare the callback owner

```c
#include <consent.h>
#include <glib.h>

struct completion {
  GMainLoop *loop;
  consent_result_t *owned_result;
  int status;
};

static void completed(int status, const consent_result_t *result, void *data) {
  struct completion *completion = data;
  completion->status = status;
  if (status == 0)
    completion->status = consent_result_clone(result,
                                             &completion->owned_result);
  g_main_loop_quit(completion->loop);
}
```
The result is NULL when callback status is nonzero. Clone failure is another
error. Never free the borrowed callback argument. `owned_result` must start as
NULL and be freed once by its owner after reading it.

## 2. Submit the approval request

```c
/* Fragment: client uses context; params is the complete Argo input from API 03.
 * Run on its creating thread; completion stays alive through destroy. */
struct completion completion = {.loop = loop};
consent_async_id_t local_id = 0;
int status = consent_request_async(client, params, completed, &completion,
                                   &local_id);
consent_params_free(params);
params = NULL;
/* Only after API return: run the loop with the watchdog from request.c.
 * Do not let this stack context outlive completion/detach/destroy. */
```
Use the complete `request.c` loop and watchdog rather than an unbounded wait.
It sets a 60000 ms remote deadline and a 65000 ms application watchdog. On
watchdog expiry it detaches local delivery, leaves loop iteration, then attempts
remote cancellation with the original request identity.

## 3. Read the three outcomes separately

| Value | Meaning | Next action |
| --- | --- | --- |
| Submission return 0 | Accepted locally | Keep dispatching; not approval |
| Callback status 0 | A result is available | Read its decision |
| Decision ALLOWED | Approval request succeeded | Enforcer still calls AUTHORIZE |
| DENIED/EXPIRED/CANCELLED/INVALIDATED | No usable request approval | Do not execute |
| Nonzero submission or callback status | API/transport failure | Reconcile; no effect |

These are source-backed contract outcomes, not fresh execution output.
Management results can have no decision; read their operation-specific fields.

## 4. Keep each ID in its proper place

| ID | Owner and use |
| --- | --- |
| `consent_async_id_t` | Local handle; suppress callback with async_detach |
| `client_request_id` | Argo's stable input for remote result/cancel lookup |
| Returned `request_id` | Stored approval request; pass to UI for get_prompt |
| `operation_id`, `step_id` | Enforcer's immutable AUTHORIZE retry key |

Detaching does not cancel remote work or undo a consumed ONCE grant. Remote
cancellation does not revoke an already final approval. Keep identical remote
payloads when retrying after an uncertain outcome.

## 5. Shut down in ownership order

Destroy the watchdog, detach if necessary, reconcile remote cancellation outside
context iteration, then successfully destroy the client before releasing
callback
user_data or its context/loop. Read and free the owned clone. Never race destroy
with another call. Destroy in a callback preserves that callback's borrowed
result until return; it suppresses queued deliveries afterward.

## Troubleshooting

- WOULD_DEADLOCK: do synchronous work outside ownership of the callback context.
- DISCONNECTED: create a new handle; keep remote retry identities for reconciliation.
- Timeout: do not assume request cancellation or protected execution failure.
- Fork/exec or identity change: create a new handle; inherited handles are unusable.

## Reference: dispatcher, errors and GIO lifetime

## Asynchronous approval and dispatcher lifetime

```sh
consent-example-request SUBJECT PROFILE DEFINITION POLICY_VERSION \
  SCOPE PURPOSE RECIPIENT CLIENT_REQUEST_ID OPERATION_ID [SESSION GENERATION]
```

The example creates an explicit `GMainContext`, creates the client with that
context, submits `consent_request_async()`, frees the copied input builder, and
only then runs `GMainLoop` on the creating thread. It sets a 60-second remote
approval deadline and a 65-second application watchdog. The library's own ASYNC
local timeout is 300 seconds. A separate authenticated UI must service the
prompt.

`0` from submission means local acceptance. The callback is queued after API
return, including immediate/cache results, at most once per live registration,
without library locks. Callback status can still be an error; a successful
decision can be DENIED, EXPIRED, CANCELLED or INVALIDATED. Request handling
polls
remote PENDING until a terminal result or local timeout. A direct result lookup
can return PENDING.

The callback clones the borrowed result and quits the loop; the main function
reads and frees the owned clone afterward. It never performs synchronous work
while owning the callback context. On watchdog expiry it first detaches the
local callback, then attempts remote cancellation **outside** context iteration
using the original request identity. A cancellation can observe an already final
request and does not revoke its grants. Failed cancellation still needs later
reconciliation. Finally it destroys the client before releasing callback data
and the loop/context, so queued callbacks cannot reference expired stack memory.

Destroy is also supported from inside a callback; the current borrowed result
lasts until that callback returns. Do not run the same dispatcher on a second
thread or nest loop iteration inside ASYNC submission. SYNC calls from a thread
owning the callback context return WOULD_DEADLOCK. Fork/exec or identity changes
require a new handle. Disconnect requires a new online handle, using the
original
remote retry IDs when reconciling existing work.


## Errors and safe reconciliation

Use `consent_error_e` and `consent_error_string()`; standard values alias
`tizen.h`.
PROTOCOL, OUTCOME_UNKNOWN, SESSION_INACTIVE, SESSION_CLOSED, CONFLICT and
STORAGE
are the six module-local values beginning at `TIZEN_ERROR_MIN_MODULE_ERROR`.
They are not a separately assigned platform module range. All consumers, daemon
and library must use the same error ABI; old unpublished `-200x` values are
obsolete.

| Outcome | Caller action |
|---|---|
| Status 0, decision not ALLOWED | Do not execute; follow the reported request/check state. |
| INVALID_PARAMETER | Fix schema/ownership; do not reinterpret it as denial or approval. |
| PERMISSION_DENIED | Correct trusted identity/delegation; do not switch to offline as a fallback. |
| TIMEOUT / OUTCOME_UNKNOWN | Do not execute. Reconnect if necessary, look up request state, or retry the same supported deduplication key with identical payload. |
| DISCONNECTED | Recreate the online handle; preserve remote operation identity when reconciling. |
| WOULD_DEADLOCK | Leave callback context ownership or use an ASYNC API. |
| STALE / SESSION_INACTIVE / SESSION_CLOSED | Reconcile current policy/session/installation state. Old approval is not reusable. |
| CONFLICT | The ID is already bound to different content; resolve the operation instead of overwriting it. |
| BUSY / TOO_LARGE / NO_SPACE / IO / STORAGE / PROTOCOL | Stop protected execution and handle the reported bounded resource, storage or transport failure. |

The header descriptions follow local references
`platform/core/api/device/include/battery.h`, `device/doc/device_doc.h`,
`app-manager/include/app_context.h` and
`app-manager/doc/appfw_app_manager_doc.h`.
The contracts above come from this repository's client, wrappers and daemon;
external platform version/privilege annotations were deliberately not copied.


## Client GIO ownership and process signals

Each online client owns an `IoContext` with its own I/O thread,
`GMainContext` and `GMainLoop`. `SocketTransport` owns the nonblocking
`GSocket`, read/write sources and bounded partial-frame buffers.
`CallbackDelivery` retains the caller's callback context and delivery sources.
The I/O context attaches its reusable wake source before starting the thread;
stop is monotonic and remains effective when requested before loop entry.
One scheduler combines operation deadlines, result polling and partial-frame
deadlines. It does not run a periodic timer when the client is idle.

Close first stops and joins the I/O producer, then destroys delivery sources
outside the state lock. User callbacks run on the caller context without locks;
closing from a callback preserves its borrowed result until return. In a fork
child, each owner abandons inherited GLib pointers and avoids inherited locks,
source destruction and thread joins. The transport closes its inherited socket
FD once. Create a new handle after fork; inherited handles remain unusable.

GIO has a process-wide signal effect: the first `GSocket` initialization on the
verified target GLib 2.80.5 sets `SIGPIPE` to `SIG_IGN`, even if a custom
handler
was installed previously. A dedicated target probe confirmed this behavior
(PID 3558234); socket sends also use GIO's signal-suppression behavior.
The client does not save or restore signal handlers around GIO calls.
Applications embedding other libraries must account for this GIO policy.
This transport change preserves the C ABI, wire protocol and authentication
checks, but does change the signal behavior of the former raw-socket client.

Online create can return OUTCOME_UNKNOWN if hello transmission was attempted
but no reply arrived before disconnect or timeout. Both create functions leave
the output
handle NULL on failure; create a new handle to reconnect. This handshake error
does not mean a protected action or approval request was issued. An
authenticated
role rejection can surface as DISCONNECTED or OUTCOME_UNKNOWN during hello;
connection failure alone is not a PERMISSION_DENIED policy decision.

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

---

[Previous](03-request-and-check.en.md) · [Next](05-sessions-and-data.en.md) ·
[API overview](../02-c-api.en.md)
