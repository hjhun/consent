# Guide 07: Verification status

[한국어](07-verification.ko.md)

This guide summarizes executed checks and the limits of those results. API
examples and test source alone do not prove target execution. The detailed
[historical record](../history/07-verification-history.en.md) retains commands,
failed attempts and earlier snapshots. Editing these documents adds no new
build or emulator result.

## Latest verified workflow

The repository-owned native UI runner was verified with the Release28 build
and frozen `source-r14.json` on an x86_64 development emulator. GBS completed
with exit 0: 30 CTest cases passed and four root-only cases were skipped.
Managed UI checks and 58 host safety tests also passed.

The full one-command run covered real UI choices and worker effects, normal
restart, stopped DB deletion and installation-generation retirement. A
separate generation run and a focused functional repeat also passed. Across
six transactions, cleanup succeeded and the original production/PoC package
files, protected state and service metadata were restored or unchanged as
specified. Temporary TPK replacement and permitted timestamp changes were
recorded separately. These results use synthetic CM/CE participants and real
isolated consent IPC; they are not product adapter verification.

See [Guide 17](17-native-ui-smoke.en.md) for prerequisites, the command and
result files. Raw evidence is under
`/var/tmp/consent-artifacts/consent-ui-smoke-10/`.

## Representative checkpoints

| Snapshot | What was checked | Recorded result |
| --- | --- | --- |
| Release19, smoke01 r6 | CM parser, role-separated checks, DB recovery | 22 PASS + 4 SKIP; smoke 0, strict product 1 |
| Release21, tool r4 | JSON-RPC provider, CE records, retry and recovery | 23 PASS + 4 SKIP; tools/default 0, strict 1 |
| Release23, mock r5 | Persistent CM/CE services and authoritative retries | 23 PASS + 4 SKIP; mock/tools/default 0 |
| Release25, profile r8 | Profile fences, native private-bus tests and cleanup | 25 PASS + 4 SKIP; profiles/mock/tools/default 0, strict 1 |
| Release26, maintenance r2 | Configuration errors, callback exceptions, child cleanup | 27 PASS + 4 SKIP; installed modes 0, strict 1 |
| Release27, native UI r7 | Actual period choice, receipts and lifecycle follow-up | 29 PASS + 4 SKIP; completed functional/generation runs 0 |
| Release28, UI runner r14 | Automated UI and four lifecycle phases | 30 PASS + 4 SKIP; full run and repeats 0 |

These are separate snapshots, not one cumulative test count. An expected
strict-product exit 1 means the isolated scenarios passed but a required
product integration was unavailable. The historical record and each guide
identify the exact commands, failure history and skipped cases.

## Known limits

The UI runner's r12 OFF-probe denial was not observed; its cause remains
unknown. The r13 generation run failed when a Next action did not advance in
the first observation. Later diagnostics and bounded page-transition waits
improved observation, but do not prove a native input/refresh race was fixed.
The matching full run and functional repeat passed; intermittent failures
remain a reproducibility limit and still cause a failing runner result.

Product CM/CE adapters, trusted UI/argo deployment, sessiond account mapping
and privileges, and real holder deletion remain unverified. Each plugin owns
Installer hooks and must honor authenticated publication and installation
generations. Registry loss needs a trusted definition producer; definitions
cannot restore lost approvals. Orderly reboot and injected failures do not
establish abrupt power-loss safety or representative sustained-load behavior.

Use [Guide 11](11-acceptance.en.md) for the historical CEP acceptance mapping
and [Guide 16](16-maintenance-and-integration.en.md) for remaining work.

---

[Related task](01-development.en.md) · [Continue](11-acceptance.en.md) ·
[Reading paths](../README.md)
