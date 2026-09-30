# Guide 13: Capability and context fixture tools

This extends [Guide 12](12-developer-smoke.en.md) with opt-in `--tools` in
`emulator-smoke.py`. The default smoke01 behavior and fixture paths remain.
CM's actual offline parser publishes a CLI descriptor. Separate authenticated
C CM/CE examples use the installed consent library and isolated real daemon.
They execute a synthetic provider or read synthetic context only after
AUTHORIZE returns ALLOWED with a receipt. This is a developer example, not
`capmgr_client_execute()` integration or an `app_fw` launcher.

The current public CM create gate returns permission denied; production execution
transport and internal consent adapter are absent. The discovered
`tizen-context-cli` gRPC client exposes context/screenshot/key-event commands
but has no consent/data-level registration contract and needs a JSON-RPC adapter.
Current CE server source, identity and taxonomy remain external gates. This
example makes no personal-context RPCs and does not substitute old contextd.

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
Release21 separates the final package snapshot from accepted Release19/r6
archives and the initial Release20 development attempts.

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

Use the same runner's explicit `--cleanup` after inspecting evidence. All fixture
ownership/unit/path guards from Guide12 apply. New evidence belongs under
`/var/tmp/consent-artifacts/consent-integration-02/`; smoke01/r6 is preserved.

## Descriptor, tools and consent tuples

Installed `tool-package/cli.json` uses the actual CM descriptor schema:

```json
{"version":1,"key":"smoke-tool","name":"Fixture summary tool",
 "desc":"Synthetic JSON-RPC provider; not a product adapter",
 "executable":"bin/consent-smoke-tool",
 "inputSchema":{"type":"object","properties":{"record":{"type":"string",
 "enum":["summary","fail","timeout","malformed","stderr","nonzero",
 "conflict","nul"]}},"required":["record"],"additionalProperties":false},
 "outputSchema":{"type":"object","properties":{"summary":{"type":"string",
 "maxLength":1024}},"required":["summary"],"additionalProperties":false}}
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

## Admission and recovery boundaries

QUERY never executes. Before approval, after revocation, or on an authorization/
API error, admission counters stay unchanged. Native provider errors happen
following admission and retain count1 with the separate `native_error` state. The ledger records a receipt before provider spawn or context read.
Success, native error and unknown execution are distinct states. Timeout, invalid
streams and process failure do not permit an automatic second admission under
the same receipt. A retry from another actor with no ledger blocks as unknown.
The daemon binds enforcer identity + operation_id + step_id to an immutable
requirement fingerprint. Different records/tuples need distinct operation IDs;
only an identical retry reuses an ID. The CE runner uses `context-levelN-once`
for the first/retry and `context-levelN-next` for the next operation.
This ledger survives explicit handle recreation in the same actor, but is process
local; product enforcers need durable side-effect deduplication when required.

Each stdout/stderr stream is independently collected to16KiB with a1.5s deadline.
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

## Verified Release21 snapshot (2026-09-30)

CONSENT-INTEGRATION-02 implementation r4 uses baseline `a569363` plus the
developer-example changes. `source-r4.json` records21 native/build/runner files
and matches the executed and installed Release21 snapshot. Publication removes
only three trailing spaces from the runner; its Python AST is identical, but its
published byte hash differs from installed r4. The original logs/RPMs remain;
external `publication-whitespace.json` records old/new hashes. No package was
rebuilt for this mechanical publication correction. Documentation evidence is
updated after validation.
All evidence below is preserved in
`/var/tmp/consent-artifacts/consent-integration-02/`.

| Evidence | Actual result |
| --- | --- |
| `gbs-r4.log`, `gbs-r4.exit` | Exact build command above; exit0, CTest23 PASS +4 root-only SKIP of27 |
| `rpms-r4/`, `rpm-r4-sha256.json` | Archived Release21 RPMs; matching source manifest |
| `install-r4.log`, `install-r4.exit` | Normal upgrade exit0, runtime/devel/tests only |
| `installed-r4-hash.log` | All17 installed smoke payload hashes equal archived RPM digests; runner equals executed source-r4 |
| `installed-tools-seed20261002-r4.log` | SMOKE_EXIT0, TOOLS_OUTER_EXIT0; all tool scenarios and3 recoveries PASS |
| `installed-strict-seed20261003-r4.log` | All tool scenarios PASS, then explicit product gate; SMOKE_EXIT1, STRICT_OUTER_EXIT1 |
| `installed-default-seed20261004-r4.log` | Default smoke01 regression SMOKE_EXIT0, DEFAULT_OUTER_EXIT0 |
| `final-cleanup-r4.log`, `final-services-r4.log` | Final cleanup0; both smoke units not-found and four fixture directories absent |
| `device-before-r2.log`, `final-services-r4.log` | Production PID31569 active/success; PoC PID0 inactive/success; complete state metadata fingerprint unchanged |

`results-r4.json` summarizes exits and recovery cases. SDB host status is0 even
for the strict remote failure; remote SMOKE_EXIT and OUTER markers are the
assertions. `commands.txt` preserves each exact host/device command. The emulator
was discovered as `emulator-26101`, architecture x86_64, profile
`tizen_10_1_emulator`. Installed `consent`, `consent-devel`, `consent-tests` are
0.1.0-21; production `consentd` and `consent-poc` remain0.1.0-18. The production
daemon RPM produced by GBS was archived and never installed. CM remains0.1.0-15;
JSON-GLib is1.8.0. Package dependency audits show JSON-GLib only in tests.

Exact final installation and replay commands (selected emulator):

```sh
sdb -s emulator-26101 shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System::Privileged rpm -Uvh \
  /tmp/consent-integration-02-r4-rpms/consent.rpm \
  /tmp/consent-integration-02-r4-rpms/consent-devel.rpm \
  /tmp/consent-integration-02-r4-rpms/consent-tests.rpm'
# For each replay, use this same invocation prefix with the options below:
sdb -s emulator-26101 shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --tools --seed 20261002'
```

Replay order/options: `--tools --seed 20261002` → `--cleanup` →
`--tools --require-product --seed 20261003` → `--cleanup` →
`--seed 20261004` → `--cleanup`. The recorded commands also print and assert the
remote outer exit before exiting the device shell.

The actual parser publishes `cli:smoke-tool` with owner/executable verified.
Public CM preflight remains `product_public_api/BLOCKED create_status=-2
handle=null`, PREFLIGHT_EXIT3. stderr-only and nonzero-exit7 native replies succeed;
valid native error is `native_error`; timeout/malformed/conflicting/NUL streams
are `unknown`, each retry deduplicated with count1. CE levels1–3 each have exact
first `state=succeeded count=1` and retry `deduplicated state=succeeded count=1`.
Both CM and CE tool actors execute after fresh approval in stopped-delete,
corrupt and running-delete scenarios, with all three definition/grant/epoch
readbacks recorded. The strict run repeats these successfully before its sole
expected product-integration failure.

Preserved failures/corrections: `installed-tools-seed20261002-r2.log` had remote1
at CElevel2 because `context-once` reused another level's immutable tuple;
`cleanup-failed-r2.log` is0. R3 gives each level a distinct operation namespace
without changing daemon policy. `gbs-r1/r2/r3.log` all passed; `install-r3.log`
records remote3, where Tizen MSM rejected changed runner content under identical
Release20 despite `--replacepkgs`. Release21 normal upgrade resolves this without
force-file replacement. The original oversized SDB hash command failed with
service-name-too-long, preserved in
`installed-r4-hash-command-size-failure.log`; shorter read-only hash collection
then verified all17 files. Installation ldconfig permission warnings are retained
in install logs; the transaction exit and installed hashes were checked.
Previous smoke01/r6 artifacts remain unchanged. No commit or push was made.
