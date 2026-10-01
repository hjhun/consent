# Guide 09: Storage maintenance and recovery boundaries

[한국어](09-storage-maintenance.ko.md)

The repository now compacts unusable metadata while retaining operation retries,
request results and holder cleanup evidence. It does not implement a retention
TTL, remove completed request/session/artifact rows, or recover a lost
definitions
registry. The registry-loss workflow below is a design for a separate increment.

## 1. Validate maintenance in the SDK build

This is a framework-maintainer task. There is no public maintenance CLI or C
API.
Run the repository test from a configured, matching native SDK build directory:

```sh
cd /path/to/consent-build
ctest -R '^repository-test$' --output-on-failure
```

Require the selected test to pass, not merely an empty CTest selection. This is
an internal test command, not a fresh verification claim or permission to open
the live DB from a second process. Only consentd owns the live database.

## 2. Interpret the maintenance behavior

On the normal DB executor, Tick runs a bounded batch no more often than once per
60 seconds. Invalid authorization payloads, obsolete UI tokens and unusable
resume tokens may be compacted. Retry identities, valid receipts, request state
and holder cleanup evidence remain. File size need not shrink; VACUUM and TTL
pruning are not provided.

If maintenance returns BUSY, FULL or I/O error, stop uncertain execution and
handle storage failure. Do not replace the database or discard retry history.
For isolated recovery testing, follow [Guide 12](12-developer-smoke.en.md); for
holder cleanup, follow [API 05](api/05-sessions-and-data.en.md).

## Reference: transaction and capacity rules


## Execution and transaction boundary

`Repository::Maintain()` is an internal C++ method owned by the serialized DB
executor. It is not a public C API, IPC method, root CLI or separate SQLite
writer.
`Tick()` runs one batch after its normal integrity, definition reconciliation
and
expiry work, no more often than once per 60 seconds. Opening the repository
starts
that interval. Internal tests may invoke `Maintain()` directly without waiting.

One `BEGIN IMMEDIATE` transaction selects at most **128 logical records in
total**
across four classes. The starting class rotates after each successful commit so
one class cannot permanently consume the entire budget. An authorization record
can have several grant edges; deleting those edges does not mean only 128
physical
SQL rows change. A newly unreferenced grant may become eligible in a later
batch.

The batch limit bounds selected work, not elapsed time or history scans.
Candidate
selection can still scan historical authorization, session or grant rows. Two
additive indexes, `authorization_grant_source(grant_id)` and
`artifact_grant_source(grant_id)`, support the correlated reference checks.
SQLite
uses these covering indexes for those checks; the outer revoked-grant selection
can still scan `grants`. Large product workloads need separate latency/capacity
measurement. These indexes are created idempotently with the existing schema-2
startup transaction; no stored row format changes.

## What changes and what remains

| Candidate | Compacted data | Data preserved |
|---|---|---|
| Authorization with `valid=0` | Payload and `authorization_grants` edges | Receipt ID, enforcer/operation/step unique key, fingerprint, creation time and invalid status |
| ALLOWED, DENIED, CANCELLED, EXPIRED or INVALIDATED request with UI remnants | Token/UI-owner/UI-instance columns and private `prompt_token`, `ui_owner`, `ui_instance`, `_prompt_locale` payload fields | Request ID, scoped client-request key, fingerprint, final result and public context |
| CLOSED session | Resume-token hash | Session identity, ownership, state, generation and cleanup context |
| Revoked grant referenced by neither authorization nor artifact | Entire unusable grant row | Every grant still referenced by either relation |

`get_prompt` stores its private displayed locale and nonempty UI ownership in
one
UPDATE. Existing terminal transitions retain that ownership until maintenance
clears the columns and payload together. Thus cancelled, expired and invalidated
typed prompts are selected even after their token was cleared. Maintenance does
not reinterpret unknown request states or scrub malformed rows
opportunistically.

`ReceiptValid()` rejects an invalid authorization before parsing its payload.
The retained unique key and fingerprint therefore preserve STALE for identical
invalid execution retries and CONFLICT for changed retries. A valid
authorization
keeps its payload and source edges. In particular, a consumed ONCE grant and its
valid receipt remain available for the original execution retry; maintenance
does
not create another allowance.

Artifact rows, `artifact_grants`, `artifact_parents` and cleanup
acknowledgements
are retained, including DELETED artifacts. A live derived artifact has its own
materialized source-grant union and remains independent of its parent's physical
residency. This increment conservatively preserves both the union and parent
history. A deleted artifact's registration tombstone still prevents recreating
data with its old receipt, and a repeated successful cleanup ACK remains valid.

No registration receipt, removal tombstone, obsolete offline receipt,
installation
authority receipt or offline spool file is deleted. No request-result contract
or
cleanup evidence is shortened. No policy revision is published merely for this
internal compaction, and `cleanup_reconciliation_required` is never cleared.

## Failure and capacity behavior

All selected mutations commit together. A write failure rolls back earlier work
in the batch. Explicit maintenance reports failure; the timer logs the failure
and fences storage through the normal reconciliation path. The next scheduled
attempt is delayed by the same interval, including after an error. The explicit
method also checks the final snapshot epoch before reporting success.

BUSY, FULL and I/O errors do not authorize replacing the DB or discarding
receipt
history. Existing explicit SQLite corruption recovery remains separate. An
uncertain maintenance outcome is safe to retry because the eligibility
predicates
and retained tombstones make compaction idempotent.

There is no `VACUUM`, DB replacement, or promise that the file immediately
shrinks.
SQLite may reuse freed space. Request/session/artifact rows and durable ledgers
can still grow; existing registry/spool capacity errors remain explicit.
Arbitrary
TTL deletion of retry keys is unsafe: it could turn an old operation into a new
execution or revive a removed registration. Full pruning needs an explicit
retry-generation/acknowledgement/expiry contract and separate implementation.

## Verification scope

`repository-maintenance-test` uses isolated state and actual Repository API
paths
to verify ONCE retry/consumption, STALE and CONFLICT preservation, unchanged
request
results, live derived data after parent deletion, artifact nonrevival, repeated
cleanup ACKs, typed prompt cleanup, closed-session nonreactivation and a
129-record
fixture that leaves one authorization payload after the first 128-target batch.
Test-only SQLite interposition injects IOERR, FULL and BUSY after the payload
UPDATE and before the edge DELETE, proving rollback of the earlier write.
Test-only monotonic-clock advancement exercises the real Tick path, minute
scheduling, error fencing and a later successful retry. There are no production
fault flags or time overrides.

The new executable and existing repository, provenance, storage-fault and
offline
repository regressions were compiled with SDK headers and run against host GLib
and SQLite as supplementary native checks. This is not a GBS/emulator, physical
disk-full, device I/O, power-loss or sustained-load verification claim. Target
execution belongs in [the verification record](07-verification.en.md).

### Build 24 target checkpoint

GBS source tree `84042ee0d4113006872fbc334fcf3d755d72fd76` passed
14 native CTests with four root-only skips. On the selected development
emulator,
the exact packaged `repository-maintenance-test` passed all eight scenarios
under `consent-maintenance24.service`, exiting normally with status 0. Its
installed SHA-256 matched the RPM payload:
`a6f296239f60f401437a8a24e266584a843abaf6d60d08356f7ae5d2e083672e`.
The recorded output is
`/var/tmp/consent-artifacts/emulator-build-24/maintenance.log`; the source/RPMs
and build logs are preserved under `/var/tmp/consent-artifacts/gbs-build-24`.
This target fixture uses isolated state and injected SQLite errors; it does not
establish physical storage-full, device-write failure or power-loss behavior.

## Registry-loss recovery and bootstrap boundary

Build43 (Release11) implements a root-protected bootstrap receipt and an
admission fence before `LoadRegistrations`. Fresh installation claims one
systemd invocation; the daemon checks its unit, cgroup and exact MainPID.
An initialized receipt permits automatic DB-only recovery from a trusted
registry, creating a new DB incarnation and durable `cleanup_unknown=1`.
Missing registry, missing receipt, a partial fresh claim, or a recovery-required
receipt blocks admission. The planned recovery contract requires a complete
validated current desired source. The current production `--begin` checks the
recovery ID, then returns a missing-source error without reading a source or
mutating the stores. The helper never writes SQLite. A missing DB and registry
are not treated as a fresh install.

Build43 isolated bootstrap and POC classification fixtures passed on the
emulator. A Release7-to-8 transaction completed with production and POC
receipts both INITIALIZED. The POC intact-pair path follows from code and the
final receipt; no pre-upgrade POC file identity was captured. The isolated
POC fixture proves an incomplete pair becomes
`RECOVERY_REQUIRED`; it is not an actual absent-state POC RPM upgrade. The
trusted desired-state producer, import-complete fence and physical holder
reconciliation remain unimplemented. No former approval or cleanup completion
is inferred from metadata loss.

### Future total-registry recovery design — not implemented

The current implementation deliberately blocks when the definitions registry is
missing or corrupt while a DB remains. A healthy-looking DB cannot reconstruct
the independent trusted desired-state source or prove that old approvals are
current. Do not delete both files manually to bypass this block.

A future root-only helper should provide a resumable two-phase recovery
operation:

1. Require an explicit stable recovery ID, a confirmed stopped daemon and the
   existing exclusive lifecycle lock. Validate protected ancestors, ownership,
   regular files, single links and exact managed names before mutation. A live
   daemon/Installer, unexpected entry or unreadable source must prevent progress.
2. Durably publish a root-owned PREPARED record outside daemon-writable state
   before moving anything. Startup must reject an incomplete recovery before
   opening SQLite or importing registrations. Record the original installation
   generations and exact managed-file identities needed for a safe retry.
3. Quarantine the DB, applicable journal/WAL/SHM artifacts, registry and managed
   temporary/retired files, plus the old offline spool, without copying approvals
   into new state. Use protected directory FDs, same-filesystem moves and fsync
   both source and destination directories. Same-ID retries reconcile recorded
   identities and completed moves; errors never trigger automatic restoration of
   an old DB or an empty-success result.
4. Have the trusted Installer verify the actual installed packages and current
   desired definitions. Fence former installation generations, then stage only
   that verified desired set with fresh operations through the existing offline
   C API. Preserve installation authority receipts. The old spool alone is not a
   current desired-state inventory: without registry tombstones it can resurrect
   a definition previously removed under the same generation. Missing trusted
   input must leave definitions unavailable.
5. Durably publish helper `SOURCE_READY` only after source/generation validation
   and completed quarantine. This is a handoff, not daemon admission or systemd
   `READY=1`. Consentd alone creates the new SQLite DB incarnation, validates
   pkgmgr and authority, durably records `cleanup_unknown=1`, imports all
   definitions and commits `import_complete=1` before listener/admission and
   systemd READY. It restores no grants, consumption receipts, sessions or
   artifacts. A crash with partial import must remain fenced and resumable.

The implementation must preserve `cleanup_reconciliation_required` until a
separate evidence-based holder reconciliation protocol can clear it. Empty new
tables do not prove old physical data was deleted. Crash, rename/fsync failures,
partial quarantine, unsafe paths, stale seeds and actual installation changes
all need dedicated tests before this helper can be shipped.

Complete ledger loss also loses knowledge of some old operation IDs. The current
response/cache epoch alone cannot prove permanent deduplication of those unknown
IDs. Recovery must retain uncertain-outcome semantics and require fresh
approval;
an across-reset exactly-once claim requires an additional durable retry
protocol.

See [storage design](../design/04-storage-design.en.md),
[installation authority](03-installation-authority.en.md) and
[offline registration](04-offline-registration.en.md) for the existing
boundaries.

## Reference: recovery fixtures and storage contract

The root-only installation authority manages `installations.conf` through
`begin`, `attach`, `commit`, and `remove` commands. A begin operation rotates
the
generation and fences the old app list; attach records each package app; commit
activates the completed list. Use `absent` only for a never-recorded package.
Each command also takes a unique operation ID and expected generation; retry
the same operation after an uncertain result. Register only after the real
package installation and authority commit have succeeded. For removal,
mark the installation removed in the authority first, then unregister consent
definitions using the same expected generation. The tombstone prevents new
authorization while package-wide cleanup proceeds. Reinstallation uses a new
generation. The utility is an
Installer integration surface, not evidence that platform hooks are installed.

The C API exerciser accepts `METHOD [PACKAGE [APP]] key=value ...`, `--async`,
`--timeout-ms=N`, `--repeat=N`, `--expect-status=N`, and
`--expect-decision=VALUE`. Mismatches return a nonzero exit status. For example,
after provisioning a trusted checker with the matching delegated context:

```sh
/usr/libexec/consent/tests/consent-api-test check \
  subject=org.example.agent profile=owner count=1 \
  r0.definition=calendar.read r0.operation=read r0.scope=today \
  r0.purpose=answer-calendar --async --expect-decision=CONSENT_REQUIRED
```

The `-isolated` tool links a separate static test client. `consentd-test` uses
the same role checks with socket/config paths under `/tmp/consent-test` and
persistent state under `/opt/var/lib/consent-test`; only its installation
inventory
adapter is substituted.
The production binaries have no runtime switch to enable that adapter. The
client unit test uses a mock transport peer and therefore tests the client
contract, not daemon policy or platform identity.

`scripts/emulator-scenario.sh` provides the isolated emulator phases. `basic`
requires fresh test installation state; it does not silently delete previous
results. Later phases cover persistence, running/stopped DB deletion, corrupt DB
recovery, stale DB replacement and same-UID role rejection. Run only on the
selected development emulator as root with the `System` security label.
`endpoint-fixture` is a separate manual endpoint-authentication fixture excluded
from CTest because it uses the actual `/run/.consentd.sock` path. Follow the
[validation evidence](07-verification.en.md) setup before using it.

Keep shutdown evidence separate for incomplete IPC input, a request waiting for
UI, and DB work that is actually pending. The manual `wire-scenario
--shutdown-wait` fixture proves closure of a connection
holding a partial header;
the supervisor also checks normal service exit and the `database-drained` log.
That alone does not prove a pending DB job was completed. A request awaiting UI
holds neither a DB transaction nor a worker, so its disconnect/restart behavior
is a separate scenario. See the recorded results for the exact tested boundary.

The separate `consentd-shutdown-test` executable supports a controlled
pending-DB
scenario. It uses the normal isolated daemon configuration and adds only the
`repository_shutdown_interposer.cc` test helper; production `consentd` and the
ordinary `consentd-test` do not contain that helper. The test controller
prepares
daemon-account-owned mode0700 `/tmp/consent-shutdown-gate` and a mode0600 FIFO
named
`shutdown-db-release`, keeping it open for reading and writing. A revoke that
actually changes a grant pauses before COMMIT and creates mode-0600
`shutdown-db-ready` containing `pid=N state=before-commit`. Match that PID to
the
test daemon, initiate service shutdown, then send the single byte `C` through
the FIFO within five seconds. Require the supervisor's normal-exit and
`database-drained` checks and inspect the committed revocation only after the
daemon stops. A gate error or timeout fails the transaction; reaching the ready
marker alone is not a successful drain test. This describes the test protocol;
executed PASS claims belong to the snapshot-specific verification record.

Keep process kills, orderly reboots and abrupt emulator power interruption as
separate test scenarios. Forced DB deletion must target only an isolated test
state or the selected development emulator's consent state. Do not remove any
other platform DB. Lost approvals require fresh approval; installed definitions
need a separately trusted reconstruction source. Permission, storage-full and
I/O failures must remain errors rather than silently wiping the DB.

The initial storage implementation uses one serialized SQLite connection,
`journal_mode=DELETE`, `synchronous=EXTRA`, foreign keys, and a 100 ms busy
timeout. The rollback journal belongs to SQLite; do not delete it separately
during recovery. A protected `definitions.registry` preserves definitions and
package-removal tombstones independently of the DB. An additional Installer
generation registry and platform `pkgmgr-info` bind definitions to current
installations. Reconstruction restores definitions only, never user approvals.
Missing or replaced live DB files retire the old handle and create a new epoch.
Ordinary restarts invalidate pending requests and sessions while preserving
eligible persistent grants. Holder cleanup remains outstanding until evidence
of deletion is received.

GBS builds the native Parcel client, daemon, C exercisers and installation
authority with GCC 14.2 and `-Werror`. Package checks cover client contracts,
repository policies/recovery, injected storage failures, process-kill boundaries
and IDL generation. Build-root tests are separate from emulator integration.
The [validation evidence](07-verification.en.md) records each tested snapshot,
executed results and remaining acceptance gaps. Mocked identities do not prove
product identity integration; real Installer, argo and UI policy integration
remains necessary.

The daemon marks eligible PERSISTENT/SESSION grants cacheable for at most 500 ms
and reconciles installation generations during its timer work. Each client
handle keeps at most 64 request-cache entries; SESSION entries require the
server-confirmed session and generation, and cannot outlive the supplied TTL or
session deadline. The cache is not shared across handles. Events, epoch changes
and disconnects invalidate entries. QUERY and AUTHORIZE always check the daemon.
The real approval UI, Installer lifecycle hooks and product policy need
integration; a protocol exerciser is not a production UI.

---

[Related task](api/05-sessions-and-data.en.md) ·
[Continue](07-verification.en.md) · [Reading paths](../README.md)
