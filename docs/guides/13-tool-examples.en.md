# Guide 13: Execute a tool and read context with approval

<a id="guide-13-tool-execution-and-context-lookup-examples"></a>

[한국어](13-tool-examples.ko.md)

This guide has two complete examples. CM executes a synthetic JSON-RPC summary
tool. CE reads a synthetic context record. Both use separate authenticated
Installer, Argo, UI and checker executables, and both require current consent
before an effect. No personal context service is called.

## Before you start

Use the [Guide 12 build/deployment
setup](12-developer-smoke.en.md#1-build-run-and-clean-up).
Enable CONSENT_BUILD_SMOKE and CONSENT_BUILD_SMOKE_TOOLS. JSON-GLib is a
test-only
dependency. Install matching runtime/devel/tests RPMs; do not replace the
production daemon for this fixture. The quickest complete test is:

```sh
sdb -s DEVICE shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --tools --seed 20261002'
```

Record SMOKE_EXIT and the remote exit separately from the host SDB status.
A successful developer run does not establish product CM/CE integration.
After inspecting the output, run the same installed runner with `--cleanup`.

The actor commands below illustrate stages of an active fresh fixture. The
runner owns its metadata, generation, state and role enrollment. A completed
runner leaves stopped registry-loss state; these are not post-run commands.
The snippets must run in separately enrolled root/System actor contexts.

## Tool execution walkthrough

### 1. Register the approval definition

The protected mapping identifies the gate and keeps the body out of consentd:

```json
{
  "smoke.cm.tool.summary": {
    "definition": "smoke.cm.tool.summary",
    "enforcer": "cm",
    "level": 1,
    "record": "summary"
  },
  "smoke.ce.tool.level1": {
    "definition": "smoke.ce.tool.level1",
    "enforcer": "ce",
    "level": 1,
    "record": "level1"
  }
}
```
Package `smoke.package` and app `smoke.app` are separate API arguments. The
following strings are the full registration params for the summary definition.
They illustrate `consent_params_set()`; no public JSON loader exists.

<a id="installer-registration-inputs"></a>

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "operation_id": "register-summary",
  "expected_generation": "<trusted Installer generation>",
  "definition": "smoke.cm.tool.summary",
  "enforcer": "cm",
  "policy_version": "1",
  "text_revision": "1",
  "level": "1",
  "modes": "ONCE,PERSISTENT",
  "retention_ms": "60000",
  "default_locale": "en",
  "message.en.title": "Developer smoke approval",
  "message.en.body": "Allow this isolated fixture tool operation?"
}
```

| Field | Type in params | Required here | Meaning |
| --- | --- | --- | --- |
| `subject`, `profile` | string | Yes | Delegated approval context |
| `operation_id` | string | Yes | Stable registration retry ID |
| `expected_generation` | string | Yes | Committed Installer generation |
| `definition`, `enforcer` | string | Yes | Policy name and delegated checker |
| `policy_version` | decimal string | Yes | Positive semantic policy version |
| `text_revision` | decimal string | Yes | Positive message revision |
| `level` | decimal string | Yes | Trusted sensitivity, 0–3 |
| `modes` | comma-separated string | Yes | Permitted access grant lifetimes |
| `retention_ms` | decimal string | Set here | Retained-data limit, not grant time |
| `default_locale` | string | Yes | Complete fallback translation |
| `message.en.title`, `message.en.body` | string | Yes | Displayed approval text |

```c
#include <consent.h>
#include <stddef.h>

/* client belongs to the authenticated Installer; generation is provisioned. */
int register_summary(consent_client_h client, const char *generation) {
  const struct { const char *key; const char *value; } fields[] = {
    {"subject", "smoke.subject"},
    {"profile", "smoke.profile"},
    {"operation_id", "register-summary"},
    {"expected_generation", generation},
    {"definition", "smoke.cm.tool.summary"},
    {"enforcer", "cm"},
    {"policy_version", "1"},
    {"text_revision", "1"},
    {"level", "1"},
    {"modes", "ONCE,PERSISTENT"},
    {"retention_ms", "60000"},
    {"default_locale", "en"},
    {"message.en.title", "Developer smoke approval"},
    {"message.en.body", "Allow this isolated fixture tool operation?"}
  };
  consent_params_t *params = NULL;
  int status = consent_params_create(&params);
  for (size_t i = 0; status == 0 && i < sizeof(fields) / sizeof(fields[0]); ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_register(client, "smoke.package", "smoke.app", params);
  consent_params_free(params);
  return status;
}
```
The helper requires a real enrolled Installer client and its committed
generation.
Registration status 0 creates the definition, not user approval. The complete
create/register/destroy example is [register.c](../../src/examples/register.c).

### 2. Request approval as Argo

<a id="requester-and-enforcer-inputs"></a>

Argo builds this exact input. The checker will use the same requirement tuple:

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

See the [complete requirement builder and request
call](api/03-request-and-check.en.md).
For this active fixture, start Argo in terminal A and copy its REQUEST_ID while
it waits:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-argo smoke.cm.tool.summary demo-approval
```
### 3. Fetch and respond as the separate UI

Concurrently in terminal B, pass that request ID to the test UI. It calls
get_prompt, copies the displayed prompt_token, then responds. This explicit
automatic response is a smoke convenience, not a product user's decision.

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-ui --auto-approve-smoke REQUEST_ID ONCE
```
Wait for Argo's callback. Submission return 0 is acceptance only; callback
status 0 makes its decision readable. ALLOWED still does not execute the tool.
[API 04](api/04-results-and-callbacks.en.md) explains borrowed callback results.

### 4. Authorize and execute as CM

CM constructs the following AUTHORIZE input. Only status 0, ALLOWED and receipt
permit entry to the provider; the admission ledger still prevents a duplicate
effect. QUERY never executes and check never opens UI.

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
```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'authorize demo-authorized ALLOWED
' |
  /usr/libexec/consent/smoke/consent-smoke-tool-cm smoke.cm.tool.summary
```
The command uses the actual [tool-check.c](../../tests/smoke/tool-check.c).
Source-backed output excerpts for a fresh actor are:

```text
# Before approval: no provider admission
PROTECTED_TOOL blocked/advisory count=0
# After the first approved successful execution
PROTECTED_TOOL state=succeeded count=1
```

These excerpts illustrate the branch output, not a newly executed run. A
long-lived
actor's exact same retry prints `deduplicated state=succeeded count=1`. A new
process cannot claim the old ledger. Keep the actor alive if testing
deduplication.

## Context data lookup walkthrough

### 1. Register the CE definition

Use the same registration shape above with these explicit changes. The runner
owns the record-to-level mapping; the requester never supplies a level or path.

| Input | CM summary | CE level1 |
| --- | --- | --- |
| Registration `definition` | smoke.cm.tool.summary | smoke.ce.tool.level1 |
| Registration `enforcer` | cm | ce |
| Registration `operation_id` | register-summary | A distinct CE registration ID |
| Requirement `r0.definition` | smoke.cm.tool.summary | smoke.ce.tool.level1 |
| Requirement `r0.operation` | execute | read |
| Requirement `r0.scope` | cli:smoke-tool/summary | context.fixture/level1 |

Level, modes, messages and retention remain the shown values. For level3,
registration must instead use level `3` and modes `ONCE`. Level0 also requires
consent. These are fixture levels, not product CE taxonomy.

### 2. Request and display the same CE tuple

Repeat the Argo/UI steps above with `smoke.ce.tool.level1` and distinct request
IDs. Purpose is `developer-tool-smoke`, recipient is `fixture-provider`, and
policy_version is `1`. The separate authenticated UI receives only Argo's ID;
CE cannot initiate approval. No `approval_version` or `grant_mode` is present
in these unversioned requester/checker inputs.

### 3. Authorize before reading the body

After the CE approval, use a distinct protected execution ID:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'authorize context-demo-authorized ALLOWED
' |
  /usr/libexec/consent/smoke/consent-smoke-tool-ce smoke.ce.tool.level1
```
Before approval the read count is 0; after a successful authorized lookup it is
1. The body comes from `tool-context/level1.txt`: `synthetic context summary
level1` followed by a newline. Consent stores the definition and approval
metadata, never that body.

Retention `60000` bounds receipt/session-bound MEMORY_ONLY artifacts when an
authenticated holder calls consent_data_register(). It does not delete this
fixture file. This flow demonstrates access gating; use
[API 05](api/05-sessions-and-data.en.md) for actual retained-data lifecycle.

## Troubleshooting

- CONSENT_REQUIRED: request through Argo; do not invoke the provider directly.
- API_ERROR, STALE or unknown prior execution: do not execute or deliver old data.
- Native provider error: one admission already occurred; do not retry the effect
  under the same receipt. See the execution-state rules below.
- Changed scope, purpose, recipient or policy: request the changed tuple explicitly.

## Descriptor, tools and consent tuples

Installed `tool-package/cli.json` uses the actual CM descriptor schema:

```json
{
  "version": 1,
  "key": "smoke-tool",
  "name": "Fixture summary tool",
  "desc": "Synthetic JSON-RPC provider; not a product adapter",
  "executable": "bin/consent-smoke-tool",
  "inputSchema": {
    "type": "object",
    "properties": {
      "record": {
        "type": "string",
        "enum": [
          "summary",
          "fail",
          "timeout",
          "malformed",
          "stderr",
          "nonzero",
          "conflict",
          "nul"
        ]
      }
    },
    "required": [
      "record"
    ],
    "additionalProperties": false
  },
  "outputSchema": {
    "type": "object",
    "properties": {
      "summary": {
        "type": "string",
        "maxLength": 1024
      }
    },
    "required": [
      "summary"
    ],
    "additionalProperties": false
  }
}
```

The manifest uses `http://tizen.org/metadata/capability/cli`, owner
`smoke.package`, and the installed tool-package as root. The real
`capmgr-package-tool --offline ABS_DB stage MANIFEST` and
`finalize tool-install success` publish canonical `cli:smoke-tool`. Replaying
finalize leaves revision1. CM's C example reads the actual catalog owner and
executable, requires the fixed installed path, checks protected root-owned
ancestors/inode, and pins the executable FD. It uses `posix_spawn` through that
FD without a shell. Parser publication does not import consent metadata.

The separate Installer registers explicit package/app/generation and
definitions.
Argo and both check actors use the same `smoke_requirement()` mapping:

| Tool record | Definition | Enforcer/level | Requirement operation and scope |
| --- | --- | --- | --- |
| CM summary | `smoke.cm.tool.summary` | cm/1 | execute, `cli:smoke-tool/summary` |
| CM failure/stream probes | `smoke.cm.tool.RECORD` | cm/1 | execute, `cli:smoke-tool/RECORD` |
| CE level0–3 | `smoke.ce.tool.levelN` | ce/N | read, `context.fixture/levelN` |

All tool tuples use purpose `developer-tool-smoke`, recipient `fixture-provider`
and policy_version1. Provider arguments contain exactly the bound record; the
enforcer fixes the tool name/path. Scope, operation, purpose and recipient
mismatch probes must require new consent with zero additional admissions.
CE record level comes from protected root-owned `tool-metadata.json`; a caller
can select a known record but cannot supply a lower level or another path.
Level0 still requires consent; unknown levels are rejected and level3 is ONCE
only. These are demonstration levels, not verified product CE taxonomy.


### Action confirmation and the separate consent binding

A real Tizen Action can declare this boolean fragment:

```json
{
  "requiresConfirmation": true
}
```

The field is optional and defaults to false. The Action API
`action_is_confirmation_required(action, &required)` reads it; CM's Action
catalog projection preserves `requiresConfirmation`. The Action-to-consent
mapping and final Action execution consent gate are not implemented.
This is not a consent field in the CLI descriptor shown above.
The boolean says whether confirmation is required; it does not select grant
modes. False alone cannot bypass unrelated data-access permissions.



## Protocol and concrete inputs

The fixture accepts one whole JSON-RPC2 request argv after `--json`, a nonempty
string id up to95 bytes, method `tools/call`, name `cli:smoke-tool`, and
arguments
with one known `record`. No arbitrary command/path or caller level is accepted.
A direct provider invocation demonstrates the protocol only and bypasses the
fixture enforcer; it is not a product security boundary:

```sh
/usr/libexec/consent/smoke/tool-package/bin/consent-smoke-tool --json \
  '{"jsonrpc":"2.0","id":"demo","method":"tools/call","params":{"name":"cli:smoke-tool","arguments":{"record":"summary"}}}'
```

Result: `{"jsonrpc":"2.0","id":"demo","result":{"summary":"synthetic
capability summary"}}`.
`fail` returns native error code-32001 with matching id. Unknown names/arguments
return-32602. `stderr` emits its valid response only on stderr; `nonzero`
emits a
valid result then exits7. `timeout`, `malformed`, `conflict` and `nul`
deliberately
exercise failed/unknown execution. These controls are synthetic test records.

The separate actor walkthrough above includes the concurrent Argo/UI recipe.

## Files and build

`tests/smoke/tool.c` is the synthetic JSON-RPC provider. `tool-check.c` builds
separate `consent-smoke-tool-cm` and `consent-smoke-tool-ce` executable
identities.
`tool-process.c` owns provider spawn, pipe collection and final wait.
`tool-json.c` uses JSON-GLib for the restricted fixture protocol. These are C
examples; shared helpers handle JSON and resource ownership.

`CONSENT_BUILD_SMOKE_TOOLS=ON` also requires `CONSENT_BUILD_SMOKE=ON` and
`pkg-config json-glib-1.0`. Explicit configuration fails if the dependency is
missing. RPM builds enable tools by default; `--define '_without_smoke_tools 1'`
disables them. JSON-GLib links only the new test binaries, adding an automatic
ELF requirement to the tests RPM, not the production library or daemon.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

Discover the emulator and architecture as in Guide12, install matching runtime,
devel and tests RPMs, and leave the production daemon RPM/service untouched.
Requested `--tools` mode fails before setup if its binaries are absent.

```sh
sdb -s DEVICE shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --tools --seed 20261002'
```

After checking the result, run the same runner with `--cleanup`. The ownership,
unit and path checks in Guide 12 apply. Use a new evidence directory for each
run; the recorded checkpoint is linked below.


## Safety checks and recovery

QUERY never executes. Before approval, after revocation, or on an authorization/
API error, admission counters stay unchanged. Native provider errors happen
after admission and retain count1 with the separate `native_error` state.
The ledger records a receipt before provider spawn or context read.
Success, native error and unknown execution are distinct states. Timeout,
invalid
streams and process failure do not permit an automatic second admission under
the same receipt. A retry from another actor with no ledger blocks as unknown.
The daemon binds enforcer identity + operation_id + step_id to an immutable
requirement fingerprint. Different records/tuples need distinct operation IDs;
only an identical retry reuses an ID. The CE runner uses `context-levelN-once`
for the first/retry and `context-levelN-next` for the next operation.
This ledger survives explicit handle recreation in the same actor, but is
process
local; product enforcers need durable side-effect deduplication when required.

Each stdout/stderr stream has its own 16KiB bound and 1.5-second deadline.
One valid matching response with whitespace on the other stream is accepted;
matching dual responses are accepted, conflicting/logging/malformed/NUL streams
are rejected. Native response and exit/signal metadata are recorded separately,
including a valid response with nonzero exit. The provider is always waited
after
completion or kill. Metadata and CE records require bounded complete reads and
reject NUL bytes. This restricted JSON-GLib helper is not a comprehensive
product
CM collector or duplicate-member validation replacement.

```mermaid
sequenceDiagram
  participant I as Installer
  participant A as argo and opt-in UI
  participant D as isolated consentd
  participant E as CM or CE tool actor
  participant P as Synthetic provider or data
  I->>D: register explicit tool binding
  A->>D: request and approve same tuple
  E->>D: AUTHORIZE operation/step
  D-->>E: ALLOWED receipt
  E->>E: record admission before side effect
  E->>P: pinned spawn or protected lookup
  P-->>E: native result/error or unknown execution
  E->>E: retain receipt state; no duplicate admission
```

The seeded scenarios exercise actual new tool gates before/after each running
DB deletion, stopped deletion and corruption. They assert both tool approvals
lost, new epoch, schema2/integrity/active definitions12/grants0/cleanup_unknown1
before fresh approval, then actual provider execution and CE lookup. Normal
restart preserves persistent grants; old handles are DISCONNECTED and explicitly
recreated while actor PIDs/ledgers remain. Total registry loss fails closed.
No populated QUERY-cache invalidation proof is claimed.


## Product integration status

CLI descriptor publication uses the actual CM parser, but the parser has no
consent fields. Public CM create returns permission denied; the product
execution transport and consent adapter remain absent. The inspected
`tizen-context-cli` offers gRPC context/screenshot/key-event calls, not a
consent or data-level registration API. Current CE server identity and taxonomy
still need a platform contract. These examples call no personal context RPCs
and do not implement `capmgr_client_execute()` or an `app_fw` launcher.


## Verified checkpoint

Release21 r4 completed GBS with 23 PASS and four root-only SKIP. Installed
tools and default modes returned 0; strict mode returned the expected 1 after
all scenarios, and cleanup returned 0. Installed payload hashes matched the
RPMs. Actual parser publication passed; public CM preflight remained BLOCKED.
CM and CE tools ran only after fresh approval following all three DB losses.
Production daemon18 and PoC18 were unchanged. Evidence:
`/var/tmp/consent-artifacts/consent-integration-02/`.

[Detailed snapshot and failure
history](../history/07-verification-history.en.md#guide-13-checkpoint)

[Approval metadata and API inputs](#installer-registration-inputs)


<a id="approval-metadata-and-api-inputs"></a>
---

[Related task](api/02-registration.en.md) · [Continue](14-mock-services.en.md)
· [Reading paths](../README.md)
