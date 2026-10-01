# Historical verification record

This record preserves earlier build and emulator results. Each section
refers to its named snapshot, not the current checkout. For the current
summary, read [Guide 07](../guides/07-verification.en.md). Personal host paths
have been replaced with portable examples; original commands remain in the
external artifact archives. No tests were rerun for this document edit.


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
removal of extra EOF blank lines from five mock C wrappers/eighteen INI files
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
next increment. Guide10 reproduction additions occurred after27 export.


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
and Guide10 introduction/reproduction updates are post-archive documents;
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
  /path/to/consent
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

## Build 30: bounded cleanup traversal

Frozen source tree `46349c78b1f607d3100d1d0fc8928f3ae9eaee83`, based on
`d76b21b`, is preserved under `/var/tmp/consent-artifacts/gbs-build-30/`.
Release `0.1.0-2` packages use a normal upgrade NEVRA. GBS command:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

The first dependency expansion failed because the repository no longer supplied
fixed `csapi-tizenfx-nuget 14.0.0.19364`. The exact existing cached RPM was added
to the local GBS repository; its source/hash and the failure are preserved in
`cache-dependency.json` and `dependency-failure.log`. The successful retry passed
17 CTests with four explicit root skips and zero failures. New cleanup
GTest/GMock ran ten tests (391ms). Runtime/daemon/devel/PoC RPM autoRequires do
not include GTest/GMock; only consent-tests requires those test libraries.

On the rediscovered x86_64 emulator-26101, all five packages upgraded normally.
The initial four-package dry-run rejected the installed old devel package's
exact version coupling; adding the matching devel package passed without nodeps
or force. Target capi-base-common remains 0.4.82 and its already-installed
0.4.82 development provider satisfies pkg-config dependencies. Package install
reported existing ldconfig permission diagnostics; the normal transaction and
subsequent loading/tests succeeded. Evidence is under
`/var/tmp/consent-artifacts/emulator-build-30/`.

- `installed-hash-audit-batched.json`: 76 installed regular files match RPM
  payload hashes, including library, daemon, tests, public headers and examples.
  The first hash command exceeded SDB service-name length; the failure remains
  in installed-hashes.log. Symlink zero digests are excluded from content hashes.
- `cleanup-gtest.log`: ten packaged tests PASS, 47ms, service exit status0.
- `cleanup-api.log`: 97 artifacts registered through the C API with real isolated
  approval/authorization receipts. The first48 cleanup ACKs fail; continuation
  reaches all later49, then a fresh sweep retries the failed48 and empties the
  list. integrity_check=ok/schema2; service exit status0, 3.060s.
- `root-fixtures.log` and `image-authority.log`: all four fixtures skipped in
  abuild ran as actual root and passed against private fixture/image state.
- `public-abi-default-deny.log`: public ABI test passed. The first production
  probe expected PERMISSION_DENIED but observed DISCONNECTED; corrected
  default-deny-disconnect.log passes the observed transport contract. The daemon
  logged a rejected kernel-identified peer with no matching trusted identity.
  default-deny-actor-file.log records actor PID3539575/UID0; the paired
  default-deny-daemon-file.log records instance4 with the same PID/UID and
  rejection reason. No production role was added. An earlier shell-inline
  probe had systemd dollar expansion and an OUTCOME_UNKNOWN disconnect race;
  those logs are retained and are not the correlated successful probe.
- `installed-state.log`: production DB/registry retain device/inodes
  `65026:128673`/`65026:128674`, owner402:402 and mode0600. Production service and
  socket remain active under security_fw/default-deny.

The auxiliary host+SDK test passed ten tests in484ms. Successful compiler argv
and run environment are in native-increment30/successful-command.json; earlier
failed link commands/logs remain separate. This does not replace GBS/device data.
Build29's failed persistent baseline remains failed. Its successful 1000-query
smoke returns CONSENT_REQUIRED and service runtime1.845s includes process/client
startup and output; it is not per-call latency/cache/async performance evidence.

This snapshot verifies cleanup traversal. Final ownership/GIO/DLOG/storage and
normal reboot regressions remain subsequent work; no previous-build storage or
shutdown result is counted as this snapshot's new execution.


## Preserved Build31 failure and Build32 ownership/DLOG verification

Increment 2 froze Build32 tree
`8b78319ed8c73c4d3f7b8c1d609b989d56f0134d` on 2026-09-29. Build31 tree
`b9c5413af357ac727e5c03cde0b56a41d8673483` failed at %build linking because
its offline identity test lacked key_file.cc. Its archive, diff and log remain
under `/var/tmp/consent-artifacts/gbs-build-31/`. The target source connection
was corrected and frozen under a new number. Source stayed unchanged during
Build32's build.

Exact command, archive, diff, GBS log, LastTest.log, RPMs and SHA256SUMS are in
`/var/tmp/consent-artifacts/gbs-build-32/`:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

%check: 18 PASS, 4 explicit real-root SKIP, 0 FAIL out of 22 tests. Cleanup's
10 GTest/GMock tests and 13 ownership/logger tests passed. Supplementary native
13 PASS used SDK headers and host GLib/SQLite; they do not establish GBS or
emulator success. Exact successful argv/env and initial failures remain separate
under `/var/tmp/consent-artifacts/native-increment31/`.

Matching Release3 consent/consentd/tests/devel/poc packages were installed on
emulator-26101 x86_64. The first System-context install partially failed when
SMACK denied label and service-file writes. Original install.log is retained.
A normal rpm -Uvh --replacepkgs transaction under System::Privileged reinstalled
all five with service exit0 and matching 0.1.0-3 NEVRA. No --nodeps or production
role relaxation was used. GTest/GMock requires belong only to tests, absent from
runtime/daemon/devel/poc.

New target evidence is in `/var/tmp/consent-artifacts/emulator-build-32/`:

- Packaged ownership 13 PASS/12ms, cleanup 10 PASS/41ms and public C API ABI/
  ownership PASS.
- Fresh isolated basic, actual C API cleanup97, malformed/fragmented/pressure
  wire, partial-I/O shutdown and accepted DB revoke drain with durable revocation
  after restart passed.
- All four root skips were rerun using packaged offline registration/identity,
  storage prepare and image-root authority fixtures, each with service exit0.
- installed-hash-audit-batched.json matches 141 of 142 regular files. The prior
  PoC fixture's /etc/consent-poc/roles.conf is preserved by %config(noreplace) and
  recorded separately. installed-payload-audit.json matches all 141 remaining
  files. Production library/daemon/devel rpm -V also exited0.
- Production default-deny actor PID3549928 UID0 status=-107 is paired with the
  daemon's same PID/UID instance1 authentication rejection. Disconnect alone is
  not role evidence. Service identity remains security_fw UID/GID402, state700
  and DB600.
- Actual dlog_print output preserves I/CONSENT, PID3547174, ownership_test.cc:155
  source and literal percent=100%. This is separate from the GMock sink test.
  Daemon PID3548033's I/CONSENTD/server.cc output, partial input2 and
  stop-admission/database-drained are paired with stderr/journal fixtures.
  Global logs were not cleared.

The implementation adds transactional admission, FD/GLib allocation RAII,
bounded Dispatcher cancellation, noexcept callback/Close/Stop boundaries and an
allocation-free shutdown job. A preallocated I/O quit source preserves shutdown
before the thread enters Run. Wake sources survive producer joins and queued
callback cancellation. The Stop/drain test checks counters after the actual DB
sentinel and worker join.

Build29/30 evidence retains its earlier source scope. Client GIO migration,
matched persistent-handle performance comparison, registry-loss helper design/
implementation and final storage/reboot/CEP audit remain incomplete. Failed
performance fixture setup logs are not successful baselines. External product
identity/provider connections retain explicit requirements for actual sources.

## Build32 repository-tool baseline before GIO

The repository performance tool and extracted helper differ from the preserved
seventh prototype baseline. Their authoritative comparison baseline is separate:
`/var/tmp/consent-artifacts/performance-build32-tool2/baseline-first.log`
(exit 0, 19.218 s). It uses the Build32 isolated static client archive and packaged
`consentd-test`, not the production shared-library variant. Compile argv and
seed/binary hashes are in `compile-command.json` and `prepare.log` there.

Each API uses warmup 100 and measured n=1000. QUERY/AUTHORIZE/CHECK/D16 return
ALLOWED through the daemon; legacy sync/async report CACHE 1000 for this trial.
D16 approval_version=1 remains cacheable=0. Acceptance latency and callback
latency are separate; interval reciprocal and measured wall throughput are
reported separately. Idle context switches (79/2 s) are a process-wide proxy
with measured client plus supporting UI handle, hence two I/O threads.

`scripts/emulator-performance.sh` restores the isolated DB/registry/authority/
control from `/opt/var/lib/consent-perf-template-build32-tool2` before each
unique attempt and restores base runtime roles on exit. `--capture` creates a
new seed only after fresh basic setup and refuses to overwrite an existing seed.
The pre/post comparison must use identical tool/helper source, explicit gcc/g++
flags, seed, actor path and measurement conditions; only the compared archives
and daemon change. A CMake-built tool is not automatically flag-identical.
The seventh prototype and all failed attempts remain distinct evidence.
At this historical checkpoint Build33 GBS/device and post-GIO measurements
were pending; the completed results follow below.

## Build33 GIO verification and performance follow-up

Frozen tree `0b5c8ed5847c32b64c9462d47b76f73e64999136`, Release4, is preserved
in `/var/tmp/consent-artifacts/gbs-build-33`. GBS exit0: 25 CTests comprise
21 PASS, 4 explicit root SKIP, 0 FAIL (17.20 s). Only consent-tests requires
the three GTest/GMock libraries. Installed evidence is in
`/var/tmp/consent-artifacts/emulator-build-33`: normal matching five-RPM
upgrade, client/IO4/owner3/signal/ownership13/cleanup10/publicAPI and actual
root4 fixtures pass with service exit0. Installed regular files: 146/147 match;
the sole POC roles.conf difference is preserved `%config(noreplace)`.
Versioned public ABI symbols remain 42.

The first basic attempt failed the fresh-control-generation guard; preserve
`scenario-basic.log` as failure. After preserving old isolated directories,
`scenario-basic-second.log` reports phase PASS and service0. Cleanup97, wire,
partial shutdown and accepted-DB drain each report phase PASS and service0.
SDB transport exit is separate from systemd service exit in actual command JSON.
Default-deny first expect-107 failed because hello returned OUTCOME_UNKNOWN.
The second installed-library probe (PID3562808/UID0) returned -107 and UNKNOWN
for the two constructors, both NULL handles; daemon instances2/3 reject the same
PID/UID with no matching trusted identity. This proves authentication rejection
with connection/hello failure, not a PERMISSION_DENIED policy reply.
Target DLOG PID3560012 preserves literal100% and source/tag/level; shutdown
PID3560555 logs are preserved without global log clearing.

The exact tool2 Build32-to-Build33 pair and a second post trial are preserved in
`performance-build33-tool2`. Source/helper/flags/seed/actor path match the new
Build32 baseline. A separate controlled comparison in
`performance-controlled-build33` runs both client versions against the same
Build33 daemon: CPU1.72/1.66 versus2.37/2.08s, RSS5148–5240 versus7816–7988KiB,
idle process context switches79/80 versus1/1 over2s. All four trials pass with
ALLOWED and explicit CACHE/DAEMON counts. This does not equate the controlled
daemon scope with the original full-package baseline. Persistent CPU/QUERY cost
motivates an approved immediate nonblocking-write follow-up; sole causation is
not established. At this historical checkpoint Build34/Release5 verification
and performance were pending; completed Build34 results follow below.

Build34 follow-up native evidence is supplementary SDK-header/host-GLib testing
in `/var/tmp/consent-artifacts/native-increment34`. The first owner run failed
4 PASS/1 FAIL: one 30000-byte value was invalid before I/O. Its logs, failed
command and binary are preserved. The corrected fixture uses eight bounded
4000-byte values per frame. The second owner run reports5 PASS, including real
partial WOULD_BLOCK and exact ordered two-frame restoration; existing client
regression also passes. Successful second argv/env/exit are separate files.
GBS and target validation of this follow-up remain pending at this checkpoint.

## Build34 immediate-write evidence

Release5 frozen tree `0b4dc11144511bf55b63b82a7d95a4535dfa2dbf` is preserved
in `gbs-build-34`: GBS0, CTest21 PASS/4 root SKIP/0 FAIL (16.64 s).
`emulator-build-34` records normal five-RPM upgrade, client/IO4/owner5/signal/
ownership13/cleanup10/publicAPI/root4 service0, and fresh basic/cleanup97/wire/
partial shutdown/DB drain phase PASS plus service0. Installed regular files
146/147 match with the same sole preserved POC config; symlinks9/9 and ABI42
match. Default-deny PID3566797/UID0 returns two UNKNOWN errors with NULL handles
and same-PID daemon role rejection. Actual DLOG ownership PID3565699 preserves
100%; shutdown PID3566335 matches the journal evidence.

Exact-tool full-package trials in `performance-build34-tool2` use new archives
3b9ee069/c5a85e13, daemon5bf9fc06 and pulled actor b2bbefb1. CPU1.91/1.88s,
RSS7936/7880KiB and idle proxy1/1 differ from the earlier Build33 package pair.
A separate same-Build34-daemon comparison in `performance-controlled-write34`
runs before33/after34 clients: CPU2.15/2.16 versus1.89/2.23s. All trials pass
ALLOWED/source counts; this mixed result does not prove uniform CPU improvement
or identify a sole cause. The write path demonstrably avoids a readiness source
when an immediate nonblocking write drains output, preserving partial I/O.

The next approved increment skips I/O wake for already-completed cache hits,
while keeping caller-context delivery and noncache wake unchanged. Native,
GBS, installed and performance validation of that increment are pending.

Build35 cache-only wake native checks pass: owner6 (including cache-hit/miss
context readiness contrast and no remote bytes) and the existing client suite.
Exact successful argv/env/exit are in `native-increment35`; these are
SDK-header/host-GLib supplementary results. Release6 GBS/device and same-tool
performance remain pending. After this focused GIO checkpoint, proceed to the
separate 80-column checkpoint, reviewed registry-loss helper, and authoritative
final-source storage/reboot/CEP audit rather than speculative optimization.

## Build35 cache-only wake verification (2026-09-30)

Release6 frozen tree `07d663838c306aa2eda89727e7e90ffe6d98e2f6` is preserved
in `/var/tmp/consent-artifacts/gbs-build-35`. GBS exit0 and CTest25 comprise
21 PASS, 4 explicit root SKIP, 0 FAIL (16.98 s). Test libraries remain confined
to the tests RPM. The paired documentation corrections after GBS completion
are outside this frozen source snapshot.

`emulator-build-35` records the normal matching five-RPM transaction and actual
client/IO4/owner6/signal/ownership13/cleanup10/publicAPI/root4 service exit0.
Fresh basic, cache, cleanup97, wire, partial-I/O shutdown and accepted DB drain
each report phase PASS and service0. Installed regular files match146/147; the
sole preserved POC configuration has `%config(noreplace)`. Symlinks match9/9,
public versioned ABI symbols remain42, and security_fw UID402/storage700/DB600
are retained. Default-deny actor PID3574606/UID0 returns UNKNOWN and NULL for
both constructors, with the same PID/UID rejected by daemon instances1/2.
This is connection/hello failure following authentication rejection. DLOG
ownership PID3572539 preserves literal100% and source/tag/level; shutdown
PID3574521 is correlated with the isolated journal without global clearing.

Exact tool2 full-package trials in `performance-build35-tool2` use actor
`eea4147c`, daemon `75ae7a17`, unchanged source/helper/script/compile flags and
the same immutable seed. Both trials pass all9 metrics, n1000/warm100/ALLOWED,
legacy CACHE1000 and CHECK/D16 DAEMON1000. CPU1.99/1.88s, RSS8020/7812KiB and
idle proxy1/1 do not establish a uniform improvement over Build34. The client
is the isolated static archive variant, not the production shared library.

| Work | Current evidence | Remaining work |
| --- | --- | --- |
| Internal cleanup/ownership/DLOG/GIO/style | Build43 scoped GBS/device regression | Final CEP evidence mapping |
| Registry-loss boundary | Build43 receipt fence and DB-only recovery | Trusted current desired source, total-loss import and holder reconciliation |
| External product integration | Actual source and installed-package investigation | Installer lifecycle and authenticated role/provider/history/model adapters |
| Final validation | Build43 scenarios, fixtures and orderly reboot | Abrupt power-loss proof and CEP A-01–A-63 audit |

Earlier pending statements describe their historical checkpoints. The Build43
section below updates the implementation and storage rows; the listed remaining
work is still open.

The separate `performance-controlled-cache35` comparison uses the same
Build35 daemon (`75ae7a17`) for before34/after35 clients, with two alternating
trials each. All four pass9 metrics and the same seed/source-count conditions.
CPU2.16/2.17 versus2.11/2.15s and RSS7816/7884 versus7912/7896KiB show small
variation, not a broad performance improvement. The focused change avoids the
cache-only I/O wake as verified by owner6; further speculative optimization
is deferred in favor of the remaining implementation and final audit.

## Build36 mechanical style checkpoint (2026-09-30)

Release7 frozen tree `99abb4cea73186fbda07859641dcba0069d979cd`
contains 229 files. Its archive, byte manifest and target GBS export match in
`/var/tmp/consent-artifacts/gbs-build-36`. GBS exit0 and CTest25 comprise
21 PASS, four explicit root SKIP and zero FAIL (16.80 s). Only the tests RPM
requires GTest/GMock. Formatting preserves include/using order, decoded native
literals, generated IDL bytes, Python/C# structure and values, XML values and
GLib INI values. Frozen host proofs are in
`/var/tmp/consent-review-20260929/build36-*`; they supplement target tests.
Diagnostic line numbers and macro stringification can move. Thirty lines in
twelve project files remain above 80 columns because they contain indivisible
quoted shell/SQL arguments, CMake arguments, INI values, JSON IDL strings or a
service-unit path. Splitting them needs separate behavior review.

`/var/tmp/consent-artifacts/emulator-build-36` records the ordinary matching
five-RPM Release6-to-7 upgrade and eleven packaged test fixtures with service
exit0. Six fresh isolated API phases (basic, cache, cleanup97, wire,
shutdown and DB drain) report phase PASS and service0. The formatted offline
and platform-offline scripts pass actual target setup and teardown. The PoC
registration service installs the current TPK with matching digest and app ID;
the injected socket-start failure fixture removes its own override. Host .NET
UI unit tests pass five groups, and GBS produces both UI and negative TPKs.

RPM metadata lists 147 regular payloads; 146 installed hashes match. The one
different `/etc/consent-poc/roles.conf` is preserved by `%config(noreplace)`.
The installed public ABI remains 42 versioned symbols. The production service
runs as security_fw UID402 with state mode700 and DB mode600. Default-deny actor
PID3588992/UID0 gets negative UNKNOWN and NULL handles for both constructors;
daemon PID3587308 instances1/2 reject that PID/UID for lack of a live trusted
identity. This is connection/hello failure, not a policy response. DLOG records
actor PID3587548 with literal `100%`, CONSENT/INFO and source line160; no global
log clear was used. `systemctl show` parsed the changed PoC unit arguments.
The target lacks `systemd-analyze`, so no verify-tool PASS is claimed.

Build35 performance tool bytes remain frozen. Formatting changed Build36 tool
bytes, so Build35 numbers are not a same-source Build36 performance pair. This
checkpoint does not complete registry-loss recovery, product Installer/role
integration or the authoritative final storage/reboot/CEP audit.

## Build39–43 bootstrap and storage checkpoint (2026-09-30)

Numbered logs under `/var/tmp/consent-artifacts/gbs-build-{38,39,40,41,42,43}`
retain failures and successful retries. Build38 failed three unused-function
`-Werror` checks and Build41 failed two checked-write fortify checks. Build39
had a scenario startup failure and its start probe failed at cycle10;
Build40 failed at cycle14. Build42's after-exec diagnostic showed eight
output bytes after five milliseconds at cycle5. A repeated ready/HUP pipe
loop is the code-based inference; no revents trace was captured. It was not
a real two-second timeout or MainPID=0 response.
Build43's deadline/EOF correction preserves exact unit, cgroup and self-PID
checks. None of those failed runs counts as a PASS.

Build43 Release11 frozen archive SHA256 starts `17425aa2`; its 236 files
matched the GBS export and worktree at correlation time. These guide edits
are outside that freeze. GBS exit0 reports CTest26:
22 PASS, four explicit root SKIP and zero FAIL. The matching five-RPM
upgrade and 100/100 isolated stop/start cycles passed. The four skipped
root fixture classes ran separately on the emulator. The isolated bootstrap
script reports seven PASS, including missing-source no-mutation, DB-only
recovery, receipt loss and a forged foreign unit with inherited activation
FD. The isolated POC classification script reports three PASS, including
exact ExecStartPre refusal and unchanged real POC files. This does not prove
an actual absent-state POC Release7 upgrade or a clean production first-install
RPM transaction.

Fresh isolated `basic` and sixteen scenario phases each report internal
`SCENARIO_EXIT=0`, phase PASS and SDB exit0. Seventeen packaged native
fixtures report service exit0. The frozen offline-image script reports
`OFFLINE_EXIT=0`; the production platform-offline script reports
`OFFLINE_PLATFORM_EXIT=0`, validates real pkgmgr identity and restores the
original production DB, registry, authority and receipt inodes. Neither
fixture supplies a product Installer hook or current desired source for
total-registry recovery.

The installed RPM metadata lists 151 regular files; 150 target hashes
match. Only `/etc/consent-poc/roles.conf` differs under
`%config(noreplace)`. The installed library exports 42 `CONSENT_0.1`
versioned public symbols. A current packaged API check returns UNKNOWN
under default deny. A separate legacy constructor probe returns UNKNOWN
and NULL handles twice for actor PID3622926/UID0; current daemon
PID3621672 instances2/3 reject the same PID/UID as having no live trusted
identity. This is connection/hello failure, not a policy decision.

An actual orderly reboot changed boot ID `1842a2f1…` to `e55266b3…`.
All five Release11 RPMs remained; consentd PID2565 became ready and active.
Registry and root receipt hashes/inodes stayed unchanged. The DB retained
its inode but had a different hash after reboot; `PRAGMA integrity_check`
returned `ok` under `System::Privileged`. SDB root mode had to be restored
after reboot before protected-file inspection. This proves orderly reboot
startup/integrity. A separate fresh isolated fixture checked its PERSISTENT
grant as ALLOWED before a second actual reboot (boot ID `e55266b3…` to
`75d2b4dc…`). After reboot, the matching frozen script again checked it as
ALLOWED with DB integrity `ok`; registry and receipt hashes/inodes remained.
The first attempt failed because `/tmp` was cleared. Re-pushing the frozen
script produced the successful run. This is an isolated orderly-reboot grant
test, not abrupt power-loss durability or product role integration.
Build43 running/stopped DB deletion, stale replacement, corruption,
shutdown and DB-drain scenario PASS records use isolated state; repository
crash fixtures use `/tmp`.

Total-registry recovery, physical `cleanup_unknown` resolution, real
Installer/role-provider/history/model integration and the final CEP
A-01–A-63 evidence audit remain open. Production `--begin` returns a
missing-source error without mutation until a validated desired-definition
producer exists.

Build43 evidence files: `source-correlation.json`, `gbs-success-log.txt`,
`rpm-upgrade-actual.log`, `isolated-start100-attempt1.log`,
`bootstrap-device-attempt1.log`, `poc-classify-device-attempt1.log`,
`packaged-fixtures-actual.log`, `scenario-*-attempt1.log`,
`offline-image-attempt1.log`, `offline-platform-actual.log`,
`installed-hash-audit.json`, `installed-abi-symbols.log`,
`default-deny-probe.log`, `default-deny-daemon.log`, `reboot-before.log`,
`reboot-after-root.log`, `reboot-integrity-daemon.log`, and
`reboot-persistent-{before,after}.log`. Each execution log carries an
internal service/script result; SDB exit alone is not used as PASS.

## Build44 failure and Build45 activation/version tests (2026-09-30)

Build44 Release12 is a retained failed attempt. Its activation fixture set
`LISTEN_FDS=0` with `LISTEN_PID`, which made `sd_listen_fds(1)` return
`-EINVAL` instead of exercising absent activation. GBS `%check` failed one
of 27 tests; no Build44 RPM was installed or called a pass. The corrected
fixture removes both environment variables in the no-FD case.

Build45 Release13 archive SHA256 starts `98fad5b9`. All 239 files match the
GBS export and worktree at correlation time. GBS exit0 reports CTest27:
23 PASS, four explicit root SKIP and zero FAIL. The matching five-RPM
upgrade returned internal and SDB exit0. The installed test-only activation
binary has a separate `/tmp` endpoint and never opens a production listener.
Its packaged harness passed absent FD, wrong socket type at the exact test
path, stream socket at a wrong path, and two FDs. Target DLOG records
`CONSENTD` PIDs9814–9817 with `count=0`, `endpoint`, `endpoint`, `count=2`.
The child exit1 and exact log prove negative admission before bootstrap;
parent FD identity stayed intact. No explicit child close syscall was traced,
so A-51 remains Partial for the FD leak clause.

The installed isolated wire scenario changed only native Parcel envelope
version bytes at frame offsets4..7 from 1 to 2. It checked identical frame
length and other bytes, sent it after authenticated hello, observed bounded
connection close, then established another healthy hello with the same epoch.
The script confirmed the same daemon MainPID9973 and phase/service exit0.
This proves unsupported-version rejection; it does not prove early allocation
behavior or every A-62 malformed-input clause, so A-62 remains Partial.

The production socket, DB, registry and bootstrap receipt had identical
device/inode, ownership, mode and size before and after these fixtures.
The three regular files also retained their SHA256 hashes.
Installed RPM metadata lists 155 regular files: 154 hashes match and only
the preserved `%config(noreplace)` POC roles file differs. The installed
public ABI still has 42 versioned symbols. GTest/GMock runtime requirements
appear only in `consent-tests`; devel/tests require matching Release13.
Evidence is in `/var/tmp/consent-artifacts/gbs-build-{44,45}`, particularly
Build45 `source-correlation.json`, `LastTest.log`,
`rpm-upgrade-actual.log`, `activation-device-attempt1.log`,
`activation-dlog-after.log`, `wire-device-attempt1.log`,
`production-{before,after}-fixtures.log`, `installed-hash-audit.json`,
`rpm-requires-audit.json` and `installed-abi-symbols.log`. These tests
change no production policy, wire decoder, ABI or security flag.

## Build45 installed regression and orderly reboot rerun

The final Release13 RPM was exercised again because its production binary
hash differs from Build43's. Under
`/var/tmp/consent-artifacts/gbs-build-45/current-rerun`, packaged client,
I/O4 and owner6 fixtures passed. The isolated `persistent`, `running-delete`,
`shutdown`, `db-shutdown`, `cache` and `wire` phases each returned internal
zero. The first `stopped-delete` and `db-shutdown` attempts failed the
fixture's immediate `systemctl is-active` check after successful recovery or
DB drain: systemd still reported `activating`. `db-shutdown` attempt2 passed;
both `stopped-delete` attempts remain failures. Separate service-start,
integrity and `cleanup_unknown=1` observations are supporting evidence, not
a retroactive phase pass.

After earlier deletion tests, the inherited isolated DB produced an expected
decision mismatch in the first `persistent` attempt (exit1). That log does
not identify the cause of the mismatch. The three isolated stores were
hashed and moved to named
`*-build45-archive` paths. A fresh isolated `basic` then created a durable
PERSISTENT approval and both pre- and post-reboot `persistent` phases passed.
The orderly reboot changed boot ID from
`75d2b4dc-6fb5-43eb-8530-0ac117840417` to
`41d99246-ba13-4ed6-97f3-85fe7c756f93`. Five Release13 packages
persisted; production `consentd` was active with PID2556. The isolated
registry and receipt SHA256 and all three store inode identities matched
before and after, and post-reboot DB integrity was `ok`. This is isolated
approval persistence across an orderly reboot, not abrupt power-loss or
product argo/session evidence. Production restart/integrity and default-deny
were also rerun; actor PID13300/UID0 matched daemon PID9642's two
`role=rejected` logs, with negative create status and NULL handles.

## Build46 identity and session regression (2026-09-30)

Release14 archive SHA256 starts `150a785d`. All 239 frozen source files
matched the GBS export and working tree at correlation time; these guide
edits are later. GBS exit0 completed 27 CTest entries: 23 PASS and four
explicit root SKIP. The five matching RPMs upgraded on the emulator with
internal exit0. Installed repository-test passed server-generated session ID,
valid first-session use and cross-session `data_check`/derived-parent
`-EACCES`; its stopped fixture DB had no second-session artifact.

The installed checker-only wire fixture sent `session_open` with valid
subject/profile but forged PID/UID/GID. Actor PID5693/UID0/GID0 and forged
105693/100000/100000 are in the test log. The correlated reply was exact
`CONSENT_ERROR_PERMISSION_DENIED` with no session, followed by a healthy
Hello. DLOG from isolated daemon PID5683 reported the real peer PID/UID/GID
and `role=wire-scenario` twice. With the daemon stopped for both reads,
session count stayed 1 before and after. Outer identity policy and inner
repository role guard share the denial code; this proves the combined
fail-closed result, not which layer returned it. A-47 remains Partial.

The scenario's bounded `/proc/uptime` readiness check now requires
`ActiveState=active`, `Result=success` and an unchanged nonzero MainPID.
Installed `stopped-delete`, `db-shutdown` and `running-delete` each passed
with that check, recovery and stopped-DB integrity.
After archiving the prior isolated state/authority/control with hashes,
Release14 also passed a fresh `basic`, followed by `cache` and full `wire`
phases, each with internal exit0. The production
default-deny probe returned negative statuses and NULL handles; actor
PID6436/UID0 was rejected by daemon PID5455 in two matching DLOG entries.
With the production service stopped, a privileged readback returned
`integrity_check=ok` and schema version2; the socket/service then restarted
with PID8067, `active` and `Result=success`.
Installed regular-file hashes matched 154/155 RPM entries; the sole mismatch
is the preserved POC roles file marked `%config(noreplace)`, so full `rpm -V`
exited 1 for that config only. ABI remains 42 versioned symbols, and
GTest/GMock runtime requirements occur only in `consent-tests`.
The packaged offline registration, offline identity, storage prepare,
recovery-state (three GTests), and image-root authority fixtures also ran
as actual root/System::Privileged on the emulator with internal exit0.

Build46 source, GBS, package and device logs are under
`/var/tmp/consent-artifacts/gbs-build-46`. Build45 failures remain in their
original numbered files. Production desired-definition source, total
registry recovery, physical holder cleanup and external product adapters
remain open.

## Build47 failure and Build48 edited-IDL integration (2026-09-30)

Build47 Release15 was a failed `%check` attempt. Its new nested IDL test
compiled and linked both endpoints after a temporary edit, then rejected the
generated header for lacking a literal `Apache-2.0` string. The header has
the full Apache License Version 2.0 notice, and the generator validates the
IDL's SPDX metadata separately. Build47 `LastTest-failed.log` retains that
test-only assertion failure; no Build47 RPM was installed or called a pass.

Build48 Release16 archive SHA256 starts `8039344d`. All 240 frozen files
matched the GBS export and working tree at correlation time; later guide
edits are outside that freeze. GBS exit0 ran CTest28 with 24 PASS and four
explicit root SKIP. The corrected incremental test took 54.82s. It copied
source inputs into a temporary build with tests/tools/PoC off, compiled and
linked both actual endpoints, changed only that copy's valid IDL key limit
from 128 to 127, and observed a changed generated header plus actual verbose
`client.cc`/`server.cc` compile and client/daemon link commands. The build
after restoring the IDL produced the baseline IDL and header bytes exactly;
the real IDL and generator hashes stayed unchanged. The existing generator
test also passed deterministic repetition, invalid schema rejection and
license metadata. This is a GBS build-integration result. Installed packages
use the original IDL and wire version.

Five matching Release16 RPMs upgraded on the emulator with internal exit0.
Installed repository and full wire fixtures passed. Root-only packaged
offline registration, identity, storage prepare, recovery-state (three
GTests), and image authority tests passed with recorded UID0 and
`System::Privileged` labels. Their exact unit commands and actor evidence
are in `*-root-command.txt` and `*-root-device.log`; Build46's analogous
`*-root-proof-command.txt`/`*-root-proof-attempt2.log` also substantiate the
earlier guide's root claim. Installed hashes matched 154/155 regular RPM
entries, with only the `%config(noreplace)` POC roles file different. ABI
remains 42 versioned symbols and only `consent-tests` requires GTest/GMock.
Default-deny actor PID12244/UID0 had negative create statuses and NULL
handles; production daemon PID11808 logged the same kernel peer as rejected.

The first Release16 reboot attempt ran cache after fresh `basic` and before
reboot. Cache's own scenario unregisters and re-registers the same package;
post-reboot `persistent` then returned a decision mismatch (exit1). That log
does not isolate the cause, so it remains a failed mixed-input attempt. The
three isolated stores were hashed and archived. A clean second attempt ran
fresh `basic` and `persistent` with no intervening cache mutation. Boot ID
changed from `cf0b2708-ad08-47af-9b8e-c854bfa786d9` to
`126c815c-8314-4d86-9f09-f3f44eb0d1f8`; all five Release16 packages
persisted. The isolated registry and receipt SHA256/inodes and DB inode
matched before and after. Post-reboot `persistent` and stopped-DB integrity
both passed. The same final RPM then passed `stopped-delete`,
`running-delete` and `db-shutdown` phases with bounded READY checks. A
separate stopped production DB readback returned integrity `ok`, schema2;
socket/service restart reached PID4057, active and success. These are
orderly reboot and isolated recovery results, not abrupt power-loss or
total-registry restoration evidence.

Build48 source, GBS and device logs are under
`/var/tmp/consent-artifacts/gbs-build-48`. Production desired-definition
source, complete registry recovery, physical holder deletion and external
product adapters remain open.

## Build49 synchronous timeout and remote lookup (2026-09-30)

Build49 Release17 froze 240 files (archive SHA256 `30ebf7aa`); all matched
the GBS export and working tree at correlation time. GBS exit0 ran CTest28:
24 PASS, four explicit root SKIP. The new `sync-timeout` phase is an
installed emulator C API test, not a GBS CTest case. Five matching RPMs
upgraded with internal transaction exit0. Installed hashes matched 154/155
regular files; the sole difference was the preserved POC
`%config(noreplace)` roles file. ABI remained 42 versioned symbols, and
GTest/GMock dependencies remained confined to `consent-tests`.

The phase used a fresh caller-known subject/profile/client_request_id and
operation_id, a 30s remote request deadline and a 2s local synchronous wait.
`consent_request()` returned exact TIMEOUT with a NULL result. A second
authenticated handle then found that same request PENDING by subject,
profile and client_request_id. The paired TIMEOUT and fresh-ID PENDING
observations establish that remote work existed; TIMEOUT alone would also
be possible before transmission. Wrong ID, subject and profile lookups
returned negative status with NULL results. The original scoped lookup
cancelled the request and then returned terminal CANCELLED. Four target
runs used distinct IDs and each ended with internal service exit0 and
`PASS emulator phase=sync-timeout`. No UI response was injected.

Packaged client, I/O, owner, public API and repository tests also exited0.
The first command for the separate wire/storage representative rerun had a
shell phase-argument error and remains in `scenario-regression.log` as a
failed command attempt. Corrected `*-attempt2.log` commands passed wire,
stopped-delete, running-delete and DB-shutdown with phase PASS and internal
exit0. These are Release17 checks; Build48's orderly reboot result remains
a separate earlier result.

For a Release17 orderly reboot, the three existing isolated stores were
hashed and moved to distinct Build49 archive paths. A fresh `basic` seeded
approval and `persistent` passed before reboot. The device does not support
the direct `sdb reboot` command; that failed command remains logged. The
subsequent `sdb shell reboot` exited0 and changed boot ID from
`126c815c-8314-4d86-9f09-f3f44eb0d1f8` to
`cf76e1bb-8a96-4b66-a9dc-7a123c0b1313`. All five Release17 RPMs remained
installed. DB, registry and receipt inode and SHA256 values matched across
reboot, while production consentd restarted active/success with PID2564.
After restoring root SDB mode and repushing the same script hash,
`persistent` passed with isolated DB integrity `ok`, schema2 and internal
exit0. This is an orderly emulator reboot, not abrupt power-loss evidence.

Source, GBS, RPM and target logs are under
`/var/tmp/consent-artifacts/gbs-build-49`.

## Build50 test layout and header spacing (2026-09-30)

Release18 moves test-only source to root `tests/` and adds one empty line
between the Apache license and header guard in 39 public headers.
The generated protocol header also separates its license, generated notice
and guard with empty lines. The header changes preserve all other bytes;
the generated header preserves every nonblank line and wire definition.
All eight CMake option combinations configured, and 115 target names matched
the prior layout. The source archive froze 242 files (SHA256 `1b56ee73`);
the archive, GBS export and working tree matched at correlation time.
GBS exited0 with CTest28: 24 PASS and four explicit root-only SKIP.

Five matching Release18 RPMs upgraded on the x86_64 emulator with internal
transaction exit0. Runtime, daemon, development, PoC and test RPM file lists
matched Release17. Only the test RPM requires GTest/GMock. Installed regular
files matched 154/155 RPM hashes; the sole difference is the preserved PoC
`%config(noreplace)` roles file. Eight installed packaged test executables
passed with service exit0. `consent-mock-runtime-test` passed in GBS CTest
but has never been installed by the test RPM. Its attempted device command
failed and is retained separately; this layout change does not install it.

The first root-fixture device commands had a shell quoting error that hid
their internal result markers. Their logs are retained as failed evidence
capture. Corrected `*-root-attempt2-command.txt` and matching logs record
UID0, `System::Privileged`, fixture exit0 and SDB exit0 for offline identity,
offline registration, recovery state (three GTests), storage prepare and
image authority. The frozen scenario script was copied with matching host
and device SHA256. Installed `sync-timeout`, `wire` and `cleanup-pages` each
reported phase PASS and internal exit0.

The existing isolated state, authority and control directories were hashed
and moved to distinct Build50 archive paths before a fresh `basic` run.
That run and `persistent`, `stopped-delete`, `running-delete` and
`db-shutdown` each reported phase PASS and internal exit0. The installed
library retained 42 versioned symbols, production consentd was active and
successful, and the isolated DB integrity check returned `ok`. These are
Release18 installed behavior checks; earlier reboot evidence remains tied
to its own release. Logs are under
`/var/tmp/consent-artifacts/gbs-build-50`.

<a id="guide-12-checkpoint"></a>

## Guide 12 checkpoint details

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
`/path/to/GBS-ROOT/local/repos/tizen_10_1_emulator/x86_64/RPMS/`:
`consent`, `consent-devel`, `consent-tests`, and built-but-uninstalled `consentd`,
all `0.1.0-19.x86_64.rpm`. The evidence directory also archives the installed
three RPMs and their SHA256 hashes separately for r5 and r6.

<a id="guide-13-checkpoint"></a>

## Guide 13 checkpoint details

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

<a id="guide-14-checkpoint"></a>

## Guide 14 checkpoint details

## Verified Release23 snapshot (2026-09-30)

CONSENT-MOCK-INTEGRATION-04 final implementation r5 uses baseline8f0c441 plus
these reviewed changes. Evidence root:
`/var/tmp/consent-artifacts/consent-mock-integration-04/`.
source-r5.json records15 executed/packaged source paths. All11 implementation
hashes matched before publication (implementation-r5-postcheck.json). Publication
wraps one CMake line only: native/runner10 hashes remain equal, and the CMake
command-argument tokens are identical while its byte hash changes. External
publication-format.json records old/new hashes; no RPM rebuild or target replay
is claimed for that formatting delta. This paired guide receives
final evidence and concurrent-input prose after the build; the archived RPMs
contain the earlier guide draft, not this later verification prose.

Exact build from the consent repository:

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

gbs-r5.log/.exit0; CTest27 =23 PASS +4 root-only SKIP, no failures,73.41s.
rpms-r5/ and rpms-r5.json preserve Release23 RPMs/hashes, including the produced
production daemon RPM which was not installed. On discovered emulator-26101
x86_64, normal RPM upgrade installed only consent/consent-devel/consent-tests23
under the verified System::Privileged transaction context (install-r5.log
INSTALL_EXIT0). ldconfig permission diagnostics are retained, not suppressed.
installed-hash-r5.log confirms all19 installed smoke payload hashes match the
archived tests RPM, including current runner and both service executables.

The exact target command shape (commands.jsonl records the individual commands):

```sh
systemd-run --quiet --wait --pipe -p User=root -p SmackProcessLabel=System \
  /usr/bin/python3 /usr/libexec/consent/smoke/emulator-smoke.py \
  --mock-services --seed 20261005
```

| Installed scenario/log | Actual remote result |
| --- | --- |
| installed-mock-seed20261005-r5.log | SMOKE_EXIT0, MOCK_OUTER_EXIT0 |
| installed-tools-seed20261006-r5.log | SMOKE_EXIT0, OUTER_EXIT0 |
| installed-default-seed20261007-r5.log | SMOKE_EXIT0, OUTER_EXIT0 |
| cleanup-after-mock-r5.log | SMOKE_EXIT0, CLEANUP_EXIT0 |
| cleanup-after-tools-r5.log | SMOKE_EXIT0, CLEANUP_EXIT0 |
| cleanup-final-r5.log | SMOKE_EXIT0, CLEANUP_EXIT0 |

SDB hostexit0 alone is not remote success; both runner and outer markers are
checked. Mock mode used fixture catalog (no product create/parser dependency).
The required legacy tools regression exercised actual installed parser/catalog
and expected blocked public preflight. No optional actual mock-catalog run or
strict-product target run is claimed for this checkpoint.

Mock evidence includes role-specific CM8/CE4 discovery, actual permission errors,
invalid inputs/byte frames, no-effect QUERY, approved CM payload and CE0..3,
different RPC-id dedup, immutable tuple conflict, unknown prior receipt block,
revoked prior receipt STALE(-116), new operation CONSENT_REQUIRED, real argo/UI
DENIED callback followed by CONSENT_REQUIRED with unchanged admissions1,
and native_error/timeout cached outcomes with no repeated admission.
Recovery order running-delete/corrupt/stopped-delete retains mock PIDs113749 and
113753; each readback reports integrity=ok/schema2/definitions12/grants0/
cleanup_unknown1 before fresh approval and new actual fixture effects. Old
handles are explicitly rejected/recreated after daemon stop; running-delete uses
the original handles. Registry loss startup fails and both services reject actions.

Initial gbs-r1/r2 success history is retained. gbs-r3.exit1 was an build sequencing
error: a new build started during prior GBS teardown and safely rejected the
in-use mounted root. No unmount workaround was applied; final builds were
sequential. gbs-r4.exit0 and installed-mock-seed20261005-r4 remote1 are retained:
the actual denied callback was correct; the runner incorrectly expected a durable
DENIED check. cleanup-failed-r4.log is0. The final narrow assertion fix and normal
Release23 upgrade preserve the daemon contract.

device-before-r5.log/device-after.log confirm full service/package and protected
state inode/size/mtime/uid/mode equality. Production consentd18 PID31569 remains
active; PoC18 PID0 remains inactive; installed product CM15 unchanged. Final
smoke units are both not-found and all four fixture directories absent. Prior
smoke01 r6, Integration02 r4 and CM prerequisite archives remain unchanged.
results-r5.json summarizes the outcomes. Product CM principal/provisioning/
transport/consent enforcement and latest CE server/taxonomy remain external gates.

<a id="guide-17-checkpoint"></a>

## Guide 17 checkpoint details

### Executed r14 checkpoint (2026-10-01)

Evidence is retained under
`/var/tmp/consent-artifacts/consent-ui-smoke-10`. The exact development build was:

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all
```

`gbs-r14.log` and `gbs-r14.exit` record exit 0 / Done, CTest 34 cases:
30 PASS and four root-only SKIP. Managed binding, review, period-choice and
worker lifetime checks also passed. `host-tests-r14.log` records 58 host tests.
`rpms-r14/` retains 11 RPMs including source/debug packages;
`build-audit-r14.json` records their SHA256 values and exact equality of all 21
frozen files in current source and `source-export-r14.tar.gz`.

The selected target was `emulator-26101`, x86_64, correlated with the running SDK
emulator and its SDB serial log. The following is a portable form of the host inputs. Original absolute paths
are retained in the external command logs, not used as runner defaults:

```sh
BASE=/var/tmp/consent-artifacts/consent-ui-smoke-10
AURUM=/path/to/aurum-ui
CACHE=/path/to/existing/aurum-cache
python3 scripts/consent-ui-smoke.py \
  --build-dir "$BASE/rpms-r14" --serial emulator-26101 \
  --aurum-cli "$AURUM" --aurum-cache "$CACHE" --port 55059 \
  --scenario all --seed 20261015 --output "$BASE/attempt-r14-02"
```

| Output | Scenario / seed | Actual result |
| --- | --- | --- |
| `attempt-r14-01` | generation / 20261014 | exit 0 |
| `attempt-r14-02` | all / 20261015 | exit 0; all four subruns PASS |
| `attempt-r14-03` | functional / 20261016 | exit 0; focused repeat PASS |

The generation-only and repeat commands use the same inputs as above, with
only `--scenario`, `--seed` and `--output` changed as listed. Each command's
adjacent `.log` and `.exit`, attempt-level `result.json`/`proof.json`, and
per-phase `scenario.json`/`finally.json` retain the actual outcomes.

Both functional runs proved actual CE count 1 (default ONCE), no-effect denials
including separate ON/OFF reset probes, checked approval count 2, fresh-operation
persistent reuse count 3 without another consent popup, revoked denial staying
at 3, and ONCE-only CM count 4 with a disabled checkbox. Actual UI and both worker
mapped device/inode/SHA evidence is retained in each functional phase. The
checkbox is the installed NUI theme's orange control, not a Samsung theme claim.

The all-run restart readback retained one PERSISTENT grant and a new actual CE
operation succeeded without another approval. DB deletion readback showed
integrity `ok`, schema 2, two definitions, zero grants and `cleanup_unknown=1`;
the saved operation required consent before a fresh actual UI approval/effect.
Owned-helper generation change similarly required fresh consent, rejected the
saved old operation with exact STALE -116, and then produced a fresh UI-approved
CE operation/receipt. Gate-only receipts and worker execution receipts are
recorded separately. Old processes were stopped/drained and new authenticated
handles created; this is not same-handle recovery or a TPK reinstall.

All six successful transactions (generation-only, four all-run phases and the
functional repeat) recorded empty finally errors, owned cleanup exit 0, and exact
before/after equality for original `package_files`, `protected_trees` and `units`.
Production `consentd.service` remained active with PID 31569; both original PoC
services remained inactive with PID 0. No global RPM/library/policy installation
occurred. Original TPK restoration and allowed app inode/time changes were
verified separately; runtime-parent protected identities/modes/labels matched
while expected timestamps were recorded. Protected inventory/FD deletion removed
the owned payload after state, authority, endpoints and units were cleaned.

The executed package matches frozen `source-r14.json`, not later evidence prose.
The published source retains 16 byte-identical files. Three Python files have
only reviewed publication formatting: one trailing space removed in `ui.py`,
one comprehension wrapped in `aurum_tree.py`, and three lines wrapped in
`emulator-ui-native.py`. `publication-format-r14.json` records executed/current
SHA256, identical ASTs and exact reversal to executed bytes for all three.
This paired Guide 17 contains the two later evidence-prose updates; no rebuild
or device replay was performed for these publication edits. Earlier UI09 manual
evidence remains separate from these repository-runner results.

### Retained failures and limits

All earlier command output and restoration proofs remain alongside the final
checkpoint. They are not retroactively marked PASS:

| Attempt | Retained result / correction |
| --- | --- |
| r1-01 | missing generated-audit mode and read-mutated atime guard; exact owned FD recovery cleanup 0 retained |
| r3-01 | known Aurum bootstrap diagnostic prefix rejected before JSON |
| r4-01 / r5-01 | empty startup trees; actual settings pixels retained, then source-backed registered-root route established |
| r7-01 | ordinary active taskbar blocked; verified platform identity and control-center conflict checks added |
| r8-01 / r8-02 | omitted dump package rejected; bounded no-input diagnostic proved exact-ID own-package omission |
| r9-01 | oracle omitted the actual retention word “maximum”; no Allow sent |
| r10-01 | colored journal JSON rejected after the first actual CE effect |
| r11-01 | owned host interruption for timing review; neither runtime failure nor PASS |
| r12-01 | OFF-probe terminal DENIED not observed; cause remains unknown |
| r13-01 | final generation Next did not advance in the first observation; full all-run failed |

Post-deny diagnostics and bounded one-input page waits improve evidence and
semantic observation; they do not prove a native refresh/input race was fixed.
The matching full run and focused functional repeat passed, but the unexplained
r12 failure remains an honest reproducibility limit. Unknown UI/collector errors
still fail rather than accepting PENDING, repeating clicks or skipping review.

This is an isolated developer mock CM/CE execution demonstration over actual
consent IPC and native UI. Product CM/CE adapters, profile provisioning/privilege,
trusted deployment UI/argo roles and physical holder cleanup remain external
integration work. Installer hooks belong to each plugin under existing
publisher/generation contracts.

<a id="guide-15-checkpoint"></a>

## Guide 15 checkpoint details

The exact build command was:

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

Evidence is retained under `/var/tmp/consent-artifacts/consent-profile-05/`.
`source-r8.json` records the executed Release25 snapshot, all 29 files matching
before target verification (`source-r8-postcheck.json`). Final evidence prose in
this paired guide was added afterward; the other 27 implementation/build/test/
runner/navigation files retain their executed hashes. Archived RPMs and logs
remain unchanged; installed package documentation describes the earlier draft.
No documentation-only rebuild is claimed.

| Evidence file | Actual result |
| --- | --- |
| `gbs-r8.log`, `gbs-r8.exit` | exit0; CTest 25 PASS + 4 root-only SKIP |
| `rpms-r8/`, `rpms-r8.json` | matching Release25 runtime/devel/tests plus preserved full build artifacts |
| `install-r8.log` | remote INSTALL_EXIT:0; only runtime/devel/tests upgraded |
| `installed-r8-hash.log` | 25 installed payload hashes match RPM, HASH_EXIT:0 |
| `installed-profiles-seed20261010-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0 |
| `installed-strict-seed20261011-r8.log` | all scenarios PASS; SMOKE_EXIT1 / OUTER_EXIT:1 solely unverified product profile provisioning/privilege |
| `installed-mock-seed20261012-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0 |
| `installed-tools-seed20261013-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0, actual installed CM parser/catalog exercised |
| `installed-default-seed20261014-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0 |
| `cleanup-final-r8.log` | SMOKE_EXIT0 / CLEANUP_EXIT:0 |
| `device-before-r7.log`, `device-before-r8.log`, `device-after-r8.log` | complete production/PoC fingerprint equality; production daemon18 active PID31569; PoC18 inactive PID0; smoke units2 not-found, fixture directories4 absent |
| `native-readonly-preflight-r8.log` | security_fw/System, explicit synthetic configured UID1: library file observation0, native NameHasNoOwner/BLOCKED3 |
| `verification-r8.json`, `commands.jsonl` | consolidated outcomes and exact device command argv |

Profile scenarios verify warm A sync/async request source=DAEMON, missing/
unknown/undelegated/inactive rejection, terminal INVALIDATED callback exactly
once, late UI rejection, unrevoked CE receipt invalidation across A→B→A, fresh
persistent A reuse without old session revival, owner-gap approval retirement,
all three DB losses with integrity/schema2/definitions12/grants0/cleanup_unknown1
readback, fresh approvals and payload effects, same CM/CE service PIDs and explicit
new handles, and safe total registry-loss blocking. Authorization/API errors
have no effects; process-local mock receipts never replace current AUTHORIZE.
Native private-bus tests exercise the actual adapter's sender/owner/init/stale
successful reply/removal/ACK timeout/shutdown paths. Socketpair tests verify real
blocked and partially written sensitive responses cannot complete after fence.

Retained history is explicit: r1 compiler signedness failure; r2/r3 successful
builds; r4 compiler dangling-else fixture failure; r5/r6 invalid test envelope
failures; r7 successful build/profile0/strict1 followed by the legacy mock
helper-variable NameError1 and owned cleanup0. R8 restores the original dynamic
definition-count assertion and upgrades normally to Release25. No protocol
bounds, role policy or production fatal behavior was weakened.

Native product activation remains unverified: this emulator has no owner for
`org.tizen.sessiond`. The library filesystem getter nevertheless returned0,
which demonstrates why it is not readiness evidence. UID1 belongs only to the
synthetic configured fixture, not a discovered/provisioned product account
mapping. Product mapping, privilege, live native switch/provider coordination,
actual CM authorization adapter and current CE server integration remain external
gates. No real account switch, production daemon installation or policy change
was performed. See [Guide 14](../guides/14-mock-services.en.md) for existing mock boundaries.

The focused repository cleanup test registers an A artifact under an owned
session and receipt, switches to B, rejects A data use, and permits authenticated
A cleanup listing/ACK while inactive. Matching ACK advances CLOSING to CLOSED;
returning to A cannot revive the session. This is control-metadata evidence, not
proof that physical data was deleted.

<a id="guide-8-checkpoint"></a>

## Guide 8 checkpoint details

### UI09 executed r7 evidence (2026-09-30)

Evidence is retained under
`/var/tmp/consent-artifacts/consent-ui-native-09/`. The executed 34-file
`source-r7.json` matches the frozen/exported build inputs; the final Guide08/10
English/Korean evidence paragraphs were written afterwards. The other 30 files
remain byte-identical. No rebuild is claimed for the later documentation.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all
```

`gbs-r7-command.json`, `gbs-r7.log` and `gbs-r7.exit` record exit0 and CTest
33 tests: 29 PASS, 4 root-only SKIP. `rpms-r7/` retains Release27 runtime,
devel, daemon, PoC and tests RPMs. No global RPM was installed for this run:
matching tests helpers were extracted into the protected owned fixture, and
matching signed main TPK bytes supplied both the UI and private unit library
binds. The installed runtime/devel/tests were26, daemon/PoC18. The original
installed main TPK was backed up, certificate identity checked, and restored.

`host-provenance-r7.json`, `source-postcheck-r7.json` and
`tpk-native-audit-r7.json` retain inputs and ELF evidence. Signed TPK libraries
match full native TARGET_FILE hashes exactly. The RPM-packaged libraries have different byte hashes and GNU build IDs;
the four inspected sections `.text`, `.rodata`, `.dynsym`, `.gnu.version` match.
A postprocessing explanation is an inference, not a demonstrated cause.
This is not a same-build-ID or RPM-byte-equality claim. Actual `/proc` maps in
`attempt-r7-02/interactive/65-result.json` prove the UI and both CM/CE workers
used the exact staged device/inode/hash; daemon executable provenance is
recorded separately.

The archived host commands were `python3 interactive_r7_02.py`,
`python3 interactive_r7_06.py` and `python3 interactive_r7_09.py`, run from the
evidence directory with explicit selected emulator `emulator-26101` (x86_64).
Each owns bounded Aurum startup/forward/stop, fixed unit dispatch and TPK
restoration in finally; the prompt deadline remains60s and action window600s.

- `attempt-r7-02`: exit0. Actual unchecked ONCE approval starts CE count0→1;
  denial leaves1; checked/full-review PERSISTENT starts2; a new operation with
  the same complete tuple executes without another approval popup, count3 and
  a fresh CE receipt. Revoke plus denial leaves3. Trusted ONCE-only device
  policy disables the checkbox; actual CM action reaches4. Screenshots and
  `actions-journal-final.log` distinguish choice from immutable base ONCE.
- `attempt-r7-06`: outer1, partial lifecycle evidence retained. Normal restart
  preserves PERSISTENT1 and authorizes with a fresh receipt. Owned stopped DB
  deletion gives schema2/definitions2/grants0/cleanup_unknown1; fresh actual
  UI approval executes CE. Generation retirement gives new REQUIRED and old
  operation retry STALE(-116). Final fresh-generation UI was not proved in that attempt.
- `attempt-r7-09`: host exit0, fresh-generation positive completed. The owned
  installation-authority helper changes generation, then authenticated
  registration returns2 definitions. A new operation requires approval and
  the saved old operation is STALE(-116). Actual default-off popup, checked
  full review, approval and CE operation `feature-7f244d44-...` produce new
  effect receipt `3334dd94...`, count1 in a new coordinator epoch, retry0.
  Separate public gate receipt `5737...` is authorization-only evidence.
  `generation-positive-summary.json` retains exact values. This is an owned
  helper generation change, **not a TPK reinstall lifecycle test**.

All completed mutation attempts retain finally/restoration logs. Successful
02/09 and partial06 finally report `errors=[]`; original package files,
protected production/PoC trees and units compare exactly. Runtime parent
protected metadata stays equal, with expected timestamps separately reported;
TPK replacement/restoration inode/time changes are explicit. Production18
PID31569 stays active and original PoC remains inactive. Owned units, endpoints,
state/authority and retained finite payload were removed; Aurum bootstrap and
owned forward were stopped. No global graphics, policy or account was changed.

Earlier failures remain: r1 SDK input, r2 private fixture call, r3 invalid
same-version fixture policy, r5 protected unit-parent rejection, r6 missing
initial lifecycle lock, r7-01 capture-route failure, r7-03 forbidden legacy
check field, r7-04 JSONL artifact parser, r7-05 retained-root preflight,
r7-06 timeout/black capture, r7-07 queue method and r7-08 missing gate boolean.
The generated artifact Python cache cleanup records its executed helper hash
separately from a later unexecuted ROOT-FD check. No native validation was
weakened for these corrections. CM/CE participants remain synthetic providers
using actual isolated consent APIs; production CM/CE adapters are not verified.

<a id="guide-16-checkpoint"></a>

## Guide 16 checkpoint details

Artifacts: `/var/tmp/consent-artifacts/consent-style-06/`.
Sequential `gbs-r1.log/.exit` and final `gbs-r2.log/.exit` both record exit0,
31 tests: 27 PASS and four documented root-only SKIP. The checked-string,
post-spawn kill/reap and real private-bus throwing-barrier regressions passed.
The production adapter used a private test bus for the latter; no real native
sessiond user switch was performed.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator \
  --include-all --define '_without_poc 1'
```

`source-r2.json`, `export-r2.json` and `inputs-r2/` preserve the executed
25-file source and exported inputs. `rpms-r2/` holds all nine produced RPMs
with hashes in `rpms-r2.json`; install selected only
`consent-0.1.0-26.x86_64.rpm`, `consent-devel-0.1.0-26.x86_64.rpm` and
`consent-tests-0.1.0-26.x86_64.rpm`. Production daemon/PoC RPMs were excluded.
`rpm-dependencies-r2.log` retains the dependency audit; libsessiond, dbus and
JSON-GLib remain test-package dependencies rather than new production ones.

On discovered `emulator-26101`, architecture `x86_64`, `install-r2.log` records
INSTALL_EXIT0. `installed-payload-r2.log` verifies all 162 regular installed
payload files against the archived RPMs (VERIFY_EXIT0). Installed native config
and process regressions each have NATIVE_EXIT0 in `native-config-r2.log` and
`native-process-r2.log`.

| Installed runner mode | Seed | SMOKE_EXIT / OUTER_EXIT | Evidence log |
| --- | --- | --- | --- |
| profiles | 20261020 | 0 / 0 | installed-profiles-seed20261020-r2.log |
| profiles --require-product | 20261021 | 1 / 1, expected | installed-strict-seed20261021-r2.log |
| mock-services | 20261022 | 0 / 0 | installed-mock-seed20261022-r2.log |
| tools | 20261023 | 0 / 0 | installed-tools-seed20261023-r2.log |
| default | 20261024 | 0 / 0 | installed-default-seed20261024-r2.log |

The exact command record is `commands.jsonl`, with the host driver
`target-owner.py`. Runner commands used `systemd-run --quiet --wait --pipe`,
`User=root`, `SmackProcessLabel=System`, and
`/usr/bin/python3 /usr/libexec/consent/smoke/emulator-smoke.py` with the listed
mode/seed arguments. Strict mode completed the profile scenarios, then failed
solely with `product profile provisioning/privilege unverified`. Expected
registry-loss service start failure is a protected block, not scenario failure.
Both profile runs prove begin/reconnect/new async/new pending without admission;
old callbacks are detached before the Pending owner is cleared.

Every inter-mode cleanup and `cleanup-final-r2.log` record cleanup0.
`device-before-r2.log` and `device-after-r2.log` have exactly equal full
production/PoC metadata fingerprints: production daemon18 active PID31569,
PoC18 inactive PID0, CM15 unchanged. Final smoke units are both not-found and
all four fixture directories absent. No product activation, policy changes,
real account switch or external reference edits were performed.

Executed-source and publication distinction: after r2 export, only adjacent
standard `<type_traits>`/`<utility>` include order in `repository.cc` changed;
reversing that swap exactly reproduces the executed hash, recorded with both
hashes in `publication-include-order.json`. At that point the other 24 files
were byte-equal. Final publication adds later evidence prose in this paired
guide: 22 other files remain byte-equal, one has the approved include-order
change and two have later prose. No RPM rebuild or installed hash equality is
claimed for these publication deltas. Earlier r1 source/RPM evidence remains retained.

<a id="guide-8-settings-history"></a>

## Historical build26 verification

Frozen26 passed GBS compilation/managed tests, actual TPK installation and
paired socket identity checks. Actual EN/KO page review, language reset, Allow
and Deny, long unbroken text, cancellation, timeout and Back were exercised.
The Allow flow completed QUERY/ONCE authorization/retry and holder cleanup.
See [Guide 07](../guides/07-verification.en.md) for hashes, screenshots, exact evidence
and the separate24/25 failures. After verification the PoC daemon/socket and
actors are stopped; explicit PoC state/roles and installed TPKs remain for review.
This is a Common Emulator PoC, not product role/UI/Installer or physical TV acceptance.

## Feature Settings walkthrough (build27–29)

The initial DEFAULT app-control opens Settings without changing a selection.
`consent-poc-launch org.tizen.consentui --settings en-US` uses this path; VIEW
continues to open a specific approval request. Launch parameters never select,
save or execute a function. See [Guide 10](../guides/10-feature-approval.en.md) for the
selection, missing-only approval and execution contract.

The private `libconsent-feature-poc.so.0` connects only to
`/opt/var/lib/consent-feature-runtime/argo.sock`, activated by
`consent-feature-poc.socket` and `consent-feature-poc.service`. The UI verifies
the root-protected path, PID1/UID0, original kernel bind address, inode and
`System::Privileged` listener label. The coordinator verifies the actual UI
UID, protected loader and exact package label. The app cannot read the root
coordinator's process identity on the selected target; no cross-UID executable
inspection or added UI capability is claimed. Only the dedicated runtime leaf
is prepared root:root 0755 with SMACK `_`; the socket is root:users 0660.

The argo actor owns the immutable catalog, selection CAS and coordinator epoch.
Saved SESSION or 30-minute selections survive closing Settings while the
conversation/selection period remains valid. An explicit task-only ONCE choice
is separate from those saved settings. Every mutation carries the viewed
catalog hash, epoch, expected revision and stable command ID. An ambiguous
reply retains the immutable submission for explicit same-command retry. A
coordinator restart rejects old commands and requires a newly reviewed choice.
All conditions must fit the combined prompt budget; no partial batch or silent
splitting is performed.

After ordinary PoC identity and generation setup, use a fresh host artifact
directory and the selected emulator serial:

```sh
python3 scripts/emulator-feature-flow.py --serial "$CONSENT_SERIAL" \
  --artifact-dir /var/tmp/consent-feature-run start --locale en-US
# Review Settings and approval pages and operate the actual application.
python3 scripts/emulator-feature-flow.py --serial "$CONSENT_SERIAL" \
  --artifact-dir /var/tmp/consent-feature-run collect
# Choose the explicit conversation-close task and observe holder cleanup first.
python3 scripts/emulator-feature-flow.py --serial "$CONSENT_SERIAL" \
  --artifact-dir /var/tmp/consent-feature-run stop
```

The driver uploads the matching protected preparation script and runs it in
root/System::Privileged context. It temporarily assigns distinct mock CM/CE
enforcers and restores the original PoC roles on stop. It collects evidence;
it does not approve requests or assert a visual result. Final ordinary PoC
shutdown still uses `emulator-poc-setup.sh stop`. A force-stop without a holder
ACK is not successful data cleanup.

The separate `consent-feature-gate-*` executables are installed only with tests.
`scripts/emulator-feature-gate.py` prepares an explicit protected
`/etc/systemd/system/consent-feature-poc.service.d/gate.conf` override, records
a real AUTHORIZE receipt/operation/job/PID, and holds action dispatch for at
most 30 seconds without blocking the actor. Its prepare/wait/release/audit/cleanup
phases require actual UI selection removal, verify cancellation and zero matching
action events, and restore the saved roles/unit configuration. The ordinary
coordinator has no gate or runtime switch. Native injected-completion evidence
and this target experiment are reported separately in Guide 07.

`scripts/emulator-feature-endpoint-test.py` uses the installed private bridge
and endpoint fixture to reject a directly bound fake server and a different
PID1 socket renamed to the expected path before any request bytes are sent.
It uses protected temporary `/etc/systemd/system` units and restores only the
recorded feature-unit states. The separate negative .NET package also probes
the bridge; correlate its PID with `feature-peer-rejected` /
`ui-peer-rejected` in the coordinator log, in addition to the consent daemon's
role-rejection evidence. A transport error alone does not establish this result.

For the separate reuse boundary, `prepare --kind reuse` selects instrumentation
only in the dedicated test coordinator; the default is `--kind acquisition`.
Acquisition evidence is `proof_kind=acquisition-receipt`. Reuse evidence is
`proof_kind=artifact-permit`, recorded after the real `reuse-data` permission
check, with artifact, session/generation, canonical exact-context SHA-256 and
operation/job/PID. It is not a new acquisition receipt. Both kinds pause before
the actor's final selection check and start dispatch. Preserve the old gate run
as evidence before preparing another run; the script refuses existing records.


Worker channels use a transient root-owned 0600 pathname under the protected
feature runtime directory. The coordinator connects and accepts before fork,
verifies its exact kernel PID/UID0/GID0 and System label at both ends, then
removes the listener pathname and transfers only the connected FD to the
verified executable. The worker still verifies the parent's executable inode,
start time and lifetime. The selected kernel returns an empty peer label for
socketpair; that case is rejected rather than accepted as a fallback. Channel
startup/exit diagnostics contain only stages and status; an unprovable leftover
node is reported by its generated path and is never blindly unlinked.

After installing the matching tests RPM and preparing the protected feature
runtime directory, the dedicated target test requires actual SMACK evidence:

```sh
# On the selected target, in the privileged test preparation context:
systemd-run --wait --pipe -p User=root -p Group=root \
  -p SmackProcessLabel=System -p CapabilityBoundingSet=CAP_SYS_PTRACE \
  -p AmbientCapabilities=CAP_SYS_PTRACE -p NoNewPrivileges=yes \
  /usr/libexec/consent/tests/consent-feature-test --worker-channel
```

This explicit mode must pass both-end kernel identity, Parcel transfer,
wrong-parent executable rejection, transient-stat retry and owned-node cleanup;
it does not skip when SMACK is unavailable. Ordinary host/GBS tests label that
positive platform portion SKIP while still testing wrong kernel PID rejection.
See Guide07 for the actual build and target outcome.

### Verified selected-feature flow (build29)

The exact build29 TPK/RPM set completed actual KO SESSION and EN 30-minute TIMED
selection/approval, app close/reopen with heartbeat, same-conversation artifact
reuse, a device-only missing-permission popup, and calendar-plus-device execution.
Denying a wider one-time calendar request kept both the saved selection and
execution counter unchanged; an explicitly chosen alternative reused the old
narrow artifact. Empty selection save and explicit conversation-close completed
holder wipe/ACK and CLOSED/pending0. [Guide 07](../history/07-verification-history.en.md#build-29-selected-feature-preapproval-and-actual-task-execution)
records exact hashes, request/job/PID/revision context, screenshots, commands and
limits. This is Public Common Emulator acceptance of isolated mock providers,
not TV hardware or production Settings/argo integration.

The installable positive TPK is
`/var/tmp/consent-artifacts/gbs-build-29/org.tizen.consentui-0.1.0.tpk`.
Use that directory's matching runtime/daemon/PoC RPMs and the setup steps above;
the TPK alone does not provision trusted roles or generation authority.
Actual screenshots and phase logs are in
`/var/tmp/consent-artifacts/emulator-build-29/feature-main/`. In particular,
`prompt-ko-1.png` through `prompt-ko-3.png` show exact calendar scope and separate
retention, `missing-device-ko-1.png` through `-3.png` show the single missing
condition, and `timed-prompt-en-1.png` through `-5.png` show the 30-minute choice.
After wider denial, uncheck **This task only: once** before selecting the
already-authorized narrow alternative; that operation must not save the
one-time task into Settings.

For the actual dispatch-boundary test, copy the matching script to the selected
development emulator and run each phase through an explicit root privileged
unit. For example, on the target:

```sh
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py prepare --kind acquisition
# Operate the real Settings/approval UI; submit the reviewed calendar task.
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py wait --timeout 10
# Within the unchanged 30-second gate, uncheck calendar, review, and save.
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py release
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py audit
```

Preserve the completed evidence directory before the next `prepare`; never
replace records from an unfinished run. Use `--kind reuse` for the separate
reuse test, first acquire one artifact normally and then request it again.
After release and before audit/unit restoration, choose **conversation-close**
while the holder is still alive and confirm CLOSED/pending0. On interruption,
run the script's `cleanup` phase; a forced stop is not evidence of physical data
erasure. Both actual29 gates passed with matching action events0, while the
reuse baseline retained its single acquisition action. The first acquisition
attempt expired during inspection and is separately recorded as failure.
Native injected late-result/retry/epoch tests are distinct from these actual UI
gates; actions already started cannot be undone by later deselection.

Final actual29 cleanup restored ordinary PoC roles, stopped all feature/PoC/test
units, removed owned gate FIFOs/overrides/transient sockets, and left production
active with default-deny roles and unchanged DB/registry inodes. Installed
packages and protected regular evidence remain available for review. The scoped
Aurum bootstrap/forward were stopped. Final Guide07/08 evidence and Guide10
updates are post-archive documentation only; source and tested TPKs remain exact29.


<a id="guide-10-checkpoint"></a>

### Executed UI09 verification

The matching Release27/r7 native build passed CTest33 (29 PASS, 4 root-only
SKIP); managed choice/review/geometry checks are included in that build. The
actual selected x86_64 emulator used the signed TPK's exact full library bytes
for both UI and privately namespaced CM/CE workers, with extracted owned test
helpers rather than a global RPM upgrade. RPM-packaged byte hashes/build IDs
are separately recorded and are not claimed equal to TPK payloads.

`/var/tmp/consent-artifacts/consent-ui-native-09/attempt-r7-02/` records actual
unchecked ONCE, denial/no effect, checked/full-review PERSISTENT, new-operation
reuse with a fresh CE receipt, revoke/denial and ONCE-only CM execution. Base
`grant_mode` remains ONCE even when the chosen approval is PERSISTENT. Actual
NUI default checkbox color is orange; compact styling does not install a
Samsung runtime theme.

Partial `attempt-r7-06/` proves normal restart persistence and owned DB loss
(grants0, cleanup_unknown1) followed by actual fresh approval/effect. Its final
fresh-generation window timed out and is not counted as a positive. Distinct
`attempt-r7-09/` completes that positive: actual owned helper generation change,
new REQUIRED/old STALE(-116), default-off refreshed popup, checked all-page
review, fresh CE action/receipt in a new coordinator epoch and authoritative
ALLOWED gate. This is not actual TPK reinstall lifecycle evidence. The gate-only
receipt and actual effect receipt are distinct.

Successful02/09 host exit0/finally errors[] and partial06 restoration retain
exact original global package/state/config/unit equality, expected package
inode/time and runtime-parent timestamp deltas separately. Owned fixtures and
Aurum were cleaned. Production18 PID31569 and original inactive PoC remained
unchanged. All earlier build/capture/controller failures remain archived.

[Guide08's UI09 evidence](../history/07-verification-history.en.md#guide-8-checkpoint)
contains exact commands, files and limits. `source-r7.json` is the executed
34-file snapshot; only the final paired Guide08/10 prose differs afterwards,
with the remaining30 files byte-identical. These are synthetic CM/CE participants
using actual consent APIs, not production CM/CE adapters or deployed product UI.
