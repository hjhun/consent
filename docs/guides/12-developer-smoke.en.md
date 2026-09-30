# Guide 12: Developer CM/CE consent smoke

This smoke uses the real installed CM offline package parser and the public
consent C API over real socket activation. The CM catalog and consent binding
are separate. CM product authorization currently fails closed; no CM internal
consent adapter is installed. The CE example uses a portable demonstration
level policy, pending identification of the current CE source and API.
It does not establish product integration or replace user approval.

The code is in `tests/smoke/`; `scripts/emulator-smoke.py` is the single target
runner. `CONSENT_BUILD_SMOKE=ON` creates test-only binaries in the test RPM.
Production role policy, endpoints, library symbols, and daemon behavior stay
unchanged. The smoke library compiles the normal endpoint and connected-peer
checks, including the PID1/root/SMACK identity of the activated listener.
The isolated daemon uses the existing compile-time test package-identity fixture
and a protected Installer generation registry rather than installed app metadata.

## Build and run

Discover the connected development emulator and query its architecture first:

```sh
sdb devices
sdb -s DEVICE shell uname -m
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

Install matching runtime and test RPMs using `rpm -Uvh` on the selected
emulator. The runner requires the installed CM parser at
`/usr/libexec/capmgr/capmgr-package-tool`, `libcapmgr.so.0`, Python3, SQLite,
and systemd. Missing CM or a missing API symbol fails the smoke.
The runner fixes LD_LIBRARY_PATH to the private installed smoke directory;
this also supports RPM builds that strip RPATH.
The test RPM adds no CM dependency to production packages.

Run the installed runner in an explicitly selected development emulator:

```sh
sdb -s DEVICE root on
sdb -s DEVICE shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --seed 20260930'
```

Preserve stdout/stderr, `SMOKE_EXIT`, and the outer command exit separately.
The runner records subprocess commands, exit statuses, actor PIDs, seed,
recovery order, and daemon journal. An interrupted or failed run stops only
its validated managed units and reaps its children. It retains owned fixture
artifacts for inspection. Python optimized mode is forbidden because safety
assertions must remain active.

After inspecting the evidence, explicitly remove this fixture before repeating:

```sh
sdb -s DEVICE shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --cleanup'
```

Cleanup verifies the marker, directory owners, and exact recorded managed-unit
hashes before stopping or removing anything. Existing foreign paths or units
are rejected before setup mutates state. No implicit reset is available.

## What the actors do

| Actor | Authentication and work |
| --- | --- |
| Installer | Separate executable; register explicit package/app and generation |
| CM example | `cm` role and `cm` enforcer only; QUERY, AUTHORIZE, protected skill read |
| CE example | `ce` role and `ce` enforcer only; QUERY, AUTHORIZE, protected fixture data read |
| argo example | `argo` role; async acceptance, request-ID lookup, callback after return |
| Smoke UI | `ui` role; explicit `--auto-approve-smoke`, prompt token, response |
| Admin | `admin` role; revoke delegated subject/profile decisions |

Each executable is independently authenticated by executable identity and kernel
credentials. CM/CE registration, request initiation, and cross-enforcer
AUTHORIZE are rejected. UI receives the request ID from argo; it does not call
argo's request-result lookup API. Automatic approval belongs only to this fixture.

The real parser processes this manifest against an isolated absolute catalog:

```json
{"version":1,"operation":"smoke-install","owner":"smoke.package",
 "mode":"replace","root":"/tmp/consent-smoke/catalog-package",
 "metadata":[{"key":"http://tizen.org/metadata/capability/skill",
 "value":"skill.json"}]}
```

The descriptor identifies `consent-smoke` and `res/skills/smoke`. Stage returns
pending/revision0; successful finalize publishes revision1, and replay leaves
revision1. The runner checks the published `skill:consent-smoke` and its package
owner using its own read-only catalog connection. This is actual parser/catalog
execution. A separate developer mapping binds that identity to `smoke.cm.read`,
level1 and enforcer `cm`. The CM parser does not import consent metadata.

The CE demonstration binds levels0–3 to four distinct definitions. Level0 still
requires a grant. Unknown levels fail; level3 permits ONCE only. The Installer
also exercises the daemon rejection of level4 and level3 PERSISTENT metadata.
These numbers are demonstration policy, not verified CE taxonomy.

Before any approval, AUTHORIZE returns CONSENT_REQUIRED and the resource read
counter stays zero. After ALLOWED and a receipt, the actor opens and reads the
isolated resource. QUERY is advisory. ONCE AUTHORIZE consumes atomically; a retry
with the same operation/step reuses the receipt and deduplicates the fixture read.
A new operation requires fresh approval. The example deduplication is process
local; real enforcers need durable side-effect deduplication where applicable.

```mermaid
sequenceDiagram
  participant I as Installer example
  participant A as argo example
  participant D as consentd smoke
  participant U as opt-in smoke UI
  participant E as CM or CE example
  I->>D: register(package, app, definition)
  E->>D: AUTHORIZE
  D-->>E: CONSENT_REQUIRED (read blocked)
  A->>D: request_async
  D-->>A: accepted, request ID
  A->>U: request ID via runner
  U->>D: get_prompt, respond(token)
  D-->>A: ALLOWED callback
  E->>D: QUERY then AUTHORIZE(operation, step)
  D-->>E: ALLOWED + receipt
  E->>E: deduplicate receipt, read protected fixture
```

## Concrete C example commands

The runner owns fresh setup and generation provisioning. Inside an enrolled
root/System actor context, these inputs show the public API usage sequence.
Use the generation returned by the runner; do not invent it. These are
adaptations for the corresponding stages of an active freshly provisioned fixture.
The completed runner leaves stopped registry-loss state; do not paste these
commands after a completed smoke. Run the pre-approval commands before the
concurrent argo/UI pair, then the post-approval commands after its callback.

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
smoke=/usr/libexec/consent/smoke
"$smoke/consent-smoke-installer" "$GENERATION" smoke.cm.read cm 1 register-demo
printf 'query advisory CONSENT_REQUIRED\nauthorize demo-before CONSENT_REQUIRED\n' |
  "$smoke/consent-smoke-cm" smoke.cm.read \
  /tmp/consent-smoke/catalog-package/res/skills/smoke/SKILL.md
printf 'query advisory CONSENT_REQUIRED\n' |
  "$smoke/consent-smoke-ce" smoke.ce.level0 /tmp/consent-smoke/context-data.txt
```

Argo waits for its callback. Start it in terminal/context A; while it is
waiting, copy the printed REQUEST_ID into the independent UI context B.
Both contexts must already be enrolled root/System actors.

Terminal/context A:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-argo smoke.cm.read approve-demo
```

Concurrent terminal/context B:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-ui \
  --auto-approve-smoke "$REQUEST_ID" PERSISTENT
```

After the argo callback, the post-approval CM check/read:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
smoke=/usr/libexec/consent/smoke
printf 'query advisory ALLOWED\nauthorize demo-authorized ALLOWED\n' |
  "$smoke/consent-smoke-cm" smoke.cm.read \
  /tmp/consent-smoke/catalog-package/res/skills/smoke/SKILL.md
```

These are independent example invocations, not a second setup runner. Each
CM/CE invocation owns one handle; the runner keeps their stdin open to exercise
explicit reconnects and preserve the actor's receipt ledger. Request IDs and
prompt tokens belong to their originating argo/UI operations.

## Recovery and evidence boundaries

Mutable fixture paths are `/tmp/consent-smoke`,
`/opt/var/lib/consent-smoke-runtime`, `/opt/var/lib/consent-smoke-state`, and
`/opt/var/lib/consent-smoke-authority`; units are `consentd-smoke.socket/service`.
The runtime socket parent is protected even though control files use sticky /tmp.
Only consentd opens the live consent DB. Runner SQLite inspection occurs with
both managed service and socket stopped.

A normal restart preserves both persistent CM/CE grants. The seeded loss order
covers deletion while running, deletion while stopped, and deliberate corruption
while stopped. Both check actor PIDs and their receipt ledgers span recovery. After daemon
stop/restart, old handles return exactly DISCONNECTED. An explicit reconnect
command destroys the old handle and creates/authenticates a new one, logging
handle_generation. Running DB deletion uses the original handle when connected. Existing grants are rejected,
epoch changes, and readback before fresh approval confirms integrity `ok`,
schema2, five active definitions, zero grants, and cleanup reconciliation required.
Fresh approvals then permit CM and CE AUTHORIZE and actual fixture reads.
A retry receipt absent from the live ledger blocks the read because prior
execution is unknown. Check APIs are authoritative and do not use the argo request cache; these live
handle cases prove stale authorization rejection, not a request-cache-hit test.

Total definition-registry plus DB loss intentionally blocks daemon startup and
protected execution. The runner never reconstructs approvals or silently resets
the registry. It preserves the failed fixture until explicit cleanup.
Production and existing PoC state metadata are compared before/after the run.
This is no abrupt reboot or interrupted-commit proof; the existing verification
guide records those separate tests.

`catalog_actual/PASS` and `developer_smoke/PASS` differ from
`product_public_api/BLOCKED` and `product_internal_integration/BLOCKED`.
The preflight calls the actual public `capmgr_client_create` symbol and records
status/null-handle; expected denial exits3, missing library/symbol exits2.
`--require-product` always fails until both product adapters exist, including CE.

## Executed evidence (2026-09-30)

The accepted implementation is uncommitted Release19 on baseline `a569363`
Release18. Final source/build snapshot **r6**, installed-runner reproduction,
strict-product negative and cleanup are the completion evidence. CM installed
version is `capability-manager-0.1.0-15.x86_64`; target is
`emulator-26101`, x86_64. Implementation, packaging and available emulator
verification were independently ACCEPTED within this developer smoke scope.
Actual product CM internal adapter and current CE integration remain open gates.

Earlier snapshots remain separate: the first GBS attempt failed on PoC SDK
provisioning, r2 failed the nested IDL fixture's missing cmake copy, and r5 target
execution failed before destructive scenarios because of fresh unit loading and
partial-bootstrap cleanup. Corrected-runner/native-r5 exploratory execution then
passed. Those logs establish the fixes; they are not substituted for the final
installed-r6 results below.

Full commands, failed attempts, subprocess statuses, journal and outputs are
preserved at `/var/tmp/consent-artifacts/consent-smoke-01/` on the build host.
The source hash manifest is `source-r6.json`; build output is
`consent-smoke-gbs-r6.log`. This smoke does not establish actual CE APIs, product
CM internal AUTHORIZE, abrupt reboot, interrupted commit or request-cache-hit
coverage. Final guide-only corrections followed the frozen native snapshot.

Final r6 matching-package replay: GBS exited0 (CTest26: 22 PASS, four root-only
SKIP), and the native/runner files matched `source-r6.json`. The same-NVR r6
replacement initially exited3 for changed files; explicit replacement of those
same runtime/devel/test packages with `--replacepkgs --replacefiles` exited0.
The installed runner SHA256 was
`912502e58186dd4b82e093c1fe2ab4c51b59bb76520dd415941e8acc73e67bf0`.

| Installed r6 run | Seed | Internal / outer exit | Evidence file |
| --- | --- | --- | --- |
| Full developer smoke | 20260930 | 0 / 0 | `device-r6-seed20260930.log` |
| Strict product negative after full smoke | 20260931 | 1 / 1 (expected) | `device-r6-strict-seed20260931.log` |
| Explicit final cleanup | — | 0 / 0 | `final-cleanup-services-r6.log` |

The default order was running-delete/corrupt/stopped-delete; strict order was
stopped-delete/corrupt/running-delete. Each run confirmed unchanged production
and PoC state metadata. Production consentd remained Release18, active/success,
MainPID31569 before and after installation/tests. PoC stayed inactive/success,
MainPID0. Final cleanup left both smoke units not-found/MainPID0 and all four
managed fixture directories absent. Runtime/devel/tests are Release19; the
production daemon RPM/service and existing PoC installation were untouched.
Doc-only invocation/evidence corrections followed the frozen r6 native build.

Final install (`install-r6-replace.log`, INSTALL_EXIT0):

```sh
systemd-run --quiet --wait --pipe --unit=consent-smoke-install-r6-replace \
  -p SmackProcessLabel=System::Privileged rpm -Uvh \
  --replacepkgs --replacefiles /tmp/consent-smoke-runtime.rpm \
  /tmp/consent-smoke-devel.rpm /tmp/consent-smoke-tests.rpm
```

Final commands (each issued through `sdb -s emulator-26101 shell`):

```sh
systemd-run --quiet --wait --pipe --unit=consent-smoke-run-r6 \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --seed 20260930
systemd-run --quiet --wait --pipe --unit=consent-smoke-strict-r6 \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py \
  --seed 20260931 --require-product
systemd-run --quiet --wait --pipe --unit=consent-smoke-clean-final-r6 \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --cleanup
```

An explicit cleanup separated the two full runs. SDB transport exited0 even for
the expected strict failure; the internal/outer markers establish its exit1.
The r6 RPMs are under
`/home/hjhun/GBS-ROOT/local/repos/tizen_10_1_emulator/x86_64/RPMS/`:
`consent`, `consent-devel`, `consent-tests`, and built-but-uninstalled `consentd`,
all `0.1.0-19.x86_64.rpm`. The evidence directory also archives the installed
three RPMs and their SHA256 hashes separately for r5 and r6.
