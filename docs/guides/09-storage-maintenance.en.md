# Guide 09: Storage maintenance and recovery boundaries

The repository now compacts unusable metadata while retaining operation retries,
request results and holder cleanup evidence. It does not implement a retention
TTL, remove completed request/session/artifact rows, or recover a lost definitions
registry. The registry-loss workflow below is a design for a separate increment.

## Execution and transaction boundary

`Repository::Maintain()` is an internal C++ method owned by the serialized DB
executor. It is not a public C API, IPC method, root CLI or separate SQLite writer.
`Tick()` runs one batch after its normal integrity, definition reconciliation and
expiry work, no more often than once per 60 seconds. Opening the repository starts
that interval. Internal tests may invoke `Maintain()` directly without waiting.

One `BEGIN IMMEDIATE` transaction selects at most **128 logical records in total**
across four classes. The starting class rotates after each successful commit so
one class cannot permanently consume the entire budget. An authorization record
can have several grant edges; deleting those edges does not mean only 128 physical
SQL rows change. A newly unreferenced grant may become eligible in a later batch.

The batch limit bounds selected work, not elapsed time or history scans. Candidate
selection can still scan historical authorization, session or grant rows. Two
additive indexes, `authorization_grant_source(grant_id)` and
`artifact_grant_source(grant_id)`, support the correlated reference checks. SQLite
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

`get_prompt` stores its private displayed locale and nonempty UI ownership in one
UPDATE. Existing terminal transitions retain that ownership until maintenance
clears the columns and payload together. Thus cancelled, expired and invalidated
typed prompts are selected even after their token was cleared. Maintenance does
not reinterpret unknown request states or scrub malformed rows opportunistically.

`ReceiptValid()` rejects an invalid authorization before parsing its payload.
The retained unique key and fingerprint therefore preserve STALE for identical
invalid execution retries and CONFLICT for changed retries. A valid authorization
keeps its payload and source edges. In particular, a consumed ONCE grant and its
valid receipt remain available for the original execution retry; maintenance does
not create another allowance.

Artifact rows, `artifact_grants`, `artifact_parents` and cleanup acknowledgements
are retained, including DELETED artifacts. A live derived artifact has its own
materialized source-grant union and remains independent of its parent's physical
residency. This increment conservatively preserves both the union and parent
history. A deleted artifact's registration tombstone still prevents recreating
data with its old receipt, and a repeated successful cleanup ACK remains valid.

No registration receipt, removal tombstone, obsolete offline receipt, installation
authority receipt or offline spool file is deleted. No request-result contract or
cleanup evidence is shortened. No policy revision is published merely for this
internal compaction, and `cleanup_reconciliation_required` is never cleared.

## Failure and capacity behavior

All selected mutations commit together. A write failure rolls back earlier work
in the batch. Explicit maintenance reports failure; the timer logs the failure
and fences storage through the normal reconciliation path. The next scheduled
attempt is delayed by the same interval, including after an error. The explicit
method also checks the final snapshot epoch before reporting success.

BUSY, FULL and I/O errors do not authorize replacing the DB or discarding receipt
history. Existing explicit SQLite corruption recovery remains separate. An
uncertain maintenance outcome is safe to retry because the eligibility predicates
and retained tombstones make compaction idempotent.

There is no `VACUUM`, DB replacement, or promise that the file immediately shrinks.
SQLite may reuse freed space. Request/session/artifact rows and durable ledgers
can still grow; existing registry/spool capacity errors remain explicit. Arbitrary
TTL deletion of retry keys is unsafe: it could turn an old operation into a new
execution or revive a removed registration. Full pruning needs an explicit
retry-generation/acknowledgement/expiry contract and separate implementation.

## Verification scope

`repository-maintenance-test` uses isolated state and actual Repository API paths
to verify ONCE retry/consumption, STALE and CONFLICT preservation, unchanged request
results, live derived data after parent deletion, artifact nonrevival, repeated
cleanup ACKs, typed prompt cleanup, closed-session nonreactivation and a 129-record
fixture that leaves one authorization payload after the first 128-target batch.
Test-only SQLite interposition injects IOERR, FULL and BUSY after the payload
UPDATE and before the edge DELETE, proving rollback of the earlier write.
Test-only monotonic-clock advancement exercises the real Tick path, minute
scheduling, error fencing and a later successful retry. There are no production
fault flags or time overrides.

The new executable and existing repository, provenance, storage-fault and offline
repository regressions were compiled with SDK headers and run against host GLib
and SQLite as supplementary native checks. This is not a GBS/emulator, physical
disk-full, device I/O, power-loss or sustained-load verification claim. Target
execution belongs in [the verification record](07-verification.en.md).

### Build 24 target checkpoint

GBS source tree `84042ee0d4113006872fbc334fcf3d755d72fd76` passed
14 native CTests with four root-only skips. On the selected development emulator,
the exact packaged `repository-maintenance-test` passed all eight scenarios
under `consent-maintenance24.service`, exiting normally with status 0. Its
installed SHA-256 matched the RPM payload:
`a6f296239f60f401437a8a24e266584a843abaf6d60d08356f7ae5d2e083672e`.
The recorded output is
`/var/tmp/consent-artifacts/emulator-build-24/maintenance.log`; the source/RPMs
and build logs are preserved under `/var/tmp/consent-artifacts/gbs-build-24`.
This target fixture uses isolated state and injected SQLite errors; it does not
establish physical storage-full, device-write failure or power-loss behavior.

## Registry-loss recovery design — not implemented

The current implementation deliberately blocks when the definitions registry is
missing or corrupt while a DB remains. A healthy-looking DB cannot reconstruct
the independent trusted desired-state source or prove that old approvals are
current. Do not delete both files manually to bypass this block.

A future root-only helper should provide a resumable two-phase recovery operation:

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
5. Durably publish READY only after source/generation validation and completed
   quarantine. Ordinary daemon startup creates a new DB incarnation and epoch,
   revalidates pkgmgr and generation authority, and imports definitions before
   readiness. It restores no grants, consumption receipts, sessions or artifacts.
   A protected recovery marker must make `cleanup_unknown=1` durable before READY;
   otherwise an empty registry and DB would incorrectly look like a first install.

The implementation must preserve `cleanup_reconciliation_required` until a
separate evidence-based holder reconciliation protocol can clear it. Empty new
tables do not prove old physical data was deleted. Crash, rename/fsync failures,
partial quarantine, unsafe paths, stale seeds and actual installation changes
all need dedicated tests before this helper can be shipped.

Complete ledger loss also loses knowledge of some old operation IDs. The current
response/cache epoch alone cannot prove permanent deduplication of those unknown
IDs. Recovery must retain uncertain-outcome semantics and require fresh approval;
an across-reset exactly-once claim requires an additional durable retry protocol.

See [storage design](../design/04-storage-design.en.md),
[installation authority](03-installation-authority.en.md) and
[offline registration](04-offline-registration.en.md) for the existing boundaries.
