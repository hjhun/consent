# 16. Authored-style audit and remaining development

This audit uses exact author `h.jhun@samsung.com`, authored dates from
2020-01-01 through 2026-01-31, and local Git refs. It reuses the dated
2020–2025 skill investigation (142 repositories, 53 authored repositories,
61 representative patches). Those historical counts are not a current census.
Ten representative authored diffs were directly validated across nine
repositories; this does not claim every historical or current line was read.

| Repository | Authored patch | Relevant evidence |
| --- | --- | --- |
| base/bundle | 0337ede3538b (2020) | Correct resource deleters and wrappers |
| api/app-control | f3e351be4cc5 (2021) | Public API/private ownership separation |
| api/preference | a2f8c65d2a7b (2021) | Backend responsibilities |
| appfw/amd | 13a4bd592dc5 (2023) | Reuse shared queue ownership |
| appfw/aul-1 | 0a4f77d53ff8 (2023), 7f0a555a0e63 (2025) | Publish after construction; callback lifetime |
| appfw/launchpad | f85766b27ea8 (2023) | Small worker/job responsibilities |
| appfw/rpc-port | 50bfbe4a86e3 (2024) | Static transport callback adapters |
| api/app-common | b3879488c97a (2025) | Explicit multiuser public inputs |
| api/app-event | 44361a3d58fc (2025) | Retain ownership during callbacks |

AMD `29ed218b` and AUL `ac581e7` are release snapshots authored by another
person, not evidence that their entire contents are HJHun-authored changes.
Watcher remains the requested layout reference; no pre-cutoff authored patch
was located. Historical `.h` names and record naming exceptions are preserved.
The explicit 80-column requirement is not attributed uniformly to all old code.

## Bounded changes

Native profile callbacks use named `noexcept` adapters and fail-closed Stop.
Async envelopes retain their owner; timer invocation pins its envelope without
copying a potentially allocating function, including reentrant source removal.
Lifecycle facades and SQLite owners prohibit copying. SQLite error strings use
the correct `sqlite3_free` deleter; client locking reuses `MutexLock`.

Profile configuration has a focused checked loader. Missing configuration keeps
legacy behavior; a present malformed file fails startup. Every required string
checks GLib decoding errors. Explicit `subsession=` remains valid, whereas
`subsession=\x` is rejected. Protected file opening, bounded complete reads,
NUL/duplicate/unknown-key rejection and publication only after validation remain
required. Shared `KeyValue` semantics are unchanged.

The private provider fault test proves post-spawn setup failure kills and reaps
the child. Profile examples use one Pending owner with explicit reset/wait/take,
callback-after-return and one result transfer. Four overlong native/CMake lines
are wrapped locally; the smoke version map is a link dependency. ABI, wire,
roles, profile generation and storage policy are preserved.

## Remaining development priorities

1. Product CM trusted identity, consent delegation and final execution adapter;
   current CE source, identity, taxonomy and adapter contracts.
2. Installer lifecycle provisioning, production UI and authenticated roles.
3. Protected sessiond mapping and privilege provisioning. Profile05 discovery
   found installed sessiond inactive, bus names unowned and mapping absent;
   that observation does not establish why the service was inactive.
4. Real holder cleanup and trusted registry production after total DB loss.
5. Storage/power interruption and representative sustained-load verification.

Synthetic CM/CE and profile fixtures validate isolated consent behavior. They
are not product adapter, native user-switch or physical cleanup evidence.
See [Guide15](15-profile-authority.en.md) for profile trust boundaries.
Guide11 historical style counts are preserved.

## Verified Release26 checkpoint

Baseline `c15336c` inventory checked 159 tracked native/CMake files for line
length; four lines exceeded 80 columns. The focused semantic review and the
representative authored patch inspection are not a claim of every-line review.
Existing style skills already covered the findings; no skill changes were made.

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
claimed for these publication deltas. Earlier r1 source/RPM evidence remains
retained; its original representative repository count was corrected from
eight to nine in r2.
