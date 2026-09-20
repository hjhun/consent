# Verification evidence and remaining scope

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
