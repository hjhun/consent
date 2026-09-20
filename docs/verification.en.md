# Verification evidence and remaining scope

This is a foundation increment, not completed product integration. Results below
were observed on 2026-09-20 on the selected development emulator. Later working
tree changes are not covered by the frozen build unless separately stated.

## Frozen build 7

- GBS source tree: `10749441a5839db60024af9ca2d61c6a5164f73c` (tree, not commit).
- Command: `gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4`.
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
