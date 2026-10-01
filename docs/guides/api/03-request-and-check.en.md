# API 03: Request approval and authorize access

[한국어](03-request-and-check.ko.md)

Follow one summary-tool operation through separate authenticated executables:
Installer registers, Argo requests, UI responds, and CM authorizes execution.
The same requirement values must reach every role. A process cannot obtain a
role by changing a parameter.

## Before you start

[Register the definition](02-registration.en.md) first. Enroll the four
executables with their own roles and delegated context. The values below belong
to the isolated Guide 13 fixture, not a provisioned product identity.

## 1. Prepare Argo's approval input

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "count": "1",
  "r0.definition": "smoke.cm.tool.summary",
  "r0.operation": "execute",
  "r0.scope": "cli:smoke-tool/summary",
  "r0.purpose": "developer-tool-smoke",
  "r0.recipient": "fixture-provider",
  "r0.policy_version": "1",
  "client_request_id": "demo-approval",
  "operation_id": "demo-approval",
  "deadline_ms": "20000"
}
```

| Field | Type | Required for request | Meaning |
| --- | --- | --- | --- |
| `subject`, `profile` | string | Yes | Explicit delegated context |
| `count` | decimal string | Yes | Number of AND conditions |
| `r0.definition` | string | Yes | Registered definition |
| `r0.operation`, `r0.scope` | string | Yes | Exact protected action and scope |
| `r0.purpose`, `r0.recipient` | string | Yes | Intended use and recipient |
| `r0.policy_version` | decimal string | Set here | Expected current policy |
| `client_request_id` | string | Yes | Argo's stable remote request identity |
| `operation_id` | string | Yes | Immutable request retry identity |
| `deadline_ms` | decimal string | No | Approval deadline; 20000 here |

This JSON illustrates string parameters; the C API does not parse it. Build
the shared requirement with this complete function:

```c
#include <consent.h>

/* Caller owns *output; fields are copied by every request/check call. */
int summary_requirement(consent_params_t **output) {
  consent_params_t *params = NULL;
  int status = consent_params_create(&params);
  if (status == 0)
    status = consent_params_set(params, "subject", "smoke.subject");
  if (status == 0)
    status = consent_params_set(params, "profile", "smoke.profile");
  if (status == 0)
    status = consent_params_add_requirement(params, "smoke.cm.tool.summary",
        "execute", "cli:smoke-tool/summary", "developer-tool-smoke",
        "fixture-provider");
  if (status == 0)
    status = consent_params_set(params, "r0.policy_version", "1");
  if (status != 0) {
    consent_params_free(params);
    params = NULL;
  }
  *output = params;
  return status;
}
```
## 2. Request as Argo

The following is a call fragment on an authenticated Argo handle. Its
synchronous
wait must run outside ownership of the callback context. An independent UI
process handles the pending prompt while Argo waits.

```c
consent_params_t *params = NULL;
consent_result_t *result = NULL;
int status = summary_requirement(&params);
if (status == 0)
  status = consent_params_set(params, "client_request_id", "demo-approval");
if (status == 0)
  status = consent_params_set(params, "operation_id", "demo-approval");
if (status == 0)
  status = consent_params_set_int64(params, "deadline_ms", 20000);
if (status == 0)
  status = consent_request(client, params, 25000, &result);
/* Only status 0 permits reading result; ALLOWED remains advisory. */
consent_result_free(result);
consent_params_free(params);
```

For nonblocking request-ID handoff, use `consent_request_async()` and the
[complete callback example](04-results-and-callbacks.en.md). Argo retrieves the
pending request ID using its original subject/profile/client_request_id and
hands that ID to the UI. Local async IDs are not UI request IDs.

## 3. Display and respond as UI

This walkthrough uses unversioned/legacy approval. For approval-v2, preserve
the displayed base grant_mode and original selection context, and return the
explicit chosen_grant_mode separately. Only negotiated eligible choices are
accepted. See [Guide 10](../10-feature-approval.en.md) and
[the current UI](../08-consent-ui-poc.en.md); do not add v2 fields to this legacy
request or silently change its period.

In the separate UI process, set request_id and locale, call
`consent_get_prompt()`, and display all returned conditions. Copy prompt_token
into response params before freeing the prompt. After an explicit user choice,
call `consent_respond()` with that token, decision and permitted grant_mode.
Check its status and final decision: a stale policy or token may invalidate an
intended approval. UI does not call Argo's result lookup API.

[Guide 13](../13-tool-examples.en.md#protocol-and-concrete-inputs) provides an
explicitly concurrent Argo/UI command recipe. Its automatic UI is test-only;
a product UI must receive the user's decision.

## 4. Authorize as CM immediately before execution

The published check.c and request.c executables are generic operation=read
examples. check.c performs AUTHORIZE and may consume ONCE, but causes no resource
effect. They are not complete executables for this summary's operation=execute.
Use [Guide 13](../13-tool-examples.en.md) and its fixed smoke Argo/tool-check
sources for that exact tuple. Approval for read cannot be reused for execute.

Use the same requirement with a distinct execution identity:

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "count": "1",
  "r0.definition": "smoke.cm.tool.summary",
  "r0.operation": "execute",
  "r0.scope": "cli:smoke-tool/summary",
  "r0.purpose": "developer-tool-smoke",
  "r0.recipient": "fixture-provider",
  "r0.policy_version": "1",
  "operation_id": "demo-authorized",
  "mode": "AUTHORIZE",
  "step_id": "tool-admission"
}
```
Set mode to AUTHORIZE and set both stable IDs before `consent_check()`.
Do not execute until status is 0, decision is ALLOWED and receipt exists.
The [complete check example](../../../src/examples/check.c) supplies every
field,
checks every call and frees the result; it deliberately accesses no resource.
Guide 13 adds the real synthetic effect behind this boundary.

## 5. Check the expected outcome

| Stage | Source-backed expected outcome | Protected effect |
| --- | --- | --- |
| Before approval | CONSENT_REQUIRED | None |
| QUERY after approval | ALLOWED advisory observation | None |
| AUTHORIZE after approval | Status 0, ALLOWED, receipt | May execute bound action |
| Identical AUTHORIZE retry | Existing valid receipt | Reconcile; do not repeat effect |
| New operation after ONCE consumption | CONSENT_REQUIRED | None |

These are contract examples, not new execution records. Free each owned result
and builder, then destroy the caller's client on its creating thread.

## Troubleshooting

Keep the same operation_id/step_id and identical tuple when reconciling an
uncertain AUTHORIZE. A changed tuple under the same IDs is CONFLICT. A request
result or cached approval is never a replacement for current AUTHORIZE.
Check does not open UI; only Argo requests it.

## Reference: field limits and retry rules

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
| Register/update | Separate package/app arguments; `definition`, `enforcer`, `operation_id`, `expected_generation`, `policy_version`, `text_revision`, `level`, `modes`, `default_locale`, `message.<locale>.title/body`. Optional `retention_ms` and typed schema in API 02. |
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
have operation-ID deduplication and must not be blindly retried after
uncertainty.



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
use advisory QUERY to decide whether to reconcile AUTHORIZE. Checks never open
UI;
argo must separately request approval when required.

## Parameter fields

The public ABI uses opaque `consent_params_t` builders and string fields.
`consent_params_set()` copies UTF-8 input; integer setters encode decimal
values.
Protocol identity fields and underscore-prefixed internal fields are reserved.

| Operation | Required fields and interpretation |
|---|---|
| register/update | `operation_id`, `expected_generation`, `definition`, `enforcer`, positive `policy_version` and `text_revision`, `level` 0–3, comma-separated `modes`, `default_locale`, `message.<locale>.title` and `.body`; optional typed template schema and locale aliases in API 02; package/app are separate API arguments |
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
results (A `CONSENT_REQUIRED`, B `ALLOWED`). The explicit, still-valid B grant
is
retained and remains unused. This does not authorize the combined operation.
The old request is terminal and cannot reopen its prompt automatically; an
authenticated requester must initiate a new request with fresh request/operation
IDs if further approval is needed. Actual protected work still requires
`AUTHORIZE` against all conditions.

For example, an authenticated enforcement service can make an advisory check
with the following C API sequence. Every nonzero status blocks execution.

Use the complete requirement builder above and API 01 query program.
Always include the current policy version; inspect the decision after status 0.

---

[Previous](02-registration.en.md) · [Next](04-results-and-callbacks.en.md) ·
[API overview](../02-c-api.en.md)
