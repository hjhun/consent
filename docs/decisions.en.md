# Implementation decisions

Date: 2026-09-20. These are PO/architect decisions for implementation, not claims
of completed code or validation. `CEP_Consent_Framework.md` remains the design
proposal. Record actual implementation and evidence in the paired developer and
architecture guides.

## Coordination and delivery

- Herdr pane `w1:pA` owns product scope, architecture decisions, review, and user
  communication. Existing Codex pane `w1:pJ` owns implementation and integration.
  Pane IDs describe this development session only.
- The implementation lead reports decisions with evidence, alternatives, and a
  recommendation to the PO pane, and continues independent work while awaiting a
  decision. Routine engineering decisions do not need another user approval.
- Preserve the CEP and `AGENTS.md`. The user's subsequent instruction authorizes
  commits and pushes for each completed, verified increment. The PO pane is the
  sole Git index/commit/push owner; implementation agents report ready file sets.
  Reference appfw repositories remain read-only.
- Every source file must carry the existing appfw-style Samsung copyright and
  full Apache-2.0 notice. Include headers, tests, tools, and language-appropriate
  notices in build/script files; an SPDX-only placeholder is insufficient here.
- Integrate foundation, approval/session flows, cache, and data lifecycle in
  reviewable increments. A partial increment is not full framework completion.
- Production code, test adapters, host checks, and real platform integration
  evidence must be distinguishable. Product integration gaps remain explicit.

## D-01: Authenticated roles and package ownership

Adopt a protected, default-deny role configuration backed by kernel peer
credentials, socket security label, and verified executable identity. A shared
UID, root UID, executable basename, or caller-supplied role is insufficient.
Use `pkgmgr-info` to validate the explicit package-name/app-ID relationship.

Configuration, executable, and parent-directory ownership and permissions must
prevent untrusted modification. Reject ambiguous executable identity, deleted
executables, required label absence, and invalid configuration. Process identity
must remain bound to the accepted peer lifetime; querying a reused PID is not
authentication. Check target support for pidfd or equivalent lifecycle evidence.

Discover real argo, Capability Manager, Context Engine, approval UI, Installer,
session-controller, and holder identities from platform evidence. Do not invent
paths, labels, or existing privileges. An unconfigured role remains denied.
Role checks do not replace subject/profile delegation, definition ownership,
enforcement-owner checks, or prompt/session binding.

The AMD Cynara socket-credential adapter is a useful platform reference. Select
dependencies only after checking target availability and policy. Test identity
adapters belong to separate build targets and isolated configuration/state;
production must not expose an authentication-bypass switch.

Local `libcynara-commons` source needs particular care: commit `3ca3518f`
(2025-07-01) removed process creation time from `cynara_session_from_pid()`;
the inspected implementation returns only a PID string despite the header
description. It cannot establish peer lifetime. The socket credential helpers
also declare themselves not thread-safe. Do not inherit either assumption.

## D-02: Serialized SQLite durability and recovery

Start with one SQLite-owning DB executor, `journal_mode=DELETE`,
`synchronous=EXTRA`, foreign keys enabled, and bounded busy waits. Verify pragma
readback on the target. EXTRA includes directory synchronization after rollback
journal removal; actual storage and reboot behavior still require testing.
See [SQLite synchronization documentation](https://www.sqlite.org/pragma.html#pragma_synchronous).

Use one recovery barrier shared with ordinary DB jobs. Detect missing/replaced
files and unusable storage during operation as well as startup. Retire old
handles before recreating the pathname, change DB generation, invalidate pending
work/caches/sessions, and prevent authorization from uncertain state. Do not
interpret permission, full-storage, or arbitrary I/O errors as permission to wipe
the database. Recovered user approvals must never be inferred from definitions.

## D-03: Definitions-only recovery authority

Dynamic registration needs a recovery source independent of `consent.db`.
Use a bounded, protected, daemon-owned definitions registry as the authoritative
desired state for definitions. The DB holds its transactional projection.
This choice does not claim an existing installed consent-manifest format.

The registry contains schema/revision, verified package/app/install identity,
registration incarnation, complete policy/translations, removal tombstones, and
hash-bound operation deduplication information. It never contains user decisions,
grants, usage, or sessions. Identical retries preserve the incarnation; removal
and reinstallation must not reconnect old grants to identical definition text.

The single DB executor performs this sequence:

1. Validate authenticated Installer authority, package/app ownership, request
   fingerprint, and current installation identity.
2. Write the next registry snapshot to a temporary file in the same directory;
   synchronize it, rename it atomically, then synchronize the directory.
3. In one DB transaction apply definitions, all related invalidations, and the
   applied registry revision. Publish success/events after commit.

These are two persistence boundaries. If the registry is durable but DB
application fails, fence authorization, report a storage/unknown-outcome error
as appropriate, and converge by replay/retry with the same operation identity.
A failed rename/sync does not justify pretending the old snapshot is authoritative.
Reconcile before READY or authorization, including when the DB itself is intact.

On DB loss, rebuild only definitions from the validated registry and require new
approval. Revalidate installed ownership and installation identity; uncertain
definitions remain inactive. Registry loss/corruption requires re-registration,
not invented reconstruction. Tombstones prevent restoration of removed
definitions even while their package remains installed. Total loss cannot prove
holder cleanup; surviving holders must invalidate and reconcile their state.

Use a protected installation-generation authority supplied by the authenticated
Installer. Each new installation rotates its generation; uninstall persists a
tombstone. Implement the format, validation, expected-generation binding, and
provisioning/test tooling here. Actual platform Installer lifecycle-hook
deployment is a separate integration dependency. Missing or mismatched evidence
must reject registration/activation explicitly. Second-resolution installation
time, version, and root inode/ctime are not sufficient proof of one installation.

Check DB pathname identity before work and after commit before publishing.
Store expected DB identity/incarnation in separate protected control metadata to
detect a valid stale DB substituted while stopped. Integrity checking or a UUID
copied with the DB cannot prove freshness. Arbitrary privileged in-place rollback
is not proven detectable by inode checks. Do not independently open/read/close
the live SQLite file for validation; this can interfere with POSIX locks.
See [SQLite corruption guidance](https://www.sqlite.org/howtocorrupt.html).

## D-04: Execution and public API contracts

Keep the CEP GLib/GIO model: main-loop lifecycle, bounded I/O contexts and short
worker jobs, and the serialized DB executor. Do not wait for approval while
holding a worker, mutex, or DB transaction. Use explicit ownership and shutdown.

Keep package and app as separate `consent_register()` arguments and remove by
package. Async request/check acceptance is separate from the decision. Deliver
accepted results on the owner dispatcher after API return, at most once and
without library locks; use explicit sources and the no-nested-loop contract.
GLib `invoke()` may run inline; see the
[GLib contract](https://docs.gtk.org/glib/method.MainContext.invoke.html).

QUERY does not consume or open UI. AUTHORIZE evaluates all conditions and
records one-time consumption atomically. Cache results never replace
authoritative execution checks. Session restart invalidation and holder cleanup
ACK semantics remain mandatory.

## D-05: Initial target and wire choices

The implementation panel reports x86_64, kernel 4.4.35, systemd 244 with SMACK,
GLib 2.80.5, and SQLite 3.50.2. Its verification records must capture the exact
commands. The old kernel does not provide the proposed pidfd path. Use the strict
socket-label/credential/executable adapter with process-start verification before
and after identity lookup and on requests. Document the remaining connect-to-first
lookup PID-reuse limitation; do not claim it is eliminated.

An initial root daemon and `0660 root:system_share` socket are accepted subject to
target group, participant DAC, and SMACK verification. Root does not bypass role
checks. Role configuration starts empty/default-deny until identities are proven.

The user's subsequent requirement replaces the initial GVariant choice with the
actual `parcel` library from `platform/core/base/bundle`. Use generated C++
`tizen_base::Parcelable` messages inside the four-byte big-endian length frame.
Call `SetByteOrder(true)` on both sides for fixed-width integer fields and lengths.
Keep public C ABI independent of the wire representation. Earlier GVariant build
results are intermediate evidence and do not validate the replacement protocol.

## D-06: Small IDL compiler and bounded Parcel decoding

Adopt a consent-specific IDL and a Python-standard-library compiler as permitted
by the user. Store source/schema under `src/`, generate deterministic C++
Parcelable code into the build directory, and share it between client and daemon.
Python is a build/test dependency. Use bounded strings/arrays, records, and
fixed-width integers; reject duplicate declarations, unknown types, invalid
bounds, and unsupported recursion. Generated files carry the full license notice.
Do not grow this into a general RPC runtime or replace the required GLib/UDS model.

Validate all primitive read results, lengths before allocation, remaining bytes,
UTF-8, string termination, duplicates, correlation, version, and trailing bytes.
The inspected Parcel `ReadString()` allocates from the wire length before
validating data and does not establish safe string termination. Use bounded
helpers built on Parcel's integer/raw-byte APIs for untrusted strings.
`ReadParcelable()` always returns success after its void virtual reader, so
generated objects must preserve and expose their own decode error state.

Avoid native structs, native-endian encoding and float/double. Keep the Parcel or
owned encoded bytes alive across partial writes. Handle allocation exceptions at
C ABI and GLib boundaries. Test deterministic generation, invalid IDL, known wire
vectors, truncated/oversized/malformed Parcel data, and actual emulator exchange.
The independent DB/registry canonical format must remain explicitly separate
from wire versioning. CEP v0.4 records this requirement without claiming completion.

## D-07: Target paths and client verification of the activation endpoint

The selected emulator resolves `/var` through `/opt/var`; use canonical
`/opt/var/lib/consentd` and `/opt/var/lib/consent-test` state directories. Keep
strict no-symlink traversal for protected state rather than weakening it to
accept an alias. The mandatory endpoint remains `/run/.consentd.sock`.

The target `/run` is `root:system_share` mode 0775. Permit its group-write bit
only for that exact directory and verified owner/group. Retain rejection of
world-writable parents and socket nodes, symlinks, and other writable parents.
Check root socket ownership and unchanged device/inode before and after connect.
These pathname checks alone do not prevent temporary replacement and restoration.

Before sending protocol data, also require connected kernel `SO_PEERCRED`
UID 0/PID 1, and `getpeername()` reporting exactly `/run/.consentd.sock` with a
validated AF_UNIX family, length and NUL termination. The peer name is the stored
bind address: renaming another systemd socket does not change it. This binds the
system manager's listener provenance to the consent endpoint, rather than trusting
PID 1 alone. A directly bound impostor fails the listener-credential check.
See [Linux 4.4 AF_UNIX implementation](https://github.com/torvalds/linux/blob/v4.4/net/unix/af_unix.c)
and [Linux peer credentials](https://man7.org/linux/man-pages/man7/unix.7.html).

Require the expected `SO_PEERSEC` value established by target observation and
socket-unit policy. A service process label or socket-file label does not prove
the listener's outgoing label. Verify the real credential/name/label tuple on the
target, plus rejection of a renamed other-service socket and a direct listener.
The implementation panel's target probe reports UID/GID 0, PID 1, credential
length 12, peer label `System::Privileged` (19 bytes including NUL), and peer
address length 22 including `/run/.consentd.sock` and NUL. Adopt that label for
this target; do not substitute the daemon's `System` process label.
Do not add an environment bypass. The test endpoint remains separately compiled.
The assumption is a trusted system manager, kernel and privileged unit policy;
these checks cannot prevent a writable-parent attacker from causing denial of
service. Record measurements and executed tests separately from this decision.

## D-08: Holder restart and cleanup authority

Keep data-use permission bound to the holder process instance. A replacement
process must not inherit an old acquisition receipt, active artifact, grant or
session. Cleanup needs a separate reconciliation path: an authenticated holder
with the same stable identity may discover and acknowledge outstanding cleanup
for its artifacts only after subject/profile and stored ownership checks.
Do not require the dead instance to remain present to complete cleanup.

This authority permits deletion reconciliation only. It never reactivates data
or extends expiry. Retain pending/failed status until an explicit, validated
holder ACK; a reconnect, missing process, or DB reconstruction alone is not proof
of physical deletion. Retry must preserve completed deletion and reject another
holder or unauthorized context. Test failed ACK, holder/daemon restart, retry,
TTL, and derived-data invalidation separately from the basic session-close path.

Expose bounded pending-cleanup discovery through the public C API, since a new
holder may no longer know its artifact IDs. Connect it to the same authenticated
cleanup-only repository path; the existing data-release API can carry its ACK.
Version the new durable ACK table as schema 2 and migrate schema 1 transactionally.
Preserve valid persistent grants/definitions and apply normal restart invalidation
to transient state. Unknown future schema versions remain explicit errors.

## D-09: Data provenance and final approval boundaries

Validate data-use and derived-data operations against stored artifact provenance,
including every source in multiple-parent and multi-level derivations. Caller
requirement fields are not a replacement for the recorded grant/definition links.
Check holder instance, subject/profile, session generation and active lifetime,
artifact retention, revocation, definition version and trusted installation
generation before creating derived metadata or returning usable data metadata.
Repeat the applicable checks after commit and before publishing success, since
the protected installation authority can change outside the SQLite transaction.
Do not depend on a later periodic tick to reject an obsolete installation.

Keep access consumption separate from retention: a consumed ONCE allowance does
not itself invalidate an already acquired artifact, and access-grant expiry must
not replace artifact retention policy. A derived artifact's synthetic receipt is
not an original acquisition receipt. Revalidation must never extend retention or
silently grant a new holder instance data-use rights. Failed publication checks
must leave any committed artifact unusable under the same provenance checks.

Keep the operation's DB epoch fixed from the initial successful storage check
through result publication. A final metadata snapshot can itself detect loss and
recover the DB; compare its epoch with the operation epoch before attaching it
to a successful result. An epoch mismatch returns an explicit error without the
old decision, receipt or permit. Never relabel an old success with a recovered
epoch, which would defeat the server's subsequent epoch comparison. Test actual
DB unlink during final validation, recovery to a new epoch, and fresh approval
requirements on the next operation using the same repository.

Within the UI response transaction, evaluate all current request conditions
again after recording the newly approved grants. Use non-consuming QUERY
semantics and derive the overall and per-condition final results together.
Previously allowed conditions may have been revoked, expired or consumed while
the prompt was open; never overwrite their current result with ALLOWED. Return
one final result without automatically opening another prompt. AUTHORIZE still
performs the atomic conjunction and consumption immediately before execution.

If re-evaluation finds a missing condition, finish the request as INVALIDATED
with a reason and retain its actual per-condition results. Explicitly approved
new grants remain valid even though this combined request cannot proceed. On UI
denial, retain current evaluations for existing conditions and mark the newly
refused conditions DENIED. Evaluation exceptions roll back the transaction and
return an error, never a fabricated decision. Later revocation does not rewrite
the historical result of an already completed request.

Required regression cases include generation rotation before the next tick,
rotation between commit and publication, one invalid source in mixed and nested
derivations, unaffected artifacts from other packages, and valid retention after
ONCE consumption. UI tests must change an already allowed condition while another
condition is awaiting approval and verify that QUERY does not consume grants.
This decision defines the repair contract; the build10 checkpoint explicitly
records these paths as incomplete until later implementation and verification.

## Initial review findings to verify before completion

- Recovery must retire/quarantine the applicable DB and journal artifact set
  after closing old handles, including startup with missing DB and surviving
  rollback journal. A fresh generation must not replay a previous DB's journal.
- Test offline substitution by a valid older SQLite DB; ordinary integrity checks
  cannot establish that decisions are current.
- A failed registry directory sync keeps authorization fenced until durability
  is re-established. Reading the renamed snapshot does not establish durability.
- Carry corruption classification from its detection point; later connection
  error state may be changed by rollback/finalization, and a failed integrity
  check can return a description without a SQLite execution error.
- Package removal needs a stable Installer operation ID and expected installation
  generation, without requiring an app ID. A delayed retry after same-name
  reinstallation must not remove the new installation. Reflect this in the new
  API rather than assuming package-name equality proves retry identity.
- The tizen-watcher metadata parser plugin demonstrates Installer install,
  upgrade, uninstall, and rollback hooks. A consent-owned plugin is a possible
  integration route without changing reference repositories, but the callbacks
  do not themselves provide a durable installation generation.

## Evidence and unresolved product settings

- AMD `29ed218b`: `src/modules/component-manager/src/worker.{h,cc}` shows explicit
  worker ownership and `Quit()`/`Join()`; `src/modules/cynara-core/cynara_manager.cc`
  shows socket-derived identity and platform authorization adapters.
- AUL `ac581e7`: `src/aul/launch_with_result.cc` transfers callback ownership under
  a mutex and invokes the callback after releasing it.
- AUL `ac581e7`: `src/aul/socket/packet.hh` and `socket/client.cc` show the existing
  Parcelable/write/send pattern. Bundle `5ef6073` (2026-01-14) already provides
  the integer, byte-order and reader-position APIs needed for the bounded codec;
  avoid relying on newer capacity constructors or UInt8 methods without target checks.
- Available tizen-watcher history starts with `a1de9f6` on 2026-02-02. Use it for
  requested layout/current packaging; use January AMD/AUL for the historical
  handwritten-style criterion.
- Target architecture, dependency versions, role identities, installation
  generation source, resource/time limits, service identity/labels, and external
  holder/UI integration must be established by implementation evidence.
- GBS RPM generation, emulator installation/API runs, fault injection, and reboot
  are separate validation results. Process kill or orderly reboot does not prove
  abrupt power-loss resilience. Do not mark CEP acceptance cases passed by review.
