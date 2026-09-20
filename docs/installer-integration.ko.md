# 플랫폼 Installer 통합 인계

상태: 2026-09-20 로컬 소스 조사. 이 문서는 아직 필요한 외부 통합을 설명하며,
Installer plugin 구현 완료나 emulator 설치 lifecycle 검증을 보고하지 않는다.
기존 callback만을 근거로 production metadata plugin을 등록해서는 안 된다.
구현된 provisioning 도구는 [installation authority](installation-authority.ko.md),
설계 의도는 [CEP 14절](CEP_Consent_Framework.md)을 참고한다.

## 조사한 소스 snapshot

참조 저장소는 `~/tizen/platform/core/appfw/` 아래에 있으며 읽기만 수행했다.
조사한 파일은 아래 로컬 commit과 일치했다. target 설치 패키지 버전을 뜻하지 않는다.

| 저장소 | 로컬 commit |
| --- | --- |
| `tizen-watcher` | `634db823961293fb7f24177ade9a1076640034b4` |
| `app-installers` | `d17375167982fa9f8038bdd66ea0bbbe3f93389e` |
| `slp-pkgmgr` | `3947f339c201570c96081eb6e97e32878ff91bf8` |
| `consent`, `src/tools/installation_authority.cc` | `9bdfcfaba6f4a7a0714d25cf2534deeceadea37f` |

아래 경로는 명시한 저장소 기준 상대 경로다. 다른 플랫폼 revision이나 Installer
backend를 통합할 때는 해당 함수의 동작을 다시 확인한다.

## 확인한 hook 순서와 한계

`tizen-watcher/src/tizen-watcher-plugin/watcher_metadata_plugin.cc`는
`PKGMGR_MDPARSER_PLUGIN_INSTALL`, `UPGRADE`, `UNINSTALL`, `CLEAN`, `UNDO`,
`REMOVED`, `RECOVER*`를 제공한다. 인자는 package ID, app ID, metadata뿐이다.
`CLEAN`과 `UNDO`는 아무 작업도 하지 않는다. recovery event 매핑은 설치 generation
규약이 아니므로 이를 승인 복원 방식으로 복사해서는 안 된다.

`app-installers/src/common/installer/app_installer.cc`의 `InstallSteps()`,
`UpdateSteps()`, `UninstallSteps()`에서 다음 실행 순서를 확인했다.

| 경로 | 관련 순서 | 의미 |
| --- | --- | --- |
| Install | pkgmgr 등록 → `INSTALL` → storage/ownership/symlink 단계 | `INSTALL`은 최종 commit이 아니다. |
| Upgrade | 파일/security 변경 → pkgmgr 수정 → `UPGRADE` → 남은 filesystem 단계 | callback 전에 기존 generation을 차단해야 한다. |
| Remove | `UNINSTALL` → filesystem/pkgmgr/security 제거 | 이른 tombstone 적용 후보지만 rollback 연동도 필요하다. |
| 성공 | 모든 process 단계 → 역순 cleanup → `sync()` → finished event | durable하고 재실행 가능한 완료 계약이 필요하다. |

`src/common/installer_runner.cc`의 `InstallerRunner::Run()`은 batch 전체를
처리한 뒤 cleanup한다. 뒤 package 실패로 앞 package도 undo할 수 있다.
`GlobalRecoveryFile::AppendCleanUp()`과 cleanup 결과를 확인하지 않는다.
`AppInstaller::Clean()`도 `RecoveryFile::WriteAndCommitFileContent()` 결과를
확인하지 않는다. 따라서 plugin이 검증된 durable commit 결정을 받는 구조가 아니다.

`src/common/step/pkgmgr/step_run_parser_plugins.cc`의
`StepRunParserPlugin::clean()`은 `CLEAN` callback 결과를 버린다.
`undo()`는 Install과 Upgrade에서 `UNDO`를 호출하며 Upgrade에는 이전 manifest를
전달한다. Uninstall 분기는 plugin 정보만 복원하고 `UNDO`를 호출하지 않는다.
따라서 plugin의 `vitalness=true`만으로 필요한 lifecycle을 강제할 수 없다.

`src/common/step/pkgmgr/step_recover_parser_plugins.cc`의
`StepRecoverParserPlugin::Cleanup()`은 정상 `CLEAN`과 다른 `CLEANUP`을 호출한다.
`RecoverPlugin()`은 plugin 실패를 기록하고 성공으로 계속한다.
`src/common/step/recovery/step_recovery.cc`의 `StepRecovery::process()`는
cleanup flag보다 먼저 Uninstall recovery를 분기한다. 모든 `RECOVER*` 또는
`CLEANUP` 호출을 설치 성공의 증거로 해석해서는 안 된다.

`src/common/plugins/metadata_plugin.cc`의 `MetadataPlugin::Run()`은
package/app별 metadata에 따라 callback을 호출한다. Upgrade의 `REMOVED`는
특정 app에서 metadata key가 사라졌다는 뜻일 수도 있으며 package 제거와 다르다.
이를 package 전체의 `consent_unregister()`에 직결하면 다른 app도 제거될 수 있다.

## Transaction 신원과 권한의 미충족 부분

`slp-pkgmgr/client/src/api_stub.cc`의 `GetReqKey()`는 새 client 요청마다 UUID를
만든다. `installer/src/api_stub.cc`의 `pkgmgr_installer_get_session_id()`는
metadata ABI에 없는 Installer handle을 통해 session ID를 제공한다.
`installer/src/pkgmgr_installer_info.h`는 target UID, privilege level과 flag만
제공하며 transaction/recovery ID는 제공하지 않는다. 새 API 요청은 새 UUID를
만들므로 결과가 불확실한 이전 설치의 재시도임을 이것만으로 식별할 수 없다.

`app-installers/src/common/recovery_file.cc`의 `WriteAndCommitFileContent()`는
작업 종류, unpacked 경로, package ID, backup/cleanup/security flag를 저장한다.
session UUID는 저장하지 않는다. `RecoveryFile::~RecoveryFile()`은 보존 설정이
없으면 임시 recovery 파일을 제거한다. 그 경로는 durable한 설치 incarnation이나
metadata callback에 전달되는 신원이 아니다.

`app-installers/src/common/step/configuration/step_configure.cc`의
`StepConfigure::precheck()`는 root 또는 `app_fw` 실행을 허용한다. 반면
`consent/src/tools/installation_authority.cc`의 `main()`은 effective UID 0을
요구한다. 따라서 모든 metadata plugin에서 authority 도구를 직접 실행할 수 있다고
가정할 수 없다. package 인증서 privilege는 호출 process의 역할 신원이 아니다.

## 필요한 외부 adapter 계약

다음은 제안하는 플랫폼 통합 계약이며 구현된 adapter API가 아니다.
authority 도구에는 `begin/attach/commit/remove`, expected-generation 검사,
operation receipt, durable 파일 교체가 구현되어 있다. 도구 자체가 플랫폼
transaction의 결과를 발견하거나 증명하지는 못한다.

1. package 변경 전에 안정된 transaction UUID를 영속화한다. target UID, package,
   작업, 이전/새 generation, 전체 app/definition desired set 또는 검증된 digest를
   결합하고 package별 작업을 직렬화한다.
2. 서로 다른 하위 작업 ID를 영속화하고 결과가 불확실할 때 같은 ID와 payload로
   재시도한다. 정상·undo·재부팅 recovery에 같은 transaction 신원을 전달하며
   recovery 전달을 위해 새 신원을 만들지 않는다.
3. 교체 전에 `begin`으로 기존 generation을 차단한다. 필요한 모든 app을 `attach`로
   준비한다. pending generation은 consent에서 사용 불가능한 상태를 유지한다.
4. 묶음 설치라면 batch 전체를 포함한 플랫폼 최종 결과를 영속화하고 확인한다.
   durable commit 결정만 authority `commit`을 허용하며 recovery도 같은 결정을
   안전하게 재실행해야 한다.
5. authority와 정의 등록이 성공할 때까지 재조정할 작업을 영속적으로 유지한다.
   authority 활성화 후 인증된 Installer API로 등록하며, 중단된 작업을 재조정한 뒤
   통합 성공을 보고한다.
6. 보호된 authority 쓰기는 인증된 privileged adapter를 사용한다. 플랫폼 근거로
   실제 UID, executable, SMACK role을 확인하여 설정한다. authority 권한을 완화하거나
   공유 UID만으로 역할을 신뢰해서는 안 된다.

플랫폼 담당자는 lifecycle/commit/recovery hook과 durable 기록, backend/process
신원, 제한된 재시도 및 receipt 보존 정책을 제공해야 한다. root 소유 상태,
pkgmgr-info 검증, consent 정책 저장소는 별도이므로 단일 원자적 DB transaction으로
가정해서는 안 된다.

## Package lifecycle 규칙

설치·재설치·교체에는 새 generation을 사용한다. 같은 package/app 이름, version,
인증서, 초 단위 설치 시각만으로 같은 incarnation임을 증명할 수 없다.
metadata가 사라진 app도 포함하여 package 전체 desired set을 재조정하고,
무관한 package는 보존하며 오래된 정의는 명시적으로 비활성화한다.
app별 callback 순서는 전체 desired set이 완성되었다는 경계가 아니다.

제거는 generation tombstone을 먼저 기록한 뒤 expected generation과 안정된
operation ID로 package 전체 `consent_unregister()`를 재시도한다.
지연된 제거가 이후 설치에 영향을 주어서는 안 된다. rollback 시 실패한 generation은
pending/removed로 유지한다. 복원 package는 신뢰 검증, 새 `begin/attach/commit`,
정의 등록이 필요하다. 복원된 package metadata에서 과거 승인, 소비된 grant,
session을 복원해서는 안 된다.

## Production 등록 전에 필요한 검증

격리된 authority fixture뿐 아니라 실제 선택한 backend와 인증된 adapter로 검증한다.
다중 app, metadata 제거, 신원 불일치, 재설치, package 제거, rollback을 실행하고
다른 package에 영향이 없는지 확인한다. 각 durable 경계 전후와 응답 전에 중단하여
Installer 재시작·emulator 재부팅 후 재실행을 확인한다. 앞 package의 callback 이후
batch가 실패하는 경우도 포함한다. adapter 실패와 storage 사용 불가 상태에서
불확실한 결과는 재조정이 끝날 때까지 차단되어야 한다.

이 Installer lifecycle 검증은 외부 통합 이후 수행할 항목이다. 기존 consent build,
authority test, DB recovery test의 성공이 이 검증의 완료를 뜻하지 않는다.
