# Design 05: Implementation decisions

Date: 2026-09-22. These are PO/architect decisions for implementation, not claims
of completed code or validation. `01-consent-framework.md` remains the design
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
The [Installer handoff](../guides/06-installer-integration.en.md) records inspected callback
limitations and the external transaction/recovery contract still required.

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
The later non-root deployment requirement is specified in D-12; this paragraph
records the identity used for the earlier verified builds.

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

## D-10: Typed messages bound to authorization fields

Implement `template_version=1` as a bounded named-variable contract. A definition
may declare at most eight `parameter.<name>` schemas, each with an explicit
source, type and applicable bounds. Sources are the requirement's `scope`,
`purpose`, `recipient` or `operation`, or the definition's `retention_ms`.
Scope can be a bounded UTF-8 string or a canonical decimal integer; the other
request sources are strings, and retention is an integer. Do not accept
independent caller-supplied display arguments or arbitrary source expressions.
Every request-derived source must be explicitly present. A missing string is
invalid; an explicitly supplied empty string is allowed within its schema bounds.

The daemon obtains values from the stored request and registered definition.
These sources already participate in grant scope, policy version, retry/cache
keys and receipt/artifact checks. Changing schema, source, type or bounds changes
policy meaning and requires a higher `policy_version`. Typed requests must name
the matching requirement policy version. A lookback such as “last 30 days” uses
the actual query scope; it must not be derived from the result's retention time.
Version 1 performs no implicit unit conversion: retention values remain in ms.

Templates use only `{name}` substitution. Reject malformed/nested syntax and
require every locale's title/body variable set to equal the declared schema.
Bound templates, values, field counts, wire size and expanded output before
allocation or publication. Substituted data remains plain text even if it
contains braces, percent signs or markup. The public C formatter returns owned
strings with a documented release API and leaves outputs null on failure.
Canonical decimal output is the initial integer format; locale-aware plural,
date and number formatting remains a separate platform integration task.

Literal definitions remain compatible. A UI must explicitly opt into template
version 1 when requesting a typed prompt; an old UI must receive an unsupported
format error instead of an unexpanded typed message. Bind typed responses to the
displayed locale and latest token, rotating the token on every redisplay.
Registered direct locale fallback mappings precede the existing explicit locale
fallbacks and default. Reject fallback chains/cycles and unregistered targets.
Also reject an alias whose source already has a complete title/body pair, so an
exact translation cannot be shadowed by a fallback mapping.
Changing translations or fallback mappings requires a new text revision and
invalidates pending displays; semantic changes still require a policy revision.
For a stable definition ID, text revisions never decrease, including after
removal or reinstallation. Compare default locale, messages and fallback mappings
independently of policy version. Unchanged text can retain its revision; changing
that text map requires a higher one even when policy version also increases.

Verify Korean/English values, unknown/missing variables, wrong types/ranges,
unsupported versions, locale/token races, bounded expansion, and literal-data
handling. Prove that a 30-day approval cannot authorize a 90-day request and that
cache, retries, receipts and artifacts retain the same scope binding. Record
native unit, GBS and real C API/emulator results separately from this decision.

## D-11: Public C headers and Tizen error values

Keep the public C API in `src/consent/inc/consent.h`; this directory contains no
private C++ headers. Preserve the installed `<consent.h>` and pkg-config contract.
Include platform `<tizen.h>` and declare `capi-base-common` as a public pkg-config
dependency and build/devel dependency. Reference `core/api/common` commit
`0e569d4`, `include/tizen.h` and `include/tizen_error.h`.

Use corresponding Tizen constants for NONE, INVALID_PARAMETER, OUT_OF_MEMORY,
PERMISSION_DENIED, BUSY, NOT_FOUND, TIMEOUT and DISCONNECTED. BUSY maps to
RESOURCE_BUSY, NOT_FOUND to NO_SUCH_FILE, TIMEOUT to CONNECTION_TIME_OUT and
DISCONNECTED to ENDPOINT_NOT_CONNECTED; these preserve the existing errno values.
WOULD_DEADLOCK uses WOULD_CAUSE_DEADLOCK. Expose names for additional standard
errors returned by the API, including stale state and argument/frame limits.

The platform has no allocated `TIZEN_ERROR_CONSENT` base. Follow its documented
module-error rule: PROTOCOL, OUTCOME_UNKNOWN, SESSION_INACTIVE, SESSION_CLOSED,
CONFLICT and STORAGE occupy `TIZEN_ERROR_MIN_MODULE_ERROR + 0..5` in that order.
These are consent-local values, not a claim of platform-wide allocation. Preserve
their distinct recovery meanings; another API's `TIZEN_ERROR_STORAGE` is not an
appropriate substitute. Daemon and tests use the same public enum, and
`consent_error_string()` continues to describe the local errors.

This corrects the unpublished v0.1 numeric contract. Rebuild and upgrade clients,
library and daemon together; compatibility with old `-200x` consumers is not
claimed. Historical verification output keeps its original numbers. Parcelable
framing is unchanged. Verify C/C++ consumers, exported ABI, module-range and
standard-value assertions, GBS and actual IPC error paths.

The RPM spec retains `License: Apache-2.0` package metadata, without a license
comment header as the user requested. CMake configuration files also omit these
comments. Source-code license notices remain required. The subsequent header
organization requirement splits public declarations by function under `inc`,
preserving `consent.h` as an umbrella and moving private implementation helpers
outside the public directory.

## D-12: Non-root service and separated installation authority

The user requires a security-account service and an RPM-installed relative
`basic.target.wants/consentd.service` symlink, following AMD. The selected emulator
has no literal `security` account; its platform security account is `security_fw`,
UID/GID 402. Use that existing account as the interpretation stated to the user,
without creating an additional account. Record the actual name explicitly.

Keep `Requires/After/Sockets=consentd.socket` and the existing default dependencies.
Add `WantedBy=basic.target`; do not add `Before=basic.target`. The service starts
at boot and inherits the systemd listener. This is compatible with socket
activation but no longer means startup only on demand. The socket remains
root:system_share 0660; the client still verifies its PID 1/root creator, label
and original bind address independently of the daemon's runtime UID.

Daemon-writable state stays at `/opt/var/lib/consentd`: directory 0700, database
and definitions registry 0600 owned by the selected account. Check root-owned
ancestors and a daemon-owned state leaf separately from root-protected role and
executable paths. Put external installation authority in
`/opt/var/lib/consent-authority`, root:selected-primary-group 0750, with
`installations.conf` root:same-group 0640. Daemon access is read-only. Keep writer
locks, temporary files, rename and directory sync in that protected directory.

A root-only preparation helper validates paths, owners, file types and link
counts before migrating existing state, preserves DB inode/incarnation and
durable decisions, and changes the state directory owner last. Daemon and
authority writer hold a shared lifecycle lock; migration holds it exclusively.
Stop a legacy daemon before migration. Interrupted migration is retryable;
uncertain state or invalid links must prevent startup. Systemd StateDirectory
and RPM ownership handling must not preempt these checks by recursively changing
ownership. The helper must run with the privileges needed for preparation while
the long-running daemon remains restricted to its selected account.
The privileged preparation process may start with a different SMACK label.
Set `security.SMACK64=System` only on validated managed directory/file descriptors
and sync them before completion. Production labeling failures block startup;
only an explicitly compiled non-SMACK fixture may omit labeling. Re-run parent
directory sync on retries, and never alter lifecycle-lock metadata before taking
the appropriate lock.

Read-only target probes of `security_fw` with SMACK System and NoNewPrivileges
showed that CAP_SYS_PTRACE alone permits the required cross-UID executable
lookup; without it `/proc/1/exe` was denied. Restrict daemon ambient and bounding
capabilities to that capability unless new evidence requires a change. Do not
copy AMD's broader capabilities or add DAC override. Observed supplemental
groups are not authentication evidence. Preserve executable/starttime/SMACK
validation, default-deny roles and the documented old-kernel PID race limit.

Validate the actual selected account, migration preservation and rejection,
authority publication/read permissions, cross-UID real API flows, recovery and
normal boot. Earlier root-daemon evidence does not verify this deployment.

## D-13: Offline registration during image installation

This is an implementation contract; completion is recorded separately in the
verification guide. The installing system service must be able to call
`consent_register()` while consentd and its socket are absent during image
creation. RPM scriptlet handling or a definition-registration CLI alone is
insufficient.

Add `consent_client_create_offline_registration(image_root, &client)`. The explicit
registration-only handle permits `consent_register()` without connecting. Success
means a durable **STAGED** definition, not activation or approval. Other handle
operations, including update, return INVALID_OPERATION. Preserve process/thread
ownership and C ABI validation. Ordinary client creation remains online; permission
errors, invalid peers, timeout and uncertain sent operations never cause implicit
offline writes.

The initial writer is root-only; the actual system service identity is not yet
provided. Do not infer its executable, UID or SMACK role. Resolve the explicit image
root using protected directory FDs and no-follow traversal. Keep writes within its
canonical `opt/var` tree, without environment overrides or `consent.db` access.
A caller inside the image may select `/`; this still requires the lifecycle lock
that excludes a live daemon. Acquire locks before changing protected metadata.

Extend the generation authority tool with `--image-root`. The trusted image
installer uses stable IDs for begin/attach/commit and commits only after confirming
durable installation. Pass that generation to the public registration API. Preserve
expected-generation checks and operation receipts for existing authority. Missing
authority does not justify restoring old generations or assuming installation
success. This supplies a fresh-image path without replacing C API registration.

Versioned root-protected records contain package/app, operation ID, expected
generation, the complete validated definition and canonical fingerprint. Limits
are 64 KiB per record, 128 records and 4 MiB total. Same-ID/same-content retries
succeed; changed content conflicts. File fsync, rename and directory fsync precede
success; uncertain outcomes require the same ID retry. Host files are root:root
0700/0600 without target NSS or host SMACK requirements. Target preparation
validates and sets the actual read group and System label before daemon access;
production labeling errors remain failures.

The serialized DB executor shares registration validation/mutation while keeping
verified offline sources distinct from online authenticated callers. Do not invent
an Installer Peer. Revalidate actual pkgmgr app/package relationship and protected
active generation through the final publication boundary. Missing, pending, stale
or uninstalled generations remain inactive; unrelated valid packages proceed.
Malformed or improperly protected sources are errors. Keep deduplication in the
durable definitions registry so DB loss or reboot cannot replay older policy or
reactivate removed definitions. Record enumeration order must not determine the
result: resolve compatible revisions deterministically and reject conflicting
payloads without treating every conflict as stale. A same-owner, same-generation
definition with both revisions at least as high and one higher makes a seed
obsolete; unchanged revision axes must still have identical meaning. An inactive
definition cannot be reactivated by a seed for the same generation. Persist the
obsolete outcome and fingerprint so exact retries remain STALE and changed
payloads conflict. Reconciliation runs before READY at startup; deferred records
require a service restart after their installation authority is corrected.
Never store approvals, execution receipts, sessions, artifacts or application
data in image records.

Verify actual daemonless C calls, retries/conflicts, path isolation, interrupted
writes, first-start reconciliation, multiple apps/packages, stale generations and
DB recovery without approval restoration. This does not complete the external
production Installer transaction adapter.

## D-14: Developer documentation and isolated .NET approval UI

This is the implementation contract for the next increment. Target evidence is
recorded separately in Guide 07; the following requirements are not a completion
claim.

Separate numbered design documents under `docs/design/` from bilingual practical
guides under `docs/guides/`. Keep the original proposal distinct from implemented
behavior. The English project README is the entry point. Document the public C
headers in the style of the local Tizen device API, including descriptions,
parameters, errors, ownership, callback context and executable C examples.
Do not assign an unallocated Tizen platform API version or privilege.

Build a single-app `org.tizen.consentui` .NET popup TPK during the GBS build and
package it with the PoC components. A host-precompiled DLL is not evidence of a
GBS .NET build. The reference application structure is
`tizen-action-examples`; reference repositories remain read-only. Keep signing
credentials outside source and build logs. Emulator signing and installation do
not establish production distribution readiness.

Use a separate library, daemon, socket, state, installation authority and role
configuration for the interactive PoC and separate argo/Capability Manager/
Context Engine/holder mocks. Endpoint selection is compiled in. Production
default-deny enrollment and its trust checks are unchanged. The PoC must use the
real public C API, Parcel transport and daemon; report mock service integration
separately from actual product integration.

A shared .NET launcher executable alone cannot establish UI identity. Enroll only
the installed single-app package after observing its socket peer SMACK label,
combined with the existing UID, executable identity and process-lifetime checks.
Verify the package/app relationship and that the app UID cannot replace its
installed managed code. Test that another .NET package using the same UID and
launcher is rejected. Do not enroll a preloaded process's generic label.

One dedicated worker owns the native C API handles. Marshal display changes to
the UI thread. Poll `get_prompt` at a bounded interval to detect terminal or
invalid requests; do not grant the UI the argo-only result-query role. Dismiss on
terminal/error state and impose a maximum 60-second local display lifetime.
A refreshed locale must use its newly fetched display token and matching text.

The initial popup offers explicit allow-once and deny choices. Enable allow only
when every requested condition supports ONCE and every page has been reviewed.
Display complete registered text and its bound values without ellipsis or markup
interpretation. Validate the paginated text against the actual font layout at the
available content width; an unconstrained natural paragraph width is not a
clipping test. Reject content that cannot be displayed completely.
Back, close, failure and timeout
must never imply approval. Do not present missing holder cleanup evidence as
successful deletion. Verify actual popup clicks as well as separate
noninteractive mock scenarios.

## D-15: Receipt-preserving storage maintenance

Run a low-frequency metadata compaction batch on the existing serialized DB
executor. At most 128 candidate records are changed in one transaction, with
rotating priority between record classes. This bounds mutations per batch, not
query scan time or total retained row count.

Keep authorization IDs, execution keys, fingerprints and invalid receipt rows.
An invalid receipt may lose its unusable payload and grant links, but identical
retries remain STALE and changed payloads remain CONFLICT. Never refill an ONCE
grant. Remove a revoked grant only when neither authorization nor artifact
provenance references it. Preserve artifact rows, recursive provenance, cleanup
acknowledgements and all registration/request deduplication records.

Clear display tokens and private UI context only for terminal requests; clear
resume hashes only for CLOSED sessions. These changes must not alter public
results or policy revisions. Commit atomically and use the existing storage
failure fencing; I/O/full/busy failures must not trigger a destructive reset.
Do not VACUUM on the request path or claim that compaction permanently bounds
disk usage. Guide 09 records the behavior and limits.

Loss of the trusted definitions registry remains a startup error. Automatically
replaying old offline seeds after losing removal tombstones could resurrect a
removed definition. A future explicit recovery tool must quarantine the old DB,
registry and spool, accept a fresh trusted desired set with renewed installation
generations, and retain the cleanup-unknown boundary. No such reset is authorized
implicitly by the maintenance batch. Losing all durable retry records also loses
the ability to recognize unknown historical operation IDs; new approval is
required and an uncertain external execution must still be reconciled.

## D-16: Feature selection, missing approvals and conversation reuse

The TV interaction is: select features to preapprove, review the permissions
still missing for the current task together, then reuse approved access or
permitted results in the same conversation. This is the implementation contract
for a new increment; executed evidence belongs in Guide 07. Guide 10 explains
the integration responsibilities.

A feature is a user-facing grouping of an exact requirement vector, not a new
wildcard grant. A protected, versioned argo catalog expands selected feature IDs
into definitions, explicit policy versions, provider identities, operations,
scopes, purposes, recipients and holders. Bind the immutable selection ID,
revision and canonical digest to each request. Later catalog additions do not
join an earlier selection. Deduplicate identical requirements before applying
the existing maximum of 16 conditions. Feature labels do not establish provider
identity: use the registered definition's verified package/app and installation
generation, with protected catalog labels for human-readable names.

Keep the existing exact GrantKey. Feature IDs, catalog revisions, selection
metadata and requested grant periods must not replace its enforcement tuple.
Adding C to a feature containing A and B must not invalidate the reusable exact
approvals for unchanged A and B. A different provider, operation, purpose,
recipient, holder, policy or scope requires its own valid approval. Scope remains
exact equality; do not infer inclusion between arbitrary strings. An already
approved, narrower alternative must match an explicit authorized plan.

Use an additive `approval_version=1` request contract with
`request_kind=PREAPPROVAL|TASK`, `selection_id`, `selection_revision`,
`selection_digest`, a common `grant_mode`, and `duration_ms` only for TIMED.
Requirement rows carry feature metadata and explicit policy versions. Validate
and preserve these fields through admission, canonical request fingerprints,
stored payloads, prompt tokens and retries. The initial UI supports ONCE,
SESSION and TIMED, not a new persistent-approval shortcut. Mixed periods in one
batch are not supported: show the common period explicitly. Settings offers
SESSION and a 30-minute TIMED preset; reserve ONCE for an explicit task-only
selection so a reusable older grant cannot turn a one-time preference into
ongoing feature activation.

PREAPPROVAL checks whether an existing grant covers the selected period; an ONCE
grant cannot satisfy a SESSION selection. Anchor a requested TIMED coverage
horizon at the first admission plus the duration, not each refresh or retry.
TASK checks whether the exact permission is valid now and issues the selected
period only for missing rows. Thus an existing TIMED approval can serve a task
without an unnecessary SESSION approval. Bind response mode/duration to the
displayed request; calculate new TIMED grants from one response timestamp.
Store the coverage horizon privately after retry deduplication; do not add a
recomputed timestamp to the caller fingerprint. Apply the same kind-aware
coverage predicate at admission, projection, response and final AND evaluation.
Preserve the current TIMED range of 100 through 3,600,000 milliseconds.

SESSION requires a live conversation and matching generation. Setting a SESSION
label without creating that session does not create authority. Expose the
existing daemon heartbeat as a documented C API for authenticated session
controllers; it renews the lease within existing idle and maximum deadlines,
never reopens a closed session. The popup receives no session-controller role.
TIMED approvals can remain valid outside the current conversation and must not
be described as conversation-only. Access approval duration is separate from
registered result retention and the artifact/session/holder checks.

Preserve the complete original AND request. For capable UIs, compact only the
currently missing conditions for display and retain their original indices in
the token-bound snapshot. Remap typed arguments consistently. Issue grants only
for conditions actually displayed under that token; do not refill a condition
already satisfied by another request. An initially hidden condition that expires,
is revoked or is consumed while waiting must not be silently approved. Recheck
the entire original vector before returning the final decision. If A is no
longer satisfied, newly approved B may remain approved while the task request
becomes INVALIDATED, matching the existing non-consuming final evaluation.
When nothing is missing, finish without an approval popup. An old UI without the
new display capability cannot respond to this contract.

Opt-in requests always reach the daemon. Exclude requests containing
`approval_version` from client cache lookup and insertion, and return
`cacheable=0`; even an invalid version must not be hidden by a cache hit.
Advisory local cache results cannot establish a new selection's period,
admission snapshot or retry identity. Existing legacy cache behavior remains
separate. Negotiate approval-v1 support in the server hello before sending these
requests; a legacy daemon that ignores unknown fields must not silently downgrade
them. Reject unknown versions and approval-only fields without an opt-in version.
Actual protected actions still require authoritative AUTHORIZE.

Settings is a selection client, not an approval-request role. In the isolated
PoC, a private bounded bridge sends feature IDs, catalog revision and a permitted
period preset to a separately authenticated argo mock endpoint. Argo alone
loads the protected catalog, owns selection revisions, creates the request
through the public consent C API, and returns the request identifier. Use a
separate PID1-created systemd listener, `consent-feature-poc.socket`, at
`/opt/var/lib/consent-feature-runtime/argo.sock`, with `consent-feature-poc.service`. The UI verifies its kernel creator UID,
SMACK label, original bound address and protected path/inode; the server validates
the inherited listener and the UI's exact process/package identity. A target
probe showed that the app UID cannot inspect a root peer's proc executable, stat
or status. Do not add UI capabilities or claim bidirectional executable checks.
Reject caller-supplied roles and arbitrary requirement payloads. Keep this
transport separate from the production consent endpoint and enroll no production
role.
Selection commands also carry a stable command ID and expected selection revision
for compare-and-swap; delayed saves must not re-enable a removed feature. Also
bind mutations to the coordinator incarnation. An old command cannot be replayed
as new work after a restart clears the in-memory command ledger and revision
counter. Preserve uncertain submissions and their IDs for same-command retries;
never turn a lost response into an automatic new operation. Bind
task-only selections to a task identity and bounded lifetime. The two screens in
one TPK have the same application identity: app-control arguments and relaunches
are not user selection or approval. PID1 listener credentials authenticate the
protected endpoint, not a claim that PID1 is the running argo process.

For private argo-to-worker connections, the target kernel's socketpair probe
returned an empty SO_PEERSEC label even under System. Keep exact label checks:
use a transient root-owned 0600 Unix stream listener in the protected feature
runtime directory, validate both connected peers as the creating parent PID,
UID/GID 0 and System, then remove the pathname and close the listener before
passing a connected FD to the child. Bound connection setup and clean up on
failure. Retain the parent's executable, start-time and liveness checks in the
worker. This does not change the UI bridge or production endpoint policy.

An unselected feature remains inactive even if an exact reusable grant exists.
Selection removal, expiration and catalog changes invalidate obsolete pending
selection work. Authenticate the live UI process on each bridge call, but retain
an accepted selection under its stable application/subject identity after the
Settings process exits. The PoC coordinator may clear selection state on its own
restart; do not promise durable Settings preferences across that restart. Before each provider action, argo verifies the current immutable
selection and execution plan; the enforcement service performs AUTHORIZE for
the actual target and effects. Serialize selection updates, catalog changes and
action admission in the argo actor. Return asynchronous AUTHORIZE completions to
that actor and recheck the current snapshot immediately before starting an action
in the same serialized turn. A check followed by an unlocked queue handoff is
insufficient. Deselecting does not undo an action that already started. A task may explicitly select an otherwise inactive
feature for that task without enabling it permanently in Settings. Deselecting
a feature stops that feature's execution; it is not a global revocation of an
exact grant shared by other selected features. A permitted alternative must be
explicitly covered by the current selection and exact approvals and must not
execute the denied original operation.

Validate the whole flow with the GBS-built TPK, real public C API and isolated
mock services. Required cases include missing-only display, immutable catalog
addition, changed provider/purpose/recipient/scope, selection removal, period
mutation, pending revocation/expiry/ONCE consumption, no ONCE refill, retry
conflicts, same-conversation reuse and session end, bridge identity rejection,
and a permitted alternative that leaves the denied action unexecuted. Keep
production integration and emulator PoC evidence distinct.

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
