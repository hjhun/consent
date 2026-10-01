# 11. CEP 19.2 acceptance audit

[한국어](11-acceptance.ko.md)

This audit maps every proposed [Design 01 acceptance
item](../design/01-consent-framework.md)
to Build43 Release11 evidence and the numbered Build45–49 supplements.
`Target` means the named framework behavior ran in emulator isolated/package
fixtures, or a build-only criterion ran in GBS as identified in its row.
It does not imply external product integration. `Partial` means a narrower
behavior ran or a failure mode remains untested. `Product` means the required
real participant or trusted producer is absent. The numbered logs are under
`/var/tmp/consent-artifacts/gbs-build-{43,45,46,48,49}`. Each row names the
later build when it changes the evidence; prior results are not a Build49
rerun. A proposed criterion is not itself a test. Counts for this historical
audit are
Target23, Partial33 and Product7.

| ID | Scope | Evidence or remaining boundary |
| --- | --- | --- |
| A-01 | Product | Mock/negative role tests; actual CM and CE identity absent. |
| A-02 | Target | Isolated basic and public API SYNC/ASYNC. |
| A-03 | Target | Client owner/cache callback ordering fixtures. |
| A-04 | Target | Check returns CONSENT_REQUIRED without UI opening. |
| A-05 | Target | Repository feature/expiry fixture. |
| A-06 | Target | Cache scenario and owner no-remote cache regression. |
| A-07 | Partial | Revoke/restart cache tests; product profile authority absent. |
| A-08 | Partial | Mock provider/subject separation; product provider absent. |
| A-09 | Partial | Decision denial tested; real protected action absent. |
| A-10 | Target | One-time concurrent authorization fixture. |
| A-11 | Target | Repository feature all-condition transaction fixture. |
| A-12 | Target | Operation ID dedup/conflict in basic/race fixtures. |
| A-13 | Target | Approval/cancel/expiry race and callback fixture. |
| A-14 | Partial | Repository crash window fixture, not exact product daemon kill. |
| A-15 | Partial | Localization and UI PoC; production UI absent. |
| A-16 | Partial | PoC stale policy response; production UI absent. |
| A-17 | Product | Installer lifecycle hook and complete desired ledger absent. |
| A-18 | Partial | Kill/injected storage and orderly reboot; no abrupt power loss. |
| A-19 | Target | Build49 isolated installed C API: local TIMEOUT/NULL plus fresh-ID remote PENDING lookup, scoped negative lookups, cancel and terminal CANCELLED; four target runs. |
| A-20 | Target | Client callback reentry/Close owner fixtures. |
| A-21 | Partial | Framework session/permit tests; product holder absent. |
| A-22 | Partial | Framework ONCE/SESSION tests; product holder absent. |
| A-23 | Partial | Framework PERSISTENT/session tests; holder cleanup absent. |
| A-24 | Partial | Isolated connection/resume tests; product session absent. |
| A-25 | Partial | Framework suspend fence; product data holder absent. |
| A-26 | Partial | Framework reconnect identity checks; product controller absent. |
| A-27 | Partial | Framework stale-generation fence; product continuation absent. |
| A-28 | Partial | Framework idle/absolute TTL tests; product holder absent. |
| A-29 | Partial | Framework expiry/provenance tests; model summary absent. |
| A-30 | Partial | Provenance revoke fixture; real derived data absent. |
| A-31 | Product | Real inference boundary and model context cleanup absent. |
| A-32 | Partial | Holder ACK retry metadata; physical erasure unproved. |
| A-33 | Target | Build46 packaged fixture: server ID overrides supplied ID; same holder/context cannot check or derive from another session's artifact. First session remains allowed; second has no artifact. Product holder remains separate. |
| A-34 | Partial | MEMORY_ONLY rule; actual memory pressure/spill unproved. |
| A-35 | Product | History/embedding writer and inherited restrictions absent. |
| A-36 | Product | External-model recipient integration absent. |
| A-37 | Partial | Isolated orderly reboot retains grant; argo restart and old session fence unproved. |
| A-38 | Partial | Framework requery path; real evicted holder data absent. |
| A-39 | Partial | Cleanup failure state visible; physical holder cleanup absent. |
| A-40 | Product | Multi-parent product summary/copy integration absent. |
| A-41 | Product | Product late result/model/history participant absent. |
| A-42 | Target | Framework inactive/closed session rejection. |
| A-43 | Target | Packaged daemon I/O and main context fixtures. |
| A-44 | Partial | Bounded thread design; sustained subscription load unproved. |
| A-45 | Partial | Async queue design; long approval wait load unproved. |
| A-46 | Target | Installed wire fragmentation/EOF/size fixtures. |
| A-47 | Partial | Build46 checker payload PID/UID/GID spoof denied session role; DLOG uses actual kernel peer. Outer versus inner role guard and other roles remain unproved. |
| A-48 | Partial | Rejected roles seen; explicit SO_PEERCRED failure injection pending. |
| A-49 | Partial | Bounds/fault fixtures; sustained overload measurements pending. |
| A-50 | Target | Socket activation and READY in matching service. |
| A-51 | Partial | Build45 four target rejections pass; explicit child FD close untraced. |
| A-52 | Target | Repeated service start with socket unit, Build43 100/100. |
| A-53 | Partial | Reentry/cache interleavings pass; full lock/no-wait invariant unproved. |
| A-54 | Target | Cache-only caller-context delivery, after-return test. |
| A-55 | Partial | Default deny same PID/UID rejection; real shared-UID roles absent. |
| A-56 | Target | Packaged shutdown/drain and ownership fixtures. |
| A-57 | Target | IO/owner late completion and FD reuse fixtures. |
| A-58 | Partial | Slow-reader bound; UNSYNCED/resync consequence needs proof. |
| A-59 | Target | Kernel PID/UID/GID and disconnect reason in target log. |
| A-60 | Partial | Retry metadata; deadline-expired holder ACK/product holder absent. |
| A-61 | Target | Generated parcel exchanges in installed API/wire fixtures. |
| A-62 | Partial | Build45 target version/size/count/trailing/EOF; early allocation and target NUL proof absent. |
| A-63 | Target | Build48 GBS isolated edit: deterministic/invalid schema tests plus regenerated header, client/daemon compile and link, license and exact restore. Installed runtime uses the original IDL. |

The unnumbered release gate is still open: no root-authenticated complete
current desired-definition producer exists. Production `--begin` therefore
fails without mutation. Total-registry import, `import_complete` fencing and
physical `cleanup_unknown` resolution cannot be claimed from the Build43
bootstrap receipt or isolated DB-only recovery tests.

---

[Related task](07-verification.en.md) ·
[Continue](16-maintenance-and-integration.en.md) · [Reading
paths](../README.md)
