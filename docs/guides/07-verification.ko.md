# 가이드 07: 검증 근거와 남은 범위

본 결과는 기반 증분이며 제품 연동 전체 완료를 뜻하지 않습니다. 아래 내용은
2026-09-20 선택한 개발 emulator에서 관측했습니다. 이후 working tree 변경은
별도 표기 없이는 고정 빌드의 검증 범위에 포함되지 않습니다.

## 고정 빌드 7

- GBS source tree: `10749441a5839db60024af9ca2d61c6a5164f73c` (commit이 아닌 tree).
- 명령: `gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4 --overwrite`.
- `/var/tmp/consent-artifacts/gbs-build-7/`에 source tar, RPM 4개,
  `log.txt`, `source-tree.txt`, `changed-files.tsv`, `SHA256SUMS`를 보관했습니다.
- source tar SHA256: `debb4ef05a631f058897731bfb4f8b596a4884c5ed424226907c01fecb62b70e`.
- CTest 4/4 PASS: client 0.28초, repository fault 0.10초, repository 0.86초,
  IDL compiler 0.12초. Release에도 `-UNDEBUG`로 검증 assert를 실행합니다.
- 실제 target Parcel 라이브러리로 시험했습니다. compiler는 잘못된 스키마,
  결정적 출력, helper/type 이름 충돌과 전이 크기 상한을 검증합니다.
  native codec은 golden bytes, truncation, 할당 상한을 검증합니다.
- public library export는 정확히 `consent_*` 38개, `CONSENT_0.1`이며
  C++ 구현 심볼을 노출하지 않습니다.

후속 `ValidString` 예약명 회귀와 지속적인 SQLite 손상 실패 fixture 보강은
본 빌드 결과와 구분합니다.

## 실제 emulator

선택 장치는 `emulator-26101`, x86_64, kernel `4.4.35-x86_64`, SMACK 지원
systemd244, Parcel0.18.15입니다. 재현 시 `sdb devices`로 현재 serial을 확인하며
다른 장치에서도 동일 serial이라고 가정하지 않습니다.

consent/consentd/consent-tests RPM을 설치했습니다. 같은 버전 개발 RPM 반복 설치는
해당 패키지만 대상으로 `rpm -Uv --replacepkgs --replacefiles`가 필요했습니다.
시험은 root, `SmackProcessLabel=System`으로 실행했고 production role은
empty/default deny를 유지했습니다. 격리 inventory는 실제 pkgmgr 조회를 생략하므로
가상의 demo 앱 검증은 제품 app/package 연동 완료 근거가 아닙니다.

```sh
sdb -s emulator-26101 push scripts/emulator-scenario.sh /tmp/consent-emulator-scenario.sh
sdb -s emulator-26101 shell 'systemd-run --wait --pipe --unit=consent-validation-basic -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh basic'
```

`basic`은 새 격리 상태에서 실행하며 기존 설치 registry를 지우지 않습니다.
후속 단계는 persistent/running-delete/stopped-delete/stale-replace/corrupt/
unauthorized이며 매번 다른 transient unit 이름을 사용합니다. script는 외부 SQLite
조회 전에 daemon을 정지하고 integrity_check가 정확히 ok, user_version이 1,
예상 metadata와 registry revision이 있는지 assert합니다.

| 관측 시나리오 | 결과 및 검증 범위 |
|---|---|
| C API basic | PASS: package/app 등록, 재시도/충돌, 반환 후 SYNC/ASYNC callback, UI prompt/respond, persistent AUTHORIZE 중복 처리, SESSION 승인, artifact cleanup ACK와 CLOSED |
| daemon 재시작 | PASS: 정상 종료/시작 후 persistent 승인 유지 |
| emulator 정상 재부팅 | PASS: persistent 승인 유지, boot ID `774896c0-0fc6-4683-b485-b193d19b28c9` → `b3e3c620-cedf-41be-8e98-6430431dc4aa` |
| 실행 중 DB 삭제 | PASS: idle 상태 열린 DB unlink 후 정의 복구, 과거 승인은 CONSENT_REQUIRED |
| 정지 중 DB 삭제 | PASS: 동일하게 정의만 복구 |
| 과거 정상 DB 교체 | PASS: 다른 inode 파일로 pathname 교체 후 과거 승인 부활 없음 |
| DB 손상 | PASS: 손상 main DB 격리/재생성, grant 복구 없음 |
| 같은 UID의 다른 실행파일 | PASS: 서버 kernel credential 로그 role=rejected 확인; 단순 실행 실패만으로 판정하지 않음 |
| production activation/default deny | PASS: 실제 client가 consentd에 도달했으며 journal에 pid23306 uid0 gid0 role=rejected |

보관 로그는 `/var/tmp/consent-artifacts/emulator-build-7/`의
platform-production.log, isolated-daemon.log, recovered-database.log입니다.
production은 journald, 격리 transient daemon stderr는 dlog의
STDERR_consentd-test에서 확인했습니다. 최초 basic은 event에 status가 포함되어
Parcel encoder가 거부하는 결함을 발견했으며 빌드7에는 수정이 포함됐습니다.

## Production endpoint 검증

실제 activated socket probe 결과는 SO_PEERCRED pid1/uid0/gid0/길이12,
SO_PEERSEC은 마지막 NUL 포함 길이19 `System::Privileged`, getpeername은
AF_UNIX와 마지막 NUL 포함 길이22 `/run/.consentd.sock`입니다.

- root가 직접 bind한 가짜 서버: production client가 hello 송신 전에 -13으로 거부.
- 다른 systemd socket을 consent 경로로 rename: uid0/pid1/동일 SMACK label이어도
  kernel 원래 bind 주소 `/run/consent-endpoint-other.sock`(길이35)을 확인하여 -13.
- 정상 production socket: endpoint 인증 이후 서버 default deny가 실행됨을
  role rejection journal로 구분해 확인.

license를 포함한 fixture는 후속 working tree의 `src/tests/endpoint-fixture.c`에
보관했으며 고정 기반 소스에는 포함되지 않습니다. 최초 시험은
host `gcc -Wall -Wextra -O2`로 빌드해 `/usr/libexec/consent/tests/endpoint-fixture`에
복사하고 chsmack으로 `_` label을 설정했습니다. production client/library는
빌드7 RPM 그대로 사용했습니다. fixture의 GBS 패키징은 다음 빌드에 반영합니다.
이 장치의 /tmp는 noexec이므로 실행파일을 그곳에 배치하지 않습니다.
후속 `scripts/emulator-endpoint-test.sh`는 systemd socket rename 시험 후 production
socket을 복구합니다. consent endpoint를 일시 교체하므로 개발 emulator 전용입니다.
stat 전후 동일성만으로 endpoint 신뢰를 증명한다고 주장하지 않습니다.

## 명시적 제한과 후속 작업

emulator 강제 전원 차단은 시험하지 않았습니다. idle-open unlink는 쓰기 중 삭제와
다릅니다. repository 시험의 synthetic sidecar는 실제 hot rollback journal 근거가
아닙니다. commit 전/중/후 응답 전 kill, 동시 ONCE/cancel/respond, 살아 있는
client의 stale cache 무효화, 재시작 holder cleanup은 후속 검증이 필요합니다.
복구 뒤 새 client를 만드는 것은 기존 live cache 시험이 아닙니다.

제품 argo/CM/CE/UI/Installer 신원과 정책 배포, 실제 Installer lifecycle hook,
실제 승인 UI 연동은 미완료입니다. 보호된 installation authority 도구가 있으며
플랫폼 hook 연동 완료를 의미하지 않습니다. literal 다국어 title/body와 exact scope
비교는 구현했고 typed template/schema와 자원별 비교는 미완료입니다. handle 사이
캐시 공유는 없습니다. registry 소실은 fail-closed이며 신뢰된 reseed 관리 절차는
미구현입니다. DB marker는 교체를 탐지하지만 권한 있는 동일 inode/incarnation
되감기를 탐지하려면 별도 commit별 monotonic authority가 필요합니다. kernel4.4의
최초 연결 PID 수명 race는 남습니다. holder 종료는 신뢰 controller의 보고가 없으면
session lease 만료까지 탐지가 지연될 수 있습니다.

## 실제 명령/출력 발췌 (빌드7)

최초 실행은 event encoding 결함으로 연결이 닫히기 전에 정의 하나를 내구성 있게
등록했습니다. 빌드7 설치 후 같은 보호된 시험 generation과 등록 operation ID로
재시도했습니다.

```text
systemd-run --wait --pipe --unit=consent-basic-seventh -p SmackProcessLabel=System /usr/libexec/consent/tests/consent-scenario-isolated basic 3334e4f5-65f2-4a15-bd96-8bf585ce5530
PASS basic: registration/retry, SYNC/ASYNC, UI, authorization, session, artifact cleanup
Main processes terminated with: code=exited/status=0
```

reboot 전에 persistent를 실행하고 정지 상태 DB를
`/opt/var/lib/consent-test/approved-snapshot.db`로 복사한 뒤 선택한 sdb shell에서
`sync; reboot`를 실행했습니다. 연결 복구와 `sdb ... root on` 이후 휘발성 /tmp의
script를 다시 push했습니다.

```text
systemd-run --wait --pipe --unit=consent-persistent-after-reboot -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh persistent
PASS persistent
PASS integrity_check=ok schema=1 expected_metadata_present
PASS emulator phase=persistent
```

동일 명령 형식에서 unit을 consent-running-delete, consent-stopped-delete,
consent-stale-replace, consent-corrupt, consent-unauthorized로 바꿔 대응 phase를
실행했습니다. 모두 status0이며 복구 4단계는 PASS recovered, integrity assert,
PASS emulator phase=<phase>를 출력했습니다. unauthorized의 client는 연결 종료로
status=-2002이며 PASS same-UID unregistered executable rejected를 출력했습니다.
판정 근거는 서버의 pid3746 uid0 gid0 role=rejected 로그입니다.

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

위 내용은 실제 tool 출력에서 발췌해 옮긴 것으로 별도 raw log 파일을 생성했다고
주장하지 않습니다. 보관한 daemon/platform/DB 로그 경로는 앞 절과 같습니다.

## 빌드9: schema migration, cleanup, 내구성

Tree `e13e9e9b6709652663cf5ed51c58af28a6bca33f`는 CTest5/5 통과했습니다
(client0.59초, crash0.25초, fault0.19초, repository3.49초, IDL0.13초).
고정 산출물은 `/var/tmp/consent-artifacts/gbs-build-9/`, source SHA256은
`f7cf5f4f71f55ccdb2e214c3667d8e810ad4dd528585e18b9c9b560a9f43ae2b`입니다.
consent_cleanup_get_pending 추가 후 export는39개입니다.

emulator에서 빌드7으로 v1 DB에 활성 PERSISTENT grant1개를 만들고 정지했습니다.
빌드9 설치 후 persistent C 시나리오가 PASS이고 integrity_check=ok/schema=2로
기존 승인을 유지했습니다. 정의와 transient session은 문서의 migration 계약을
따릅니다.

holder-restart는 패키징한 C 도구의 서로 다른 두 process를 실행했습니다. 첫째는
session을 닫고 cleanup pending을 남겼으며 artifact ID를 전달하지 않았습니다.
둘째는 public pending API로 발견한 뒤 기존 데이터 등록 거부, cleanup 실패 ACK,
CLEANUP_FAILED 재조회, 성공 ACK, 중복 ACK를 검증했습니다. 모든 assertion과
schema2 integrity가 통과했습니다. 이는 metadata와 holder 보고 ACK 검증이며
제품 holder backend의 물리적 삭제 완료 근거는 아닙니다.

패키지 제거 후 두 앱을 모두 차단하고 Installer authority가 새 generation을
발급했습니다. 두 앱 재등록에 과거 승인은 없으며 이전 unregister 재시도는 -116입니다.
script는 처음 제거된 정의에 not-found 오류를 예상했지만 실제 정책은 status0/DENIED가
맞아 script만 수정해 push한 뒤 같은 빌드9 binary로 완료했습니다. 로그는
`/var/tmp/consent-artifacts/` 아래 emulator-build-9/holder-install-cache.log 및
emulator-build-9/installation-cache.log입니다.

live-cache는 같은 handle에서 authoritative actor check보다 request를 먼저 보내고
기존 lease 안임을 확인했습니다. revoke37174us, policy update37975us, SESSION
suspend36035us에서 통과했습니다. 이미 suspend된 session을 close한 것은 별도의
ACTIVE→close cache 무효화 근거가 아닙니다. unregister probe는 helper의 필수
stable ID 누락으로 실패해 뒤 DB-loss 단계까지 도달하지 못했습니다. 다음 fixture에서
수정하며 빌드9 전체 cache 시나리오가 통과했다고 표시하지 않습니다. 독립 ONCE
경쟁은 정확히1승자/동일 retry receipt를 확인했지만 뒤 remote cancel 시험도 새
helper의 필수ID 누락으로 중단됐습니다. 다음 snapshot의 수정은 daemon 결함과
구분합니다.

패키징한 repository-crash-test와 repository-fault-test도 emulator에서
systemd-run --wait --pipe -p SmackProcessLabel=System으로 실행했습니다.
emulator-build-9/crash-fault.log에 다음 PASS를 보관했습니다.

```text
PASS SIGKILL boundary=1 validated_hot_journal=0 delete_during_write=0 decision=ALLOWED integrity=ok
PASS SIGKILL boundary=2 validated_hot_journal=1 delete_during_write=0 decision=ALLOWED integrity=ok
PASS SIGKILL boundary=3 validated_hot_journal=0 delete_during_write=0 decision=CONSENT_REQUIRED integrity=ok
PASS SIGKILL boundary=2 validated_hot_journal=1 delete_during_write=1 decision=CONSENT_REQUIRED integrity=ok
PASS directory fsync uncertainty stays fenced across retry and restart
PASS captured SQLite corruption code survives rollback
PASS metadata corruption cannot trap recovery in repeated failing SELECT
```

경계1/2는 revoke commit 전 중단으로 기존 승인을 유지하며, 경계3은 commit 후
repository reply 전 중단으로 철회를 유지합니다. 경계2는 실제 rollback-journal
header를 검증합니다. unlink+SIGKILL은 startup 복구이고 writer가 계속 실행되는
시험이 아닙니다. 빌드9 fixture는 /tmp(tmpfs)를 사용하므로 process-kill 일관성
근거이며 영속 매체 전원 차단 근거가 아닙니다. 다음 fixture에 선택 가능한 영속
시험 root와 별도 continuing-writer 케이스를 추가합니다.

## 빌드10: target 시나리오 완료

Tree `1fddd2b2e38e45b79ce8ff12980b0fda2f2d6906`, source SHA256
`581d048b325c7194a29498ae220f6d862fbc7207277ce40086213bace38292d9`를
`/var/tmp/consent-artifacts/gbs-build-10/`에 RPM4개/build log/LastTest.log/source
archive와 tree manifest/checksum과 함께 고정했습니다. GBS5/5 PASS:
client0.59초, crash0.35초, fault0.11초, repository3.40초, IDL0.13초입니다.
C fixture의 stable ID 수정과 계속 실행되는 writer의 DB 삭제 시험을 포함합니다.

동일 emulator에 RPM 설치 후 아래 명령이 각각 status0으로 완료됐습니다.
모두 선택한 sdb shell을 통해 실행했습니다.

```sh
systemd-run --wait --pipe --unit=consent-races-ten -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh races
systemd-run --wait --pipe --unit=consent-holder-ten -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh holder-restart
systemd-run --wait --pipe --unit=consent-cache-ten -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh cache
```

- 독립된 두 연결의 ONCE 경쟁에서 정확히 하나만 ALLOWED, 다른 것은
  CONSENT_REQUIRED이며 승자의 재시도 receipt가 같습니다. cancel/respond 순서별
  처리, 실제 동시 경쟁, deadline 만료에서 최종 callback1회와 잘못된 승인 차단을
  확인했습니다.
- 서로 다른 holder process가 artifact ID 전달 없이 이전 pending을 발견하고,
  등록 권한 이전 거부, cleanup 실패 기록, 재시도 성공 ACK를 검증했습니다.
  schema2 integrity와 예상 metadata가 정상입니다.
- cache handle2개를 끝까지 유지했습니다. 변경 후 첫 actor request가 authoritative
  actor check보다 먼저이며 원래 lease 안임을 확인했습니다. 경과 microsecond는
  revoke39618, policy46282, SESSION suspend35015, package제거39344, DB삭제68304로
  모두 통과했습니다. DB삭제 시 epoch 변경/과거승인소실/정의복구/새승인과cache사용을
  확인했습니다. raw build10 출력의 suspend/close 중 독립 cache 무효화는
  **suspend만** 검증했습니다. suspend된 session의 관리 close도 실행했습니다.

실제 stdout은 `/var/tmp/consent-artifacts/emulator-build-10/races-holder-cache.log`
입니다. 세 단계 모두 integrity/schema2/metadata assert로 마칩니다.

wire fixture는 consent-wire-ten으로 실행하면서 shell에서 MainPID 전후를
비교했습니다. `/var/tmp/consent-artifacts/emulator-build-10/wire.log`에 동일
PID11388/모든 hello의 동일 epoch와 다음 결과를 보관했습니다.

- 분할 header/body와 합쳐 보낸 native Parcel16프레임 정상 처리.
- 0/초과길이, 배열원소누락, 거대문자열길이, 후행바이트, 중간EOF와 부분프레임
  timeout에서 해당 socket 종료.
- checker 연결 정확히24개 허용/4개 거부, 동일 daemon의 uid-connection-limit 로그4개.
- slow-reader에 완전한38바이트 프레임2116개(80408바이트)를 보낸 뒤 output-limit으로
  연결 종료. 별도 client는13회 hello 감시 동안 응답을 유지했습니다. 실제 종료 사유는
  partial input timeout이 아닌 output-limit이며 PID/epoch가 바뀌지 않았습니다.

이 malformed hello 시험으로 모든 변경 요청의 부분실행 금지를 증명하지 않습니다.
이후 일반 service stop은 pending DB job이나 partial I/O 중 종료 근거가 아닙니다.

영속 emulator 상태에서 storage crash fixture를 실행했습니다.

```sh
systemd-run --wait --pipe --unit=consent-crash-persistent-ten -p SmackProcessLabel=System /usr/libexec/consent/tests/repository-crash-test --state-root /opt/var/lib/consent-test
```

`/var/tmp/consent-artifacts/emulator-build-10/crash-persistent.log`에는 기존
SIGKILL4개와 다음 결과를 기록했습니다.

```text
PASS live-writer boundary=2 validated_hot_journal=1 delete_during_write=1 decision=CONSENT_REQUIRED integrity=ok
```

마지막 경우 부모가 실제 journal header를 검사하고 mainDB sync에 멈춘 writer의
main DB를 unlink한 뒤 **같은 writer**를 재개합니다. 해당 쓰기는 성공을 게시하지
못하고 **같은 Repository**의 다음 query가 새 epoch와 재승인 필요 상태를 확인합니다.
이는 target 영속 파일시스템에서 process 중단/복구 근거이며 emulator 강제 전원
차단 근거가 아닙니다. fault interposition은 test target에만 존재합니다.

이 checkpoint 이후의 구체적 미검증 항목은 FULL/IOERR 보존, partial-I/O 종료,
pending DB job 중 종료, 독립 ACTIVE→close 캐시 무효화, 강제 전원 차단입니다.
제품 신원/Installer/UI와 typed localization 제한은 앞 절과 같습니다. 이후
working tree에 추가한 시험/문서가 자동으로 빌드10 검증에 포함되지는 않습니다.

### 빌드10 검증 이후 발견된 미완료 경로

최종 검토에서 derived 데이터 생성 전 parent 설치 provenance 재검증이 일관되게
적용되지 않고, data-check/derived 응답의 commit 후 게시 직전 provenance 재검증이
완전하지 않은 경로를 확인했습니다. 검증 경계 사이 외부 installation generation이
회전하면 metadata 결과가 이미 낡은 상태일 수 있습니다. 빌드10은 이 계약을
**완료하지 않았습니다**. 후속 필수 수정에서 재귀 parent/context/holder/session/
generation/retention/revocation 공통 검증과 검증 경계 사이 generation 회전 회귀를
추가합니다. 빌드10 산출물과 관측 결과는 그대로 보존합니다.

다중 조건 UI 요청에서도 prompt 생성 시 이미 허용됐던 조건이 다른 조건의 승인을
기다리는 동안 소비/철회되면 advisory 최종 결과에 ALLOWED가 남을 수 있습니다.
현재 조건 전체의 최종 UI 재평가는 미완료입니다. 실제 AUTHORIZE는 필수 조건의
AND를 다시 평가하며 request/result/cache는 보호 작업 실행 permit이 아닙니다.
이 UI advisory 공백도 빌드10 완료 범위에 포함하지 않습니다.

## 빌드11: 시험 초기화 실패, RPM 없음

소스 tree `f4414b0a1fac5861ef9a1d445b5e18471d773062`는 컴파일됐지만
CTest는4/5만 통과했다. 새 일반 저장소 오류 fixture가 영구 승인 요청을
만들 때 필수 `operation_id`를 누락하여 FULL/IOERR 주입 전에 실패했다.
빌드11 RPM 및 emulator 검증을 주장하지 않는다. 소스 archive, 빌드 로그,
`LastTest.log`, 실패 설명은 `/var/tmp/consent-artifacts/gbs-build-11-failed/`에
보존했다. 후속 소스에서 operation identity를 추가했으며 이후 성공 결과는
해당 후속 snapshot의 증거로 구분한다.

## 빌드12: provenance·UI 최종 평가·저장소 오류 회귀

GBS 소스 tree `bea3323de93eb4d843bad2293dc3ed790354e529`, 소스 archive
SHA256 `5ffd2d2e5a9cf02a917cd281d1b40add34f95f735948e629cfe06db9d949fb6f`에서
CTest 7개 suite가 모두 통과했다. 고정 소스, RPM 4개, 전체 빌드 로그,
`LastTest.log`, 파일 목록과 checksum은
`/var/tmp/consent-artifacts/gbs-build-12/`에 있다. 빌드 명령:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

assert를 켜고 실행했다: client0.59초, crash0.35초, fault0.11초,
provenance1.60초, repository3.42초, UI1.56초, IDL0.13초. 새 저장소 시험
2개도 패키징됐다. 정확히 이 runtime/library/test RPM을 `emulator-26101`에
설치했다. 후속 working tree 변경은 이 증거에 포함하지 않는다. emulator
스크립트도 최신 작업 파일이 아니라 해당 tree에서 추출했다.

실제 명령은 다음 형식이며 각 phase에 별도 unit을 사용했다.

```sh
systemd-run --wait --pipe --unit=consent-ui-twelve -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh ui-reevaluate
# consent-races-twelve/races, consent-holder-twelve/holder-restart,
# consent-cache-twelve/cache, consent-wire-twelve/wire도 각각 실행.
systemd-run --wait --pipe --unit=consent-storage-twelve -p SmackProcessLabel=System /bin/sh -c 'set -e; /usr/libexec/consent/tests/repository-provenance-test; /usr/libexec/consent/tests/repository-ui-test; /usr/libexec/consent/tests/repository-fault-test; /usr/libexec/consent/tests/repository-crash-test --state-root /opt/var/lib/consent-test'
```

`/var/tmp/consent-artifacts/emulator-build-12/`의 결과:

- `ui.log`: 실제 C API/Parcel/daemon으로 철회, TIMED 만료, 다른 실행의
  ONCE 소비, 정상 AND, DENIED를 통과했다. 각각 최종 콜백 1회이며 후속
  prompt/response 재호출은 실패했다. 새로 승인한 B는 소비되지 않은 ONCE
  1회를 유지한다. A가 부족하면 실제 조건별 결과·사유와 함께 전체가
  terminal INVALIDATED가 된다. 별도 저장소 UI suite는 UI-only 신원,
  재평가 예외 rollback, 같은 토큰 재시도도 검증한다.
- `races-holder-cache.log`: 독립 연결 ONCE 경쟁과 원격 취소/응답/deadline
  경쟁, 새 holder 프로세스의 정리 목록 발견·실패·재시도·ACK가 통과했다.
  원래 cache lease 내 무효화 경과시간은 철회37696µs, 정책37577µs,
  suspend37560µs, 제거34184µs, DB 삭제54128µs다. 독립 ACTIVE→close
  cache 무효화는 **이 빌드의 검증 범위가 아니다**.
- `storage.log`: target에서 provenance 6개가 통과했다. Tick 없이 B만
  generation을 회전하면 A+B 다단계 파생을 차단하며 다른 package는 유지한다.
  소비된 ONCE/만료된 TIMED 접근 승인은 독립 보관 권한을 없애거나 TTL을
  연장하지 않는다. 실제 SQLite COMMIT 반환 직후 generation 회전 주입으로
  register/derive/data-check/reuse-data의 이전 성공 게시를 차단하고 DB에 남은
  자식도 사용하지 못한다. 실제 제품 Installer hook이 아닌 격리 authority
  주입 시험이다.
- 같은 로그에 UI 7개와 fault 4개가 있으며 FULL/IOERR_WRITE에서도 DB
  inode·epoch·grant·quarantine 목록을 보존한다. 이 fixture들은 `/tmp`의
  격리 상태를 사용한다. crash 5개는 영속 `/opt/var/lib/consent-test`의
  fixture 하위 디렉터리에서 실제 hot journal, unlink+SIGKILL,
  동일한 살아 있는 writer의 unlink 복구까지 통과했다.
- `shutdown-wire.log`: wire는 daemon PID17458 유지, checker 정확히
  허용24/거부4, 실제 output-limit 사유를 확인했다. pressure는 완전한 frame
  2060개/78280바이트를 보냈고 별도 client가 응답했다. 성공한 각 script phase는
  integrity가 정확히 `ok`, schema2, 기대 metadata임을 검사한 뒤 PASS를 출력한다.

### 빌드12 shutdown fixture 실패와 남은 응답 게시 경합

엄격한 shutdown 스크립트는 **실패했다**. service stop 뒤 systemd가 unit을
GC하여 새 show 조회가 필요한 `ExecMainCode=1` 대신 기본값 `0`을 반환했다.
`shutdown-debug.log`에 실제 실패 조건을 보존했다. waiter journal 및
`shutdown-daemon.log`에는 PID17664의 SIGTERM 처리, fixture PID17674의
`reason=daemon-shutdown pending_input_bytes=2`, 이후 database-drained가 있다.
fixture는 EOF를 보고 성공했다. 그래도 전체 종료 status 검증은 충족하지
못했으며 pending DB transaction drain 증거도 아니다. 후속 fixture는
관찰할 unit 객체를 유지한 뒤 stop한다.

이 snapshot 이후 응답 게시 경합을 추가로 발견했다. `Execute`는 provenance
검사 전에 epoch를 확인하지만 마지막 `Snapshot()`이 그 검사 중 삭제된 DB를
복구하여 이전 성공 결과에 새 epoch를 붙일 수 있다. 이 빌드는 해당 경합이나
DB 삭제 계약을 완료하지 않았다. 다음 snapshot에서 초기 Ensure 직후 epoch와
마지막 snapshot을 비교하고, 불일치 시 이전 성공 필드 없는 오류를 반환해야
한다. 실제 commit 후 unlink·복구 및 승인 미복원도 회귀로 확인한다. 위에서
검증한 설치 generation/provenance 수정과는 별도의 제한이다.

빌드13 working tree 변경은 이 증거에 포함하지 않는다. 후속 필수 항목은
이 epoch 수정, 독립 ACTIVE→close cache 무효화, 엄격한 부분 I/O shutdown
재검증, 접수된 DB 변경 중 shutdown drain이다. 제품 역할/실제 UI/Installer
hook, 타입 있는 지역화, 급작스러운 전원 차단은 별도 통합·수용 격차로 남는다.

## 빌드13: epoch 게시·ACTIVE close·shutdown 수용 검증

합의한 후속 범위를 commit `20043c1` 기반 tree
`3299de2b1680d41e8655e4ad5e92ed8b92f53bd3`에서 검증했다. 소스 archive SHA256:
`6f477fe3d2a62260d9f6f8b36ee2a84f1bfbd1ff03b4804aa8100f06ef509a0b`.
`/var/tmp/consent-artifacts/gbs-build-13/`에 archive, RPM 4개, checksum,
tree/파일 목록, 정확한 명령, 전체 빌드 로그와 `LastTest.log`가 있다.
빌드12와 같은 GBS 명령으로 7/7 통과했다: client0.59초, crash0.33초,
fault0.10초, provenance1.79초, repository3.42초, UI1.58초, IDL0.13초.
설치한 runtime/library/test RPM 및 emulator script 모두 이 고정 snapshot이다.
앞선 observer trial은 빌드12 binary+working tree script였으며 이번 전체
빌드13 실행을 대신하는 증거로 사용하지 않았다.

실제 emulator 명령:

```sh
systemd-run --wait --pipe --unit=consent-partial-thirteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh shutdown
systemd-run --wait --pipe --unit=consent-db-drain-thirteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh db-shutdown
systemd-run --wait --pipe --unit=consent-cache-thirteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh cache
systemd-run --wait --pipe --unit=consent-storage-thirteen -p SmackProcessLabel=System /bin/sh -c 'set -e; /usr/libexec/consent/tests/repository-provenance-test; /usr/libexec/consent/tests/repository-ui-test; /usr/libexec/consent/tests/repository-fault-test; /usr/libexec/consent/tests/repository-crash-test --state-root /opt/var/lib/consent-test'
```

`/var/tmp/consent-artifacts/emulator-build-13/storage.log`에서 provenance 8개,
UI 7개, fault 4개, 영속 root crash 5개가 통과했다. 새 게시 회귀 2개는
데이터 등록과 data check의 commit 후 검증 중 실제 DB를 unlink한다.
해당 대상 COMMIT 완료와 SQLite autocommit 상태를 unlink 전에 검사한다.
결과 오류는 새 epoch이며 이전 ALLOWED/receipt/permit/artifact 필드가 없다.
동일 Repository의 다음 호출에서 정의가 복구되고 삭제 전 ALLOWED였던
별도 PERSISTENT 승인이 CONSENT_REQUIRED가 된다. 빌드12의 Snapshot epoch
바꿔 붙이기 결함을 해결한 증거이며 외부 설치 authority와 DB를 하나의 원자적
저장소로 만들었다는 뜻은 아니다.

`api-cache.log`에는 실제 C UI/races/holder/cache 실행이 있다. UI는 재팝업/
재응답의 정확한 `-ESTALE`, 저장된 조건별 결과·사유 일치, DENIED 뒤 B 승인
미생성까지 검사했다. 기존 경쟁·정리 시험도 통과한다. 두 cache handle을
살린 채 **새 ACTIVE SESSION**을 승인하고 DAEMON, sync CACHE, async CACHE를
확인한 다음 controller가 close한다. actor의 첫 요청은 그 사이 authoritative
응답 없이 원래 lease 만료 전36322µs에 SESSION_CLOSED를 반환했다.
나머지 무효화도 철회38613µs, 정책45617µs, suspend42333µs,
패키지 제거42157µs, DB 삭제67106µs에 통과했다. 완료된 각 phase는 integrity가
정확히 `ok`, schema2, 기대 metadata임을 검사한다.

`shutdown.log`는 두 엄격한 종료 시험을 기록한다.

- 부분 입력: observer target 의존성으로 unit 객체를 유지하여 systemd GC가
  exit status를 초기화하지 못하게 했다. daemon PID20401이 fixture PID20412를
  `reason=daemon-shutdown pending_input_bytes=2`로 닫고 database-drained를
  출력한다. 두 unit 모두 MainPID0, ExecMainCode1, ExecMainStatus0,
  Result=success다. fixture는182236µs에 종료를 관찰했다. 빌드12에서 실패한
  엄격한 조건을 직접 재실행해 완료했다.
- 접수된 DB 작업: 전용 `consentd-shutdown-test`에만 SQLite interposer를
  링크한다. 실제 행을 변경한 revoke UPDATE 뒤 COMMIT 전·autocommit off
  상태의 fresh marker가 daemon PID20540과 일치한다. supervisor가 SIGTERM을
  보내 stop-admission을 관찰하고, 같은 PID 생존 및 database-drained 부재를
  확인한 뒤 미리 연 O_RDWR FIFO로 C를 쓴다. 해제 후 daemon과 C revoke
  프로세스는 같은 엄격한 status 검사로 정상 종료한다. 결과 전에 소켓이
  닫혀 client는 OUTCOME_UNKNOWN을 보고하고 daemon은 database-drained를
  출력한다. 일반 daemon으로 재시작하면 이전 ALLOWED PERSISTENT 승인이
  CONSENT_REQUIRED이며 정의가 유지되고 integrity도 `ok`다.

Gate는 전용 시험 binary에 한정되며 일반 `consentd-test`와 production
`consentd`에는 gate나 런타임 우회가 없다. 링크 명령을
`daemon-link-commands.txt`에 보존했다. 보호된 FIFO를 대기 전에 열고 5초
timeout 및 실패 정리로 무한 정지를 방지한다. 이 결과는 해당 접수 변경의
정상 종료 처리를 입증하며 임의의 원격 holder 물리 정리 완료를 뜻하지 않는다.

추가 `wire.log` 재실행은 daemon PID21200 유지, 연결 정확히 24개 허용·4개
거부와 출력 압력에 의한 종료 사유를 확인한다. 완전한 frame 2104개(79952바이트)를
보내는 동안 별도 client도 응답한다. `cleanup.log`에는 observer/gate marker 제거와
일반 격리 daemon 실행 파일 복원을 기록했다. 격리 unit은 정지했으며 production
socket은 기본 거부 역할 설정으로 활성 상태를 유지한다.

합의한 후속 필수 항목 4개는 위 고정 빌드와 target 실행에서 검증됐다.
남은 제품·수용 격차는 실제 역할 배포, 실제 승인 UI/Installer lifecycle hook,
타입 있는 지역화, 실제 파일시스템 용량 소진/기기 쓰기 실패, 장시간 자원 시험,
급작스러운 emulator 전원 차단 등이다. 이전 정상 reboot, 프로세스 강제 종료,
SQLite 오류 주입 결과는 앞서 명시한 범위를 유지한다.

마지막 `wire` phase도 통과했다(`consent-wire-thirteen`, `wire.log`): 동일
PID21200, 정확히 허용24/거부4, pressure 완전 frame2104개(79952바이트),
실제 output pressure 종료와 별도 client 응답을 확인했다.
`daemon-symbol-isolation.txt`는 `nm -D --defined-only`로 전용 shutdown
binary만 SQLite step/exec interposer를 정의하며 두 일반 daemon에는 없음을
확인한다. `cleanup.log`에서 observer unit 파일과 DB gate 파일 2개 부재,
부분 입력 ready marker 제거, 격리 service의 일반 `consentd-test` 복원과
정지(MainPID0), production `consentd.socket` 활성 상태를 확인했다.
검토할 수 있도록 격리 시험 상태는 보존했다.

## Build15: 타입 있는 지역화와 승인 범위 결합

A-15 증분은 `13dcfae` 기반의 고정 tree
`58e4ab99cdefca6a3cdfbdfa61d8e0b255e7fd0d`로 검증했습니다.
74개 파일을 담은 소스 archive의 SHA256은
`a50e60137c4448c510012a099a76c8dd53a3875d58b3bf4d57f4b6b154ba543b`이며,
각 파일을 tree와 byte 단위로 대조했습니다. 정확한 소스, RPM 4개, 명령,
전체 로그, `LastTest.log`, manifest와 checksum은
`/var/tmp/consent-artifacts/gbs-build-15/`에 보존했습니다.

Build14 실패는 `gbs-build-14-failed/`에 별도로 보존했습니다. Tree는
`4d727c091065f2433d3b816b95f1d291f6a5b54d`, archive SHA256은
`c27cfb640bbd0f3847b7abd371d1754c6b3ee95cad20561fc64882b242a15071`입니다.
8개 시험은 통과했지만, build RPATH가 꺼진 환경에서 새 formatter 시험이
RPM 설치 전 `libconsent.so.0`을 찾지 못했습니다. Build15는 CTest에만
`LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent>`를 지정합니다. 실제 shared C ABI
호출 시험은 유지하며 production RPATH·실행 환경·신원 검증은 바꾸지 않았습니다.
Build14는 target에 설치하거나 target 성공 근거로 사용하지 않았습니다.

실제 빌드 명령은 다음과 같습니다.

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

Build15는 **9/9**를 통과했습니다. client0.59s, localization0.00s, crash0.32s,
fault0.10s, repository-localization0.50s, provenance1.80s, repository3.45s,
UI1.56s, IDL0.14s입니다. `binary-integration-audit.txt`는 추가된
`consent_prompt_format`을 포함한 공개 함수 40개가 모두 C `consent_*` API이고
C++ 심볼 노출은 없음을 확인합니다. 신규 시험에는 `-UNDEBUG`가 적용되며,
test RPM은 두 신규 unit 시험과 C scenario를 포함합니다. 테스트용 SQLite
interposition은 별도 shutdown daemon에만 존재합니다.

선택한 `emulator-26101`은 x86_64이며, runtime/daemon/test RPM 3개와 scenario
script 모두 이 고정 snapshot에서 설치했습니다.
`/var/tmp/consent-artifacts/emulator-build-15/commands.txt`에 설치·실행 명령,
`deploy.log`에 설치 결과를 기록했습니다. 실제 target 명령은 다음과 같습니다.

```sh
systemd-run --wait --pipe --unit=consent-localization-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh localization
systemd-run --wait --pipe --unit=consent-localization-units-fifteen -p SmackProcessLabel=System /bin/sh -c 'set -e; /usr/libexec/consent/tests/localization-test; /usr/libexec/consent/tests/repository-localization-test; /usr/libexec/consent/tests/repository-ui-test; /usr/libexec/consent/tests/repository-provenance-test'
systemd-run --wait --pipe --unit=consent-ui-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh ui-reevaluate
systemd-run --wait --pipe --unit=consent-races-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh races
systemd-run --wait --pipe --unit=consent-holder-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh holder-restart
systemd-run --wait --pipe --unit=consent-cache-fifteen -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-scenario.sh cache
```

`localization.log`는 실제 C API → Parcel → daemon 경로의 성공을 기록합니다.
한영·직접 alias·default fallback, `수신{name}%<tag>` 값의 재해석 없는 삽입,
formatter 소유권·오류 동작, capability·요청 locale·최신 token 결합을 확인했습니다.
잘못된 schema/template, 비정규·범위 밖 정수, 잘못된 UTF-8, 크기 초과 문자열과
독립 display 인자를 거절합니다. scope30 승인은 scope90 AUTHORIZE·변경된 retry·
receipt 등록·artifact/derived 재사용을 만족시키지 못하며 원래 scope30은 성공합니다.
채워진 typed cache도 scope90을 허용하지 않습니다. Schema 변경에는 policy 증가가
필요하고 pending 요청과 grant를 무효화합니다. 기존 `count="01"` 입력은 허용하되
prompt에는 `count="1"`을 반환합니다. Scenario는 exit0이며 integrity가 정확히
`ok`, schema2, 예상 metadata 존재를 확인합니다.

`localization-units.log`에서는 formatter 5개 그룹, 새 repository localization
7개 그룹, 기존 UI 7개 그룹과 provenance 8개 그룹이 target에서 모두 통과했습니다.
상한 시험은 두 locale에서 정확히 8개 schema와 placeholder를 가진 양성 대조 후,
9번째 schema와 placeholder를 함께 추가하여 거절을 확인합니다. Repository 시험은
원본 필드 누락과 명시적 empty, literal/typed count 호환, policy 증가·재설치와
독립적인 text revision 단조를 확인합니다. message/default/alias 맵 변경에는
별도의 text revision 증가가 필요합니다. 유효 token을 먼저 발급한 후 잘못된
capability/locale, 8192바이트 초과 렌더링 또는 다른 locale의 전체 prompt64KiB
초과를 거절하고, 이전 token으로 정상 응답하는 것을 확인합니다. Registry 복구는
schema/alias 정의만 복원하며 승인은 복원하지 않습니다. 소비한 ONCE도 독립적으로
유효한 artifact 보관 권리를 없애지 않습니다.

`api-cache.log`는 실제 C UI 전체 AND 5개 사례, 독립 연결 간 ONCE 경쟁·동일 receipt
재시도, 원격 cancel/respond/deadline, 새 holder 프로세스의 이전 cleanup 조회·ACK를
기록합니다. 살아있는 cache의 첫 요청은 중간 authoritative 응답 없이 기존 lease
만료 전에 실행했습니다. revoke37898us, policy42727us, suspend40397us,
독립 ACTIVE close38784us, package 제거47397us, DB 삭제65465us입니다.
두 actor handle은 계속 살아 있었고 각 phase는 exit0과 integrity `ok`, schema2,
예상 metadata 존재를 확인했습니다.

`cleanup.log`는 격리 service/socket inactive, MainPID0, 일반 `consentd-test` 복원,
DB gate 파일 부재, 기존 default-deny 역할 설정의 production socket active를
확인합니다. 첫 hash 명령의 `/usr/lib` 경로는 잘못됐으며, 뒤이어 RPM 파일 목록과
실제 `/usr/lib64` 경로의 hash로 정정했습니다. 소스 수정은 필요하지 않았습니다.
격리 시험 상태는 조사할 수 있도록 남겼습니다. 최종 검증 문서와 guide/protocol
4개의 완료 설명은 실행 후 갱신한 문서 전용 변경으로, 고정된 시험 소스를 바꾸지
않습니다.

이번 결과는 제한된 template_version1 구현과 격리 A-15 검증 범위를 완료합니다.
ICU 문법·복수형·날짜·locale별 숫자 표시는 구현하지 않았으며 실제 제품 승인 UI
검증도 아닙니다. 제품 역할 배포, Installer lifecycle hook, registry 소실 후
provisioning은 연동 과제로 남습니다. Wire/shutdown 근거는 별도로 표시한 build13
실행이며 build15에서 재실행하지 않았습니다. 강제 전원 차단과 실제 filesystem-full·
device-write 실패는 앞서 명시한 대로 미검증입니다.

## Build16–19: 공개 오류와 nonroot 서비스

Build19는 공개 헤더 이동, Tizen 오류 매핑, `security_fw` 서비스 이행을 완료한
최소 단위입니다. 소스 tree는 `0a2c5ae77841d4f503413dcff04a3984bb38f3db`,
77개 파일 source archive SHA256은
`c30cce5ea6f30056c124dfe06c853ed0cb59cea744f16d8dedb53c2ec03019c8`입니다.
고정 소스·RPM·CTest·`delta-from-17.patch`는
`/var/tmp/consent-artifacts/gbs-build-19/`에 있습니다. Build17과의 차이는 격리
cache fixture 한 파일뿐입니다. DB 삭제 전에 NSS의 `security_fw` UID/GID,
regular file·단일 link·mode0600을 확인하도록 바꿨습니다. 후속 offline 등록과
기능별 헤더 분리는 이 snapshot에 포함하지 않습니다.

진행 중인 작업을 보존하고 별도 소스 사본에서 다음 명령을 실행했습니다.

```sh
cd /var/tmp/consent-build-18-minimal
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

GBS CTest는 11개 중 10 PASS/1 SKIP입니다. Root가 필요한 소유권 fixture는
nonroot 빌드 사용자에서 77을 반환하며, 이후 emulator에서 실제 root로 실행하여
통과했습니다. Client 시험은 INT_MIN을 포함한 module 오류를 실제 Parcel/socket
응답으로 왕복합니다. 공유 라이브러리 C 시험은 기존 40개 심볼, Tizen 별칭,
error string, 출력 소유권을 확인합니다. 아직 미출시인 v0.1의 오류값 정정이므로
기존 -200x consumer도 라이브러리·데몬과 함께 재빌드해야 합니다.

중간 결과도 최종 검증과 구분하여 보존했습니다.

| Snapshot | 관측 결과 |
| --- | --- |
| 16 / `12ea20c09045711da5090de4b917219e107792c3` | GBS10 PASS/1 SKIP. 운영 nonroot 기동·수동 이행 성공. 격리 script가 label 설정 helper를 root/System에서 직접 실행하여 실패. |
| 17 / `c97287641bbb50527f2e90c0f64f6a31b377d189` | 실제 privileged ExecStartPre+ 준비 및 명시적 privileged authority writer 적용. API/races/holder/shutdown 통과 후 cache 삭제 fixture의 UID0 단정에서 중단. |
| 18 / `c4da55e730ded3b636281e661ccda27e82ada000` | GBS가 positional 소스 인자 대신 현재 작업 디렉터리를 사용하여 진행 중인 헤더 분리까지 export. GBS10 PASS/1 SKIP이지만 배포하지 않았고 이번 단위 근거로 사용하지 않음. |
| 19 | 별도 cwd에서 build17과 fixture 한 파일 차이만 감사하고 아래 실제 nonroot target 검증 완료. |

`emulator-build-16/migration-before.log`에서 build15로 PERSISTENT 승인을 먼저
저장했습니다. 실제 privileged 이행은 test DB inode128283, registry128667,
설치 authority128011과 registry/authority 내용을 보존했습니다. DB·registry는
UID/GID402 mode0600, 외부 authority는 root:402 mode0640/System이 되었습니다.
`migration-manual.log`와 `emulator-build-17/migration.log`는 같은 승인이 실제
C API에서 ALLOWED임을 보여줍니다. 운영 DB128673·registry128674도 inode를
보존했습니다. 이후 authority 쓰기는 의도적으로 새 inode에 게시하며 이행 시
inode 보존 관측과 구분합니다.

최종 target 로그는 `/var/tmp/consent-artifacts/emulator-build-19/`에 있습니다.
`commands.txt`에 명시적 `sdb -s emulator-26101` 명령을 기록했습니다. 고정된
runtime·daemon·tests RPM만 정상 의존성 transaction으로 설치했습니다. Target의
`capi-base-common-0.4.82-1`은 변경하지 않았습니다. 일치하는 devel은 확보되지
않았고 cache의 0.4.83 devel은 같은 버전 runtime을 요구합니다. `--nodeps`나
허위 Provides를 사용하지 않았습니다. Installed-tree C/pkg-config/40심볼 검증은
GBS SDK와 고정 devel RPM을 사용하며, target 근거는 설치된 shared ABI 실행이지
새 target 개발 헤더 설치·검증은 아닙니다.

`api.log`는 공개 C API 시험과 7개 script phase의 exit0을 기록합니다: 미등록
실행파일 거부, ONCE·원격 취소 경쟁, holder 재시작, live cache, typed localization,
partial-I/O 종료, pending DB 작업 종료. 각 phase는 중지한 DB의 integrity가
정확히 `ok`, schema2, 예상 metadata 존재임을 확인합니다. Live cache 첫 조회는
기존 lease 안에서 실행했습니다: revoke34946us, policy42909us, suspend33973us,
독립 ACTIVE close34606us, 제거36163us, DB 삭제49045us. 두 client handle은
계속 살아 있습니다. 종료 시험은 실제 2바이트 partial header,
`reason=daemon-shutdown`, 정상 exit와 DB drain을 확인했습니다. Revoke gate는
해제 전에 같은 PID가 살아 있고 stop-admission에 도달했음을 확인하고, 종료 후
재시작에서 내구성 있는 CONSENT_REQUIRED 결과를 검증했습니다.

`root-fixture.log`는 실제 소유권·inode·승인 보존, 중단된 이행 재시도,
parent-fsync 실패 후 재시도 barrier, 충돌 authority·symlink·hardlink·외부 소유자·
예상 밖 파일·사용 중 lifecycle lock 거부를 통과했습니다. 이 fixture의 compile-time
SMACK/systemctl 생략은 명시하며, 실제 운영 helper 기동이 해당 경계를 별도로
검증합니다. Authority provisioning은 명시적으로
`systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged`에서
실행합니다. Root UID 자체가 MAC이나 역할 인증을 우회하지 않습니다.

`boot-before.log`, `boot-after.log`, `boot-api.log`는 정상 `reboot`를 기록합니다.
Boot ID가 `b3e3c620-cedf-41be-8e98-6430431dc4aa`에서
`1842a2f1-9a74-4a2e-985a-e7ab95dff79b`로 바뀌었습니다. Client 호출 전에 운영
service/socket이 active였고 MainPID2440, UID/GID402, label System, 관측한
모든 capability set은 정확히 `0x80000`(CAP_SYS_PTRACE)이었습니다.
NoNewPrivileges=yes는 systemd 설정값입니다. AMD 방식 basic.target.wants 상대
symlink와 상속 socket FD가 함께 동작하며 DB·registry inode는 유지됐습니다.
재부팅 전 PERSISTENT 승인도 재시작 후 ALLOWED였습니다. 부팅으로 SDB가 owner로
돌아가고 `/tmp`가 비워져 개발용 root transport와 고정 시험 script를 복원했습니다.
초기 권한·script 부재 진단도 로그에 남겼습니다.

`endpoint.log`에서 PID1/UID0/GID0, credential 길이12, peer label
System::Privileged 길이19, AF_UNIX 주소 길이22 `/run/.consentd.sock`를 관측했습니다.
운영 default-deny 호출은 서버 journal의 `role=rejected` 및
`no matching live trusted identity`와 대조했으며 client endpoint 거부라고
주장하지 않습니다. 최초 진단은 잘못된 dlog stream을 조회했고, 수정한 journal
assertion은 통과했습니다. `cleanup.log`는 격리 service/socket 중지·MainPID0,
일반 test daemon 복원, observer/gate 부재, 운영 service/socket active를 확인합니다.
설치 library·daemon·test daemon hash는 고정19 RPM과 일치합니다.

검증 계정은 기존 `security_fw`이며 새 `security` 계정을 만든 것이 아닙니다.
제품 역할, 실제 승인 UI, Installer transaction hook은 연동 과제로 남습니다.
Offline 이미지 등록은 다음 별도 구현 단위입니다. 강제 전원 차단과 실제
filesystem-full/device-write 실패는 미검증이며 정상 reboot나 주입한 storage 오류로
대체하지 않습니다. 전체 malformed/quota wire 근거는 과거 명시한 snapshot 범위이고,
이번에는 endpoint와 종료 검사를 재실행했습니다.

## Build20–23: offline 등록과 기능별 C 헤더

이번 단위의 완료 기준은 Build23입니다. Frozen tree는
`971211be4af9187b9becb97d4cb919d6680ceee1`이며 source archive109개 파일을 모두
해당 tree와 byte 단위로 대조했습니다. Archive SHA256은
`1f326beeb77693e6b9609a24643dffdd04e36b0c6beedc1869c915d03775fb5e`입니다.
[빌드 산출물](/var/tmp/consent-artifacts/gbs-build-23/)에는 소스, RPM4개,
`snapshot.json`, `source-tree.txt`, `changed-files.tsv`, GBS/CTest 로그와 checksum이
있습니다. 저장소 작업 디렉터리에서 다음 명령으로 빌드했습니다.

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

GBS는12개 통과, root 전용4개 skip(CTest 총16개)입니다. `root-fixtures.log`에는
네 실행파일(storage preparation, offline identity, offline registration,
image authority)의 실제 emulator 실행이 있습니다. Repository offline 회귀20그룹도
함께 통과했고 transient unit은 exit0입니다. Private preparation fixture는 운영
SMACK/systemctl 동작을 생략하며, 실제 target helper/service 시작에서 별도로 검증합니다.

공개 C 헤더10개를 `src/consent/inc/`에서 기능별로 분리하고 `consent.h` umbrella를
유지했습니다. C wrapper도 기능별로 분리했습니다. 41번째 공개 함수는 명시적인
offline registration handle 생성 함수입니다. 같은 `consent_register()`가 DB나
승인을 만들지 않고 영속 STAGED를 반환합니다. Update 등 다른 도메인 API는
거부하며 일반 online 오류가 offline 쓰기로 전환되지 않습니다. CMake/spec의
license 주석은 제거하고 소스 notice와 RPM License metadata는 유지했습니다.

`public-installed-abi.log`는 고정 runtime/devel RPM과 archive의 consumer 시험만
사용합니다. 설치된10개 헤더의 독립·중복·역순 include를 C11/C++17로 검증했습니다.
C/C++ consumer를 GBS SDK loader로 실행했고 선언·export·양쪽 consumer 참조가
정확히41개로 일치하며 C++ 구현 심볼은 노출되지 않습니다. 설치된 pkg-config의
공개 `capi-base-common` 의존성도 확인했습니다. SDK 개발 파일은0.4.83이고 target
runtime은 `capi-base-common-0.4.82-1` 그대로입니다. 일치하는 target0.4.82 devel을
확보하지 못했으므로 target 헤더 설치·검증, core runtime upgrade, `--nodeps`나
허위 Provides를 사용했다고 주장하지 않습니다.

[Target 근거](/var/tmp/consent-artifacts/emulator-build-23/)에는 정확한
`sdb -s emulator-26101` 명령과 고정 scenario script 복사본이 있습니다.
Runtime·daemon·tests RPM만 정상 의존성 transaction으로 설치했습니다. 선택한
emulator는 x86_64 Linux4.4.35이며 기존 security_fw UID/GID402를 사용합니다.

| 근거 | 실제 결과와 범위 |
| --- | --- |
| `root-fixtures.log` | 보호 경로·0711 거부·FIFO/nonregular·128개/4MiB 상한·버전/해시/중복/변조 거부·sync 불확실성/재시도·root/thread/fork 제한·실제 nonroot 경로 접근·generation lifecycle·이미지 밖 대상 불변을 통과했습니다. |
| 같은 로그의 repository 시험 | DB 소실 뒤 receipt dedup, 결정적 revision 순서, obsolete 원결과, 같은 generation의 unregister tombstone, 다른 package 보존, postcommit generation/DB 변경 fence를 통과했습니다. Typed malformed/I/O 오류가 import/Open/마지막 Snapshot까지 보존되고, 앞서 수행한 tentative invalidation도 rollback되어 기존 persistent 승인/revision을 유지합니다. 성공·예외 모두 strict mode가 복원됩니다. |
| `offline.log` | Socket 없는 실제 C 등록이 STAGED를 반환하고 retry/conflict/미지원 메서드를 검증했습니다. Malformed와 schema2 authority 모두 preflight -22로 DB 생성 전에 시작이 차단됐습니다. 정상 내용 복원 후 복수 app·다른 package가 CONSENT_REQUIRED이며 live lifecycle 배제, 재시작, DB 삭제, unregister 후 미처리 old seed, 재설치 generation을 통과했습니다. Grant0과 정확한 integrity `ok`를 확인했습니다. |
| `platform-offline.log` | 운영 daemon이 실제 설치된 `org.tizen.calendar` app/package만 import하고, 실제 `attach-panel-camera`에 대한 잘못된 package 주장과 stale generation을 거부했습니다. 중지한 DB를 readonly 검사하여 정상 정의만 존재하고 grant0임을 확인했습니다. 제품 roles는 변경 없이 기본 거부이며 caller PID18676·daemon PID18667의 로그를 연결했습니다. 실제 pre-hello 반환은 OUTCOME_UNKNOWN이며 endpoint 인증 거부로 해석하지 않습니다. |
| `regression.log` | 새 격리 상태의 basic SYNC/ASYNC/UI/authorization/session/data cleanup, 비인가 caller, 독립 ONCE/cancel 경쟁, holder 재시작, cache, typed localization, partial-input 종료, pending-DB 종료와 공개 ABI를 모두 통과했습니다. |

Fresh 회귀 supervisor는 패키지 소스 밖 artifact의 `regression-supervisor.sh`에
보존했습니다. 고정된 기존 scenario를 phase별120초 제한으로 실행하며 원래 authority의
lifecycle EX 잠금을 유지하고 state/authority/control3개 저장소를 보존·복원합니다.
Revoke/update/suspend/독립 ACTIVE close/remove/recovery 뒤 첫 cache request는 원래
lease 이내였습니다(37518/42431/34197/34553/38839/67470us). 종료 시험은 실제2바이트
partial input, COMMIT 전 gate에 도착한 mutation, release 전 stop-admission,
정상 process 종료와 재시작 후 영속 revoke를 확인합니다. 새 전체 malformed-wire/quota
또는 강제 전원 차단 시험으로 확대 해석하지 않습니다.

실제 성공한 주요 명령은 다음과 같습니다.

```sh
systemd-run --wait --pipe --unit=consent-offline-twentythree \
  -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-offline-test.sh
systemd-run --wait --pipe --unit=consent-offline-platform-twentythree \
  -p SmackProcessLabel=System /bin/sh /tmp/consent-emulator-offline-platform-test.sh
systemd-run --wait --pipe --unit=consent-regression-twentythree \
  -p SmackProcessLabel=System /bin/sh /tmp/consent-regression.sh
```

중간 결과는 별도로 보존합니다. Build20은 최종 ancestor·저장 metadata 검증 보강 전의
root/격리 trial 통과입니다. Build21은 GBS·root/ABI·실제 pkgmgr·기존 회귀가
통과했으나 offline 음성 startup fixture가 GC된 unit의 `reset-failed`에서 중단됐습니다.
Basic의 첫 시도도 기존 test state가 있어 fresh-state 전제에서 거절됐습니다.
Build22는 script만10줄 추가/2줄 삭제했고 정상 malformed 시작 거부까지 도달했으나
target에 `cmp`가 없어 중단됐습니다. Build23은 그1줄만 실행 성공을 확인하는
sha256sum2회와 digest 비교로 대체했습니다(3줄 추가/1줄 삭제). Production 소스는
21부터23까지 동일합니다. 성공한22 binary+23 script trial은
`emulator-build-22/trial23-script.log`로 명시 구분했고, 위 최종23 결과는 모두23
RPM/script를 사용했습니다.

`cleanup.log`와 `installed-rpm-hashes.log`에서 원래 운영 DB/registry inode
128673/128674, 원래 installations.conf 부재, 원본 격리 저장소 복원과 lifecycle
잠금 해제를 확인했습니다. 운영 service/socket은 active이고 security_fw402/System,
CAP_SYS_PTRACE-only(`0x80000`), NoNewPrivileges=yes입니다. 격리 service/socket은
중지했고 일반 test daemon으로 복원했으며 observer/gate/임시 journal override가
없습니다. 설치 바이너리7개의 hash가 정확한23 RPM payload와 일치합니다. Helper
경로를 잘못 지정한 진단2회는 로그에 남겼고 실제 패키지의
`/usr/sbin/consent-storage-prepare`로 최종 재확인하여 exit0입니다. Target의
`/opt/var/lib/` 아래 `consent-offline-evidence-18021`,
`consent-offline-platform-18618`, `consent-regression-evidence-18896`에는 fixture
결과를 보존했으며 원본 저장소는 정상 경로에 돌려놓았습니다.

이번 증분은 명시적인 root system-service/image 등록 API와 첫 시작 reconciliation의
완료입니다. 제품 Installer transaction hook, 실제 승인 UI나 제품 role identity를
배포한 것은 아닙니다. Reconciliation은 startup-only이므로 수정된/deferred authority는
service 재시작이 필요합니다.23에서 reboot/poweroff를 새로 실행하지 않았으며 정상
boot 근거는19 범위입니다. 강제 전원 차단, 실제 filesystem-full/device-write failure,
automatic spool pruning은 검증 완료 주장에 포함하지 않습니다. 최종 근거 문단은
고정 빌드 뒤 작성한 문서이며 그 source archive를 소급 변경하지 않습니다.


## Build24: GBS .NET/패키지 기준과 보존한 UI trial 실패

Build24는 `382dbe6` 기준 tree `84042ee0d4113006872fbc334fcf3d755d72fd76`이며
source archive SHA-256은
`72cc199dddb01bd6a207ccffcfc7adcd298d5eebb83451f5602ef76a99513635`입니다.
`/var/tmp/consent-artifacts/gbs-build-24`에 대응 소스, RPM5개, CTest18개 결과
(14 PASS, root 전용4 SKIP), TPK2개, build.json과 GBS managed 로그를 보존했습니다.
GBS 내부 SDK8.0.421와 offline Tizen NuGet으로 두 앱을 소스부터 컴파일했고
managed 시험3그룹이 통과했습니다. PoC RPM에서 추출한 TPK bytes는 GBS 출력 및
기록 hash와 일치합니다. Positive 개발 서명 TPK의 hash는
`c9a6d7baf7de23f66988f7c72e993562c3f18ff2537fc6482181fe23c6a1808d`입니다.
SDK installed-tree에서는 공개 헤더10개의 독립 C/C++ 소비, export41개, 링크한
C/C++ consumer가 통과했습니다. Target base-common runtime은0.4.82를 유지하고
버전이 다른 devel을 설치하지 않았습니다.

`emulator-26101`에 정확한24 runtime/daemon/tests/PoC RPM을 정상 의존성 검사로
설치했습니다. 같은 NEVRA의 개발 교체에는 `--replacepkgs --replacefiles`가
필요했으며 `--nodeps`나 허위 Provides는 사용하지 않았습니다.
`/var/tmp/consent-artifacts/emulator-build-24/maintenance.log`에서 패키징된
maintenance8개 시나리오 PASS와 RPM에 일치하는 실행파일 hash를 확인했습니다.
이 maintenance 소스는 별도로 `2f10ebe`에 커밋됐습니다. RPM 등록 service의
positive TPK 설치가 성공했고 신원 시험용 negative TPK는 명시 설치했습니다.
설치된 C 예제로 운영 role 거부(daemon 로그의 같은 PID)와 별도 root image의
offline STAGED/exact retry(spool1개, consent DB 없음)를 확인했습니다. 제품 role의
양성 통합 시험은 아닙니다.

이후 trial은24 binary와 명시한 working25 script/typed mock fixture의 조합입니다.
최초 runtime 디렉터리 label 때문에 UI가 SMACK 경로 접근에서 차단됐고, 승인된
leaf에만 `_` label을 준비한 뒤 socket probe에서 UID5001/GID100 및 정확한
`User::Pkg::org.tizen.consentui`를 관측했습니다. Negative 앱은 같은 UID/loader지만
자신의 package label을 사용했으며 같은 PID의 daemon role 거부를 확인했습니다.
실제 positive UI는 UI role 연결 후 표시 단계의 `InvalidOperationException`으로
즉시 닫히고 DENIED가 됐습니다. 팝업/버튼 성공이나60초 timeout 근거가 아닙니다.
원 로그·실패 화면·driver 실패를 `emulator-build-24`에 보존했으며 후속 수정은
고정24의 검증 주장을 변경하지 않습니다. Working25 setup trial에서는 probe
override가 남은 configure 거부, 복원 후 실제 notify daemon 신원, PoC socket 시작
실패 주입 후 override 정리를 확인했습니다. 유계 driver helper와 반복 stop 수정은
후속 소스이며 다음 snapshot에서 다시 빌드하고 실행합니다.


## Build25: 실제 버튼/API 흐름 통과, 팝업 위치는 실패

고정 tree `217d5969803cabde218dd86eaff66d24ab7bdc42`(기준 `2f10ebe`), archive
SHA-256 `a49cb135297f0bf18e6c6174ae3a8b68c5ef2b230139b5b8c358267d134a51a2`에서
GBS native14개와 managed3그룹이 통과하고 root 전용4개는 skip입니다.
`/var/tmp/consent-artifacts/gbs-build-25`에 산출물, RPM 추출 TPK 및 installed
header/41-ABI 검증을 보존했습니다. 정확한25 패키지를 정상 의존성 검사로 설치했고
등록 service의 TPK 업데이트가 성공했습니다.

`emulator-build-25/allow-en`에 실제 Aurum Next/Allow once 클릭과 driver
`finish-allow` PASS를 기록했습니다. Async 결과1회, CM QUERY, CE ONCE 승인·동일
receipt 재시도·소진, 실제 holder fixture buffer의 등록·파생·재사용과 session
종료·삭제 ACK가 통과했습니다. UI 대신 응답 API를 주입하지 않았습니다. 다만
`page1.png`/`page2.png`에서 card가 의도한 위치보다 왼쪽/위로 잘려 header와
언어 버튼이 보이지 않으므로 전체 화면 수용은 실패입니다. `ui-fit.log`에서
24pixel 본문 높이180은290 이내이고, 화면 밖 old18pt 측정은 자연폭1370/제한폭의
높이1330입니다. 이전 텍스트 크기 거부와 새 pivot/배치 결함을 구분하는 근거입니다.

기존 setup 실패 fixture는 정리를 검사했지만 더 이른 사전조건 실패로도 통과할
수 있어 socket 시작 실패 주입이 실행됐다는 충분한 증거가 아닙니다. 후속26
fixture는 실패하는 ExecStartPre helper가 새 보호 marker를 남기고 이를 확인한
다음 정리를 검사합니다. `emulator-build-25/probe-marker26-trial.log`는 명시적으로
25 RPM+26 script trial이며 고정25 시험 근거가 아닙니다. 배치/비활성 버튼 및
fixture 수정은 고정26 빌드와 최종 target 실행이 필요합니다.


## Build26: 격리 UI·참여자·패키징 증분 완료

검증 소스는 `2f10ebe` 기준 tree `91e17c2ec13802933083eb1fa27dbb36b87bd215`이며
archive179개 blob이 exported tree와 일치합니다. Source SHA-256은
`fe26530b6c4120817360614f807d8e0e8c7b5616afc7f1a64afa97a90f14d7cf`입니다.
산출물은 `/var/tmp/consent-artifacts/gbs-build-26`, target 근거는
`/var/tmp/consent-artifacts/emulator-build-26`에 보존했습니다. GBS 명령은 다음과 같습니다.

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all   -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

GBS native CTest14개 PASS/root 전용4개 SKIP 및 managed3그룹 PASS입니다.
GBS 내부 SDK에서 두 .NET 앱을 소스 컴파일하고 개발 서명 TPK를 생성했습니다.
RPM5개, RPM 추출 TPK2개, 대응 build.json과 managed 로그를 SHA256SUMS와 보존했습니다.
설치 가능한 positive TPK는
`/var/tmp/consent-artifacts/gbs-build-26/org.tizen.consentui-0.1.0.tpk`이며 SHA-256은
`8cae6fa2ec1521816ca99fba0d9a523c13e22e9f0394015d683a3e103e57dac5`입니다.
Negative TPK hash는
`74fefbda2e0ba2e803a60a56f8d4216492616cc3ed4e490c2822fa19a83d51c6`입니다.
SDK installed-tree에서 독립 C/C++ 헤더10개와 정확한 C ABI export/consumer참조41개가
다시 통과했습니다. Target `installed-hash-audit.json`에서 설치 native binary/DLL
10개가 정확한 RPM/TPK payload와 일치함을 확인했습니다.

선택한 Tizen10.1 Common Emulator `emulator-26101`(1920×1080)에 정확한26
runtime/daemon/tests/PoC RPM을 설치했습니다. `rpm-install.log`/`registration.log`에
정상 의존성 검사와 global TPK 등록 service Code1/status0, 실제 single-app pkgmgr
관계·DLL digest를 기록했습니다. `probe-stop.log`에서 UI PID47636/UID5001/GID100과
정확한 socket `SO_PEERSEC=User::Pkg::org.tizen.consentui`를 관측했고,
`configure.log`에서 실제 notify daemon READY를 확인했습니다.
`configure-probe-rejection.log`는 남은 진단 override가 실제 daemon으로 오인되지
않음을 확인합니다. `probe-failure-cleanup.log`는 실패 주입 socket ExecStartPre
helper가 이번 실행의 새 marker를 남긴 사실까지 필수 검사합니다.

실제 승인 버튼에는 Aurum 입력을 사용했으며 respond API를 주입하지 않았습니다.
Guide08과 같이 고정 host driver의 `start-request`, `finish-allow`, `stop` 사이에
동일 serial/artifact 디렉터리를 사용했습니다.

| Target 근거 디렉터리/파일 | 확인한 결과 |
| --- | --- |
| `allow-en/` | EN 첫 페이지의 비활성 Allow 클릭은 무응답, Next 정상. 언어 선택 시 prompt를 교체하고 검토를 초기화합니다. KO 마지막 페이지에서 Allow 활성화와 실제 클릭 후 callback1회, QUERY, ONCE AUTHORIZE/동일 receipt 재시도/소진 및 holder 등록·파생·재사용·session 종료·cleanup ACK가 통과했습니다. Actor 반복 stop도 통과했습니다. |
| `deny-ko/` | 실제 거절로 DENIED callback1회, QUERY 차단을 확인했습니다. |
| `stress-ko/` | 연속 W256자의 purpose를3페이지에 표시하고 마지막 보관 문구·버튼까지 확인했습니다. 실제 거절과 같은 조건 QUERY 검사가 통과했습니다. |
| `cancel-ko/` | 실제 argo 취소로 CANCELLED callback1회, QUERY 차단 및 refresh에 따른 UI 종료를 확인했습니다. |
| `timeout-en/` | 표시 후 UI 입력 없이79.45초 뒤 popup 소멸, DENIED callback1회 및 QUERY 차단을 확인했습니다. Daemon request deadline 만료 시험은 아닙니다. |
| `back-en/` | 실제 Back으로 popup 종료, DENIED callback1회와 QUERY 차단을 확인했습니다. |
| `negative-identity-retry.log` | PID53574는 UID5001과 같은 보호된 hydra loader지만 실제 label이 `User::Pkg::org.tizen.consentui.negative`이며 daemon이 그 PID를 거부합니다. Client 오류만을 수용 근거로 사용하지 않습니다. |

`allow-en/page1.png`, `korean-page1.png`, `korean-page2.png`에 전체 제목·언어 선택·
안내·본문·footer를 보존했습니다. 마지막 파일에서 최종 페이지의 활성 Allow를
확인할 수 있습니다. `page2.png`는 refresh 진행 중의 비활성 상태이며 최종 ready
상태의 근거가 아닙니다. 좌표는 emulator native pixel입니다.
`stress-ko/page1.png`부터 `page3.png`는 연속 문자열 시험 화면입니다. 개발 emulator
화면이며 실제 TV 수용 완료 주장이 아닙니다. `poc-extra.py`는 stress/취소용 보조
**입력 fixture**로 고정 driver와 실제 C mock을 호출합니다. UI 응답 주입이나
runtime 인증 우회가 없고 실행 근거 옆에 보존했으며 RPM에 컴파일하지 않았습니다.
`result-summary.json`은 저장한 mock JSONL로6개 terminal decision과 callback1회를
다시 검사합니다.

`maintenance-correct-path.log`에서 패키징된 maintenance8개가 통과했습니다.
처음 잘못 입력한 실행 경로는 `maintenance.log`로 따로 보존했습니다. 설치한
offline C 예제는 durable STAGED/exact retry·record1개·DB 없음으로 통과했고,
online check 예제는 PID53879가 운영 role 거부 로그와 일치하는 기본 거부 시험입니다.
제품 role의 양성 통합 근거가 아닙니다. 처음 negative process 조회는 짧은 실행이
이미 끝난 상태였으며, 재실행한 신원/daemon 거부의 동일 PID 관측이 최종 근거입니다.

`cleanup.log`에서 소유 actor 중지, FIFO/진단 override 및 임시 offline 예제 image
root2개 제거를 확인했습니다. PoC service/socket은 중지하고 명시 PoC roles/state/
authority와 설치 TPK2개는 검토용으로 보존했습니다. 운영 service/socket은 active,
DB/registry device65026·inode128673/128674·owner402:402·mode0600이 유지됐습니다.
운영 installation authority는 여전히 없고 base-common은0.4.82입니다.
`aurum-cleanup.log`에서 scoped forward55051과 이번 작업이 시작한 bootstrap의
종료를 확인했습니다. 운영 role 정책을 완화하지 않았습니다.

이번 완료 범위는 격리 .NET UI/mock 연동·빌드/패키징 흐름·공개 API 문서/예제와
유계 maintenance입니다. 제품 role identity 배포, 운영 승인 UI/Installer lifecycle
연동, registry 유실 reset provisioning, 임의 ledger/spool pruning, 강제 전원 차단,
실제 filesystem-full/device-write failure는 완료 주장에 포함하지 않습니다.
24–26에서 reboot 시험을 새로 수행하지 않았습니다. 최종 README/Guide07/Guide08과
PO가 정리한 mock C wrapper5개/INI18개의 끝 빈줄 제거는 고정26 이후 변경입니다.
EOF 정리는 token/동작을 바꾸지 않으며 다른 구현 소스는 검증된26 내용입니다.
이 최종 문서/공백 정리는 보존된26 RPM이나 hash를 변경하지 않습니다.

## Build27: 기능 사전승인 통합, target 흐름 미완료

GBS는 base `5f3364d604d1f8bfe3fd2da06fdc227ea8a0b71e`에서
tree `db578048661ddb23c003de04386fd1838039e801`을 내보냈습니다.207 source
blob 전체가 `/var/tmp/consent-artifacts/gbs-build-27/consent-0.1.0.tar.gz`와
일치하며 SHA-256은
`943f72ef9e731b2d3ae026ae121ce18e62f340f3becd6dcc2787e0acf02688e0`입니다.
`snapshot.json`의 기존 GBS 명령으로 RPM5개, CTest16 PASS/루트전용4 SKIP,
managed5그룹 PASS를 확인했습니다. TPK2개는 GBS 내부 컴파일 결과이며 RPM에서
추출한 payload와 build 출력이 일치합니다. positive SHA-256은
`5011726c97ccc79d1f05c6599d724eb8361e975905ed53168483c46e4540fb2f`,
negative는 `5f185fe5f19d2e392d7bc0f4d944985967c24624a875a01ce67af92925cb0005`입니다.
`public-installed-abi.log`는 설치된 기능별 헤더10개의 독립 C11/C++17 컴파일,
pkg-config, `consent_session_heartbeat`를 포함한 정확히42개 C export/reference를
검증합니다. 이 SDK 검증은 target-devel 설치 증거가 아닙니다.

선택 emulator-26101에 정확한27 runtime/daemon/tests/PoC RPM transaction 및
positive TPK 등록이 성공했습니다. base-common은0.4.82를 유지했고 production
DB/registry inode128673/128674와402:402/0600 소유권을 보존했습니다.
`emulator-build-27/feature-units.log`에서 설치된 repository feature17개 시나리오,
actor fixture/backlog deadline, 공개 C ABI 실행파일이 통과했습니다. actor
completion/retry 주입은 실제 앱 동작과 구분하는 보조 증거입니다.
`endpoint-trial.log`와 `endpoint-evidence`는 직접 위조 서버 및 다른 PID1 socket
rename에 대해 실제 bridge -EACCES/NULL, 요청 byte 미송신, fixture 정상 종료 및
feature unit 복원을 확인합니다.

실제 설정 화면과 영문 검토4페이지는
`/var/tmp/consent-artifacts/emulator-build-27/feature-trial/`에 있습니다. 선택한
정확한 항목, 접근 승인 기간, 별도 보관 기간을 표시했습니다. checkbox glyph가
target font에서 부적절하게 표시되어 다음 snapshot에서 고칩니다. 최초
PREAPPROVAL은 -38로 중단됐습니다. Repository의 시험용 hello에는 있었으나
server의 실제 직접 hello 응답에 `approval_version`이 빠져 있었습니다. client는
미지원 capability를 정상적으로 거부했으며 승인 흐름 완료가 아닌 실제 통합
실패입니다. `ui-pid-logs.log`와 `first-preapproval-failure.log`에 보존했고 다음
snapshot에서 실제 hello 응답 및 actual-wire assertion을 보강합니다.

읽기 전용 실행 리뷰에서도 holder reuse가 actor의 최종 선택 검증 없이 조회와
동작을 한 호출에서 수행하고, 취소한 job의 늦은 실제 결과가 버려지는 점을
발견했습니다. 따라서 build27은 **기능 최종 수용이 아닙니다**. 다음 증분에서
재사용 조회/시작을 분리하고 유계 retired completion 증거 및 실제 target reuse
gate를 검증합니다.27 feature 서비스/socket은 중지하고 원 PoC 역할을 복원했으며
(`post-trial-state.log`) production은 active를 유지했습니다. 범위가 지정된 Aurum
session은 다음 증분에서 계속 사용합니다. PO Guide10 재현 명령 보강은27 export
후 문서 변경입니다.


## 빌드 28: 사전승인 성공, worker 시작 실패로 작업 실행 차단

고정 소스는 `2e6126f026a0a65fa94235511c2322e948b2b92b`, archive SHA-256은
`cf13b1e750af5790e260dea115b945f1e9865222317341230b6f7f993086a314`이다.
207개 source blob이 archive와 일치한다. `/var/tmp/consent-artifacts/gbs-build-28/`에
5개 RPM, GBS에서 컴파일한 TPK 2개, build metadata와 로그를 보존했다.
GBS CTest 16개 PASS/4개 root 전용 SKIP, managed 5개 그룹 PASS이다.
SDK installed-tree의 독립 C11/C++17 헤더 10개, pkg-config, 공개 C 심볼 42개도
통과했다. emulator에 matching devel 패키지를 설치했다는 의미는 아니다.

정확한 runtime/daemon/tests/PoC RPM을 `emulator-26101`에 정상 의존성으로
설치했다. production DB/registry device/inode `65026:128673` / `65026:128674`,
소유자402:402를 보존했고 capi-base-common은0.4.82-1을 유지했다.
positive TPK SHA-256은 `bf9a1a13bd10aef3f1c878f04bf1334b14812bf5529fd08cca9643453760aa9e`,
negative TPK는 `b3079d1cddd4b89da98ffee675ac947387a02aaa9b02da1eaf84acd00c299d29`이며
둘 다 RPM payload와 일치한다. target `feature-units.log`에는 repository 기능
17개 그룹, mock 시험4개 그룹 및 public C API 시험 PASS가 있다.

실제 EN Settings 4페이지, 승인5페이지를 모두 검토하고 **Allow as displayed**
버튼을 눌러 SESSION PREAPPROVAL의 ALLOWED/action_count=0을 확인했다.
화면과 양쪽 journal은 `/var/tmp/consent-artifacts/emulator-build-28/feature-en/`에
있다. 빌드27의 실제 hello capability 누락은 해소됐다. 이후 calendar-summary는
제공 앱 동작이나 CE daemon 연결 전에 실패했다. `first-task-failure.log`와
`feature-en/task-result.png`를 실패로 보존하며 기능 실행·재사용 성공으로 세지 않는다.

읽기 전용 프로세스 근거에서 CE child의 exit1을 확인했다. coordinator와 같은
root/System/capability/NNP 문맥의 격리 probe에서 socketpair SO_PEERCRED는
실제 부모 PID지만 SO_PEERSEC는 NUL 한 바이트였다(`pair-probe.log`). 따라서
필수 System label 검사가 정상적으로 채널을 거부했다. 보호된 임시 pathname의
connect/accept probe에서는 양쪽 모두 실제 부모PID/UID0와 System+NUL을
반환했다(`connect-probe.log`). 해당 임시 socket은 제거했다. 인증을 완화하지
않고 후속 소스/빌드로 transport를 수정한 뒤 실행과 gate를 검증해야 한다.
주입형 native actor 시험은 이 커널 SMACK 차이를 검증하지 않는다.

`feature-stop.log`와 `post-trial-state.log`에서 feature service/socket 중지,
override 없음, 원 PoC roles 복원, production daemon active를 확인했다.
취득 artifact가 없으며 holder cleanup ACK 근거는 아니다. Aurum은 후속 빌드
검증을 위해 이 작업이 계속 소유한다. Guide08의 serial 변수는 archive 이후
문서 정정으로 `CONSENT_SERIAL`로 통일했다.

## Build 29: 선택 기능 사전승인과 실제 작업 실행

완료된 기능 증분은 `5f3364d` 기반 frozen tree
`4400b206b611a881cbf10217a042c53fa0f4e503`(소스 207개)을 사용합니다.
archive SHA-256은
`aad23f34306a29e8d2d189bcfb4d9f2a099d5c85d11aeaf79debd76074b90542`입니다.
산출물은 `/var/tmp/consent-artifacts/gbs-build-29/`와
`/var/tmp/consent-artifacts/emulator-build-29/`에 보존했습니다. 최종 Guide07/08
검증 기록 및 PO의 Guide10 도입부·재현 절차 정리는 archive 이후 문서입니다.
이 빌드 이후 시험 소스, RPM, TPK는 변경하지 않았습니다.

GBS 빌드 root 안에서 native 구현과 두 .NET TPK를 컴파일했습니다.
CTest 20개 중 16개 PASS, root 전용 4개 명시적 SKIP이며 managed 시험 5그룹은
모두 PASS입니다. SDK installed-tree 감사는 공개 헤더 10개의 독립 C11/C++17,
중복·역순 include, pkg-config 및 `consent_session_heartbeat()`를 포함한 정확한
42개 C export/reference를 통과했습니다. 선택 emulator에서는 패키징된 repository
feature 시험 17그룹과 공개 C API 시험 2그룹을 통과했습니다
(`root-regressions.log`). 같은 로그의 앞선 잘못된 실행파일 경로 시도는 실패로
보존했습니다. 패키징된 명시적 `--worker-channel` 시험은 실제 System SMACK,
CAP_SYS_PTRACE, NNP에서 양단 kernel 신원, 연결 Parcel 교환, 잘못된 parent 거부,
일시 stat 실패 재시도 및 자기 node 정리를 통과했습니다(`worker-channel.log`).
빈 label 허용이나 인증 완화 없이 build28의 socketpair 시작 실패를 해결했습니다.

RPM 5개와 TPK 2개를 보존했습니다. target에는 runtime/daemon/tests/PoC RPM
4개를 정상 의존성 transaction으로 설치했으며 capi-base-common은 0.4.82-1을
유지합니다. 일치하는 0.4.82 devel을 구하지 못했으므로 SDK 헤더/pkg-config 결과를
target devel 설치로 표시하지 않습니다. positive TPK는
`gbs-build-29/org.tizen.consentui-0.1.0.tpk`, SHA-256은
`f2bce18e1df11d7d598d7101a1c39ff215350cfe5d8aa93ac764fe84df001647`입니다.
negative TPK SHA-256은
`117811b4fd74edf646edb94f70aeceb06c3776e4ec7b81a25ffc5164a91d102a`입니다.
`installed-hashes.log`에서 설치 ELF/TPK/DLL 59개와 정확한 RPM/TPK payload의
일치 및 설치 라이브러리의 공개 C 심볼 42개 조회를 확인했습니다. 기대 해시와
재현 감사 프로그램도 같은 폴더에 있습니다.

이번 증분의 빌드 및 시나리오 시작 명령은 다음과 같습니다. 재현 시 Guide08의 일치하는 RPM 설치·명시적 PoC setup과 새 artifact 폴더가 필요하며, 보존한 완료 run을 덮어쓰지 마세요.

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

### 실제 설정·승인·실행

대상은 1920×1080 Public Common Emulator(`emulator-26101`,
`calendar-resolution-p5`)이며 TV 하드웨어 수용 결과가 아닙니다. Aurum으로 실제
앱 버튼을 조작하고 입력 후 screenshot을 확인했습니다. API 승인 응답은 주입하지
않았습니다. 유계 private bridge, 실제 libconsent/daemon 및 격리 mock provider/holder를
사용했습니다. `feature-main/*.png`, 단계별 로그와 `analysis-final-main/`이 근거입니다.
후자는 gate 시험·재시작 전 main coordinator 원문을 보존하며 재현 가능한 범위
검사 25개를 포함합니다. 원문 journal SHA-256은
`645d09c1baf580e9df075aedc77768330442473cfd75b75405b32c0f9ddcb354`입니다.

아래 main 흐름은 모두 coordinator PID568853, epoch
`selection-6255879d-c90d-426a-8406-f43ea8758e82`에 속합니다. 최초 calendar 선택은
revision2, digest
`9d801e44597b421bb63f57fbdb18c8a4daf731b3b8dbf9a548f4b8a5330b9b94`입니다.

| 실제 동작 | 관측 결과와 화면 근거 |
| --- | --- |
| KO calendar SESSION 사전승인 | 설정 2페이지·승인 3페이지 후 실제 허용. revision2 ALLOWED, action_count0. `prompt-ko-1.png`~`prompt-ko-3.png`에서 기능/제공앱/정확한 대상, 목적/수신자, 대화 접근기간과 별도 결과 보관기간 표시를 확인했습니다. |
| 첫 작업 | 실제 CE AUTHORIZE receipt, provider action, holder 등록으로 action_count1. `task-complete-ko.png`, `task-first.log`. |
| 설정 닫기·재실행 | UI process가 30초 session lease보다 오래 닫혀 있어도 actor heartbeat가 대화/선택을 유지했습니다. 후속 작업은 같은 artifact를 재사용하여 action_count2. `settings-closed.png`, `reopened-ko.png`, `reuse.log`. |
| 기기 기능 추가 | revision3에 두 선택을 결합(digest `27ee594b9facb55db22fb496b01422a1cfbad5434ab8fd1f1d95e2aa8525221c`). 실제 승인에는 부족한 기기 조건만 1/1로 KO 3페이지에 표시. 사전승인 중 실행 없이 count2 유지. `missing-device-ko-1.png`~`-3.png`, `missing-device.log`. |
| 일정+기기 작업 | 기존 calendar artifact 재사용 후 신규 기기 AUTHORIZE/start. 정확히 두 효과로 count4. `combined-review-1.png`~`-4.png`, `combined.log`. 여러 provider 효과는 순차적이며 원자적 transaction이 아닙니다. |
| 넓은 일회 일정 작업 거부 | 실제 next30 범위 popup에서 거부. 넓은 범위 실행 없이 count4와 저장 revision3 유지. task-only는 설정을 바꾸지 않습니다. `expanded-prompt-1.png`, `expanded-denied-click.png`, `expanded-denied.log`. |
| 이미 허용된 대안 | task-only 체크 해제 후 명시적 대안은 기존 next7 artifact만 사용하며 넓은 데이터를 취득하지 않음. count5. `alternative-review-1.png`~`-3.png`, `alternative.log`. |
| EN 기기만 30분 사전승인 | 설정 4페이지·승인 5페이지 후 실제 허용. revision4, duration1800000, ALLOWED, count5. `timed-review-en-*.png`, `timed-prompt-en-1.png`~`-5.png`, `timed-confirmed.png`, `timed.log`. target에서 30분을 기다린 만료 시험은 아닙니다. |
| 전체 해제·대화 종료 | 빈 선택을 revision5로 저장하고 일반 작업 버튼 비활성화. 명시적 conversation-close 후 holder buffer wipe/ACK: CLOSED, pending0, revision6, artifact/session 비움, count5. `clear-saved.png`, `closed-ack-confirmed.png`. |

main artifact `2e531f904c95bd9226ba3402dbb78240f0c78f86ae020963`, 원래 receipt
`0362f371dddabf8031538d1a1e3b0774969de824f95fbeb3`, session
`1962ba3d75eece5e295cfd87055beda3bbccf6fc674dc07c`/generation1은 세 재사용 효과
전체에서 동일합니다. 복합 job은 `feature-0d5dd8c6-2fe8-4ad7-a56c-a6d4dd12000b`이며
`.0`은 재사용, `.1`은 기기 작업입니다. 넓은 요청은 별도로 거부했으며 대안으로
재해석하지 않았습니다. 앞선 EN 8페이지 설정 검토는 저장 전에 만료되어 선택/실행
상태를 바꾸지 않았습니다. 이를 성공한 KO 저장 및 이후 EN TIMED 승인과 분리해
보존했습니다.

### 실제 해제 gate와 별도 주입 회귀

두 시험은 별도 컴파일한 test coordinator와 정확한 test 역할을 사용하며 일반
coordinator에는 gate switch가 없습니다. 같은 coordinator/job이 멈춘 동안 실제
UI에서 calendar 체크 해제→빈 선택 검토→저장을 수행해 revision2→3으로 바뀌었습니다.
각 gate 폴더에는 screenshot을, 그 아래 `protected-evidence/`에는
`ready/observed/release/terminal/audit.json`과 `journal.jsonl`을 보존했습니다.
audit stdout은 별도 상위 경로 `emulator-build-29/gate-acquisition2-audit.log`와
`emulator-build-29/gate-reuse-audit.log`에 있습니다.

| Gate | 근거·신원·결과 |
| --- | --- |
| 취득, `gate-acquisition2/` | PID576626, job `feature-0c6ac4d0-3971-47ed-994f-5a67837e81b9`, operation `.0`, 실제 receipt `71e236bf5d283b967965935d44ff0be3bb54c79b9c27b06d`. 취소 후 release했으며 해당 action event는 0건. artifact 취득도 없음. |
| 재사용, `gate-reuse/` | PID577670, job `feature-87688740-97a0-4edd-9fea-a71eb5169ae7`, operation `.0`. artifact `b1c04b2cc47fe5e9e33fbb231ed410e8a66776017cd5bc2b`, session `38b9fea733aac264e939ff26277ee5c5a2d62f37ed19f842`/generation1의 실제 reuse-data 검사 성공. 신규 취득 receipt가 아닌 artifact-permit 근거입니다. 취소 후 release, 해당 재사용 action 0건. |

재사용 근거는 context SHA-256
`ee337b6ac381f3c15a015e53a05803fd34633b503cabdebfcbf4b15235a2a84a`에도 결합됩니다.
기준 상태에는 실제 취득 action 1건이 있었고 재사용 차단 뒤에도 count1을 유지합니다.
gate unit 중지/원복 전에 살아 있는 coordinator/holder에서 실제 UI 대화 종료를
수행했습니다. buffer wipe/ACK, CLOSED/pending0, revision4, artifact/session 비움은
`gate-reuse-closed.log`와 `gate-reuse/closed-confirmed.png`에 기록했습니다.
서비스 종료 자체를 데이터 정리 근거로 사용하지 않습니다. `analysis-final-gates/`의 독립 근거 분석은 같은 boot/invocation 및 marker/job 결합을 검사하며, 취득·재사용 release는 각각 기한 2957ms·975ms 전에 완료됐습니다.

첫 취득 시험(`gate-acquisition/`)은 UI 확인 중 기존 30초 gate 기한을 넘겨
terminal expired/release 실패했습니다. 이를 해제 gate 성공으로 세지 않고 fail-safe
실패로 보존합니다. 두 번째 시험은 기한을 늘리지 않고 통과했습니다.
Native의 CAS/retry/restart epoch, 응답 유실 dedup, 만료 및 live/retired 지연 완료
주입 회귀는 `/var/tmp/consent-artifacts/feature-native/coordinator-29.log`와
GBS 시험 로그로 분리합니다. 이미 시작된 효과/오류 및 불확실 결과의 유계 보존을
검증하지만 실제 target 지연 응답 주입이나 시작된 작업의 rollback 주장은 아닙니다.

### Endpoint·wire·최종 복원

`feature-endpoint.log`와 보호된 근거 파일은 직접 bind한 가짜 서버 및 이름만 바꾼
다른 PID1 소켓을 요청 byte 송신 전에 거부했음을 확인합니다. exact29 negative TPK
PID580603은 UID5001과 같은 `/usr/bin/dotnet-hydra-loader`를 사용하지만 실제 label은
`User::Pkg::org.tizen.consentui.negative`입니다. `negative-probe.log`,
`negative-daemon.log`는 native 오류와 coordinator `ui-peer-rejected`, consentd
`role=rejected/no matching live trusted identity`를 같은 PID로 연결합니다.
transport 오류만으로 인증 거부를 입증했다고 보지 않습니다.

exact29 기존 `wire` 시나리오는 실제 hello `approval_version=1`, 분할/합친 Parcel,
malformed frame 유계 종료와 기한, checker 연결 24개 허용/4개 거부 및 별도 client
응답을 유지한 output pressure 종료를 검사합니다. `wire.log`/`wire-daemon.log`에서
같은 daemon PID580802, quota 거부 이유, output-limit/write-timeout 이유 및
integrity_check=ok/schema2를 확인했습니다. 이 assertion 범위를 넘는 malformed
DB 미실행 또는 pending transaction shutdown 증거로 확대하지 않습니다.

`feature-stop.log`, `setup-stop.log`, `cleanup.log`는 feature/PoC/isolated 서비스와
소켓 inactive, PID0, override 없음, 일반 PoC 역할 복원, 임시 worker 소켓 없음,
소유 gate FIFO 3개 제거를 확인합니다. 보호된 일반 증거 파일과 설치 TPK는 검토용으로
유지했습니다. 소유 Aurum forward55051/bootstrap도 종료했습니다. production
서비스/소켓은 security_fw와 default-deny 역할로 active이며, 기존 DB/registry의
device/inode `65026:128673`/`65026:128674`, 소유자402:402를 유지했습니다.

완료 범위는 격리된 기능 사전승인·부족분 승인·정확한 실행/재사용 차단 경계와 UI/mock
연동입니다. 제품 Settings/argo, 실제 provider/Installer lifecycle hook 및 제품
역할 정책은 미연동입니다. registry 전체 유실은 여전히 명시적 복구/재등록이 필요하며
유계 maintenance는 임의 ledger 삭제가 아닙니다. 강제 전원 차단, 실제 filesystem-full/
device-write 실패 및 TV 하드웨어는 이 근거에 포함하지 않습니다. 이전 storage/shutdown
검증은 원래 build 범위를 유지합니다. production 권한이나 UI capability는 늘리지
않았습니다.
