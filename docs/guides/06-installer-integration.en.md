# Guide 06: Platform Installer integration handoff

Status: local source investigation, 2026-09-20. This document describes the
external integration still required; it does not report a completed Installer
plugin or an emulator-verified installation lifecycle. No production metadata
plugin should be registered on the strength of the existing callbacks alone.
See [installation authority](03-installation-authority.en.md) for the implemented
provisioning tool and [CEP section 14](../design/01-consent-framework.md) for design intent.

## Inspected source snapshots

Reference repositories are under `~/tizen/platform/core/appfw/` and were read only.
The inspected files matched these local commits; these are not target package versions.

| Repository | Local commit |
| --- | --- |
| `tizen-watcher` | `634db823961293fb7f24177ade9a1076640034b4` |
| `app-installers` | `d17375167982fa9f8038bdd66ea0bbbe3f93389e` |
| `slp-pkgmgr` | `3947f339c201570c96081eb6e97e32878ff91bf8` |
| `consent`, `src/tools/installation_authority.cc` | `9bdfcfaba6f4a7a0714d25cf2534deeceadea37f` |

Paths below are relative to the named repository. Recheck the corresponding
functions when integrating another platform revision or installer backend.

## Observed hook order and limitations

`tizen-watcher/src/tizen-watcher-plugin/watcher_metadata_plugin.cc` exports
`PKGMGR_MDPARSER_PLUGIN_INSTALL`, `UPGRADE`, `UNINSTALL`, `CLEAN`, `UNDO`,
`REMOVED` and `RECOVER*`. Each receives only package ID, app ID and metadata.
Its `CLEAN` and `UNDO` do nothing. Its recovery event mappings are not an
installation-generation protocol and must not be copied as approval restoration.

In `app-installers/src/common/installer/app_installer.cc`, `InstallSteps()`,
`UpdateSteps()` and `UninstallSteps()` establish the following order:

| Path | Relevant sequence | Consequence |
| --- | --- | --- |
| Install | pkgmgr registration → `INSTALL` → storage/ownership/symlink steps | `INSTALL` is not final commit. |
| Upgrade | file/security changes → pkgmgr update → `UPGRADE` → remaining filesystem steps | The old generation must be fenced before this callback. |
| Remove | `UNINSTALL` → filesystem/pkgmgr/security removal | This is an early tombstone candidate, with rollback handling still required. |
| Success | all process steps → reverse cleanup → `sync()` → finished event | Cleanup needs a durable, replayable completion contract. |

`src/common/installer_runner.cc`, `InstallerRunner::Run()`, processes a batch
before cleanup. A later package failure can undo earlier packages. It ignores
the result of `GlobalRecoveryFile::AppendCleanUp()` and cleanup results.
`AppInstaller::Clean()` also ignores `RecoveryFile::WriteAndCommitFileContent()`.
These calls do not provide a checked durable commit decision to a plugin.

`src/common/step/pkgmgr/step_run_parser_plugins.cc`,
`StepRunParserPlugin::clean()`, discards the result of the `CLEAN` callback.
Its `undo()` calls `UNDO` for Install and Upgrade, using the old manifest for
Upgrade; the Uninstall branch restores plugin information without calling `UNDO`.
Therefore plugin `vitalness=true` alone cannot enforce the required lifecycle.

`src/common/step/pkgmgr/step_recover_parser_plugins.cc`,
`StepRecoverParserPlugin::Cleanup()`, invokes `CLEANUP`, distinct from normal
`CLEAN`. `RecoverPlugin()` logs plugin failures and continues successfully.
`src/common/step/recovery/step_recovery.cc`, `StepRecovery::process()`, dispatches
Uninstall recovery before checking the cleanup flag. Do not interpret every
`RECOVER*` or `CLEANUP` invocation as evidence of a successful installation.

`src/common/plugins/metadata_plugin.cc`, `MetadataPlugin::Run()`, invokes
callbacks per package/app metadata. During Upgrade, `REMOVED` also means that
one app lost the metadata key; it does not necessarily mean package removal.
Mapping it directly to package-wide `consent_unregister()` can remove other apps.

## Transaction identity and privilege gap

`slp-pkgmgr/client/src/api_stub.cc`, `GetReqKey()`, creates a UUID for each new
client request. `installer/src/api_stub.cc`, `pkgmgr_installer_get_session_id()`,
exposes the session ID through an installer handle absent from the metadata ABI.
`installer/src/pkgmgr_installer_info.h` exposes target UID, privilege level and
flags, but no transaction/recovery ID. A new API request generates a new UUID;
this alone does not identify a retry of an uncertain previous installation.

`app-installers/src/common/recovery_file.cc`, `WriteAndCommitFileContent()`,
stores operation type, unpacked path, package ID, backup/cleanup/security flags.
It does not store that session UUID. `RecoveryFile::~RecoveryFile()` removes
temporary recovery files unless retained. Their paths are not a durable
installation incarnation or an identity delivered to metadata callbacks.

`app-installers/src/common/step/configuration/step_configure.cc`,
`StepConfigure::precheck()`, permits root or `app_fw` execution. In contrast,
`consent/src/tools/installation_authority.cc`, `main()`, requires effective UID 0.
Direct execution of the authority tool from every metadata plugin is therefore
not established. Package certificate privilege is not the caller's process role.

## Required external adapter contract

The following is a proposed platform integration contract, not an implemented
adapter API. The authority tool already implements `begin/attach/commit/remove`,
expected-generation checks, operation receipts and durable file replacement.
It cannot discover or prove the platform transaction outcome itself.

1. Persist a stable transaction UUID before package mutation. Bind it to target
   UID, package, operation, previous/new generation and the complete desired
   app/definition set or its verified digest. Serialize operations per package.
2. Persist distinct sub-operation IDs and reuse the same IDs and payloads after
   uncertain results. Carry the transaction identity through normal, undo and
   reboot recovery paths; do not create a new identity for recovery delivery.
3. Fence the old generation with `begin` before replacement. Stage every desired
   app with `attach`. A pending generation must remain unavailable for consent.
4. Persist and check the final platform outcome, including the whole batch when
   installation is grouped. Only a durable commit decision may authorize
   authority `commit`; recovery must replay that same decision safely.
5. Keep durable reconciliation work until authority and definition registration
   succeed. Register through the authenticated Installer API after authority
   activation; reconcile interrupted work before reporting integration success.
6. Use an authenticated privileged adapter for protected authority writes.
   Provision its actual UID, executable and SMACK role from verified platform
   evidence. Do not relax authority permissions or trust a shared UID alone.

The platform owner must supply the lifecycle/commit/recovery hook and its durable
record, the backend/process identity, and a bounded retry/receipt-retention
policy. Root-owned state, pkgmgr-info verification and consent policy storage
are separate stores; this contract must not imply one atomic DB transaction.

## Package lifecycle rules

Install, reinstall and replacement use fresh generations. Same package/app names,
version, certificate or second-resolution installation time do not prove the
same incarnation. Reconcile a complete package desired set, including apps whose
metadata disappeared; preserve unrelated packages and explicitly retire stale
definitions. Per-app callback order is not a complete desired-set boundary.

Removal first records the generation tombstone, then retries package-wide
`consent_unregister()` with its expected generation and stable operation ID.
Delayed removal must not affect a later installation. On rollback, keep the
failed generation pending/removed. A restored package needs trusted verification,
a fresh `begin/attach/commit` cycle and definition registration. Never reconstruct
old approvals, consumed grants or sessions from the restored package metadata.

## Verification required before production registration

Exercise the real chosen backend and authenticated adapter, not only isolated
authority fixtures. Cover multiple apps, metadata removal, mismatched identity,
reinstall, package removal and rollback without affecting another package.
Inject interruption before/after each durable boundary and before replies;
verify replay after Installer restart and emulator reboot, including batch
failure after an earlier package callback. Test adapter failure and unavailable
storage: uncertain outcomes must remain fenced until reconciled.

These Installer lifecycle checks are pending external integration. Existing
consent builds, authority tests and DB recovery tests do not establish them.
