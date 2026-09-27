# Guide 02: Public C API

The `consent` library exposes 42 C functions. Include `<consent.h>` or a feature
header and link with `pkg-config consent`. The headers document parameters,
ownership, callback rules and errors in the style of Tizen Device and Application
Manager APIs. `@since 0.1.0` denotes this framework's version; it does not claim a
Tizen platform API version or an assigned privilege URI.

Read [Guide 01](01-development.en.md) for building and deployment,
[Guide 03](03-installation-authority.en.md) for trusted installation generations,
and [Guide 04](04-offline-registration.en.md) for image construction. Verification
evidence and its limits are recorded in [Guide 07](07-verification.en.md).

## Reading order and key terms

Start with the terms and flow below, then select your role's API from the
[function directory](#function-directory). Use the [minimal C query program](#minimal-c-query-program)
to learn input/result handling before reading authoritative checks, asynchronous
approval, registration, and session/data lifetime. This guide describes the current
[public headers](../../src/consent/inc/consent.h) and implementation, distinct from
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
`check(AUTHORIZE)` permits actual access and consumes ONCE. Do not substitute one
result for another. Sufficient existing grants allow AUTHORIZE without a fresh UI
request. Reuse of already acquired data follows the `operation=reuse-data`
contract below.

## Prerequisites and authorization

Production clients connect to the fixed systemd endpoint `/run/.consentd.sock`.
The shipped role policy is **default-deny**. Socket group access and UID 0 alone
do not grant an online API role. Integrators must provision the actual executable,
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
development environment. The isolated test daemon in Guide 01 uses a compile-time
test inventory. The [.NET UI and participant PoC in Guide 08](08-consent-ui-poc.en.md)
has a separate compile-time endpoint and roles, but validates actual pkgmgr
package/app identity. Guide 07 records the exercised scenarios. A standalone example linked to the production library
normally receives permission denial until its actual identity is provisioned.
The approval UI and Installer product hooks remain integration responsibilities;
these examples do not implement them.

## Headers and ownership

| Header | Functions or declarations |
|---|---|
| `consent_common.h` | Handles, enums, callback type, `consent_error_string` |
| `consent_client.h` | Online constructors, offline constructor, destroy |
| `consent_params.h` | Create/free/set/set_int64/set_check_mode/add_requirement |
| `consent_request.h` | Request/check SYNC and ASYNC, detach, result lookup, cancel |
| `consent_registration.h` | Register/update/unregister/revoke |
| `consent_prompt.h` | Get prompt, respond, plain-text formatter |
| `consent_session.h` | Open/heartbeat/suspend/resume/close/get_state |
| `consent_data.h` | Register/derive/release data, cleanup state/list |
| `consent_result.h` | Free/clone/decision/get/size/get_at |
| `consent.h` | Umbrella over all feature headers |

Each feature header works alone in C and C++. Private C++ headers are not
installed. With installed development packages:

```sh
cc -std=c11 consumer.c -o consumer $(pkg-config --cflags --libs consent)
cc -std=c11 async-consumer.c -o async-consumer \
  $(pkg-config --cflags --libs consent glib-2.0)
```

| Object | Ownership and lifetime |
|---|---|
| `consent_client_h` | One owner; destroy on the creating thread. Never race destroy with a call using the raw handle. |
| `consent_params_t` | Caller-owned, single-threaded builder. Every operation copies fields before return; free or reuse afterward. |
| Synchronous result | Caller-owned on success; output is NULL on error. Free with `consent_result_free`. |
| Callback result | Borrowed only until callback return; NULL for a nonzero callback status. Clone to retain; never free the borrowed result. |
| Result field pointers | Borrowed until the owning result dies; an existing empty field is different from a missing field. |
| Formatted prompt | Caller-owned UTF-8 allocation; release with standard `free()`. |
| Error string | Static borrowed string; never free. |

Initialize outputs to NULL or zero. `consent_result_get_at()` leaves its output
pointers unchanged on invalid input. A NULL result has size zero and decision
UNKNOWN. Check the operation status before interpreting a result. Destroyed or
freed objects must never be reused.

## Function directory

These 42 functions form the public C API. Names match the declarations; consult
the linked headers for detailed arguments and errors. `consent_feature_*` belongs
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

Declarations: [consent_registration.h](../../src/consent/inc/consent_registration.h),
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

## Build executable examples

The sources under [`src/examples`](../../src/examples/) provide four executables:

| Source / executable | Purpose |
|---|---|
| [`check.c`](../../src/examples/check.c) / `consent-example-check` | Advisory QUERY followed by authoritative AUTHORIZE with stable execution IDs |
| [`request.c`](../../src/examples/request.c) / `consent-example-request` | ASYNC approval, explicit GLib dispatcher, result clone, watchdog, detach/cancel and shutdown |
| [`register.c`](../../src/examples/register.c) / `consent-example-register` | Complete online definition with separate package/app arguments |
| [`offline-register.c`](../../src/examples/offline-register.c) / `consent-example-offline-register` | Explicit root-only image staging through the same register API |

The component CMake builds them against `consent`; only the request example adds
GLib. Its install destination is `${libexecdir}/consent/examples`; source files
are installed under `${docdir}/src/examples`. Guides are installed under
`${docdir}/docs/guides`, preserving the relative source links used here. The
actual prefix, docdir and libexecdir follow the package's CMake configuration. Installed
sources can also be compiled directly; for example, from that directory:

```sh
cc -std=c11 -Wall -Wextra check.c example_common.c -o consent-example-check \
  $(pkg-config --cflags --libs consent)
cc -std=c11 -Wall -Wextra request.c example_common.c -o consent-example-request \
  $(pkg-config --cflags --libs consent glib-2.0)
```

The registration sources use the same first command with their filename.
No-argument invocation prints usage without connecting or writing state. Exit
codes are 0 for the example's successful outcome, 1 for API/setup failure,
2 for invalid argument count, and 3 for a non-ALLOWED request/check decision.
Request exit 0 remains advisory. The check example actually performs AUTHORIZE
and can consume ONCE, but deliberately performs no protected resource action.
Compilation alone is not evidence of an authorized target run.

## Build parameters and preserve retry identity

All field values are copied NUL-terminated UTF-8 strings. Generic values are
limited to 8,192 bytes; keys to 128 ASCII letters/digits/underscore/dot/hyphen.
`v`, `id`, `method`, `status`, `role`, `pid`, `uid`, `gid`, empty keys and
underscore-prefixed keys are reserved. The builder allows 253 distinct fields;
specific operations and the 64 KiB transport body impose tighter bounds.
Do not use arbitrary display arguments or a caller-supplied identity claim.

| Operation | Required fields and useful options |
|---|---|
| Request | `subject`, `profile`, `client_request_id`, `operation_id`, `count`, requirements. Optional `session` requires current `generation`; `deadline_ms` is 100–300000, default 60000. |
| Check QUERY | `subject`, `profile`, requirements; `mode=QUERY` (default). Optional session/generation must be current. |
| Check AUTHORIZE | QUERY fields with `mode=AUTHORIZE`, plus stable `operation_id` and `step_id`. |
| Requirement N | `rN.definition`, `rN.operation`, `rN.scope`, `rN.purpose`, `rN.recipient`; typed definitions require `rN.policy_version`. Add `rN.holder` for a receipt intended to retain data. |
| Register/update | Separate package/app arguments; `definition`, `enforcer`, `operation_id`, `expected_generation`, `policy_version`, `text_revision`, `level`, `modes`, `default_locale`, `message.<locale>.title/body`. Optional `retention_ms` and typed schema below. |
| Unregister | Package argument; `operation_id`, `expected_generation`. All apps in that package are affected; no app ID required. |
| Request lookup/cancel | `request_id`, or the original `subject`, `profile`, `client_request_id`. Request ownership is still checked. |
| Revoke | `definition`, `subject`, `profile`. |

Use `consent_params_add_requirement()` to append all five basic fields and
increment `count` atomically. At most 16 distinct conditions are ANDed.
`definition`, `operation` and `purpose` must be valid nonempty identifiers.
Scope compares exactly and is at most 4,096 bytes; recipient and scope can be
explicitly empty. A convenience setter's success only validates its own field;
the complete schema and identity are checked by the daemon.

| Identifier | Meaning and retry rule |
|---|---|
| `consent_async_id_t` | Handle-local callback registration; use only with detach. Never send it as a remote ID. |
| `client_request_id` | Caller-selected stable request identity scoped by authenticated owner and subject/profile. Reuse unchanged after uncertain submission. |
| `request_id` | Daemon-issued stored request ID. A cache result may omit it. |
| `operation_id` + `step_id` | One actual AUTHORIZE execution. Same enforcer/IDs/content returns its still-valid receipt without consuming ONCE twice. |
| Registration `operation_id` | One complete register/remove operation; online receipts are caller-scoped. Offline IDs must be unique across the image spool. Reuse only with identical content. |
| `expected_generation` | Installer-issued protected installation generation; never substitute package version, installation timestamp or a locally invented UUID. |

Persist these IDs with the product operation before submission. Never create
fresh IDs simply because a response was lost. The library deduplicates consent
consumption, not the application's external side effect; the enforcer must also
deduplicate execution. Session open and derived-data creation do not currently
have operation-ID deduplication and must not be blindly retried after uncertainty.

## Synchronous QUERY and AUTHORIZE

### Minimal C query program

Save the following as `query.c`; it builds without helper files. Its seven
arguments must identify a delegated subject/profile and an existing definition's
policy/scope. It fixes the action to `read` and performs only a sessionless QUERY.
For SESSION grants, also supply session/generation as in the executable example
below. Exit code 0 means **advisory ALLOWED** only; this program neither obtains
an authorization receipt nor accesses protected resources.

```c
#include <consent.h>
#include <stdio.h>

int main(int argc, char** argv) {
  consent_client_h client = NULL;
  consent_params_t* params = NULL;
  consent_result_t* result = NULL;
  int exit_status = 1;
  int status;

  if (argc != 8) {
    fprintf(stderr, "Usage: %s SUBJECT PROFILE DEFINITION POLICY_VERSION "
        "SCOPE PURPOSE RECIPIENT\n", argv[0]);
    return 2;
  }
  status = consent_params_create(&params);
  if (!status)
    status = consent_params_set(params, "subject", argv[1]);
  if (!status)
    status = consent_params_set(params, "profile", argv[2]);
  if (!status)
    status = consent_params_add_requirement(params, argv[3], "read",
        argv[5], argv[6], argv[7]);
  if (!status)
    status = consent_params_set(params, "r0.policy_version", argv[4]);
  if (!status)
    status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
  if (!status)
    status = consent_client_create(&client);
  if (!status)
    status = consent_check(client, params, 5000, &result);
  if (!status) {
    consent_decision_e decision = consent_result_get_decision(result);
    const char* name = consent_result_get(result, "decision");
    printf("QUERY: %s (advisory only)\n", name ? name : "UNKNOWN");
    exit_status = decision == CONSENT_DECISION_ALLOWED ? 0 : 3;
  } else {
    fprintf(stderr, "query: %s (%d)\n", consent_error_string(status), status);
  }
  consent_result_free(result);
  consent_params_free(params);
  if (client) {
    status = consent_client_destroy(client);
    if (status) {
      fprintf(stderr, "destroy: %s (%d)\n", consent_error_string(status), status);
      exit_status = 1;
    }
  }
  return exit_status;
}
```

```sh
cc -std=c11 -Wall -Wextra query.c -o consent-query \
  $(pkg-config --cflags --libs consent)
./consent-query SUBJECT PROFILE DEFINITION POLICY_VERSION SCOPE PURPOSE RECIPIENT
```

Replace placeholders with actual values; use `""` for an empty recipient. Exit
code 1 means API failure, 2 means wrong argument count, and 3 means a decision
other than ALLOWED. When QUERY reports CONSENT_REQUIRED, delegate approval to
argo; use AUTHORIZE below immediately before actual access.

### AUTHORIZE immediately before execution

The check example takes actual configured identities and context:

```sh
consent-example-check SUBJECT PROFILE DEFINITION POLICY_VERSION \
  SCOPE PURPOSE RECIPIENT OPERATION_ID STEP_ID [SESSION GENERATION]
```

The uppercase words are argument placeholders. Pass `""` for an explicitly empty
recipient. The example always uses protected operation `read`; both it and the
preceding approval must describe the exact same scope/purpose/recipient and
optional session. Its core sequence is:

```c
status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
if (status == 0)
  status = consent_check(client, params, 5000, &result);
/* QUERY is informational even if ALLOWED. Free its owned result. */
consent_result_free(result);
result = NULL;
if (status == 0)
  status = consent_params_set_check_mode(params, CONSENT_CHECK_AUTHORIZE);
/* Set stable operation_id and step_id before the following call. */
if (status == 0)
  status = consent_check(client, params, 5000, &result);
if (status == 0 &&
    consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED &&
    consent_result_get(result, "receipt") != NULL) {
  /* The real enforcer may execute this bound, deduplicated action. */
}
consent_result_free(result);
```

The complete source checks every setter and supplies both IDs. A retry after
ONCE consumption can have QUERY=CONSENT_REQUIRED while AUTHORIZE returns the
existing valid receipt for that same execution. The example therefore does not
use advisory QUERY to decide whether to reconcile AUTHORIZE. Checks never open UI;
argo must separately request approval when required.

## Asynchronous approval and dispatcher lifetime

```sh
consent-example-request SUBJECT PROFILE DEFINITION POLICY_VERSION \
  SCOPE PURPOSE RECIPIENT CLIENT_REQUEST_ID OPERATION_ID [SESSION GENERATION]
```

The example creates an explicit `GMainContext`, creates the client with that
context, submits `consent_request_async()`, frees the copied input builder, and
only then runs `GMainLoop` on the creating thread. It sets a 60-second remote
approval deadline and a 65-second application watchdog. The library's own ASYNC
local timeout is 300 seconds. A separate authenticated UI must service the prompt.

`0` from submission means local acceptance. The callback is queued after API
return, including immediate/cache results, at most once per live registration,
without library locks. Callback status can still be an error; a successful
decision can be DENIED, EXPIRED, CANCELLED or INVALIDATED. Request handling polls
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
require a new handle. Disconnect requires a new online handle, using the original
remote retry IDs when reconciling existing work.

## Registration online and during image construction

```sh
consent-example-register PACKAGE APP DEFINITION ENFORCER GENERATION \
  OPERATION_ID POLICY_VERSION TEXT_REVISION
consent-example-offline-register IMAGE_ROOT PACKAGE APP DEFINITION ENFORCER \
  GENERATION OPERATION_ID POLICY_VERSION TEXT_REVISION
```

Both build a complete bilingual typed definition: level 1,
`ONCE,SESSION,TIMED,PERSISTENT`, retention 60000 ms, default locale `en`, and English
and Korean title/body pairs. `template_version=1` binds string variables `scope`,
`purpose` and `recipient` to those exact requirement fields, with UTF-8 byte
limits 512, 256 and 512 respectively. Both localized bodies contain all three
placeholders, so the formatted prompt shows the actual requested values. The
UI must additionally show the available grant-mode choice and its lifetime;
the wording does not assume ONCE or PERSISTENT before that choice. The Guide 08
PoC offers Allow once only; other grant choices require product UI integration.
They pass package and app separately to
`consent_register()`. The examples do not infer the package or enforcer, generate
installation authority, or grant consent. Replace the example text and allowed
modes with the product policy before real use.

Policy/text revisions are positive integers through INT32_MAX. Change policy
version for semantic changes, and increase text revision when messages, default
locale or fallback aliases change. The default translation must have a nonempty
title/body; at most 32 message fields, each at most 4,096 UTF-8 bytes, are allowed.
Level 3 allows ONCE only. Update supplies the complete definition, not a patch.
Unregister is package-wide. Read the public headers for their errors and ownership.

Online success means the daemon committed the registration. Offline creation
requires real/effective root, a protected absolute image path, an exclusive
lifecycle lock and stable Installer-provisioned generation. It never connects or
implicitly falls back after online failure. Offline success is **STAGED** only:
the protected record is durable, but no active registration, DB or user approval
is created. Startup import validates actual installed package/app membership and
active generation. All other handle operations except register/destroy return
INVALID_OPERATION. Guide 04 covers path restrictions, authority provisioning,
bounded records and retry after interrupted publication.

## Approval UI and localized templates

The UI calls `consent_get_prompt()` with request_id, requested locale and
`template_version=1` when supporting typed prompts. It receives a rotating
prompt_token and each requirement's actual scope/purpose/recipient, definition
revisions, sensitivity, allowed modes and selected translation. Display all of
that context. Use `consent_prompt_format(result, index, "title"/"body", &text)`
and release successful output with `free()`. Render plain text; never interpret
it as markup, a printf format or a second template.

Typed registration binds named values to authorization fields:

| Field | Contract |
|---|---|
| `template_version` | Exactly `1` |
| `parameter.<name>.type` | `integer` or `string` |
| `.source` | Integer: `scope` or definition `retention_ms`; string: `scope`, `purpose`, `recipient` or `operation` |
| `.min`, `.max` | Required inclusive signed-64-bit limits for integer |
| `.max_bytes` | Required 1–512 UTF-8 byte limit for string |
| `locale_fallback.<requested>` | Direct complete registered translation; no chain/cycle or shadow of a complete translation |

At most eight names of 1–32 ASCII identifier characters are allowed. Every typed
translation has title/body and their combined `{name}` placeholders exactly
match the declared variables. Values come from validated `rN` fields and current
policy, not independent display strings. Include `rN.policy_version`; a missing
or stale version fails. Integer scope is canonical decimal (`30`, not `030` or
`+30`). A missing string source fails; explicitly empty input can be valid.
`retention_ms` defaults to zero and must meet the declared integer bounds.
Each rendered field is bounded to 8,192 bytes. ICU plural/date/localized numeric
formatting is not implemented.

Locale selection tries a complete exact translation, an explicit direct alias,
the built-in `ko-KR` to `ko` or `en-US`/`en-GB` to `en` fallback, then the
registered default. Other locale mappings require explicit aliases. Reply with the most recent
prompt_token, decision=ALLOWED/DENIED, and the chosen allowed grant_mode; typed
responses also echo the requested locale. The same UI process instance must
respond. The daemon re-evaluates the full AND without consuming new ONCE grants.
If a previously satisfied condition changed during UI wait, an ALLOWED response
can produce INVALIDATED. A still-valid grant explicitly approved for another
condition remains unused. Retrieve the resulting decision and use AUTHORIZE
before execution; the historical request result is not current authorization.

## Grant modes, sessions and data lifetime

| Mode | Access grant lifetime |
|---|---|
| ONCE | Consumed atomically by the first successful AUTHORIZE execution, not by QUERY or UI completion |
| SESSION | Bound to the logical session; suspended/closed/expired or stale-generation use is rejected |
| TIMED | UI duration_ms 100–3600000, default 300000; absolute expiry is not extended by checks |
| PERSISTENT | Durable until invalidation/revocation; does not imply unlimited data retention |

Eligible PERSISTENT/SESSION request results can have an advisory per-handle cache
lease of at most 500 ms, conservatively anchored to local admission and session
deadlines. Policy/revocation/generation changes and lost synchronization invalidate
it. ONCE, TIMED and level 3 are not cacheable. Every protected access still uses
authoritative check; `source=CACHE` is never an execution permit.

Session open requires subject/profile. Lifecycle defaults to CONNECTION_BOUND;
RESUMABLE_CONVERSATION permits suspend/resume within its bounds. Defaults/ranges:

| Field | Default ms | Inclusive range ms |
|---|---:|---:|
| `idle_timeout_ms` | 600000 | 100–86400000 |
| `max_lifetime_ms` | 3600000 | 100–86400000 |
| `lease_ms` | 30000 | 100–60000 |
| `reconnect_grace_ms` | 30000 | 100–300000 |

Keep returned session/generation/resume_token. Transitions require subject/profile,
session and current generation; resume also needs the token and the same owner
process instance. Suspend of CONNECTION_BOUND closes it; resumable suspend
increments generation and blocks use. Resume rotates generation/token and starts
a 30000 ms lease without extending the original idle/maximum deadlines. The
session owner can call `consent_session_heartbeat` with subject/profile/session/generation
before lease expiry. It sets a 30,000 ms lease without extending idle or absolute
lifetime. The synchronous wait is bounded to 5,000 ms, the caller owns inputs,
and the result is freed with `consent_result_free`. State queries and ordinary
checks do not renew the lease. Wrong owner, stale generation, inactive/closed
session or an offline handle is rejected. Daemon restart does not reactivate old sessions.
Returned deadline/expiry values use daemon monotonic milliseconds, not UTC dates.

| Data operation | Required schema and behavior |
|---|---|
| `consent_data_register` | `receipt`, subject/profile, active session/generation, exact scope/purpose/recipient; optional `requirement` index, default 0. AUTHORIZE must have bound rN.holder to this holder. Only MEMORY_ONLY is accepted. Definition retention_ms must be positive. |
| `consent_data_register_derived` | Subject/profile, active session/generation, scope/purpose/recipient, count 1–16 and distinct parent0…parentN. Same holder instance/context; earliest parent expiry and combined provenance are inherited. |
| `consent_check` data reuse | Set `operation=reuse-data`, artifact, subject/profile, active session/generation and exact scope/purpose/recipient. This validates current retained-data provenance without acquiring data again. |
| `consent_data_release` | `artifact`, explicit `success=1` only after deletion, or `success=0` after failure. Previous-holder-instance cleanup also needs `reconcile=1` and matching subject/profile. |
| `consent_cleanup_get_pending` | Subject/profile and optional `reconcile=1`; returns up to 48 aN.artifact/session/state/error entries. Acknowledge deletion, then query again. |
| Session/cleanup state | Subject/profile/session; state and cleanup_pending are observations, not lease renewal or independent proof of physical deletion. |

Original data expires at acquisition time plus definition retention_ms; retries
cannot extend it. Access grant lifetime and already acquired data retention are
separate: consuming ONCE or expiry of a TIMED access grant does not by itself
erase an independently valid data-use permit. Revocation, session closure,
policy/installation invalidation and data expiry still block use and require
cleanup. CLOSING becomes CLOSED only after recorded holder deletion ACKs.
Consent stores control metadata, never conversation bodies. Product holders
must enforce real memory/storage deletion and remote-recipient obligations.

## Errors and safe reconciliation

Use `consent_error_e` and `consent_error_string()`; standard values alias `tizen.h`.
PROTOCOL, OUTCOME_UNKNOWN, SESSION_INACTIVE, SESSION_CLOSED, CONFLICT and STORAGE
are the six module-local values beginning at `TIZEN_ERROR_MIN_MODULE_ERROR`.
They are not a separately assigned platform module range. All consumers, daemon
and library must use the same error ABI; old unpublished `-200x` values are obsolete.

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
`app-manager/include/app_context.h` and `app-manager/doc/appfw_app_manager_doc.h`.
The contracts above come from this repository's client, wrappers and daemon;
external platform version/privilege annotations were deliberately not copied.

## Feature selection requests

Use the opt-in fields and canonical digest in the [protocol guide](../design/03-protocol.en.md#feature-selection-approval-version-1).
Only argo calls request, including settings selection. PREAPPROVAL checks the
chosen period; TASK reuses valid exact grants and asks only for missing ones.
Opt-in calls bypass cache and require daemon hello capability. A batch has one
common period and at most 16 logical requirements, but the combined 240-field /
64 KiB prompt and rendered-text limits can reject it earlier with E2BIG. Do not
silently split a selected batch or treat a partial result as approval. See the
[feature workflow](10-feature-approval.en.md) for the isolated settings flow.

## Sources and example validation scope

The 42 names in the function directory were compared with declarations in the
[public headers](../../src/consent/inc/). See [wrapper.cc](../../src/consent/wrapper.cc)
for call/output copying, [params.cc](../../src/consent/params.cc) for input setters,
[result.cc](../../src/consent/result.cc) for result ownership, and
[request.c](../../src/examples/request.c) for the complete asynchronous workflow.

The minimal query program passed host C11 `-Wall -Wextra -Werror -fsyntax-only`
checks using real Tizen GBS SDK headers. The existing
[public_headers_test.py](../../src/tests/public_headers_test.py) also checked
standalone inclusion of 10 public headers and 42 declarations in C11/C++17.
These are syntax/declaration checks, not linking or emulator execution evidence.
No GBS rebuild, RPM installation or device behavior tests were performed for
this documentation expansion. See [Guide 07](07-verification.en.md) for existing
execution evidence and product integration limits.
