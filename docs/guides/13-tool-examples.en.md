# Guide 13: Tool execution and context lookup examples

This guide checks user approval before a tool call or data lookup. The CM
example executes a JSON-RPC provider; the CE example reads a test file. Both
require consentd to return ALLOWED and a receipt before the protected action.
Run them with `emulator-smoke.py --tools`, building on
[Guide 12](12-developer-smoke.en.md).

A checker (enforcer) is the service responsible for this final authorization.
Trusted metadata binds a tool or record to its consent definition. A requirement
tuple is the complete operation, scope, purpose, recipient and policy version;
changing any part can require new approval. The receipt and execution ledger
are explained in the safety section. Product integration status is listed
separately below.

## Files and build

`tests/smoke/tool.c` is the synthetic JSON-RPC provider. `tool-check.c` builds
separate `consent-smoke-tool-cm` and `consent-smoke-tool-ce` executable identities.
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

The separate Installer registers explicit package/app/generation and definitions.
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

## Approval metadata and API inputs

These JSON objects show the fields passed to the existing C API. They are
not a new JSON endpoint.

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

The following are two exact entries from protected `tool-metadata.json`:

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

The runner supplies the remaining records and protects this mapping. CM's
provider returns a synthetic summary; CE reads `tool-context/level1.txt`, whose
body is `synthetic context summary level1` followed by a newline. Bodies stay
outside consent registration and storage; the metadata identifies their gates.
These levels are fixture policy, not product CE taxonomy. Level0 also requires
consent; level3 registers only ONCE. Callers cannot supply or downgrade levels.

### Installer registration inputs

Package name `smoke.package` and app ID `smoke.app` are separate arguments to
`consent_register()`. They are validated against trusted identity/generation;
strings alone do not authenticate the publisher. The full parameter object for
this CM definition is shown below. Every value is a string, including numbers.
Localized messages use flattened `message.en.title` and `message.en.body` keys.
Replace the generation placeholder with the Installer-provisioned value.

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

This JSON illustrates `consent_params_set()` inputs, not a public JSON-register
endpoint or loader. `fields` below is a caller-owned key/value array containing
exactly those entries; `field_count` is its length and `client` an authenticated
Installer handle. Stop on any error and free the parameter object:

```c
/* Run only as the enrolled Installer; generation is trusted provisioning. */
consent_params_t* params = NULL;
int status = consent_params_create(&params);
if (status == 0) {
  for (size_t i = 0; i < field_count && status == 0; ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_register(client, "smoke.package", "smoke.app", params);
}
consent_params_free(params);
/* Propagate status; client and the borrowed fields remain caller-owned. */
```

For CE level1, change definition to `smoke.ce.tool.level1`, enforcer to `ce`
and registration operation ID to a distinct value; level and modes stay the
same. For level3, set level to `3` and modes to `ONCE`. Retention `60000` is the
fixture's data-use limit in milliseconds, independent of access grant duration.
This bound applies to receipt/session-bound MEMORY_ONLY artifacts registered by
an authenticated holder through `consent_data_register()`. It does not
automatically delete the CE fixture `.txt` file; this example demonstrates the
access gate.

### Requester and enforcer inputs

Argo uses this flattened parameter object with `consent_request_async()`:

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

The CM enforcer uses the same protected requirement tuple with
`consent_check()` and these AUTHORIZE inputs:

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

For CE level1, both actors instead bind `r0.definition` to
`smoke.ce.tool.level1`, `r0.operation` to `read`, and `r0.scope` to
`context.fixture/level1`; purpose, recipient and policy version stay unchanged.
The mapping is shared out of band, not inferred from provider arguments.
These unversioned examples contain no `approval_version` or `grant_mode`.
Argo initiates approval and hands the request ID to the authenticated UI, which
gets the prompt and responds; CM/CE check never opens UI. QUERY uses
`mode: "QUERY"` and performs no protected effect. Before execution/data read,
the enforcer requires API status0, ALLOWED and a receipt, then applies the
process-local admission ledger described below. Neither a definition nor an
approval result alone authorizes an effect; retry/unknown outcomes remain gated.

## Protocol and concrete inputs

The fixture accepts one whole JSON-RPC2 request argv after `--json`, a nonempty
string id up to95 bytes, method `tools/call`, name `cli:smoke-tool`, and arguments
with one known `record`. No arbitrary command/path or caller level is accepted.
A direct provider invocation demonstrates the protocol only and bypasses the
fixture enforcer; it is not a product security boundary:

```sh
/usr/libexec/consent/smoke/tool-package/bin/consent-smoke-tool --json \
  '{"jsonrpc":"2.0","id":"demo","method":"tools/call","params":{"name":"cli:smoke-tool","arguments":{"record":"summary"}}}'
```

Result: `{"jsonrpc":"2.0","id":"demo","result":{"summary":"synthetic capability summary"}}`.
`fail` returns native error code-32001 with matching id. Unknown names/arguments
return-32602. `stderr` emits its valid response only on stderr; `nonzero` emits a
valid result then exits7. `timeout`, `malformed`, `conflict` and `nul` deliberately
exercise failed/unknown execution. These controls are synthetic test records.

For an active freshly provisioned fixture, these are actor input adaptations,
not commands to paste after a completed runner (which leaves registry-loss state).
The runner owns provisioning, metadata and enrollment. Each terminal must already
be enrolled in the root/System fixture actor context. Set the library path:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'query demo-query CONSENT_REQUIRED\nauthorize demo-before CONSENT_REQUIRED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-cm smoke.cm.tool.summary
```

In terminal A start argo and copy its REQUEST_ID while it waits:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-argo smoke.cm.tool.summary demo-approval
```

Concurrently in terminal B respond with the copied id:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-ui --auto-approve-smoke REQUEST_ID ONCE
```

After argo's callback, the CM actor can authorize and execute. The CE executable
uses the same sequence with `smoke.ce.tool.level0` and a separate CE approval:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'authorize demo-authorized ALLOWED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-cm smoke.cm.tool.summary
# After a separate argo/UI approval for smoke.ce.tool.level0:
printf 'authorize context-demo-authorized ALLOWED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-ce smoke.ce.tool.level0
```

## Safety checks and recovery

QUERY never executes. Before approval, after revocation, or on an authorization/
API error, admission counters stay unchanged. Native provider errors happen
after admission and retain count1 with the separate `native_error` state.
The ledger records a receipt before provider spawn or context read.
Success, native error and unknown execution are distinct states. Timeout, invalid
streams and process failure do not permit an automatic second admission under
the same receipt. A retry from another actor with no ledger blocks as unknown.
The daemon binds enforcer identity + operation_id + step_id to an immutable
requirement fingerprint. Different records/tuples need distinct operation IDs;
only an identical retry reuses an ID. The CE runner uses `context-levelN-once`
for the first/retry and `context-levelN-next` for the next operation.
This ledger survives explicit handle recreation in the same actor, but is process
local; product enforcers need durable side-effect deduplication when required.

Each stdout/stderr stream has its own 16KiB bound and 1.5-second deadline.
One valid matching response with whitespace on the other stream is accepted;
matching dual responses are accepted, conflicting/logging/malformed/NUL streams
are rejected. Native response and exit/signal metadata are recorded separately,
including a valid response with nonzero exit. The provider is always waited after
completion or kill. Metadata and CE records require bounded complete reads and
reject NUL bytes. This restricted JSON-GLib helper is not a comprehensive product
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

[Detailed snapshot and failure history](../history/07-verification-history.en.md#guide-13-checkpoint)
