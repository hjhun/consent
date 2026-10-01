# Guide 02: Public C API

[한국어](02-c-api.ko.md)

Choose a task below. Each chapter starts with inputs and a usable example;
the function directory is a reference for looking up declarations.

- [Start a C API client](api/01-start.en.md)
- [Register a definition](api/02-registration.en.md)
- [Request approval and authorize access](api/03-request-and-check.en.md)
- [Handle results and asynchronous callbacks](api/04-results-and-callbacks.en.md)
- [Manage sessions and retained data](api/05-sessions-and-data.en.md)

## Reading order and key terms

Start with the terms and flow below, then select your role's API from the
[function directory](#function-directory). Use the [minimal C query
program](#minimal-c-query-program)
to learn input/result handling before reading authoritative checks, asynchronous
approval, registration, and session/data lifetime. This guide describes the
current
[public headers](../../src/consent/inc/consent.h) and implementation, distinct
from
the interface draft in [Design 01](../design/01-consent-framework.md).

| Term / field | Meaning and use |
|---|---|
| Definition | Named policy registered by the Installer: enforcer, sensitivity, grant modes and displayed messages. Registration itself grants no user approval. |
| Requirement | One condition in request/check: definition plus operation/scope/purpose/recipient. Every condition must be satisfied. |
| `subject`, `profile` | Approval subject and profile within the authenticated caller's delegation. These fields do not establish caller identity. |
| `operation`, `scope` | Protected action and exact access scope, such as `read` and a scope string accepted by the registered policy. |
| `purpose`, `recipient` | Intended use and recipient. Changing either prevents blindly reusing approval for the same resource. |
| Enforcer | Service that checks authorization immediately before and controls the protected action. |
| Grant | Access approval from a user decision, with ONCE/SESSION/TIMED/PERSISTENT lifetime. |
| Receipt | Record returned by successful AUTHORIZE, bound to execution IDs and requirements; not transferable to arbitrary actions. |
| Session / generation | Logical session and current generation, separate from the socket connection. Store the returned generation after transitions; this is also distinct from installation generation. |
| Artifact / holder | Retained-data control record and the service that actually holds/deletes the data. Do not send data bodies to consentd. |

The following flow applies when fresh approval is needed. Arrows represent each
role's C API calls. Delivering a request to the product approval UI requires
separate integration. A process cannot assume every role by changing parameters.

```mermaid
sequenceDiagram
  participant I as Installer
  participant A as argo
  participant D as consentd
  participant U as Approval UI
  participant E as Enforcer
  I->>D: consent_register(package, app, definition)
  A->>D: consent_request_async(requirements)
  Note over A,D: Return 0 means local acceptance; callback delivers decision
  U->>D: consent_get_prompt(request_id)
  D-->>U: Display context and prompt_token
  U->>D: consent_respond(token, user decision)
  D-->>A: Final result callback
  E->>D: consent_check(AUTHORIZE, operation_id, step_id)
  D-->>E: Current decision and receipt on success
  Note over E: Execute bound action only after status 0, ALLOWED and receipt
```

`request` seeks user approval, `check(QUERY)` observes current satisfaction, and
`check(AUTHORIZE)` permits actual access and consumes ONCE. Do not substitute
one
result for another. Sufficient existing grants allow AUTHORIZE without a fresh
UI
request. Reuse of already acquired data follows the `operation=reuse-data`
contract below.

## Prerequisites and authorization

Production clients connect to the fixed systemd endpoint `/run/.consentd.sock`.
The shipped role policy is **default-deny**. Socket group access and UID 0 alone
do not grant an online API role. Integrators must provision the actual
executable,
kernel security label and delegated subject/profile/package/enforcer identities.
The examples never set a role field, bypass package identity, select a mock
identity, or switch the production endpoint.

| API family | Authenticated caller and additional checks |
|---|---|
| `consent_register/update/unregister` | Installer with package delegation; trusted package/app membership and installation generation |
| `consent_request`, `consent_request_async`, request result/cancel | `argo`; delegated subject/profile and ownership of the stored request |
| `consent_check`, `consent_check_async` | `checker`, `cm`, `ce` or `holder`; ownership/delegation of each definition's enforcer |
| Prompt fetch/respond | `ui`; request context, displayed token and the same UI process instance |
| `consent_revoke` | `admin`; delegated subject/profile |
| Session open/heartbeat/suspend/resume/close | `session`; delegated context and owner process instance for transitions |
| Session/cleanup state reads | Authenticated delegated subject/profile |
| Data/cleanup list/release and data reuse | `holder`; current process instance and receipt/provenance/context binding |
| Offline constructor/register | Real and effective UID 0, protected explicit image root and exclusive lifecycle lock |

Positive online runs require those real identities or a separately configured
development environment. The isolated test daemon in Guide 01 uses a
compile-time
test inventory. The [.NET UI and participant PoC in Guide
08](08-consent-ui-poc.en.md)
has a separate compile-time endpoint and roles, but validates actual pkgmgr
package/app identity. Guide 07 records the exercised scenarios. A standalone
example linked to the production library
normally receives permission denial until its actual identity is provisioned.
The approval UI and Installer product hooks remain integration responsibilities;
these examples do not implement them.

## Function directory

These 42 functions form the public C API. Names match the declarations; consult
the linked headers for detailed arguments and errors. `consent_feature_*`
belongs
to the separate PoC bridge and is not part of this public API directory.

### Clients and parameter construction

Declarations: [consent_client.h](../../src/consent/inc/consent_client.h),
[consent_params.h](../../src/consent/inc/consent_params.h).

| Function | Purpose and use |
|---|---|
| `consent_client_create` | Connect online and capture the creating thread's default GLib context. Output is NULL on failure. |
| `consent_client_create_with_context` | Select an explicit callback `GMainContext`. The library retains a reference; the caller runs its loop on the creating thread. |
| `consent_client_create_offline_registration` | Create an image-installation handle for a protected image root; use only register/destroy. |
| `consent_client_destroy` | Release the handle and queued callbacks on the creating thread. This is not remote cancellation and must not race other calls. |
| `consent_params_create` | Create an empty input builder. |
| `consent_params_free` | Release a builder; NULL is accepted. |
| `consent_params_set` | Copy a string field, replacing an existing value for the same key. |
| `consent_params_set_int64` | Set an integer as a decimal string field. Operation-specific ranges still apply. |
| `consent_params_set_check_mode` | Set `CONSENT_CHECK_QUERY` or `CONSENT_CHECK_AUTHORIZE`. |
| `consent_params_add_requirement` | Append definition/operation/scope/purpose/recipient and count together. Set extra fields such as `policy_version` and `holder` with the corresponding `rN.*` keys. |

### Approval requests and checks

Declarations: [consent_request.h](../../src/consent/inc/consent_request.h).

| Function | Purpose and use |
|---|---|
| `consent_request` | Let argo request approval and synchronously wait for the final decision. `wait_timeout_ms` and remote `deadline_ms` are independent. |
| `consent_request_async` | Request the same approval asynchronously. Check submission status, callback status, then callback decision. |
| `consent_check` | Perform QUERY or AUTHORIZE synchronously without UI. AUTHORIZE needs two stable execution IDs. |
| `consent_check_async` | Perform the same check asynchronously. Acceptance or detach in AUTHORIZE mode must not be interpreted as cancellation of execution. |
| `consent_async_detach` | Suppress a callback using its returned local async ID. Remote request/check work may continue. |
| `consent_get_request_result` | Look up stored state using the original request identity. PENDING is not a final decision. |
| `consent_cancel_request` | Attempt cancellation of a pending remote request; this does not revoke existing grants or undo a final decision. |

### Registration, revocation and approval UI

Declarations:
[consent_registration.h](../../src/consent/inc/consent_registration.h),
[consent_prompt.h](../../src/consent/inc/consent_prompt.h).

| Function | Purpose and use |
|---|---|
| `consent_register` | Register a complete definition with separate package-name and app-ID arguments. Offline handles only stage it. |
| `consent_update` | Replace an online definition with complete new content, not a partial patch. |
| `consent_unregister` | Remove registrations for every app in the named package; no app-ID argument. |
| `consent_revoke` | Let admin revoke approval for definition/subject/profile. |
| `consent_get_prompt` | Let the UI fetch stored request display context and the latest prompt_token. |
| `consent_respond` | Submit the UI decision bound to its displayed token. Also check the final decision after response-time reevaluation. |
| `consent_prompt_format` | Format a prompt title/body as plain UTF-8 text. Release output with `free()`. |

### Sessions and data cleanup

Declarations: [consent_session.h](../../src/consent/inc/consent_session.h),
[consent_data.h](../../src/consent/inc/consent_data.h).

| Function | Purpose and use |
|---|---|
| `consent_session_open` | Create a logical session. Retain returned session/generation/resume_token for the required lifetime. |
| `consent_session_heartbeat` | Renew the owned active session's lease without extending idle/maximum lifetime. |
| `consent_session_suspend` | Suspend a resumable session and increment its generation; close a CONNECTION_BOUND session. |
| `consent_session_resume` | Resume from the same owner process with current generation/token. Store the new generation/token. |
| `consent_session_close` | Block access and schedule holder cleanup. Success alone does not mean physical deletion is complete. |
| `consent_session_get_state` | Read state/current generation/cleanup count using subject/profile/session; neither renew the lease nor retrieve resume_token. |
| `consent_data_register` | Let a holder register retained-memory metadata bound to an AUTHORIZE receipt. |
| `consent_data_register_derived` | Register derived-data provenance and lifetime from parent artifacts. The holder performs the actual transformation. |
| `consent_data_release` | Acknowledge the holder's actual deletion result. This function does not delete application data. |
| `consent_cleanup_get_state` | Read recorded cleanup progress for a session. |
| `consent_cleanup_get_pending` | Fetch artifacts awaiting holder cleanup; delete/release them, then query again. |

### Results and errors

Declarations: [consent_result.h](../../src/consent/inc/consent_result.h),
[consent_common.h](../../src/consent/inc/consent_common.h).

| Function | Purpose and use |
|---|---|
| `consent_result_free` | Release an owned synchronous result or clone; NULL is accepted. Never use on a borrowed callback result. |
| `consent_result_clone` | Copy a result to retain after callback return. Handle clone failure too. |
| `consent_result_get_decision` | Read the decision enum; missing/unrecognized fields yield UNKNOWN. |
| `consent_result_get` | Borrow a string by key. Missing keys return NULL; existing empty values return an empty string. |
| `consent_result_size` | Read the number of result fields; NULL yields zero. |
| `consent_result_get_at` | Read a key/value by zero-based index. Do not assign meaning to field order; find required fields by key. |
| `consent_error_string` | Borrow a static status description; never free it. |

Registration/session/cleanup results may have no decision. Determine success
from function status and the operation's own result fields. UNKNOWN alone does
not establish management-operation failure, and status 0 alone never authorizes
protected access.


## Headers and ownership

[Read the task chapter: Start a C API client](api/01-start.en.md).

## Build executable examples

[Read the task chapter: Start a C API client](api/01-start.en.md).

## Build parameters and preserve retry identity

[Read the task chapter: Request approval and authorize
access](api/03-request-and-check.en.md).

## Synchronous QUERY and AUTHORIZE

[Read the task chapter: Request approval and authorize
access](api/03-request-and-check.en.md).

### Minimal C query program

[Request approval and authorize access](api/03-request-and-check.en.md).

### AUTHORIZE immediately before execution

[Request approval and authorize access](api/03-request-and-check.en.md).

## Asynchronous approval and dispatcher lifetime

[Read the task chapter: Handle results and asynchronous
callbacks](api/04-results-and-callbacks.en.md).

## Registration online and during image construction

[Read the task chapter: Register a definition](api/02-registration.en.md).

## Approval UI and localized templates

[Read the task chapter: Register a definition](api/02-registration.en.md).

## Grant modes, sessions and data lifetime

[Read the task chapter: Manage sessions and retained
data](api/05-sessions-and-data.en.md).

## Errors and safe reconciliation

[Read the task chapter: Handle results and asynchronous
callbacks](api/04-results-and-callbacks.en.md).

## Client GIO ownership and process signals

[Read the task chapter: Handle results and asynchronous
callbacks](api/04-results-and-callbacks.en.md).

## Feature selection requests

Use the opt-in fields and canonical digest in the [protocol
guide](../design/03-protocol.en.md#feature-selection-approval-version-1).
Only argo calls request, including settings selection. PREAPPROVAL checks the
chosen period; TASK reuses valid exact grants and asks only for missing ones.
Opt-in calls bypass cache and require daemon hello capability. A batch has one
common period and at most 16 logical requirements, but the combined 240-field /
64 KiB prompt and rendered-text limits can reject it earlier with E2BIG. Do not
silently split a selected batch or treat a partial result as approval. See the
[feature workflow](10-feature-approval.en.md) for the isolated settings flow.

## Sources and example validation scope

The 42 names in the function directory were compared with declarations in the
[public headers](../../src/consent/inc/). See
[wrapper.cc](../../src/consent/wrapper.cc)
for call/output copying, [params.cc](../../src/consent/params.cc) for input
setters,
[result.cc](../../src/consent/result.cc) for result ownership, and
[request.c](../../src/examples/request.c) for the complete asynchronous
workflow.

The minimal query program passed host C11 `-Wall -Wextra -Werror -fsyntax-only`
checks using real Tizen GBS SDK headers. The existing
[public_headers_test.py](../../tests/public_headers_test.py) also checked
standalone inclusion of 10 public headers and 42 declarations in C11/C++17.
These are syntax/declaration checks, not linking or emulator execution evidence.
See [Guide 07](07-verification.en.md) for execution evidence and product
integration limits.


### Cleanup pages and retry sweeps

[Manage sessions and retained data](api/05-sessions-and-data.en.md).

## API integration contract

Executable examples, feature headers and per-function ownership/error
contracts are collected in [Guide 02](02-c-api.en.md). Receipt-preserving
metadata compaction and its remaining capacity/recovery limits are described
in [Guide 09](09-storage-maintenance.en.md).

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
cleanup access only. See the [storage
guide](../design/04-storage-design.en.md) for total DB
loss and incomplete-holder reconciliation limits.

---

[Related task](api/01-start.en.md) · [Continue](13-tool-examples.en.md) ·
[Reading paths](../README.md)
