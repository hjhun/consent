# Guide 07: Verification evidence and remaining scope

This is a foundation increment, not completed product integration. Results below
were observed on 2026-09-20 on the selected development emulator. Later working
tree changes are not covered by the frozen build unless separately stated.

## Frozen build 7

- GBS source tree: `10749441a5839db60024af9ca2d61c6a5164f73c` (tree, not commit).
- Command: `gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4 --overwrite`.
- Frozen evidence: `/var/tmp/consent-artifacts/gbs-build-7/` contains source tar,
  four RPMs, `log.txt`, `source-tree.txt`, `changed-files.tsv`, `SHA256SUMS`.
- Source tar SHA256: `debb4ef05a631f058897731bfb4f8b596a4884c5ed424226907c01fecb62b70e`.
- CTest: 4/4 PASS: client 0.28s, repository fault 0.10s, repository 0.86s,
  IDL compiler 0.12s. Release test assertions are enabled with `-UNDEBUG`.
- Tests use the real target Parcel library. The compiler tests cover invalid
  schemas, deterministic output, helper/type collisions and transitive bounds.
  Native codec tests cover golden bytes, truncation and bounded allocation.
- Public library export audit: exactly 38 `consent_*` API symbols, version
  `CONSENT_0.1`; no C++ implementation exports.

The later `ValidString` reserved-name regression and strengthened sticky SQLite
corruption regression are subsequent work, not evidence from this build.

## Actual emulator

Selected serial was `emulator-26101`, x86_64, kernel `4.4.35-x86_64`, systemd244
with SMACK, Parcel0.18.15. Select the current serial from `sdb devices` when
repeating; never assume this serial on another workstation.

Install consent/consentd/consent-tests RPMs. Repeated same-version development
replacement required `rpm -Uv --replacepkgs --replacefiles` for these exact test
packages. Tests run as root with `SmackProcessLabel=System`; production roles
remain empty/default deny. Isolated inventory omits real pkgmgr app lookup, so
its fictional demo apps are not evidence of product app/package integration.

```sh
sdb -s emulator-26101 push scripts/emulator-scenario.sh /tmp/consent-emulator-scenario.sh
sdb -s emulator-26101 shell 'systemd-run --wait --pipe --unit=consent-validation-basic -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh basic'
```

`basic` requires fresh isolated state. It does not erase an existing installation
registry. Subsequent phases use `persistent`, `running-delete`, `stopped-delete`,
`stale-replace`, `corrupt`, `unauthorized`, each with a fresh transient unit name.
The script stops consentd before external SQLite readback and requires exactly
`integrity_check=ok`, `user_version=1`, expected metadata and registry revision.

| Observed scenario | Result and boundary |
|---|---|
| C API basic | PASS: package/app registration, retry/conflict, SYNC/ASYNC callback after return, UI prompt/respond, persistent authorization dedup, SESSION grant, artifact cleanup ACK and CLOSED |
| Daemon restart | PASS: persistent approval survives normal shutdown/start |
| Normal emulator reboot | PASS: persistent approval survives; boot ID changed from `774896c0-0fc6-4683-b485-b193d19b28c9` to `b3e3c620-cedf-41be-8e98-6430431dc4aa` |
| Running DB deletion | PASS: unlink of idle open DB; definitions recover and prior approval becomes CONSENT_REQUIRED |
| Stopped DB deletion | PASS: same definitions-only recovery |
| Old valid DB replacement | PASS: pathname replaced using a different inode; old approval does not revive |
| Corrupt DB | PASS: corrupt main DB quarantined/recreated; no recovered grants |
| Same UID, different executable | PASS: server kernel-credential log says role=rejected; exit failure alone was not used as proof |
| Production activation/default deny | PASS: actual production client reached consentd; journal records pid23306 uid0 gid0 role=rejected |

Raw retained logs are in `/var/tmp/consent-artifacts/emulator-build-7/`:
`platform-production.log`, `isolated-daemon.log`, `recovered-database.log`.
Production uses journald; the isolated transient daemon's stderr was in dlog
under `STDERR_consentd-test`. The basic scenario first exposed an invalid Parcel
invalidation event (`status` copied into an event); build7 contains the fix.

## Production endpoint tests

Actual activated socket probe: `SO_PEERCRED pid=1 uid=0 gid=0 length=12`,
`SO_PEERSEC length=19 System::Privileged` including terminal NUL, `getpeername`
AF_UNIX length22 and `/run/.consentd.sock` including terminal NUL.

- Direct root fake server: production client returns -13 before sending hello.
- Another systemd socket renamed to consent's path: uid0/pid1 and the same SMACK
  label still pass those individual checks, but original kernel bind address
  `/run/consent-endpoint-other.sock`, length35, is rejected with -13.
- Normal production socket: endpoint authentication succeeds and server policy
  performs default deny, as distinguished by the role rejection journal entry.

The follow-up working tree retains the licensed probe/fake-server fixture at
`src/tests/endpoint-fixture.c`; it is not part of the frozen foundation source.
For this first run it was built with host `gcc -Wall -Wextra -O2`, copied to
`/usr/libexec/consent/tests/endpoint-fixture`, and labelled `_` with chsmack.
The production client/library remained the build7 RPM. The fixture will enter
GBS packaging in the subsequent build. `/tmp` is mounted noexec on this target;
do not place executable fixtures there. The follow-up `scripts/emulator-endpoint-test.sh`
reproduces the renamed-systemd-socket case and restores the production socket.
It temporarily replaces the consent endpoint and belongs only on a development
emulator. Stat pre/post equality alone is not the endpoint trust proof.

## Deliberate limits and subsequent work

No abrupt emulator power loss has been tested. Idle-open unlink is not deletion
during a write. Synthetic sidecars in repository tests are not proof of a real
hot rollback journal. Kill-before/during-commit/after-commit-before-reply,
concurrent ONCE/cancel/respond, live stale-cache invalidation and restarted-holder
cleanup require subsequent evidence. Creating a new client after recovery does
not test a live cache.

Production argo/CM/CE/UI/Installer identities and policy deployment, actual
Installer lifecycle hook integration, and real approval UI are incomplete.
The protected installation authority utility is implemented; it is not an
already integrated platform hook. Literal localized title/body and exact scope
matching are implemented; typed template/schema and resource-specific comparison
remain incomplete. Cache sharing across handles is absent. Registry loss fails
closed and needs a controlled trusted reseed workflow, which is not implemented.
The DB marker catches replacement, not privileged same-inode/same-incarnation
rollback; that requires a separate per-commit monotonic authority. Kernel4.4
fallback retains the documented first-connection PID lifetime race. Holder death
is bounded by the session lease unless a trusted controller reports it sooner.

## Observed command/output excerpts (build7)

The initial run had already durably registered a definition before the event
encoding fix closed the connection. After installing build7, the same protected
test generation and registration operation IDs were retried with this command:

```text
systemd-run --wait --pipe --unit=consent-basic-seventh -p SmackProcessLabel=System /usr/libexec/consent/tests/consent-scenario-isolated basic 3334e4f5-65f2-4a15-bd96-8bf585ce5530
PASS basic: registration/retry, SYNC/ASYNC, UI, authorization, session, artifact cleanup
Main processes terminated with: code=exited/status=0
```

`persistent` ran before reboot, the stopped DB was copied to
`/opt/var/lib/consent-test/approved-snapshot.db`, then `sync; reboot` was issued
through the selected sdb shell. After reconnect and `sdb ... root on`, the
script was pushed again because `/tmp` is volatile:

```text
systemd-run --wait --pipe --unit=consent-persistent-after-reboot -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh persistent
PASS persistent
PASS integrity_check=ok schema=1 expected_metadata_present
PASS emulator phase=persistent
```

The same command shape with unit names `consent-running-delete`,
`consent-stopped-delete`, `consent-stale-replace`, `consent-corrupt`, and
`consent-unauthorized` ran their corresponding phases. All returned status0;
the four recovery phases printed `PASS recovered`, the integrity assertion and
`PASS emulator phase=<phase>`. Unauthorized printed status=-2002 (connection
closed) and `PASS same-UID unregistered executable rejected`; its decisive
server log was pid3746 uid0 gid0 role=rejected.

```text
systemd-run --unit=consent-endpoint-direct -p SmackProcessLabel=System /usr/libexec/consent/tests/endpoint-fixture --direct
systemd-run --wait --pipe --unit=consent-fake-client -p SmackProcessLabel=System /usr/libexec/consent/tests/consent-api-test check --expect-status=-13
status=-13
error=permission denied
systemctl show consent-endpoint-direct.service -p ExecMainStatus
ExecMainStatus=0

systemd-run --wait --pipe --unit=consent-endpoint-rename -p SmackProcessLabel=System /bin/sh /tmp/consent-endpoint-test.sh
peer pid=1 uid=0 gid=0 length=12
security length=19 label=System::Privileged
address family=1 length=35 path=/run/consent-endpoint-other.sock
status=-13
error=permission denied
PASS renamed systemd socket rejected before hello
```

These are excerpts transcribed from observed tool output, not additional raw
log files. The persisted daemon/platform/DB log files are listed above.

## Build9: schema migration, cleanup and durability

Tree `e13e9e9b6709652663cf5ed51c58af28a6bca33f` built successfully with
5/5 CTest suites (client0.59s, crash0.25s, fault0.19s, repository3.49s,
IDL0.13s). Frozen artifacts are `/var/tmp/consent-artifacts/gbs-build-9/`;
source SHA256 `f7cf5f4f71f55ccdb2e214c3667d8e810ad4dd528585e18b9c9b560a9f43ae2b`.
The export count is now39 after `consent_cleanup_get_pending()` was added.

On the emulator, build7 created a v1 DB with one active PERSISTENT grant and
stopped. Installing build9 preserved that grant: the `persistent` C scenario
printed PASS, followed by `integrity_check=ok schema=2`. New definitions and
transient session reset follow the documented migration contract.

The `holder-restart` phase ran two separate processes of the packaged C tool.
The first closed a session leaving cleanup pending and handed over no artifact
ID. The second discovered it through the public pending API, could not register
old data, reported cleanup failure, rediscovered CLEANUP_FAILED, acknowledged
successful cleanup and repeated the ACK. All assertions and schema2 integrity
passed. This verifies control metadata and holder-reported ACK, not physical
backend deletion by a product holder implementation.

Package-wide removal blocked both apps. The Installer authority issued a fresh
generation, both apps registered again without old grants, and the previous
unregister retry returned -116. The script initially expected an API not-found
error for removed definitions; actual policy correctly returns status0/DENIED.
The corrected script was pushed separately and completed on unchanged build9
binaries. Logs: `emulator-build-9/holder-install-cache.log` and
`emulator-build-9/installation-cache.log` under `/var/tmp/consent-artifacts/`.

Live-cache probes on unchanged handles first requested within the original
lease, before an authoritative actor check: revoke37174us, policy update37975us,
SESSION suspend36035us. These passed. Closing the already suspended session is
not independent evidence for ACTIVE→close cache invalidation. The unregister
probe failed because its helper omitted required stable IDs; DB-loss coverage
in that scenario was not reached. A subsequent fixture fixes this; build9 is
not reported as a complete cache-scenario pass. Two independent ONCE contenders
produced exactly one winner and identical retry receipt; subsequent remote
cancel tests were likewise blocked by missing IDs in their new helper. Their
next-snapshot corrections are separate from daemon behavior.

The emulator also ran the packaged repository-crash-test and
repository-fault-test through `systemd-run --wait --pipe -p SmackProcessLabel=System`.
`emulator-build-9/crash-fault.log` records all passed cases:

```text
PASS SIGKILL boundary=1 validated_hot_journal=0 delete_during_write=0 decision=ALLOWED integrity=ok
PASS SIGKILL boundary=2 validated_hot_journal=1 delete_during_write=0 decision=ALLOWED integrity=ok
PASS SIGKILL boundary=3 validated_hot_journal=0 delete_during_write=0 decision=CONSENT_REQUIRED integrity=ok
PASS SIGKILL boundary=2 validated_hot_journal=1 delete_during_write=1 decision=CONSENT_REQUIRED integrity=ok
PASS directory fsync uncertainty stays fenced across retry and restart
PASS captured SQLite corruption code survives rollback
PASS metadata corruption cannot trap recovery in repeated failing SELECT
```

Boundaries1/2 interrupt a revocation before commit and preserve the previous
approval; boundary3 interrupts after commit but before repository reply and
preserves revocation. Boundary2 validates an actual rollback-journal header.
Unlink+SIGKILL is startup recovery, not a continuing writer. Build9's repository
fixture uses `/tmp` (tmpfs on this emulator); this is process-kill consistency,
not persistent-media power-loss evidence. The next fixture adds a chosen
persistent test root and a separately labelled continuing-writer case.

## Build10: completed target scenarios

Tree `1fddd2b2e38e45b79ce8ff12980b0fda2f2d6906`, source SHA256
`581d048b325c7194a29498ae220f6d862fbc7207277ce40086213bace38292d9`, is frozen at
`/var/tmp/consent-artifacts/gbs-build-10/` with four RPMs, build log,
`LastTest.log`, source archive/tree manifest and checksums. GBS passed5/5:
client0.59s, crash0.35s, fault0.11s, repository3.40s, IDL0.13s. This includes
corrected stable IDs in the C fixtures and a continuing-writer deletion test.

After installing these RPMs on the same emulator, the following commands
completed with status0 (each invoked through the selected sdb shell):

```sh
systemd-run --wait --pipe --unit=consent-races-ten -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh races
systemd-run --wait --pipe --unit=consent-holder-ten -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh holder-restart
systemd-run --wait --pipe --unit=consent-cache-ten -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh cache
```

- Two independent connections compete for ONCE: exactly one ALLOWED, the other
  CONSENT_REQUIRED; the winner retries with the same receipt. Ordered
  cancel/respond cases, a concurrent cancel/respond race and expired deadline
  each deliver one final callback and prevent invalid approval.
- Separate holder processes discover old pending cleanup without an artifact
  handoff, reject transferred registration, record a failed cleanup, retry and
  ACK successfully. Schema2 integrity and expected metadata pass.
- Both cache handles remain live throughout. The first actor request after each
  change runs before an authoritative actor check and before the original
  lease expires. Measured microseconds: revoke39618, policy46282,
  SESSION suspend35015, package removal39344, DB deletion68304. All pass.
  DB deletion changes epoch, loses approvals, restores definitions and permits
  fresh approval/cache use. The raw build10 output says suspend/close; the
  independent cache invalidation assertion covers **suspend only**. Management
  close of that suspended session is also checked.

Actual stdout is retained in
`/var/tmp/consent-artifacts/emulator-build-10/races-holder-cache.log`.
All three phases end with exact integrity/schema2/metadata assertions.

The manual wire fixture ran through `consent-wire-ten` while shell code checked
MainPID before/after. `/var/tmp/consent-artifacts/emulator-build-10/wire.log`
records unchanged PID11388 and a constant epoch across every hello, plus:

- Fragmented header/body and16 coalesced native Parcel frames pass.
- Zero/oversized length, missing array elements, huge string length, trailing
  bytes, middle EOF and incomplete-frame timeout close the offending socket.
- Exactly24 checker connections are admitted and4 refused; the same daemon log
  contains four `uid-connection-limit` reasons.
- Slow-reader pressure sends2116 complete38-byte frames (80408 bytes), then
  output-limit closes that connection. A separate client remains responsive
  throughout13 monitored hello exchanges. Actual daemon reason is
  `output-limit`, not a partial input timeout; PID/epoch are unchanged.

These malformed hello tests do not prove that arbitrary mutation requests can
never partially execute. The subsequent normal service stop is not evidence
of shutdown with pending DB jobs or partial I/O.

The storage crash fixture ran on persistent emulator state with:

```sh
systemd-run --wait --pipe --unit=consent-crash-persistent-ten -p SmackProcessLabel=System /usr/libexec/consent/tests/repository-crash-test --state-root /opt/var/lib/consent-test
```

`/var/tmp/consent-artifacts/emulator-build-10/crash-persistent.log` contains all
four prior SIGKILL cases plus:

```text
PASS live-writer boundary=2 validated_hot_journal=1 delete_during_write=1 decision=CONSENT_REQUIRED integrity=ok
```

For that final case the parent validates the real journal header, unlinks the
main DB while the writer is paused at main-DB sync, then releases the same
writer. The write cannot publish success; the next query on the **same
Repository** obtains a fresh epoch and no old approval. This is target
persistent-filesystem process-interruption/recovery evidence, not abrupt
emulator power loss. Test-only interposition does not enter production targets.

Remaining targeted work after this checkpoint: injected FULL/IOERR preservation,
partial-I/O shutdown, shutdown with pending DB jobs, independent ACTIVE→close
cache invalidation, and abrupt-power-loss testing. Product identity/Installer/UI
integration and typed localization limits remain as described above. Later
working tree tests/docs for these items are not automatically covered by build10.

### Newly identified gaps after build10 validation

Final review found that derived-data creation did not consistently revalidate
parent installation provenance before creation, and data-check/derived responses
lacked complete post-commit provenance revalidation immediately before
publication. External installation-generation rotation between those boundaries
can therefore make a metadata result stale. Build10 does **not** complete this
contract. A subsequent mandatory fix will share recursive parent/context/holder/
session/generation/retention/revocation validation and test generation rotation
between the validation boundaries. Build10 artifacts and observations remain
unchanged.

A multi-condition UI request can also finish with advisory ALLOWED for a
condition that was already allowed when the prompt opened but was later
consumed or revoked while another condition awaited approval. Final UI result
re-evaluation of all current conditions is incomplete. Authoritative AUTHORIZE
still reevaluates the required conjunction; request/result/cache is not a permit
to perform protected actions. This separate UI advisory gap is not counted as
completed by the build10 tests.

## Build11: failed fixture setup, no RPM

The source tree `f4414b0a1fac5861ef9a1d445b5e18471d773062` compiled, but
CTest passed only4/5: the new generic storage-failure fixture omitted its
required `operation_id` while creating a persistent approval request. The test
failed before FULL/IOERR injection. No build11 RPM or emulator validation is
claimed. The source archive, build log, `LastTest.log` and failure description
remain in `/var/tmp/consent-artifacts/gbs-build-11-failed/`. Subsequent source
adds the missing operation identity; successful later results belong to that
later snapshot.

## Build12: provenance, UI final evaluation and storage-error regressions

GBS source tree `bea3323de93eb4d843bad2293dc3ed790354e529`, source archive
SHA256 `5ffd2d2e5a9cf02a917cd281d1b40add34f95f735948e629cfe06db9d949fb6f`,
passed all 7 CTest suites. Frozen source, four RPMs, full build log,
`LastTest.log`, file manifests and checksums are in
`/var/tmp/consent-artifacts/gbs-build-12/`. The build command was:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

The tests ran with assertions enabled: client0.59s, crash0.35s, fault0.11s,
provenance1.60s, repository3.42s, UI1.56s, IDL0.13s. Both new repository tests
are packaged. The exact runtime/library/test RPMs were installed on
`emulator-26101`; subsequent working tree changes are excluded from this claim.
The emulator script was extracted from this tree, not the later working tree.

Actual commands used the following form, with separate units for each phase:

```sh
systemd-run --wait --pipe --unit=consent-ui-twelve -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh ui-reevaluate
# Repeat with consent-races-twelve/races, consent-holder-twelve/holder-restart,
# consent-cache-twelve/cache, consent-wire-twelve/wire.
systemd-run --wait --pipe --unit=consent-storage-twelve -p SmackProcessLabel=System /bin/sh -c 'set -e; /usr/libexec/consent/tests/repository-provenance-test; /usr/libexec/consent/tests/repository-ui-test; /usr/libexec/consent/tests/repository-fault-test; /usr/libexec/consent/tests/repository-crash-test --state-root /opt/var/lib/consent-test'
```

Results under `/var/tmp/consent-artifacts/emulator-build-12/`:

- `ui.log`: real C API/Parcel/daemon passes revoke, TIMED expiry, externally
  consumed ONCE, normal AND and DENIED cases. Each produces one final callback;
  subsequent prompt/response attempts fail. Newly approved B remains a single
  unused ONCE allowance. Missing A produces terminal INVALIDATED with its real
  condition result and a reason. The isolated storage UI suite additionally
  verifies UI-only identity, exception rollback and retry of the same token.
- `races-holder-cache.log`: independent ONCE contention and remote
  cancel/respond/deadline races pass; a new holder process discovers cleanup,
  reports failure and retries/ACKs. Live cache invalidation remains before the
  original lease expires: revoke37696us, policy37577us, suspend37560us,
  unregister34184us, DB deletion54128us. Independent ACTIVE→close cache
  invalidation is **not** covered by this build.
- `storage.log`: six provenance cases pass on the target. No-Tick B-only
  generation rotation blocks A+B descendants while another package remains
  usable; consumed ONCE and expired TIMED access do not erase independent
  retention or extend its TTL. Actual SQLite COMMIT-return hooks rotate
  generation for register/derive/data-check/reuse-data; no stale success is
  published and persisted children cannot be used. These are isolated injected
  authority tests, not real product Installer lifecycle integration.
- The same storage log records all seven UI scenarios and four fault cases,
  including FULL/IOERR_WRITE preserving DB inode, epoch, grants and quarantine
  inventory. Those fixtures use `/tmp` isolated state. The five crash cases use
  persistent `/opt/var/lib/consent-test` fixture subdirectories, including a
  validated hot journal, unlink+SIGKILL and same-live-writer unlink recovery.
- `shutdown-wire.log`: wire passes with unchanged daemon PID17458, exact
  24 admitted/4 rejected checker connections and actual output-limit reason.
  Pressure sent 2060 complete frames/78280 bytes; a separate client remains
  responsive. Every successful script phase asserts integrity exactly `ok`,
  schema2 and expected metadata before PASS.

### Build12 shutdown fixture failure and known publication gap

The strict shutdown script **failed**, rather than accepting a default success
status: after service stop, systemd garbage-collected the unit and a new show
query returned default `ExecMainCode=0` instead of the required `1`.
`shutdown-debug.log` preserves the traced assertion. The waiter journal and
`shutdown-daemon.log` do show PID17664 receiving SIGTERM, closing fixture PID17674
with `reason=daemon-shutdown pending_input_bytes=2`, then database-drained;
the fixture observes EOF and reports success. This does not satisfy the full
strict exit-status assertion and does not prove pending DB transaction drain.
The follow-up fixture will retain the observed unit objects before stopping.

Review after this snapshot also found an unresolved DB publication race:
`Execute` checks its epoch before provenance validation, but the final
`Snapshot()` may recover a DB deleted during that validation and attach its new
epoch to an old success result. This build does **not** close that race or
complete the DB deletion contract. The next snapshot must compare the final
snapshot with the epoch captured immediately after initial Ensure, reject a
mismatch without old success fields, and test real post-commit unlink plus
recovery without restoring approvals. This limitation is distinct from the
installation-generation/provenance fixes verified above.

Build13 working tree changes are not part of this evidence. Remaining targeted
work is the epoch fix, independent ACTIVE→close cache invalidation, strict
partial-I/O shutdown retry and an accepted DB mutation drained during shutdown.
Product roles/real UI/Installer hooks, typed localization and abrupt power loss
remain separate integration or acceptance gaps.

## Build13: epoch publication, ACTIVE close and shutdown acceptance

The agreed follow-up is validated from tree
`3299de2b1680d41e8655e4ad5e92ed8b92f53bd3`, based on commit `20043c1`.
The source archive SHA256 is
`6f477fe3d2a62260d9f6f8b36ee2a84f1bfbd1ff03b4804aa8100f06ef509a0b`.
`/var/tmp/consent-artifacts/gbs-build-13/` contains the archive, four RPMs,
checksums, tree/file manifests, exact command, full build log and `LastTest.log`.
The same GBS command as build12 passes 7/7: client0.59s, crash0.33s,
fault0.10s, provenance1.79s, repository3.42s, UI1.58s, IDL0.13s.
The installed runtime/library/test RPMs and emulator script are from this
frozen snapshot. The earlier observer trial used build12 binaries plus a
working tree script and is not substituted for this full build13 execution.

Actual emulator commands:

```sh
systemd-run --wait --pipe --unit=consent-partial-thirteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh shutdown
systemd-run --wait --pipe --unit=consent-db-drain-thirteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh db-shutdown
systemd-run --wait --pipe --unit=consent-cache-thirteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh cache
systemd-run --wait --pipe --unit=consent-storage-thirteen -p SmackProcessLabel=System /bin/sh -c 'set -e; /usr/libexec/consent/tests/repository-provenance-test; /usr/libexec/consent/tests/repository-ui-test; /usr/libexec/consent/tests/repository-fault-test; /usr/libexec/consent/tests/repository-crash-test --state-root /opt/var/lib/consent-test'
```

`/var/tmp/consent-artifacts/emulator-build-13/storage.log` passes all eight
provenance cases, seven UI cases, four fault cases and five persistent-root
crash cases. The two added publication regressions unlink the actual DB during
post-commit validation of data registration and data check. Each asserts a
completed target COMMIT and SQLite autocommit before unlink. The resulting
error has a new epoch and no old ALLOWED/receipt/permit/artifact fields. On the
same Repository, definitions are restored and a separate PERSISTENT approval
that was ALLOWED before deletion becomes CONSENT_REQUIRED. This closes the
specific build12 Snapshot epoch relabeling gap; it does not make the external
installation authority and DB one atomic store.

`api-cache.log` records real C UI/races/holder/cache execution. UI now also
asserts exact `-ESTALE` for re-prompt/re-response, equality of stored condition
results/reason, and no B grant after DENIED. Earlier contention/cleanup cases
continue to pass. Both cache handles stay alive. A **new ACTIVE SESSION** is
approved and warmed from DAEMON, sync CACHE and async CACHE, then closed from
the controller. The actor's first request, with no intervening authoritative
reply, returns SESSION_CLOSED at36322us, before the original lease expires.
Other event probes pass at revoke38613us, policy45617us, suspend42333us,
package removal42157us and DB deletion67106us. Every completed phase asserts
integrity exactly `ok`, schema2 and expected metadata.

`shutdown.log` records both strict shutdown cases:

- Partial input: observer target dependencies retain unit objects so exit
  status cannot reset through systemd GC. Daemon PID20401 closes fixture
  PID20412 with `reason=daemon-shutdown pending_input_bytes=2`, then emits
  database-drained. Both units have MainPID0, ExecMainCode1 and ExecMainStatus0,
  with Result=success. The fixture observes closure in182236us. This directly
  reruns and completes the strict assertion that failed in build12.
- Accepted DB job: a dedicated `consentd-shutdown-test` links the test-only
  SQLite interposer. A fresh marker matches daemon PID20540 after a nonempty
  revoke UPDATE and before its COMMIT, while autocommit is off. The supervisor
  sends SIGTERM, observes stop-admission, confirms that exact PID is alive and
  database-drained is absent, then writes C through an already-open O_RDWR FIFO.
  After release both daemon and C revoke process exit normally with the same
  strict status checks. The client reports OUTCOME_UNKNOWN because its socket
  closes before the result; the daemon reports database-drained. Restarting the
  ordinary daemon proves the previously ALLOWED PERSISTENT approval is now
  CONSENT_REQUIRED, with the definition intact and DB integrity `ok`.

The gate is confined to the dedicated test binary; ordinary `consentd-test` and
production `consentd` have no gate or runtime bypass. Build link commands are
preserved in `daemon-link-commands.txt`. The FIFO is protected and opened before
waiting; a five-second timeout and failure cleanup prevent an indefinitely
blocked test. These results establish graceful shutdown of this accepted
mutation, not completion of arbitrary remote holder cleanup.

The additional `wire.log` rerun retains daemon PID21200, admits exactly 24
connections and rejects four, and confirms the output-pressure closure reason.
It sends 2104 complete frames (79952 bytes) while a separate client responds.
`cleanup.log` records removal of observer/gate markers and restoration of the
ordinary isolated daemon executable. Isolated units are stopped; the production
socket remains active with its default-deny role configuration.

The four agreed follow-up items are covered by the frozen build and target
execution above. Remaining product/acceptance gaps include deployed real
roles, real approval UI and Installer lifecycle hooks, typed localization,
actual filesystem exhaustion/device write failure, sustained resource tests,
and abrupt emulator power loss. Earlier normal reboot, process kill and
injected SQLite error results retain their explicitly stated scope.

The final `wire` phase also passed (`consent-wire-thirteen`, `wire.log`):
same PID21200, exactly24 admitted/4 rejected, 2104 complete pressure frames
(79952 bytes), confirmed output-pressure close and a responsive separate client.
`daemon-symbol-isolation.txt` confirms with `nm -D --defined-only` that only the
dedicated shutdown binary defines SQLite step/exec interposers; neither ordinary
daemon does. `cleanup.log` confirms observer unit files and both DB gate files
are absent, the partial-input ready marker is removed, the isolated service
again points to ordinary `consentd-test` and is stopped with MainPID0, and the
production `consentd.socket` is active. Isolated test state remains for inspection.

## Build15: typed localization and approval-scope binding

The A-15 increment is validated from frozen tree
`58e4ab99cdefca6a3cdfbdfa61d8e0b255e7fd0d`, based on `13dcfae`.
Its 74-file source archive SHA256 is
`a50e60137c4448c510012a099a76c8dd53a3875d58b3bf4d57f4b6b154ba543b`.
The archive was compared byte-for-byte with that tree. Exact source, four RPMs,
commands, full logs, `LastTest.log`, manifests and checksums are preserved in
`/var/tmp/consent-artifacts/gbs-build-15/`.

Build14 is retained separately in `gbs-build-14-failed/`: tree
`4d727c091065f2433d3b816b95f1d291f6a5b54d`, archive SHA256
`c27cfb640bbd0f3847b7abd371d1754c6b3ee95cad20561fc64882b242a15071`.
Eight tests passed, but the new formatter test could not load `libconsent.so.0`
before RPM installation because build RPATH was disabled. Build15 supplies
`LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent>` only to CTest. The test still calls
the actual shared C ABI; production RPATH, runtime environment and identity
checks are unchanged. Build14 was not deployed or reported as target evidence.

The exact build command remains:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

Build15 passes **9/9**: client0.59s, localization0.00s, crash0.32s, fault0.10s,
repository-localization0.50s, provenance1.80s, repository3.45s, UI1.56s and
IDL0.14s. `binary-integration-audit.txt` confirms all 40 exported functions are
C `consent_*` APIs, including the additive `consent_prompt_format`, with no C++
exports. New tests retain `-UNDEBUG`; the test RPM includes both new unit tests
and the C scenario. Test-only SQLite interposition remains confined to the
separate shutdown daemon.

The selected `emulator-26101` is x86_64. The three runtime/daemon/test RPMs and
scenario script installed on it came from this exact frozen snapshot.
`/var/tmp/consent-artifacts/emulator-build-15/commands.txt` records deployment
and execution; `deploy.log` records installation. Actual target commands include:

```sh
systemd-run --wait --pipe --unit=consent-localization-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh localization
systemd-run --wait --pipe --unit=consent-localization-units-fifteen -p SmackProcessLabel=System /bin/sh -c 'set -e; /usr/libexec/consent/tests/localization-test; /usr/libexec/consent/tests/repository-localization-test; /usr/libexec/consent/tests/repository-ui-test; /usr/libexec/consent/tests/repository-provenance-test'
systemd-run --wait --pipe --unit=consent-ui-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh ui-reevaluate
systemd-run --wait --pipe --unit=consent-races-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh races
systemd-run --wait --pipe --unit=consent-holder-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh holder-restart
systemd-run --wait --pipe --unit=consent-cache-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh cache
```

`localization.log` records a successful real C API → Parcel → daemon scenario:
Korean/English, direct alias and default fallback; literal insertion of
`수신{name}%<tag>` without interpretation; explicit formatter ownership/error
behavior; capability, requested locale and latest-token binding. Malformed
schema/templates, noncanonical/out-of-range integers, invalid UTF-8, oversized
strings and independent display arguments are rejected. A scope30 approval
cannot satisfy scope90 AUTHORIZE, changed retries, receipt registration,
artifact or derived reuse. The original scope30 succeeds. A warmed typed cache
does not satisfy scope90; schema changes require a policy bump and invalidate
pending requests and grants. Legacy `count="01"` admission yields canonical
prompt `count="1"`. The scenario exits0, with integrity exactly `ok`, schema2
and expected metadata present.

`localization-units.log` records five formatter groups, seven new repository
localization groups, seven prior UI groups and eight provenance groups, all
passing on the target. The bounds test has a valid eight-schema/eight-placeholder
positive control in both locales before adding both the ninth schema and ninth
placeholder. Repository cases cover missing source versus explicit empty,
literal/typed count compatibility, and text revision monotonicity across policy
bumps and reinstallation. Changed message/default/alias maps require an
independent text revision increase. Tests first issue a valid token, then reject
invalid capability/locale, over8192-byte rendering or an alternate locale's
whole prompt exceeding64KiB, and successfully respond with the original token.
Schema/alias registry recovery restores definitions without approvals. Consumed
ONCE does not erase independently valid artifact retention.

`api-cache.log` records all five real C UI final-AND cases, two-connection ONCE
contention/stable receipt retry, remote cancel/respond/deadline outcomes, and a
new holder process discovering and acknowledging prior cleanup. Live cache
probes precede the original lease expiry, without an intervening authoritative
reply: revoke37898us, policy42727us, suspend40397us, independent ACTIVE close
38784us, package removal47397us and DB deletion65465us. Both actor handles remain
alive; each phase exits0 and asserts integrity `ok`, schema2 and expected metadata.

`cleanup.log` confirms isolated service/socket inactive, MainPID0, ordinary
`consentd-test` restored, no DB gate files, and production socket active with its
existing default-deny roles. Its initial hash command used the wrong `/usr/lib`
path; the subsequent RPM file listing and `/usr/lib64` hash corrected that
observation. No source fix was needed. Isolated test state is retained for
inspection. Final verification text and the four guide/protocol completion
paragraphs were updated after execution; these documentation-only edits do not
change the frozen tested source.

This closes the bounded template_version1 implementation and isolated A-15
verification scope. It does not implement ICU syntax, plural/date rules or
locale-specific numeric formatting, nor validate an actual product approval UI.
Product role deployment, Installer lifecycle hooks and registry-loss provisioning
remain integration gaps. Wire/shutdown evidence remains the separately identified
build13 execution; it was not rerun for build15. Abrupt power loss and actual
filesystem-full/device-write failure remain unverified, as previously stated.

## Builds16–19: public errors and the nonroot service

Build19 is the completed minimal unit for public-header relocation, Tizen error
mapping and `security_fw` service migration. Its source tree is
`0a2c5ae77841d4f503413dcff04a3984bb38f3db`; the77-file source archive SHA256 is
`c30cce5ea6f30056c124dfe06c853ed0cb59cea744f16d8dedb53c2ec03019c8`.
Frozen source, RPMs, CTest output and `delta-from-17.patch` are under
`/var/tmp/consent-artifacts/gbs-build-19/`. Only the isolated cache fixture's
expected database owner differs from build17: it resolves `security_fw` through
NSS and checks UID, GID, regular-file type, single link and mode0600 before unlink.
Offline registration and the subsequent feature-header split are excluded.

The exact build ran from the separate source copy, preserving ongoing work:

```sh
cd /var/tmp/consent-build-18-minimal
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

GBS reports10 passed and1 skipped out of11 CTest cases. The root-only ownership
fixture returns77 under the nonroot build user; it was subsequently executed as
real root on the emulator and passed. Signed module errors, including INT_MIN,
round-trip through actual Parcel/socket replies in the client test. The public
shared-library C test checks all40 original symbols, Tizen aliases, error strings
and output ownership. The numeric correction is for unreleased v0.1; old -200x
consumers require rebuilding alongside the library and daemon.

The intermediate results are retained without treating them as final evidence:

| Snapshot | Observed outcome |
| --- | --- |
| 16 / `12ea20c09045711da5090de4b917219e107792c3` | GBS10 PASS/1 SKIP; production nonroot startup and manual migration succeeded; isolated script incorrectly ran the label-setting helper as root/System and failed. |
| 17 / `c97287641bbb50527f2e90c0f64f6a31b377d189` | Uses the real privileged ExecStartPre+ preparation path and an explicit privileged authority writer. API/races/holder/shutdown passed; the cache deletion fixture still asserted UID0 and stopped. |
| 18 / `c4da55e730ded3b636281e661ccda27e82ada000` | Export-scope mistake: GBS used the original working directory despite a positional source argument and captured an in-progress header split. GBS10 PASS/1 SKIP, but not deployed or used as this unit's source. |
| 19 | Separate working directory, audited build17 plus exactly one fixture patch; actual nonroot target validation below. |

`emulator-build-16/migration-before.log` seeded a PERSISTENT grant with build15.
The actual privileged migration preserved test DB inode128283, registry128667,
installation authority128011 and registry/authority contents; the DB and registry
became UID/GID402 mode0600, while authority moved to root:402 mode0640/System.
`migration-manual.log` and `emulator-build-17/migration.log` show the same approval
still ALLOWED through the real C API. Production DB128673 and registry128674 also
retained their inodes. Subsequent authority writes intentionally publish a new
inode; this is distinct from the in-place migration observation.

The final target logs are under `/var/tmp/consent-artifacts/emulator-build-19/`.
`commands.txt` records explicit `sdb -s emulator-26101` invocations. Only the frozen
runtime, daemon and tests RPMs were installed in a normal dependency transaction.
The emulator's `capi-base-common-0.4.82-1` remains unchanged: matching devel was not
available, and cached0.4.83 devel requires its exact newer runtime. No `--nodeps`
or false Provides was used. Installed-tree C/pkg-config/40-symbol validation uses
the GBS SDK and frozen devel RPM; target evidence executes the installed shared
ABI, not newly installed target development headers.

`api.log` records exit0 for the public C API test and seven script phases:
unauthorized executable, ONCE/remote cancellation races, holder restart, live
cache, typed localization, partial-I/O shutdown and pending-DB shutdown. Each
phase checks stopped-DB integrity exactly `ok`, schema2 and expected metadata.
Live cache probes occur before their original lease expires: revoke34946us,
policy42909us, suspend33973us, independent ACTIVE close34606us, removal36163us,
DB deletion49045us. Both client handles remain alive. Shutdown confirms a real
two-byte pending header, `reason=daemon-shutdown`, normal exits and DB drain;
the revoke gate confirms stop-admission while the same PID remains alive before
release, then durable CONSENT_REQUIRED after restart.

`root-fixture.log` passes real ownership/inode/approval preservation, interrupted
transfer retry, repeated parent-fsync barrier after failure, and refusal of
conflicting authority, symlinks, hardlinks, foreign owners, unexpected entries
and a held lifecycle lock. Its compile-time SMACK/systemctl omissions remain
explicit; actual production helper startup separately validates those boundaries.
Authority provisioning runs explicitly with
`systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged`.
Root UID alone does not bypass MAC or role authentication.

`boot-before.log`, `boot-after.log` and `boot-api.log` record an orderly `reboot`:
boot ID changed from `b3e3c620-cedf-41be-8e98-6430431dc4aa` to
`1842a2f1-9a74-4a2e-985a-e7ab95dff79b`. Production service and socket were active
before client traffic, with MainPID2440, UID/GID402, label System and all observed
capability sets exactly `0x80000` (CAP_SYS_PTRACE). NoNewPrivileges=yes is the
systemd setting. The AMD-style relative basic.target.wants symlink and inherited
socket FD coexist; DB/registry inodes remain unchanged. A pre-reboot persistent
approval is ALLOWED after restart. Development SDB root mode and the frozen test
script were restored after boot reset the transport to owner and cleared `/tmp`;
initial permission/missing-script diagnostics remain in the logs.

`endpoint.log` observes PID1/UID0/GID0, credentials length12, peer label
System::Privileged length19 and AF_UNIX address length22 `/run/.consentd.sock`.
A production default-deny call is correlated with the server's journal
`role=rejected` / `no matching live trusted identity`; it is not claimed as a
client endpoint rejection. The first diagnostic queried the wrong dlog stream;
the corrected journal assertion passed. `cleanup.log` confirms isolated service
and socket stopped, MainPID0, ordinary test daemon restored, no observer/gate
artifacts and production service/socket active. Installed library, daemon and
test-daemon hashes match the frozen19 RPMs.

The verified service account is the existing `security_fw`, not a newly created
`security` account. Product roles, actual approval UI and Installer transaction
hooks remain unintegrated. Offline image registration is the next separate
implementation unit. Abrupt power loss and real filesystem-full/device-write
failure remain unverified; orderly reboot and injected storage errors do not
substitute for them. Earlier full malformed/quota wire evidence remains scoped
to its recorded snapshots; this unit reran endpoint and shutdown checks.

## Builds20–23: offline registration and feature C headers

Build23 completes this unit. Its frozen tree is
`971211be4af9187b9becb97d4cb919d6680ceee1`; all109 source archive files match
that tree byte-for-byte. Archive SHA256 is
`1f326beeb77693e6b9609a24643dffdd04e36b0c6beedc1869c915d03775fb5e`.
[Build artifacts](/var/tmp/consent-artifacts/gbs-build-23/) contain the source,
four RPMs, `snapshot.json`, `source-tree.txt`, `changed-files.tsv`, GBS/CTest logs
and checksums. The build used the repository working directory and this command:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

GBS passed12 tests and skipped4 root-only cases (16 CTest cases total).
`root-fixtures.log` records actual emulator execution of all four: storage
preparation, offline identity, offline registration, and image authority. It also
runs all20 repository offline regression groups; the transient unit exits0.
The private preparation fixture omits its production SMACK/systemctl operations;
actual target helper/service startup separately validates those operations.

The source implements10 independent public C headers under `src/consent/inc/`,
with `consent.h` preserved as the umbrella, and splits C wrappers by function.
The new41st exported function creates an explicit offline registration handle.
The same public `consent_register()` returns durable STAGED, without creating
consent.db or approvals. Other domain operations, including update, are rejected;
ordinary online errors never enable offline writes. CMake/spec license comments
are omitted; source notices and the RPM License metadata remain.

`public-installed-abi.log` uses only the frozen runtime/devel RPMs and archived
consumer tests. All10 installed headers compile independently, repeatedly and in
reverse order as C11/C++17. C and C++ consumers execute with the GBS SDK loader;
declarations, exported symbols and both consumers' references are exactly41,
with no exported C++ implementation symbols. Installed pkg-config resolves
`capi-base-common`. The SDK uses0.4.83 development files; target runtime remains
`capi-base-common-0.4.82-1`. Matching target0.4.82 devel was unavailable: no target
header-install claim, core runtime upgrade, `--nodeps` or fake Provides is made.

[Target evidence](/var/tmp/consent-artifacts/emulator-build-23/) includes exact
`sdb -s emulator-26101` commands and frozen copies of the scenario scripts.
Only runtime, daemon and tests RPMs were installed in a normal dependency
transaction. The selected emulator remains x86_64 Linux4.4.35, with the existing
security_fw UID/GID402 account.

| Evidence | Actual result and boundary |
| --- | --- |
| `root-fixtures.log` | Protected paths,0711 rejection, FIFO/nonregular files,128-record/4MiB bounds, version/hash/duplicate/tamper rejection, sync uncertainty/retry, root/thread/fork guards, real nonroot traversal, generation lifecycle and unchanged outside-image targets pass. |
| `root-fixtures.log` repository groups | Receipt dedup across DB loss, deterministic revision order, obsolete outcomes, same-generation unregister tombstone, other-package preservation, postcommit generation/DB change fencing pass. Typed malformed/I/O errors remain errors through import, Open and final Snapshot. Tentative invalidation rolls back; existing persistent approvals/revisions survive, and strict mode restores on success/exception. |
| `offline.log` | Actual C registration without a socket returns STAGED; retry/conflict and unsupported methods pass. Malformed and schema2 authority both fail startup with preflight -22 before DB creation. After valid-byte restoration, first startup returns CONSENT_REQUIRED for two apps and another package. Live lifecycle exclusion, repeat startup, DB deletion, unseen old seed after unregister and reinstall generation cases pass, with zero grants and integrity exactly `ok`. |
| `platform-offline.log` | The production daemon imports observed installed `org.tizen.calendar` app/package, but rejects a false package claim for observed `attach-panel-camera` and a stale generation. Stopped read-only SQLite inspection finds only the valid definition and zero grants. Product roles remain unchanged/default-deny; exact caller PID18676 and daemon PID18667 correlate the live rejection. The observed pre-hello status is OUTCOME_UNKNOWN, not an endpoint-authentication claim. |
| `regression.log` | Fresh isolated basic SYNC/ASYNC/UI/authorization/session/data cleanup, unauthorized caller, independent ONCE/cancel races, holder restart, cache, typed localization, partial-input shutdown, pending-DB shutdown and public ABI all pass. |

The fresh regression supervisor is retained as `regression-supervisor.sh` in the
artifact directory, outside the package source. It runs the frozen existing
scenario script with120-second per-phase bounds, holds the original authority's
exclusive lifecycle lock and preserves/restores state, authority and control
stores. The first cache request after revoke/update/suspend/independent ACTIVE
close/remove/recovery runs before the original lease expires (37518/42431/34197/
34553/38839/67470 microseconds). Shutdown evidence includes the actual two-byte
partial input and a mutation gated before COMMIT, stop-admission before release,
normal process exit and durable revocation after restart. It is not a new full
malformed-wire/quota or abrupt-power-loss run.

Successful actual commands include:

```sh
systemd-run --wait --pipe --unit=consent-offline-twentythree \
  -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-offline-test.sh
systemd-run --wait --pipe --unit=consent-offline-platform-twentythree \
  -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-offline-platform-test.sh
systemd-run --wait --pipe --unit=consent-regression-twentythree \
  -p SmackProcessLabel=System /bin/sh /tmp/consent-regression.sh
```

Intermediate results remain separate. Build20 passed its initial root/isolated
trial before the final ancestor and stored-metadata validation changes. Build21
passed GBS, root/ABI, actual pkgmgr and the listed existing regressions; its offline
negative-startup fixture stopped at `reset-failed` on a garbage-collected unit.
An attempted basic run also correctly refused nonfresh test state. Build22 changed
only that script (10 insertions/2 deletions), then reached the correct malformed
startup failure but stopped because target `cmp` was absent. Build23 replaces that
single comparison with two checked sha256sum calls and a digest comparison
(3 insertions/1 deletion). All production sources are identical from21 through23.
The successful22-binary/23-script trial is explicitly stored under
`emulator-build-22/trial23-script.log`; final23 results above use only23 RPM/scripts.

`cleanup.log` and `installed-rpm-hashes.log` verify original production DB/registry
inodes128673/128674, the original absence of installations.conf, restored isolated
stores and released lifecycle lock. Production service/socket are active;
security_fw402 has label System, CAP_SYS_PTRACE-only sets (`0x80000`) and
NoNewPrivileges=yes. Isolated service/socket are stopped, the ordinary test daemon
is selected, and observer/gate/temporary journal overrides are absent. Seven
installed binaries match the exact23 RPM payload hashes. Two diagnostics initially
used incorrect helper paths; the final check uses the packaged
`/usr/sbin/consent-storage-prepare` and exits0. Retained target evidence directories
are `consent-offline-evidence-18021`, `consent-offline-platform-18618` and
`consent-regression-evidence-18896` under `/opt/var/lib/`; they contain fixture
results, with original stores restored to their normal paths.

This completes the explicit root system-service/image registration API and
first-start reconciliation increment. It does not deploy a product Installer
transaction hook, actual approval UI or product role identities. Reconciliation
is startup-only; corrected/deferred authority requires service restart. No reboot
or abrupt poweroff was newly run for23; normal boot evidence remains scoped to19,
and power loss, real filesystem-full/device-write failure and automatic spool
pruning remain outside the verified claims. The final evidence paragraphs are
written after the frozen build and do not retroactively change its source archive.


## Build 24: GBS .NET/package baseline and retained UI trial failures

Build24 exported tree `84042ee0d4113006872fbc334fcf3d755d72fd76`, based on
`382dbe6`, with source archive SHA-256
`72cc199dddb01bd6a207ccffcfc7adcd298d5eebb83451f5602ef76a99513635`.
`/var/tmp/consent-artifacts/gbs-build-24` preserves the matching source, five RPMs,
18-test CTest report (14 PASS, four root-only SKIP), two TPKs, their build.json
records and GBS managed logs. GBS's SDK 8.0.421 compiled both applications from
source using its offline Tizen NuGet packages; three managed test groups passed.
TPK bytes extracted from the PoC RPM match both the GBS output and recorded hashes.
The positive development-signed TPK hash is
`c9a6d7baf7de23f66988f7c72e993562c3f18ff2537fc6482181fe23c6a1808d`.
SDK installed-tree tests separately passed ten independent C/C++ public headers,
41 exported API symbols and linked C/C++ consumers; target base-common runtime
remained 0.4.82, without a mismatched devel installation.

On `emulator-26101`, exact24 runtime/daemon/tests/PoC RPMs were installed with
normal dependency checking. Same-NEVRA development replacement required
`--replacepkgs --replacefiles`; no `--nodeps` or fabricated Provides was used.
`/var/tmp/consent-artifacts/emulator-build-24/maintenance.log` records all eight
packaged maintenance scenarios passing; the executable hash matches the RPM.
These maintenance sources were independently committed as `2f10ebe`.
The RPM registration service successfully installed the positive TPK, and the
negative TPK was explicitly installed for identity testing. The installed C
example demonstrated production role rejection (matching PID in daemon log),
and a separate root image fixture demonstrated offline STAGED/exact retry with
one spool record and no consent DB. These are not product-role positive tests.

Subsequent trials combine24 binaries with explicitly identified working25
scripts/typed mock fixtures. The initial runtime directory label blocked the UI
at SMACK traversal; after the approved leaf-only `_` preparation, the socket
probe observed UID5001/GID100 and exact `User::Pkg::org.tizen.consentui`.
The negative app used the same UID/loader but its own package label and was
rejected by the daemon at the matching PID. The real positive UI then connected
as UI but immediately failed with `InvalidOperationException` during display
and closed with DENIED. This is neither successful popup/button validation nor
60-second timeout evidence. Logs, initial failed screenshots and driver failures
remain under `emulator-build-24`; no later correction changes the frozen24 claim.
Working25 setup trials also verified refusal to configure while a probe override
exists, real notify-daemon identity after restoration, and cleanup after injected
PoC socket startup failure. The bounded driver helper/repeated-stop corrections
are subsequent source changes, to be rebuilt and exercised in the next snapshot.


## Build25: real button/API flow passed; popup placement still failed

Frozen tree `217d5969803cabde218dd86eaff66d24ab7bdc42` (base `2f10ebe`),
archive SHA-256 `a49cb135297f0bf18e6c6174ae3a8b68c5ef2b230139b5b8c358267d134a51a2`,
passed GBS14 native tests plus three managed groups, with four root-only skips.
Artifacts, RPM-extracted TPKs and the installed-header/41-ABI audit are preserved
under `/var/tmp/consent-artifacts/gbs-build-25`. Exact25 packages installed with
normal dependencies and the registration service updated the TPK successfully.

`emulator-build-25/allow-en` records actual Aurum Next and Allow once clicks,
then driver `finish-allow` PASS: one async result, CM QUERY, CE ONCE authorization,
identical-receipt retry and exhaustion, real holder fixture buffers with
registration/derivation/reuse, session close and deletion ACKs. No response API
was injected in place of the UI. However `page1.png` and `page2.png` show the card
clipped above/left of the intended position, hiding the header/language control;
this snapshot fails complete visual acceptance. `ui-fit.log` measures the new
24-pixel body at height180 within290, while the offscreen old18pt measurement is
natural width1370 and constrained height1330. It confirms the old text-fit
rejection separately from the new pivot/placement defect.

The original setup failure fixture checked cleanup but could also pass after an
earlier precondition failure. It is not sufficient proof that the injected
socket-start failure ran. The subsequent26 fixture adds a fresh protected marker
written by the failing ExecStartPre helper and requires it before checking
cleanup. `emulator-build-25/probe-marker26-trial.log` is explicitly a25-RPM/26-script
trial, not frozen25 test evidence. The layout/disabled-button and fixture fixes
require a new frozen26 build and final target execution.


## Build26: completed isolated UI, participant and packaging increment

The tested source is tree `91e17c2ec13802933083eb1fa27dbb36b87bd215`, based on
`2f10ebe`, with179 archive blobs matching the exported tree. Source SHA-256 is
`fe26530b6c4120817360614f807d8e0e8c7b5616afc7f1a64afa97a90f14d7cf`.
Artifacts are under `/var/tmp/consent-artifacts/gbs-build-26`; target evidence is
under `/var/tmp/consent-artifacts/emulator-build-26`. The GBS command remains:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all   -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

GBS passed14 native CTests with four root-only skips, plus all three managed test
groups. Its SDK compiled both .NET applications and generated the development
signed TPKs inside the build. Five RPMs, two RPM-extracted TPKs, matching build.json
and managed logs are retained with SHA256SUMS. The installable positive TPK is
`/var/tmp/consent-artifacts/gbs-build-26/org.tizen.consentui-0.1.0.tpk`, SHA-256
`8cae6fa2ec1521816ca99fba0d9a523c13e22e9f0394015d683a3e103e57dac5`.
The negative TPK hash is
`74fefbda2e0ba2e803a60a56f8d4216492616cc3ed4e490c2822fa19a83d51c6`.
The SDK installed-tree audit again passed10 independent C/C++ headers and exactly
41 C ABI exports/consumer references. Target `installed-hash-audit.json` verifies
ten installed native binaries/DLLs against the exact RPM/TPK payloads.

Exact26 runtime/daemon/tests/PoC RPMs were installed on the selected Tizen10.1
Common Emulator `emulator-26101`, with1920×1080 output. `rpm-install.log` and
`registration.log` record dependency-checked installation and the global TPK
registration service's Code1/status0, actual single-app pkgmgr relationship and
DLL digest. `probe-stop.log` observes UI PID47636/UID5001/GID100 and exact socket
`SO_PEERSEC=User::Pkg::org.tizen.consentui`; `configure.log` reaches the real
notify daemon. `configure-probe-rejection.log` confirms a leftover diagnostic
cannot be treated as that daemon. `probe-failure-cleanup.log` now also requires
the fresh marker written by the injected failing socket ExecStartPre helper.

Actual Aurum input was used for the approval controls; no respond API was injected.
The frozen host driver was invoked with the same serial/artifact directory across
`start-request`, `finish-allow` and `stop`, as documented in Guide08.

| Target evidence directory/file | Checked result |
| --- | --- |
| `allow-en/` | EN first page's disabled Allow ignores a click; Next works; language selection replaces the prompt and resets review; KO last page enables Allow. Actual Allow returns one callback, followed by QUERY, ONCE AUTHORIZE/identical receipt retry/exhaustion and holder register/derive/reuse/session-close/cleanup ACK PASS. Repeated actor stop also passes. |
| `deny-ko/` | Actual Deny yields one DENIED callback and QUERY remains blocked. |
| `stress-ko/` | A256-character unbroken W purpose spans three fully inspected pages, including the final retention text and buttons; actual Deny and matching QUERY checks pass. |
| `cancel-ko/` | Actual argo cancellation yields one CANCELLED callback, QUERY remains blocked and UI refresh dismisses the popup. |
| `timeout-en/` | No UI input after the visible prompt; after79.45 seconds the popup is gone, one DENIED callback exists and QUERY remains blocked. This is not daemon request-deadline expiration. |
| `back-en/` | Actual Back closes the popup, with one DENIED callback and blocked QUERY. |
| `negative-identity-retry.log` | PID53574 has UID5001 and the same protected hydra loader, but actual label `User::Pkg::org.tizen.consentui.negative`; the daemon rejects that same PID. A client error alone is not the acceptance evidence. |

`allow-en/page1.png`, `korean-page1.png` and `korean-page2.png` capture the complete
header, language selector, notice, body and footer. The latter shows the enabled
final-page Allow control. `page2.png` captures a refresh-in-progress disabled state,
not the final ready state. The screenshot coordinates are native emulator pixels.
`stress-ko/page1.png` through `page3.png` preserve the unbroken-value checks.
These are development-emulator screenshots, not a claim of physical TV acceptance.
`poc-extra.py` is a preserved supplemental **input fixture** for stress/cancellation:
it calls the unchanged frozen driver and real C mocks, with no UI response injection
or runtime authentication override. It is stored beside the execution evidence,
not compiled into the RPM. `result-summary.json` audits all six terminal decisions
and exactly-once callbacks from the saved mock JSONL.

`maintenance-correct-path.log` passes all eight packaged maintenance scenarios.
The initially mistyped executable path is retained separately as `maintenance.log`.
The installed offline C example passes durable STAGED/exact retry with one record
and no DB; the online check example is explicitly a default-deny result with its
PID53879 matched to production role rejection. Those example results are not
positive production-role integration. A first negative-process inspection missed
the short-lived process; the repeated paired identity observation is the final proof.

`cleanup.log` confirms all owned actors stopped, FIFOs and diagnostic overrides
removed, and the two temporary offline example image roots removed. PoC service
and socket are stopped; explicit PoC roles/state/authority and both installed TPKs
are intentionally retained for inspection. Production service/socket are active;
DB/registry device65026 and inodes128673/128674 remain owner402:402 mode0600.
Production installation authority remains absent and base-common stays0.4.82.
`aurum-cleanup.log` records removal of scoped forward55051 and termination of the
bootstrap started for this work. Production role policy was not relaxed.

This completes the isolated .NET UI/mock integration, build/package flow, public
API documentation/examples and bounded maintenance increment. Product role
identity deployment, production approval UI/Installer lifecycle integration,
registry-loss reset provisioning, arbitrary ledger/spool pruning, abrupt power
loss and real filesystem-full/device-write failure remain outside these claims.
No new reboot test was performed in24–26. The final README/Guide07/Guide08 text and
PO's removal of extra EOF blank lines from five mock C wrappers/eighteen INI files
were applied after frozen26; the EOF cleanup changes no tokens or behavior.
Other implementation sources remain the tested26 content. These final document
and formatting changes do not alter the archived26 RPMs or their hashes.

## Build27: feature-preapproval integration, incomplete target flow

GBS exported tree `db578048661ddb23c003de04386fd1838039e801` from base
`5f3364d604d1f8bfe3fd2da06fdc227ea8a0b71e`. All207 source blobs match
`/var/tmp/consent-artifacts/gbs-build-27/consent-0.1.0.tar.gz`; SHA-256 is
`943f72ef9e731b2d3ae026ae121ce18e62f340f3becd6dcc2787e0acf02688e0`.
The unchanged GBS command in `snapshot.json` produced five RPMs,16 passed and
four root-only skipped CTests, and five managed regression groups. Both TPKs
were compiled inside GBS. Extracted RPM payloads match the corresponding build
outputs: positive SHA-256
`5011726c97ccc79d1f05c6599d724eb8361e975905ed53168483c46e4540fb2f`,
negative `5f185fe5f19d2e392d7bc0f4d944985967c24624a875a01ce67af92925cb0005`.
`public-installed-abi.log` verifies10 installed feature headers independently
in C11/C++17, pkg-config and exactly42 exported/referenced C symbols, including
`consent_session_heartbeat`. This SDK audit does not install target-devel.

On emulator-26101, the exact27 runtime/daemon/tests/PoC RPM transaction and
positive TPK registration succeeded. Base-common remained0.4.82; production
DB/registry inodes128673/128674 and402:402/0600 ownership were preserved.
`emulator-build-27/feature-units.log` passes the packaged17 repository feature
scenarios, actor fixture/backlog deadline and public C ABI executable. Actor
completion/retry injection remains supplementary to actual application actions.
`endpoint-trial.log` and `endpoint-evidence` prove direct fake-server and renamed
alternate PID1 socket rejection with actual bridge -EACCES/NULL, no request
bytes received, successful fixture exit and restored feature units.

The actual Settings screen and four English review pages are in
`/var/tmp/consent-artifacts/emulator-build-27/feature-trial/`. They show the
complete selected tuple, access period and separate retention. The checkbox
glyph was unsuitable on the target font and is corrected in the next snapshot.
The first PREAPPROVAL stopped with -38: the server's direct hello path omitted
`approval_version`, although Repository's test-facing hello included it. The
client correctly refused the unsupported capability; this is an actual
integration failure, not a completed approval flow. `ui-pid-logs.log` and
`first-preapproval-failure.log` preserve the failure. The next snapshot adds
the field to the actual hello reply and an actual-wire assertion.

Read-only execution review also found that holder reuse combined authorization
and action without the actor's final selection check, and cancelled jobs dropped
late action results. Build27 is therefore **not final feature acceptance**.
The following increment separates reuse check/start, retains bounded retired
completion evidence and tests the actual target reuse gate. Build27's feature
service/socket were stopped and original PoC roles restored (`post-trial-state.log`);
production remained active. The scoped Aurum session remains in use for the
next increment. PO's Guide10 reproduction additions occurred after27 export.


## Build 28: preapproval succeeds; worker startup still blocks task execution

The frozen source is `2e6126f026a0a65fa94235511c2322e948b2b92b`, with
archive SHA-256 `cf13b1e750af5790e260dea115b945f1e9865222317341230b6f7f993086a314`.
All 207 source blobs match the archive. `/var/tmp/consent-artifacts/gbs-build-28/`
contains five RPMs, both GBS-compiled TPKs, build metadata and logs. GBS passed
16 CTests and skipped four root-only tests; all five managed test groups passed.
The SDK installed-tree audit passed ten independent C11/C++17 headers,
pkg-config and exactly 42 public C symbols. This does not install the unavailable
matching development package on the emulator.

The exact runtime/daemon/tests/PoC RPMs installed normally on `emulator-26101`.
Production DB/registry device/inodes `65026:128673` / `65026:128674` and owner
402:402 remained unchanged; capi-base-common stayed at 0.4.82-1. The positive
TPK SHA-256 is `bf9a1a13bd10aef3f1c878f04bf1334b14812bf5529fd08cca9643453760aa9e`;
the negative TPK is `b3079d1cddd4b89da98ffee675ac947387a02aaa9b02da1eaf84acd00c299d29`.
Both match the RPM payload. Target `feature-units.log` records all 17 repository
feature groups, four mock test groups and the public C API test passing.

Actual EN Settings review (four pages), approval review (five pages), and the
real **Allow as displayed** button completed SESSION PREAPPROVAL with ALLOWED
and action_count=0. Screenshots and paired journals are under
`/var/tmp/consent-artifacts/emulator-build-28/feature-en/`. This resolves build27's
missing real hello capability. The following calendar-summary task failed
before any provider action or CE daemon connection. Preserve that failure as
`first-task-failure.log` and `feature-en/task-result.png`; it is not a successful
feature execution or reuse test.

Read-only process evidence found the CE child exited with status1. An isolated
root/System probe using the coordinator's capability/NNP policy observed
socketpair SO_PEERCRED with the actual parent PID but SO_PEERSEC containing only
a NUL byte (`pair-probe.log`). Thus the required exact System peer-label check
correctly rejected the child channel. A protected temporary pathname
connect/accept probe returned actual parent PID/UID0 and System+NUL on both ends
(`connect-probe.log`); its socket was removed. A subsequent source/build must
repair that transport without relaxing authentication before execution or gate
acceptance can be claimed. Native injected actor tests do not cover this kernel
SMACK distinction.

`feature-stop.log` and `post-trial-state.log` confirm feature service/socket
stopped, no overrides, saved PoC roles restored and production daemon active.
No artifact was acquired; this is not holder cleanup-ACK evidence. The Aurum
session remains owned by this ongoing verification for the subsequent build.
Guide08's serial variable spelling was corrected after this archive; it uses
`CONSENT_SERIAL` consistently.

## Build 29: selected-feature preapproval and actual task execution

The completed feature increment uses frozen tree
`4400b206b611a881cbf10217a042c53fa0f4e503` (207 source blobs), based on
`5f3364d`. Archive SHA-256:
`aad23f34306a29e8d2d189bcfb4d9f2a099d5c85d11aeaf79debd76074b90542`.
Artifacts are preserved under `/var/tmp/consent-artifacts/gbs-build-29/` and
`/var/tmp/consent-artifacts/emulator-build-29/`. The final Guide07/08 evidence
and PO's Guide10 introduction/reproduction updates are post-archive documents;
the tested source, RPMs and TPKs were not changed after this build.

GBS compiled the native implementation and both .NET TPKs inside the build
root: 16 CTests passed and four root-only tests were explicitly skipped out of
20; all five managed test groups passed. The SDK installed-tree audit passed
ten standalone C11/C++17 public headers, duplicate/reverse includes, pkg-config,
and exactly 42 C exports/references, including `consent_session_heartbeat()`.
On the selected emulator, the packaged repository feature test passed all
17 groups and the public C API test passed both groups (`root-regressions.log`).
Its earlier wrong executable-path attempts remain in that log as failures.
The explicit packaged `--worker-channel` test passed with real System SMACK,
CAP_SYS_PTRACE and NNP (`worker-channel.log`): both-end kernel identity,
connected Parcel exchange, wrong-parent rejection, transient-stat retry and
owned-node cleanup. This closes build28's empty-label socketpair startup fault
without accepting an empty label or weakening authentication.

Five RPMs and both TPKs are retained. The four runtime/daemon/tests/PoC RPMs were
installed normally; the target's capi-base-common remains 0.4.82-1. Matching
0.4.82 development headers were unavailable, so SDK header/pkg-config results
are not presented as a target devel-package installation. Positive TPK:
`gbs-build-29/org.tizen.consentui-0.1.0.tpk`, SHA-256
`f2bce18e1df11d7d598d7101a1c39ff215350cfe5d8aa93ac764fe84df001647`.
Negative TPK SHA-256:
`117811b4fd74edf646edb94f70aeceb06c3776e4ec7b81a25ffc5164a91d102a`.
`installed-hashes.log` verifies 59 installed ELF/TPK/DLL files against the exact
RPM/TPK payloads and resolves all 42 public symbols from the installed library.
The expected hashes and reproducible audit are beside that log.

Build and scenario entry commands used for this increment are below. Reproduction requires the matching RPM install and explicit PoC setup from Guide08, and a fresh artifact directory; do not overwrite the preserved completed run.

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite \
  /home/hjhun/samba/workspace/consent
python3 scripts/emulator-feature-flow.py --serial emulator-26101 \
  --artifact-dir /var/tmp/consent-artifacts/emulator-build-29/feature-main \
  start --locale ko-KR
# Target: matching uploaded frozen29 script; isolated endpoint/state only.
systemd-run --wait --pipe -p SmackProcessLabel=System \
  /bin/sh /tmp/consent-scenario-29.sh wire
```

### Actual Settings, approval and execution

This is a 1920×1080 Public Common Emulator (`emulator-26101`,
`calendar-resolution-p5`), not TV-hardware acceptance. Actual application
buttons were operated through Aurum, with screenshots inspected after inputs;
no API approval response was injected. The bounded private bridge and real
libconsent/daemon were used, with isolated mock providers/holders. Evidence is
in `feature-main/*.png`, the phase logs, and `analysis-final-main/`. The latter
preserves the main coordinator journal before later gate tests/restarts and
contains 25 reproducible scoped assertions. Its raw journal SHA-256 is
`645d09c1baf580e9df075aedc77768330442473cfd75b75405b32c0f9ddcb354`.

All rows below belong to coordinator PID568853 and epoch
`selection-6255879d-c90d-426a-8406-f43ea8758e82`. The initial calendar selection
is revision2, digest
`9d801e44597b421bb63f57fbdb18c8a4daf731b3b8dbf9a548f4b8a5330b9b94`.

| Actual operation | Observed result and visual evidence |
| --- | --- |
| KO calendar SESSION preapproval | Two Settings pages, three approval pages, real Allow; revision2 ALLOWED, action_count0. `prompt-ko-1.png` through `prompt-ko-3.png` show feature/provider/exact target, purpose/recipient, conversation access and distinct result retention. |
| First task | Actual CE AUTHORIZE receipt, provider action and holder registration; action_count1. `task-complete-ko.png`, `task-first.log`. |
| Close Settings and reopen | Selection/session survived the UI process gap of more than the 30-second session lease because the actor heartbeat continued; subsequent task reused the same artifact, action_count2. `settings-closed.png`, `reopened-ko.png`, `reuse.log`. |
| Add device feature | Revision3 binds both choices (digest `27ee594b9facb55db22fb496b01422a1cfbad5434ab8fd1f1d95e2aa8525221c`); actual approval shows only the missing device condition, 1/1, across three KO pages. No action during preapproval; count remains2. `missing-device-ko-1.png` through `-3.png`, `missing-device.log`. |
| Calendar plus device task | Existing calendar artifact reuse then fresh device AUTHORIZE/start; exactly two effects, count4. `combined-review-1.png` through `-4.png`, `combined.log`. Effects are sequential, not an atomic multi-provider transaction. |
| Wider one-time calendar task denied | Actual next30 scope prompt and Deny; no wider action, count4 and saved revision3 remain. Task-only selection does not change Settings. `expanded-prompt-1.png`, `expanded-denied-click.png`, `expanded-denied.log`. |
| Already-authorized alternative | After unchecking task-only, explicit alternative uses the existing next7 artifact, without acquiring wider data; count5. `alternative-review-1.png` through `-3.png`, `alternative.log`. |
| EN device-only 30-minute preapproval | Four Settings and five approval pages, real Allow; revision4, duration1800000, ALLOWED and count5. `timed-review-en-*.png`, `timed-prompt-en-1.png` through `-5.png`, `timed-confirmed.png`, `timed.log`. This does not claim a 30-minute target expiry wait. |
| Clear all / close conversation | Empty selection saved as revision5 and ordinary task button disabled. Explicit conversation-close then holder buffer wipe/ACK: CLOSED, pending0, revision6, no artifact/session and count5. `clear-saved.png`, `closed-ack-confirmed.png`. |

The main artifact `2e531f904c95bd9226ba3402dbb78240f0c78f86ae020963`, original
receipt `0362f371dddabf8031538d1a1e3b0774969de824f95fbeb3`, and session
`1962ba3d75eece5e295cfd87055beda3bbccf6fc674dc07c`/generation1 remain identical
through all three reuse effects. The combined job is
`feature-0d5dd8c6-2fe8-4ad7-a56c-a6d4dd12000b`, with reuse operation `.0` and
device operation `.1`. The wider task was separately denied, not reinterpreted
as the alternative. An earlier EN eight-page Settings review expired before
save; it left selection/action state unchanged and is preserved separately
from the successful KO save and later successful EN TIMED run.

### Actual deselection gates and separate injected regressions

Both experiments use the separately compiled test coordinator and exact test
roles; the ordinary coordinator has no gate switch. The real UI removed the
calendar choice, reviewed the empty selection and saved revision2→3 while the
same coordinator/job was paused. Each gate directory contains screenshots and a
`protected-evidence/` subdirectory with `ready/observed/release/terminal/audit.json`
and `journal.jsonl`. Audit stdout is in the sibling files
`emulator-build-29/gate-acquisition2-audit.log` and
`emulator-build-29/gate-reuse-audit.log`.

| Gate | Proof, identity and result |
| --- | --- |
| Acquisition, `gate-acquisition2/` | PID576626, job `feature-0c6ac4d0-3971-47ed-994f-5a67837e81b9`, operation `.0`, actual receipt `71e236bf5d283b967965935d44ff0be3bb54c79b9c27b06d`. Cancellation preceded release; audit found zero matching action events. No artifact was acquired. |
| Reuse, `gate-reuse/` | PID577670, job `feature-87688740-97a0-4edd-9fea-a71eb5169ae7`, operation `.0`, actual successful reuse-data check for artifact `b1c04b2cc47fe5e9e33fbb231ed410e8a66776017cd5bc2b`, session `38b9fea733aac264e939ff26277ee5c5a2d62f37ed19f842`/generation1. This is an artifact-permit, not a newly issued acquisition receipt. Cancellation preceded release; zero matching reuse actions. |

Reuse proof binds context SHA-256
`ee337b6ac381f3c15a015e53a05803fd34633b503cabdebfcbf4b15235a2a84a`.
Its baseline had one real acquisition action; the counter remained1 after
blocked reuse. Before stopping or restoring the gate unit, the actual UI
closed that conversation while coordinator/holder were alive: buffer wipe/ACK,
CLOSED/pending0, revision4 and empty artifact/session are recorded in
`gate-reuse-closed.log` and `gate-reuse/closed-confirmed.png`.
Service termination alone is not used as cleanup evidence. The independent artifact analysis in `analysis-final-gates/` checks the same boot/invocation and marker/job bindings; acquisition and reuse releases preceded their deadlines by 2957ms and 975ms.

The first acquisition experiment (`gate-acquisition/`) exceeded the unchanged
30-second gate deadline during UI inspection. Its terminal was expired and
release failed; retain it as a fail-safe failure, not a passing deselection
gate. The second experiment passed without increasing the deadline.
Native injected CAS/retry/restart-epoch, lost-reply dedup, expiry and live/retired
late-completion tests are separately recorded in
`/var/tmp/consent-artifacts/feature-native/coordinator-29.log` and the GBS test
log. These validate bounded retention of already-started effects/errors and
uncertain outcomes; they are not claims of an actual target late-reply injection
or of rolling back an action that had already started.

### Endpoint, wire and final restoration

`feature-endpoint.log` and its protected evidence verify that both a direct
fake server and a different PID1 socket renamed to the expected endpoint are
rejected before any request byte. Exact29 negative TPK PID580603 used UID5001
and the same `/usr/bin/dotnet-hydra-loader`, but its real SMACK label was
`User::Pkg::org.tizen.consentui.negative`. `negative-probe.log` and
`negative-daemon.log` pair native error results with both the coordinator's
`ui-peer-rejected` and consentd's `role=rejected/no matching live trusted
identity`; transport errors alone are not counted as authorization proof.

The exact29 legacy `wire` scenario checks actual hello `approval_version=1`,
fragmented/coalesced Parcel frames, bounded malformed-frame close and deadlines,
24 accepted/4 rejected checker connections, and output-pressure close while a
separate client remains responsive. `wire.log`/`wire-daemon.log` confirm the
same daemon PID580802, quota rejection reason and output-limit/write-timeout
reason, followed by integrity_check=ok/schema2. This does not claim malformed
DB nonexecution or pending-transaction shutdown beyond those assertions.

`feature-stop.log`, `setup-stop.log` and `cleanup.log` confirm feature, PoC and
isolated service/socket inactivity, PID0, no overrides, ordinary PoC roles
restored, no transient worker socket, and removal of all three owned gate
FIFOs. Protected regular evidence and installed TPKs remain for review.
The owned Aurum forward55051 and bootstrap were stopped. Production service
and socket remain active under security_fw with default-deny roles; original
DB/registry device/inodes `65026:128673`/`65026:128674`, owner402:402, are unchanged.

The completed scope is isolated feature preapproval, missing-only approval,
exact action/reuse fencing and UI/mock integration. Product Settings/argo,
real provider/Installer lifecycle hooks and product role policy remain
unintegrated. Full registry loss still requires explicit recovery/re-registration;
bounded maintenance is not arbitrary ledger deletion. Abrupt power loss,
actual filesystem-full/device-write failures and TV hardware remain outside
this evidence; earlier storage/shutdown evidence retains its original build
scope. No extra production authorization or UI capability was introduced.
