# 과거 검증 기록

이 기록은 이전 빌드와 에뮬레이터 검증 결과를 보존합니다. 각 절은 표시된
스냅샷의 결과이며 현재 소스의 검증 결과가 아닙니다. 현재 요약은
[가이드 07](../guides/07-verification.ko.md)을 참고하세요. 개인 호스트 경로는
이식 가능한 예제로 바꿨으며 실제 명령은 외부 산출물 보관본에 남아 있습니다.
문서 수정으로 테스트를 다시 실행하지 않았습니다.


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
정리한 mock C wrapper5개/INI18개의 끝 빈줄 제거는 고정26 이후 변경입니다.
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
session은 다음 증분에서 계속 사용합니다. Guide10 재현 명령 보강은27 export
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
  /path/to/consent
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

## Build 30: 유계 cleanup 순회

`d76b21b` 기반 frozen source tree
`46349c78b1f607d3100d1d0fc8928f3ae9eaee83` 및 산출물은
`/var/tmp/consent-artifacts/gbs-build-30/`에 보존합니다.
Release `0.1.0-2`는 정상 업그레이드 NEVRA입니다. 실행 명령:

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

첫 의존성 해석은 repository의 고정 csapi-tizenfx-nuget14.0.0.19364 부재로
실패했습니다. 기존 cache의 동일 RPM을 local GBS repository에 추가했고
cache-dependency.json에 원본/hash, dependency-failure.log에 실패를 보존했습니다.
재시도 GBS CTest는 17 PASS/실제 root 전용4 SKIP/0 FAIL입니다. 신규 cleanup
GTest/GMock10개는391ms에 통과했습니다. runtime/daemon/devel/PoC autoRequires에
GTest/GMock는 없으며 consent-tests에만 시험 library 의존성이 있습니다.

sdb devices로 다시 확인한 x86_64 emulator-26101에 RPM5개를 정상 업그레이드했습니다.
처음4개 RPM dry-run은 기존 devel의 exact version coupling 때문에 실패했고
matching devel을 함께 넣어 nodeps/force 없이 통과했습니다. target의
capi-base-common0.4.82와 이미 설치된0.4.82 development provider를 유지했습니다.
설치 중 기존 ldconfig permission 진단은 로그에 남았지만 정상 transaction 및
후속 loading/시험은 성공했습니다. 장치 근거는
`/var/tmp/consent-artifacts/emulator-build-30/`에 있습니다.

- installed-hash-audit-batched.json: library/daemon/tests/공개 헤더/examples 등
  설치 regular file76개가 RPM payload hash와 일치했습니다. 첫 명령은 SDB
  service-name 길이 제한으로 실패하여 installed-hashes.log에 보존했습니다.
  symlink의 zero digest는 내용 hash 비교에서 제외합니다.
- cleanup-gtest.log: packaged10개 PASS47ms, service exit0.
- cleanup-api.log: 실제 격리 승인/authorization receipt와 C API로 artifact97개
  등록. 첫48 ACK 실패 후 continuation으로 뒤49개에 도달하고 새 sweep에서
  실패48개를 재시도하여 목록이 비었습니다. integrity_checkok/schema2,
  service exit0/3.060s.
- root-fixtures.log/image-authority.log: abuild에서 생략된4개 fixture를 실제
  root로 private fixture/image state에서 실행하여 통과했습니다.
- public-abi-default-deny.log: 공개 ABI 시험 통과. 첫 production probe는
  PERMISSION_DENIED를 기대했지만 DISCONNECTED를 관측했고 corrected
  default-deny-disconnect.log에서 실제 transport 계약으로 통과했습니다.
  default-deny-actor-file.log에 actor PID3539575/UID0, 대응하는
  default-deny-daemon-file.log에 같은 PID/UID의 instance4 인증 거부 및 no
  matching trusted identity 이유가 있습니다. production role은 추가하지
  않았습니다. 앞선 shell-inline probe의 systemd dollar expansion 및
  OUTCOME_UNKNOWN disconnect race 로그는 보존하며 성공한 대응 근거와 구별합니다.
- installed-state.log: production DB/registry device/inode65026:128673/128674,
  owner402:402/mode0600 유지. security_fw/default-deny service/socket active.

보조 host+SDK10개 시험은484ms에 통과했습니다. 최종 성공 compile argv/run env는
native-increment30/successful-command.json, 앞선 link 실패는 별도 로그입니다.
보조 시험은 GBS/device 근거를 대체하지 않습니다. Build29 persistent baseline
실패는 실패로 남깁니다. 성공한 미승인 QUERY1000 smoke는 CONSENT_REQUIRED이며
service runtime1.845s는 process/client 생성·출력을 포함하므로 per-call latency,
cache/async 성능 근거가 아닙니다.

이번 snapshot의 검증 범위는 cleanup 순회입니다. 최종 ownership/GIO/DLOG/storage/
정상 reboot 회귀는 후속 작업이며 이전 빌드 storage/shutdown을 이번 실행으로
소급하지 않습니다.


## Build31 실패 보존과 Build32 ownership/DLOG 검증

2026-09-29의 증분2 frozen source는 Build32 tree
`8b78319ed8c73c4d3f7b8c1d609b989d56f0134d`입니다. Build31 tree
`b9c5413af357ac727e5c03cde0b56a41d8673483`는 offline identity 시험의
`key_file.cc` 누락으로 %build 링크 실패했습니다. 실패 archive/diff/log를
`/var/tmp/consent-artifacts/gbs-build-31/`에 보존하고, 해당 target 연결을
수정한 뒤 새 번호로 고정했습니다. Build32 빌드 중 소스는 변경하지 않았습니다.

정확 command, archive, diff, GBS log, LastTest.log, RPM 및 SHA256SUMS는
`/var/tmp/consent-artifacts/gbs-build-32/`에 있습니다. 명령은 다음과 같습니다.

```sh
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root --threads 4 --overwrite
```

%check: 22개 중 18 PASS, 실제 root 전용 4 SKIP, 0 FAIL입니다.
cleanup GTest/GMock 10개, ownership/logger 13개가 통과했습니다.
부가 native 13 PASS는 SDK headers와 host GLib/SQLite 조합이며 GBS 또는
emulator 근거로 확대하지 않습니다. 정확 성공 argv/env와 초기 실패 로그는
`/var/tmp/consent-artifacts/native-increment31/`에 구분 보존했습니다.

Release3 matching consent/consentd/tests/devel/poc 5종은
emulator-26101 x86_64에 설치했습니다. 첫 System context 설치는 SMACK label과
service 파일 쓰기 거부로 부분 실패했습니다. 원본 install.log를 유지하고,
System::Privileged에서 정상 rpm -Uvh --replacepkgs transaction으로 전체를
재설치해 service exit0 및 동일 0.1.0-3 NEVRA를 확인했습니다. --nodeps 또는
production role 완화는 사용하지 않았습니다. requires의 GTest/GMock는 tests에만
있고 runtime/daemon/devel/poc에는 없습니다.

새 실제 근거는 `/var/tmp/consent-artifacts/emulator-build-32/`에 있습니다.

- ownership 13 PASS/12ms, cleanup 10 PASS/41ms, public C API ABI/ownership PASS.
- fresh isolated basic, C API cleanup97, malformed/fragmented/pressure wire,
  partial-I/O shutdown, accepted DB revoke drain 후 durable revocation PASS.
- root SKIP 4종을 packaged offline registration/identity, storage prepare,
  image-root authority fixture로 다시 실행해 모두 service exit0.
- installed-hash-audit-batched.json: regular 142 중 141 일치. 기존 PoC 시험의
  `/etc/consent-poc/roles.conf`는 %config(noreplace) 보존으로 별도 기록합니다.
  installed-payload-audit.json의 보존 config 제외 141/141은 일치합니다.
  production daemon/library/devel rpm -V도 exit0입니다.
- production default-deny actor PID3549928 UID0 status=-107과 동일 PID/UID,
  instance1의 daemon 인증 거부를 결합했습니다. disconnect 단독을 role 거부
  근거로 사용하지 않습니다. daemon security_fw UID/GID402, state700/DB600.
- 실제 dlog_print: PID3547174의 I/CONSENT, ownership_test.cc:155 source와
  percent=100% 원문을 확인했습니다. GMock sink 시험과 별개의 실제 backend
  근거입니다. daemon PID3548033의 I/CONSENTD server.cc source, partial input2,
  stop-admission/database-drained는 stderr/journal fixture와 대조했습니다.
  전역 log clear는 하지 않았습니다.

이번 구현은 transactional admission, FD/GLib allocation RAII, bounded
Dispatcher 취소, noexcept callback/Close/Stop 경계와 할당 없는 shutdown job을
추가합니다. preallocated I/O quit source는 thread가 Run에 진입하기 전에 발생한
종료를 보존합니다. wake source는 producer join 및 queued callback 취소 이후
파괴합니다. Stop/drain 시험은 실제 DB sentinel→join 뒤 카운터를 확인합니다.

Build29/30 근거는 이전 source 범위로 유지합니다. GIO client 전환, 동일 persistent
handle 성능 전후 비교, registry-loss helper 설계/구현, final storage/reboot/CEP
감사는 아직 완료가 아닙니다. 성능 fixture의 실패 setup 로그를 성공 baseline으로
취급하지 않습니다. 외부 제품 identity/provider 연결은 실제 공급원이 필요한
범위를 별도로 유지합니다.

## GIO 전 Build32 저장소 도구 baseline

저장소 성능 도구와 추출 helper는 보존된 seventh prototype과 소스가 다릅니다.
권위 있는 비교 baseline은 별도 파일
`/var/tmp/consent-artifacts/performance-build32-tool2/baseline-first.log`이며
exit 0, 전체 19.218 s입니다. Build32 격리 static client archive와 packaged
`consentd-test` 조합으로 production shared-library variant 수치가 아닙니다.
동일 디렉터리의 `compile-command.json`, `prepare.log`에 compile argv와
seed/binary hash가 있습니다.

API별 warmup 100, 측정 n=1000입니다. QUERY/AUTHORIZE/CHECK/D16은 daemon을
통해 ALLOWED를 반환하며 이번 legacy sync/async는 CACHE 1000입니다.
D16 approval_version=1은 cacheable=0을 유지합니다. Acceptance와 callback
latency, API interval 역수와 실제 measured wall throughput을 각각 구분합니다.
Idle context switch 79/2 s는 측정 client와 보조 UI handle 두 I/O thread를
포함하는 process-wide proxy입니다.

`scripts/emulator-performance.sh`는 매 unique attempt 전에
`/opt/var/lib/consent-perf-template-build32-tool2`의 격리 DB/registry/authority/
control을 복원하고 종료 시 base runtime roles를 복원합니다. `--capture`는
fresh basic 준비 후 새 seed만 생성하며 기존 seed를 덮어쓰지 않습니다.
전후 비교는 동일 tool/helper source, 명시적 gcc/g++ flags, seed, actor path와
측정 조건을 사용하며 비교하는 archives와 daemon만 바뀝니다. CMake-built 도구의
flags가 자동으로 동일하다고 간주하지 않습니다. Seventh prototype과 실패 attempt는
별도 근거로 유지합니다. 이 역사적 checkpoint 당시 Build33 GBS/device와
GIO 후 측정은 미완료였으며 완료 결과는 아래 Build33 section에 기록합니다.

## Build33 GIO 검증과 성능 후속 증분

Frozen tree `0b5c8ed5847c32b64c9462d47b76f73e64999136`, Release4는
`/var/tmp/consent-artifacts/gbs-build-33`에 보존됩니다. GBS exit0이며 CTest25는
21 PASS, root 전용4 SKIP, 0 FAIL(17.20 s)입니다. 세 GTest/GMock library는
consent-tests만 의존합니다. `emulator-build-33`에는 정상 matching5 RPM upgrade와
installed client/IO4/owner3/signal/ownership13/cleanup10/publicAPI, 실제 root4의
service exit0 근거가 있습니다. Regular file은147중146 match이며 유일한 POC
roles.conf 차이는 `%config(noreplace)` 보존입니다. Versioned ABI symbol42 유지.

최초 basic은 기존 control generation의 fresh guard에서 실패했으며
`scenario-basic.log`는 실패 근거로 보존합니다. 이전 격리 디렉터리를 보존한 뒤
`scenario-basic-second.log`에서 phase PASS/service0를 확인했습니다. Cleanup97,
wire, partial shutdown, accepted DB drain도 각각 phase PASS/service0입니다.
Actual command JSON은 SDB transport exit와 systemd service exit를 구분합니다.
Default-deny 최초 expect-107은 hello OUTCOME_UNKNOWN 때문에 실패했습니다.
두 번째 installed-library probe PID3562808/UID0는 두 constructor에서 -107과
UNKNOWN을 반환하고 handle은 둘 다NULL입니다. Daemon instance2/3의 동일PID/UID
no matching trusted identity 거부를 결합합니다. 이는 인증 거부에 따른 연결/hello
실패이며 PERMISSION_DENIED 정책 응답 근거가 아닙니다. 실제 DLOG PID3560012의
literal100%/source/tag/level과 shutdown PID3560555 출력은 global clear 없이 보존.

정확한 tool2 Build32→Build33 비교와 두 번째 post trial은
`performance-build33-tool2`에 있습니다. Source/helper/flags/seed/actor path는
새 Build32 baseline과 같습니다. 별도 `performance-controlled-build33`은 동일
Build33 daemon에 두 client를 교차 실행했습니다. CPU1.72/1.66→2.37/2.08s,
RSS5148–5240→7816–7988KiB, idle process context switch79/80→1/1(2s)입니다.
네 trial 모두 ALLOWED와 CACHE/DAEMON 집계를 확인하고 PASS했습니다. Controlled
daemon 범위는 원 full-package baseline과 구분합니다. 반복된 CPU/QUERY 비용 때문에
immediate nonblocking write 후속 설계를 승인받았으나 유일한 원인으로 확정하지
않습니다. 이 역사적 checkpoint 당시 Build34/Release5 시험과 성능 비교는
미완료였으며 완료 결과는 아래 Build34 section에 기록합니다.

Build34 후속 native 근거는 SDK header/host GLib 보조 시험이며
`/var/tmp/consent-artifacts/native-increment34`에 있습니다. 최초 owner는
4 PASS/1 FAIL로 30000-byte 단일 값이 I/O 전에 거부됐습니다. 실패 로그/명령/binary를
보존했습니다. 수정 fixture는 frame마다 제한 내4000-byte 값8개를 사용합니다.
두 번째 owner5 PASS는 실제 partial WOULD_BLOCK과 두 frame 순서/완전복원을
확인하며 기존 client 회귀도 PASS입니다. 성공한 두 번째 argv/env/exit는 별도 파일.
이 checkpoint에서 후속 GBS와 target 검증은 아직 미완료입니다.

## Build34 immediate-write 근거

Release5 frozen tree `0b4dc11144511bf55b63b82a7d95a4535dfa2dbf`는
`gbs-build-34`에 보존됩니다. GBS0, CTest21 PASS/root4 SKIP/0 FAIL(16.64 s).
`emulator-build-34`에는 정상5 RPM upgrade, client/IO4/owner5/signal/ownership13/
cleanup10/publicAPI/root4 service0와 fresh basic/cleanup97/wire/partial shutdown/
DB drain의 phase PASS/service0가 있습니다. Regular146/147 match에서 유일한
POC config는 이전과 같은 보존 항목이며 symlink9/9, ABI42도 일치합니다.
Default-deny PID3566797/UID0는 두 UNKNOWN/NULL handle과 동일PID daemon role
거부를 결합합니다. 실제 DLOG ownership PID3565699 literal100%, shutdown
PID3566335를 journal 근거와 결합해 보존합니다.

`performance-build34-tool2`의 exact-tool full-package trial은 새 archives
3b9ee069/c5a85e13, daemon5bf9fc06, 실제 pull한 actor b2bbefb1을 사용합니다.
CPU1.91/1.88s, RSS7936/7880KiB, idle proxy1/1은 이전 Build33 package pair와
구분합니다. 별도 `performance-controlled-write34`는 같은 Build34 daemon에
before33/after34 client를 실행하며 CPU2.15/2.16→1.89/2.23s입니다. 모든 trial은
ALLOWED/source 집계와 PASS를 확인했지만 혼재된 결과로 CPU 개선을 일률적으로
주장하거나 유일한 원인을 확정하지 않습니다. Immediate nonblocking write가
output을 비우면 readiness source를 만들지 않으며 partial I/O 계약은 유지됩니다.

다음 승인 증분은 이미 완료된 cache hit의 I/O wake만 생략하고 caller-context
delivery와 noncache wake를 유지합니다. 이 증분의 native/GBS/installed/성능
검증은 아직 미완료입니다.

Build35 cache-only wake native는 owner6(cache hit/miss context readiness 대조와
원격 byte 없음 포함), 기존 client 모두 PASS입니다. 정확한 성공 argv/env/exit는
`native-increment35`에 있으며 SDK-header/host-GLib 보조 결과로 분류합니다.
Release6 GBS/device와 동일도구 성능은 아직 미완료입니다. Focused GIO checkpoint
뒤에는 별도80열 checkpoint, 검토된 registry-loss helper, authoritative final-source
storage/reboot/CEP 감사 순서로 진행하며 추측성 최적화를 추가하지 않습니다.

## Build35 cache-only wake 검증 (2026-09-30)

Release6 frozen tree `07d663838c306aa2eda89727e7e90ffe6d98e2f6`는
`/var/tmp/consent-artifacts/gbs-build-35`에 보존합니다. GBS exit0이며
CTest25는21 PASS/root4 명시적 SKIP/0 FAIL(16.98 s)입니다. Test library 의존성은
tests RPM에만 있습니다. GBS 종료 후 한영 문서 보완은 이 frozen source snapshot
밖의 변경입니다.

`emulator-build-35`에는 정상 matching5 RPM transaction 및 실제 client/IO4/
owner6/signal/ownership13/cleanup10/publicAPI/root4 service exit0가 있습니다.
Fresh basic/cache/cleanup97/wire/partial-I/O shutdown/accepted DB drain은 각각
phase PASS/service0입니다. Installed regular146/147 match에서 유일한 보존 POC
설정은 `%config(noreplace)`입니다. Symlink9/9, versioned ABI42, security_fw
UID402/storage700/DB600을 유지합니다. Default-deny actor PID3574606/UID0는
두 constructor에서 UNKNOWN/NULL을 반환하고 daemon instance1/2의 동일PID/UID
거부를 결합합니다. 인증 거부 후 connection/hello 실패 근거입니다. DLOG ownership
PID3572539의 literal100%/source/tag/level, shutdown PID3574521과 isolated
journal을 전역 clear 없이 대조합니다.

`performance-build35-tool2`의 exact-tool full-package 두 trial은 actor
`eea4147c`, daemon `75ae7a17`, 동일 source/helper/script/compile flags와
immutable seed를 사용합니다. 모두9 metrics/n1000/warm100/ALLOWED, legacy
CACHE1000 및 CHECK/D16 DAEMON1000으로 PASS입니다. CPU1.99/1.88s,
RSS8020/7812KiB, idle proxy1/1은 Build34 대비 일률적 개선 근거가 아닙니다.
격리 static client archive variant이며 production shared-library 수치가 아닙니다.

| 작업 | 현재 근거 | 남은 작업 |
| --- | --- | --- |
| 내부 cleanup/ownership/DLOG/GIO/style | Build43 범위별 GBS/device 회귀 | 최종 CEP 근거 매핑 |
| Registry 유실 경계 | Build43 receipt 차단과 DB-only 복구 | 신뢰할 최신 desired source, 전손 import, holder reconciliation |
| 외부 제품 통합 | 실제 source 및 installed package 조사 | Installer lifecycle 및 인증 role/provider/history/model adapter |
| 최종 검증 | Build43 scenario·fixture·정상 reboot | 갑작스러운 전원 종료와 CEP A-01–A-63 감사 |

앞선 pending 문장은 당시 역사적 checkpoint 상태입니다. 아래 Build43 절이
구현·storage 행을 갱신하며 표에 적은 남은 작업은 계속 열려 있습니다.

별도 `performance-controlled-cache35`는 동일 Build35 daemon(`75ae7a17`)에
before34/after35 client를 각각 두 번 교차 실행합니다. 모두9 metrics와 동일
seed/source 집계 조건으로 PASS입니다. CPU2.16/2.17→2.11/2.15s,
RSS7816/7884→7912/7896KiB는 작은 편차이며 광범위한 성능 개선 주장은 하지 않습니다.
이번 변경의 cache-only I/O wake 생략은 owner6으로 검증했습니다. 추가 추측성
최적화 대신 남은 구현과 최종 감사로 진행합니다.

## Build36 기계적 스타일 checkpoint (2026-09-30)

Release7 frozen tree `99abb4cea73186fbda07859641dcba0069d979cd`의
229파일 archive·byte manifest·target GBS export가 일치하며 근거는
`/var/tmp/consent-artifacts/gbs-build-36`에 있습니다. GBS exit0, CTest25는
21 PASS/root4 명시적 SKIP/0 FAIL(16.80초)입니다. GTest/GMock 의존성은 tests
RPM에만 있습니다. Include/using 순서, native decoded literal, 생성 IDL byte,
Python/C# 구문과 값, XML 값, GLib INI 값을 보존했습니다. Frozen host 보조
증명은 `/var/tmp/consent-review-20260929/build36-*`에 있으며 target 시험의
대체 근거가 아닙니다. 진단 line과 macro stringification은 이동할 수 있습니다.
프로젝트 파일12개의30줄은 분할하기 어려운 shell/SQL quoted argument,
CMake 인자, INI 값, JSON IDL 문자열, service-unit path 때문에 80열을
넘습니다. 추가 분할에는 별도 동작 검토가 필요합니다.

`/var/tmp/consent-artifacts/emulator-build-36`에는 정상 matching5 RPM
Release6→7 upgrade와 packaged test fixture11개의 service exit0가
있습니다. Fresh isolated basic/cache/cleanup97/wire/shutdown/DB drain 여섯
API 단계는 phase PASS/service0입니다. Format된 offline/platform-offline
script도 target setup·teardown 실행에 통과했습니다. PoC registration service는
현재 TPK digest와 app ID를 일치시켜 등록했고, socket-start 실패 fixture는
자신의 override를 정리합니다. Host .NET UI unit 다섯 그룹이 통과하고,
GBS는 실제 UI·negative TPK 두 개를 만듭니다.

RPM metadata regular147개 중 installed146개 hash가 일치합니다. 나머지
`/etc/consent-poc/roles.conf`는 `%config(noreplace)`로 보존됩니다. Installed
public ABI는 versioned symbol42개입니다. Production service는 security_fw
UID402, state mode700, DB mode600입니다. Default-deny actor PID3588992/UID0의
두 constructor는 negative UNKNOWN/NULL이고 daemon PID3587308 instance1/2는
같은 PID/UID를 live trusted identity 없음으로 거부합니다. 이는 connection/
hello 실패이며 정책 응답이 아닙니다. DLOG에는 actor PID3587548의 literal
`100%`, CONSENT/INFO/source160이 남았으며 전역 log clear는 하지 않았습니다.
`systemctl show`는 변경된 PoC unit 인자를 파싱했습니다. Target에는
`systemd-analyze`가 없어 verify 도구 PASS는 주장하지 않습니다.

Build35 성능 도구 byte는 그대로 보존합니다. Format으로 Build36 도구 byte가
바뀌었으므로 Build35 수치를 Build36 동일-source 성능쌍으로 주장하지
않습니다. 이 checkpoint로 registry 전손 복구, 제품 Installer/role 연결,
최종 authoritative storage/reboot/CEP 감사를 완료한 것은 아닙니다.

## Build39–43 bootstrap·storage checkpoint (2026-09-30)

`/var/tmp/consent-artifacts/gbs-build-{38,39,40,41,42,43}`의 번호별 로그에는
실패와 성공 재시도가 함께 보존돼 있습니다. Build38은 미사용 함수 세 곳,
Build41은 검사하지 않은 write 두 곳의 `-Werror`로 빌드 실패했습니다.
Build39 scenario startup과 시작 반복의 10번째가 실패했고, Build40은
14번째, Build42는 5번째에 실패했습니다. Build42의 after-exec 진단은
5ms 시점 output 8바이트를 기록했습니다. 반복 ready/HUP pipe loop는
코드에 근거한 추론이며 revents trace는 없습니다. 실제 2초 timeout이나
MainPID=0 응답은 아닙니다.
Build43의 deadline/EOF 수정은 정확한 unit·cgroup·자기 PID 검사를
유지합니다. 실패 실행을 PASS에 포함하지 않습니다.

Build43 Release11 frozen archive SHA256은 `17425aa2`로 시작하며 236개
파일이 correlation 당시 GBS export와 worktree에 일치했습니다. 현재 guide
수정은 그 freeze 밖입니다. GBS exit0, CTest26은
22 PASS/root4 명시적 SKIP/0 FAIL입니다. Matching5 RPM upgrade와
isolated stop/start 100/100이 통과했습니다. Root SKIP 네 fixture 종류는
emulator에서 별도로 실행했습니다. Isolated bootstrap script는
missing-source 무변경, DB-only recovery, receipt 유실, activation FD와
environment를 복사한 외부 unit 거부를 포함해 7 PASS입니다. Isolated
POC classification은 정확한 ExecStartPre 거부와 실제 POC 파일 불변을
포함해 3 PASS이며 실제 absent-state POC Release7 upgrade 또는 깨끗한
production 최초 설치 RPM transaction 근거는 아닙니다.

Fresh isolated `basic` 및 시나리오 16 phase 모두 내부
`SCENARIO_EXIT=0`, phase PASS, SDB exit0입니다. Packaged native fixture
17개는 service exit0입니다. Frozen offline-image script는
`OFFLINE_EXIT=0`, production platform-offline script는
`OFFLINE_PLATFORM_EXIT=0`이며 실제 pkgmgr identity 확인 후 원본 production
DB·registry·authority·receipt inode를 복원했습니다. 이 fixture는 제품
Installer hook이나 registry 전손 복구용 최신 desired source가 아닙니다.

설치된 RPM metadata의 regular file151개 중 target hash150개가
일치합니다. 나머지 `/etc/consent-poc/roles.conf`는
`%config(noreplace)` 보존값입니다. Installed library의 `CONSENT_0.1`
public versioned symbol은 42개입니다. 현 packaged API의 default-deny
check는 UNKNOWN을 반환합니다. 별도 설치된 과거 constructor probe는
actor PID3622926/UID0에서 두 번 UNKNOWN/NULL을 얻었고, 현 daemon
PID3621672 instance2/3이 같은 PID/UID를 live trusted identity 없음으로
거부했습니다. 정책 판단이 아닌 connection/hello 실패입니다.

실제 정상 emulator reboot에서 boot ID `1842a2f1…`이 `e55266b3…`으로
바뀌었습니다. Release11 다섯 RPM이 유지됐고 consentd PID2565가 READY 후
active가 됐습니다. Registry와 root receipt의 hash/inode는 유지됐습니다.
DB inode는 같고 재부팅 뒤 hash가 달랐으며 `System::Privileged`의
`PRAGMA integrity_check`는 `ok`였습니다. 재부팅 후 보호 파일 검사 전에
SDB root mode를 다시 켰습니다. 이는 정상 reboot startup/integrity
근거입니다. 별도 fresh isolated fixture에서 PERSISTENT grant를 ALLOWED로
확인한 후 두 번째 실제 reboot(boot ID `e55266b3…`→`75d2b4dc…`)를
실행했습니다. 이후 동일 frozen script가 다시 ALLOWED와 DB integrity
`ok`를 확인했고 registry·receipt hash/inode가 유지됐습니다. 첫 시도는
reboot로 `/tmp` script가 지워져 실패했고 동일 frozen hash를 재전송한 후
성공했습니다. 이는 isolated 정상 reboot grant 시험으로, 갑작스러운
전원 종료 durability나 제품 role 통합 근거는 아닙니다. Build43의
running/stopped DB 삭제, stale replacement,
corruption, shutdown, DB drain phase는 isolated state이고 repository
crash fixture는 `/tmp`를 사용합니다.

Registry 전손 복구, 물리적 `cleanup_unknown` 해소, 실제 Installer·role
provider·history/model 연결 및 CEP A-01–A-63 최종 근거 감사는 남았습니다.
검증된 최신 desired-definition producer 전까지 production `--begin`은
저장소 변경 없이 missing-source 오류를 반환합니다.

Build43 근거 파일은 `source-correlation.json`, `gbs-success-log.txt`,
`rpm-upgrade-actual.log`, `isolated-start100-attempt1.log`,
`bootstrap-device-attempt1.log`, `poc-classify-device-attempt1.log`,
`packaged-fixtures-actual.log`, `scenario-*-attempt1.log`,
`offline-image-attempt1.log`, `offline-platform-actual.log`,
`installed-hash-audit.json`, `installed-abi-symbols.log`,
`default-deny-probe.log`, `default-deny-daemon.log`, `reboot-before.log`,
`reboot-after-root.log`, `reboot-integrity-daemon.log`,
`reboot-persistent-{before,after}.log`입니다. 각 실행 로그의 내부
service/script 결과를 확인하며 SDB exit만으로 PASS를 판단하지 않습니다.

## Build44 실패와 Build45 activation/version 시험 (2026-09-30)

Build44 Release12는 보존된 실패 시도입니다. Activation fixture가
`LISTEN_FDS=0`과 `LISTEN_PID`를 함께 설정해 `sd_listen_fds(1)`가 실제
FD 없음의 0 대신 `-EINVAL`을 반환했습니다. GBS `%check` 27개 중 한 개가
실패했고 Build44 RPM은 설치하거나 PASS로 기록하지 않았습니다. 수정된
fixture는 FD 없음에서 두 환경변수를 모두 제거합니다.

Build45 Release13 archive SHA256은 `98fad5b9`로 시작합니다. 239파일은
correlation 당시 GBS export와 worktree에 모두 일치했습니다. GBS exit0,
CTest27은 23 PASS/root4 명시적 SKIP/0 FAIL입니다. Matching5 RPM upgrade의
내부 결과와 SDB exit가 모두 0입니다. 설치된 test-only activation binary는
별도 `/tmp` endpoint를 사용하며 production listener를 열지 않습니다.
Packaged harness는 FD 없음, 정확한 test path의 잘못된 socket type, 다른
path의 stream socket, FD 두 개를 모두 거부했습니다. Target DLOG에는
`CONSENTD` PID9814–9817의 `count=0`, `endpoint`, `endpoint`, `count=2`가
있습니다. Child exit1과 정확한 로그는 bootstrap 전 음성 admission을
증명합니다. Parent FD identity는 유지됐지만 child의 명시적 close syscall은
추적하지 않았으므로 A-51의 FD leak 조항은 Partial입니다.

설치된 isolated wire scenario는 native Parcel frame의 envelope version
offset4..7만 1에서 2로 바꿨습니다. Frame 길이와 다른 byte가 같음을
확인하고 인증된 hello 뒤 전송해 bounded close를 관측했으며 다음 연결의
정상 hello와 같은 epoch를 확인했습니다. Script는 동일 daemon MainPID9973과
phase/service exit0을 확인했습니다. 이는 unsupported-version 거부 근거이나
early allocation 및 A-62의 모든 malformed-input 조항까지 증명하지 않아
A-62는 Partial입니다.

Production socket·DB·registry·bootstrap receipt는 fixture 전후 device/inode,
owner, mode, size가 일치했고 세 일반 파일의 SHA256도 같았습니다.
설치 RPM metadata regular155개
중 154개 hash가 맞고 나머지 POC roles 설정은 `%config(noreplace)`입니다.
Installed public ABI는 versioned symbol42개로 유지됐습니다. GTest/GMock
runtime 의존성은 `consent-tests`에만 있으며 devel/tests는 Release13과
결합됩니다. 근거는 `/var/tmp/consent-artifacts/gbs-build-{44,45}` 아래
Build45 `source-correlation.json`, `LastTest.log`, `rpm-upgrade-actual.log`,
`activation-device-attempt1.log`, `activation-dlog-after.log`,
`wire-device-attempt1.log`, `production-{before,after}-fixtures.log`,
`installed-hash-audit.json`, `rpm-requires-audit.json`,
`installed-abi-symbols.log`입니다. Production policy·wire decoder·ABI·
security flag는 변경하지 않았습니다.

## Build45 설치 회귀와 정상 재부팅 재시험

Release13 생산 바이너리 hash가 Build43과 달라 실제 설치 RPM을 다시
시험했습니다. `/var/tmp/consent-artifacts/gbs-build-45/current-rerun`의
packaged client, IO4, owner6 시험과 격리 `persistent`, `running-delete`,
`shutdown`, `db-shutdown`, `cache`, `wire`는 내부 종료값 0입니다. 첫
`stopped-delete`와 `db-shutdown` 시도는 복구 또는 DB drain 뒤 단발
`systemctl is-active`가 아직 `activating`을 읽어 실패했습니다.
`db-shutdown` 2차 시도는 통과했지만 `stopped-delete` 두 시도는 실패로
보존합니다. 별도 서비스 시작, DB 무결성, `cleanup_unknown=1`은 보조
근거이며 실패한 phase를 PASS로 바꾸지 않습니다.

앞선 삭제 시험 뒤 기존 격리 DB의 첫 `persistent`는 예상 decision 불일치로
종료값 1을 기록했습니다. 그 로그만으로 불일치의 원인은 확정할 수
없습니다. 기존 격리 store 세 곳을 hash로 기록하고 각각 이름에
`-build45-archive`를 붙여 보존했습니다. 새 격리 `basic`으로 PERSISTENT
승인을 만들고 재부팅 전후 `persistent`를 모두 통과했습니다. 정상
재부팅으로 boot ID는 `75d2b4dc-6fb5-43eb-8530-0ac117840417`에서
`41d99246-ba13-4ed6-97f3-85fe7c756f93`으로 바뀌었습니다. Release13
패키지 5개가 유지되고 생산 `consentd`는 PID2556으로 active였습니다.
격리 registry·receipt의 SHA256과 세 store의 inode가 전후 같고,
재부팅 뒤 DB 무결성은 `ok`입니다. 이는 정상 재부팅에서 격리 승인
지속성의 근거이며 돌연 전원 상실이나 제품 argo/session 근거는 아닙니다.
생산 서비스 재시작·무결성과 default-deny도 다시 확인했습니다. actor
PID13300/UID0은 daemon PID9642의 두 `role=rejected` 로그와 일치했고,
create는 음수 상태와 NULL handle을 반환했습니다.

## Build46 신원·세션 회귀 (2026-09-30)

Release14 archive SHA256은 `150a785d`로 시작합니다. 고정 소스 239개는
그 시점의 GBS export·작업 트리와 모두 같고, 이 가이드 수정은 그 뒤
변경입니다. GBS 종료값 0, CTest27 중 23 PASS와 root 전용 4 SKIP입니다.
동일 릴리스 RPM 5개의 emulator 업그레이드 내부 종료값은 0입니다.
설치된 repository-test는 서버 발급 session ID, 첫 세션의 정상 사용,
다른 세션의 `data_check`·derived parent `-EACCES`를 확인했습니다.
중지된 fixture DB에는 두 번째 세션 artifact가 없었습니다.

설치된 checker 전용 wire fixture는 유효한 subject/profile과 위조
PID/UID/GID를 담아 `session_open`을 보냈습니다. 실제 actor는
PID5693/UID0/GID0, 위조값은 105693/100000/100000입니다. 연관된 응답은
정확히 `CONSENT_ERROR_PERMISSION_DENIED`, 반환 session 없음이었고 다음
Hello는 정상입니다. 격리 daemon PID5683의 DLOG 두 건은 실제 커널
PID/UID/GID와 `role=wire-scenario`를 기록했습니다. daemon을 중지하고
읽은 DB session 수는 전후 1입니다. 바깥 identity policy와 안쪽
repository role guard가 같은 거부 코드를 쓰므로 어느 층이 반환했는지는
이 시험 하나로 구분할 수 없습니다. 결합된 fail-closed 동작을 검증했으며
A-47은 Partial입니다.

시나리오의 `/proc/uptime` 기반 제한 대기는 `ActiveState=active`,
`Result=success`, 변하지 않은 0이 아닌 MainPID를 요구합니다. 설치된
`stopped-delete`, `db-shutdown`, `running-delete`는 이 조건과 복구·중지된
DB 무결성 검사를 모두 통과했습니다. 기존 격리 state/authority/control을
hash와 함께 archive한 뒤 Release14의 새 `basic`, 이어진 `cache`와 전체
`wire`도 각각 내부 종료값 0입니다. 생산 default-deny probe는 음수
상태와 NULL handle을 반환했고 actor PID6436/UID0은 daemon PID5455의
DLOG 두 건에서 거부됐습니다.
생산 서비스를 중지한 상태의 privileged DB 조회는
`integrity_check=ok`와 schema version2였고, socket/service 재시작은
PID8067, `active`, `Result=success`였습니다. 설치 regular file은
RPM 155개 중 154개 hash가 일치합니다. 유일한 차이는
`%config(noreplace)`인 POC roles 파일로,
전체 `rpm -V` 종료값 1도 이 설정 파일 때문입니다. ABI는 versioned
symbol42개이고 GTest/GMock runtime 의존성은 `consent-tests`에만 있습니다.
설치된 offline registration, offline identity, storage prepare,
recovery-state(GTest 3개), image-root authority fixture도 실제
root/System::Privileged에서 내부 종료값 0으로 실행했습니다.

Build46 소스·GBS·패키지·기기 로그는
`/var/tmp/consent-artifacts/gbs-build-46`에 있습니다. Build45 실패는
원래 번호의 파일에 남깁니다. 생산 desired-definition 공급원, registry
전손 복구, 실제 holder 삭제, 외부 제품 adapter는 여전히 열린 과제입니다.

## Build47 실패와 Build48 수정 IDL 통합 시험 (2026-09-30)

Build47 Release15는 `%check` 실패 시도입니다. 새 중첩 IDL 시험은 임시
수정 뒤 양 endpoint의 compile·link까지 수행했지만, 생성 헤더에
`Apache-2.0`이라는 literal이 없다고 실패했습니다. 생성 헤더에는 전체
Apache License Version 2.0 고지가 있고, generator가 IDL SPDX metadata를
별도로 검증합니다. `LastTest-failed.log`에 이 시험 assertion 오류를
남겼습니다. Build47 RPM은 설치하지 않았고 PASS로 기록하지 않습니다.

Build48 Release16 archive SHA256은 `8039344d`로 시작합니다. 고정 소스
240개는 상관 검사 시점에 GBS export·작업 트리와 모두 같고 이후 가이드
수정은 freeze 밖입니다. GBS 종료값 0, CTest28 중 24 PASS와 root 전용
4 SKIP입니다. 수정된 증분 빌드 시험은 54.82초였습니다. 임시 소스
트리에서 tests/tools/PoC를 끄고 실제 client·daemon을 빌드한 뒤, 그
복사본에서만 유효한 IDL key 상한을 128에서 127로 바꿨습니다. 생성
헤더 변화와 verbose 로그의 실제 `client.cc`·`server.cc` compile 및
양쪽 link 명령을 확인했습니다. IDL을 복원한 빌드에서는 IDL과 생성
헤더가 기준 바이트로 정확히 돌아왔고 실제 저장소 IDL·generator hash는
불변입니다. 기존 generator 시험의 반복 결정성·invalid schema 거부·
license metadata도 통과했습니다. 이는 GBS 빌드 통합 근거입니다.
설치 RPM은 원래 IDL과 wire version을 사용합니다.

동일 Release16 RPM 5개는 emulator에서 내부 종료값 0으로 업그레이드됐고,
설치 repository 및 전체 wire fixture가 통과했습니다. root 전용
offline registration·identity·storage prepare·recovery-state(GTest 3개)·
image authority 시험도 기록된 UID0·`System::Privileged` label로
통과했습니다. 정확 unit 명령과 actor 근거는 `*-root-command.txt`와
`*-root-device.log`에 있습니다. Build46의
`*-root-proof-command.txt`·`*-root-proof-attempt2.log`도 앞선 가이드의
root 주장을 직접 뒷받침합니다. 설치 regular file은 RPM 155개 중
154개 hash가 일치하고 차이는 `%config(noreplace)` POC roles 파일뿐입니다.
ABI는 versioned symbol42개이고 GTest/GMock 요구는 `consent-tests`에만
있습니다. default-deny actor PID12244/UID0의 두 create는 음수 상태·NULL
handle이었고 생산 daemon PID11808은 같은 kernel peer를 거부했습니다.

Release16 첫 재부팅 시도는 새 `basic` 뒤 재부팅 전에 cache를 실행했습니다.
cache 시나리오 자체가 같은 package를 unregister·재등록하며 재부팅 뒤
`persistent`는 decision 불일치(종료값 1)였습니다. 이 로그만으로 원인을
분리할 수 없어 혼합 입력의 실패 시도로 보존합니다. 격리 store 세 곳을
hash와 함께 archive하고, 두 번째 시도는 사이에 cache 변경 없이 새
`basic`·`persistent`를 실행했습니다. Boot ID는
`cf0b2708-ad08-47af-9b8e-c854bfa786d9`에서
`126c815c-8314-4d86-9f09-f3f44eb0d1f8`로 바뀌고 Release16 RPM 5개가
유지됐습니다. 격리 registry·receipt SHA256/inode와 DB inode는 전후
같았습니다. 재부팅 뒤 `persistent`와 중지된 DB 무결성 검사도 통과했습니다.
같은 최종 RPM의 `stopped-delete`·`running-delete`·`db-shutdown`은 제한된
READY 검사와 함께 통과했습니다. 별도 중지된 생산 DB는 integrity `ok`,
schema2였고 socket/service 재시작은 PID4057, active, success였습니다.
이는 정상 재부팅·격리 복구 근거이며 돌연 전원 상실이나 registry 전손
복원 근거는 아닙니다.

Build48 소스·GBS·기기 로그는
`/var/tmp/consent-artifacts/gbs-build-48`에 있습니다. 생산
desired-definition 공급원, registry 전손 복구, 실제 holder 삭제, 외부
제품 adapter는 열린 과제입니다.

## Build49 동기 대기 만료와 원격 조회 (2026-09-30)

Build49 Release17은 240개 파일을 동결했습니다(archive SHA256 `30ebf7aa`).
상관 검사 시점에 GBS export·작업 트리와 모두 같았습니다. GBS 종료값 0,
CTest28 중 24 PASS와 root 전용 4 SKIP입니다. 새 `sync-timeout` phase는
설치 emulator C API 시험이며 GBS CTest 항목은 아닙니다. 같은 release의
RPM 5개는 내부 transaction 종료값 0으로 업그레이드됐습니다. 설치 regular
file 155개 중 154개 hash가 일치하고 차이는 보존된 POC
`%config(noreplace)` roles 파일뿐입니다. ABI versioned symbol은 42개,
GTest/GMock 의존성은 `consent-tests`에만 남았습니다.

이 phase는 새 subject/profile/client_request_id와 operation_id, 원격
요청 기한 30초, 로컬 동기 대기 2초를 사용했습니다. `consent_request()`는
정확히 TIMEOUT과 NULL result를 반환했습니다. 두 번째 인증된 handle은
같은 subject·profile·client_request_id로 원격 PENDING을 조회했습니다.
TIMEOUT과 새 ID의 PENDING을 함께 확인해야 원격 작업의 존재가 증명됩니다.
TIMEOUT만으로는 전송 전 만료를 배제할 수 없습니다. 잘못된 ID·subject·
profile 조회는 음수 상태와 NULL result였고, 원래 범위의 조회는 요청을
취소한 뒤 최종 CANCELLED를 반환했습니다. 서로 다른 ID로 target 4회를
실행해 모두 내부 service 종료값 0과
`PASS emulator phase=sync-timeout`을 확인했습니다. UI 응답은 넣지
않았습니다.

설치 client·I/O·owner·public API·repository 시험도 종료값 0이었습니다.
별도 wire/storage 대표 회귀의 첫 명령은 shell phase 인수 오류로 실패했고
`scenario-regression.log`에 실패 명령 시도로 보존했습니다. 수정된
`*-attempt2.log`에서는 wire·stopped-delete·running-delete·DB-shutdown이
phase PASS와 내부 종료값 0을 보였습니다. 이는 Release17 시험입니다.
Build48의 정상 재부팅 결과는 별도 이전 근거로 유지합니다.

Release17 정상 재부팅 시험에서는 기존 격리 store 세 곳의 hash를 기록하고
Build49 고유 archive 경로로 옮겼습니다. 새 `basic`으로 승인을 만든 뒤
재부팅 전 `persistent`가 통과했습니다. 기기는 직접 `sdb reboot` 명령을
지원하지 않아 그 실패 명령도 보존했습니다. 이어진 `sdb shell reboot`는
종료값 0이고 boot ID는 `126c815c-8314-4d86-9f09-f3f44eb0d1f8`에서
`cf76e1bb-8a96-4b66-a9dc-7a123c0b1313`으로 바뀌었습니다. Release17
RPM 5개가 유지됐고 격리 DB·registry·receipt의 inode와 SHA256이 전후
일치했습니다. 생산 consentd는 새 PID2564로 active/success였습니다.
SDB root mode를 다시 켜고 같은 script hash를 재전송한 뒤 `persistent`가
격리 DB integrity `ok`, schema2와 내부 종료값 0으로 통과했습니다. 이는
emulator 정상 재부팅 근거이며 돌연 전원 상실 근거는 아닙니다.

소스·GBS·RPM·기기 로그는
`/var/tmp/consent-artifacts/gbs-build-49`에 있습니다.

## Build50 시험 배치와 헤더 공백 (2026-09-30)

Release18은 시험 전용 소스를 최상위 `tests/`로 옮기고 손으로 작성한
헤더 39개의 Apache 라이선스와 guard 사이에 빈 줄 하나를 넣었습니다.
생성 프로토콜 헤더도 라이선스·생성 안내·guard를 빈 줄로 구분합니다.
일반 헤더의 그 밖의 바이트는 동일하고 생성 헤더의 공백이 아닌 모든
줄과 wire 정의도 같습니다. CMake 옵션 조합 8개가 모두 configure됐고
이전 배치와 target 이름 115개가 일치했습니다. 소스 242개를 동결했고
archive SHA256은 `1b56ee73`입니다. 상관 검사 시점에 archive·GBS
export·작업 트리가 같았습니다. GBS 종료값 0, CTest28 중 24 PASS와
명시적인 root 전용 4 SKIP입니다.

같은 Release18 RPM 5개는 x86_64 emulator에서 내부 transaction 종료값
0으로 업그레이드됐습니다. runtime·daemon·development·PoC·test RPM의
파일 목록은 Release17과 같았습니다. GTest/GMock 요구는 test RPM에만
있습니다. 설치 regular file의 RPM hash는 155개 중 154개가 일치하며
차이는 보존된 PoC `%config(noreplace)` roles 파일 하나입니다. 설치된
시험 실행 파일 8개는 service 종료값 0으로 통과했습니다.
`consent-mock-runtime-test`는 GBS CTest에서는 통과하지만 이전부터 test
RPM 설치 대상이 아닙니다. 기기 실행 시도는 실패로 따로 보존했고 이번
이동에서 새 설치 대상으로 추가하지 않았습니다.

첫 root fixture 기기 명령은 shell quoting 오류로 내부 결과 표시가
누락됐습니다. 해당 로그는 실패한 증거 수집 시도로 보존합니다.
수정된 `*-root-attempt2-command.txt`와 대응 로그에는 offline identity,
offline registration, recovery state(GTest 3개), storage prepare, image
authority의 UID0·`System::Privileged`·fixture 종료값 0·SDB 종료값 0이
기록됐습니다. 동결된 scenario script는 host와 device SHA256 일치를
확인하며 복사했습니다. 설치된 `sync-timeout`·`wire`·`cleanup-pages`는
각각 phase PASS와 내부 종료값 0을 기록했습니다.

기존 격리 state·authority·control 디렉터리의 hash를 기록한 뒤 Build50
고유 archive 경로로 옮기고 새 `basic`을 실행했습니다. 이 시험과
`persistent`·`stopped-delete`·`running-delete`·`db-shutdown`은 모두
phase PASS와 내부 종료값 0입니다. 설치 라이브러리의 versioned symbol은
42개이고 생산 consentd는 active/success였으며 격리 DB의 무결성 검사는
`ok`였습니다. 이는 Release18 설치 동작 근거이며 이전 재부팅 근거는
각각의 release에 속합니다. 로그는
`/var/tmp/consent-artifacts/gbs-build-50`에 있습니다.

<a id="guide-12-checkpoint"></a>

## 가이드 12 검증 상세

## 실제 실행 증거 (2026-09-30)

수용된 구현은 기준 `a569363` Release18 위의 미커밋 Release19다. 최종
source/build snapshot **r6**, 설치 runner 재현, strict-product negative,
cleanup이 완료 증거다. 설치 CM은 `capability-manager-0.1.0-15.x86_64`,
기기는 `emulator-26101`, x86_64다. 개발자 smoke 범위에서 구현·패키징·가용
emulator 검증은 독립 리뷰 ACCEPTED를 받았다. CM 제품 내부 adapter와 최신 CE
제품 통합은 외부 미해결 gate다.

이전 snapshot은 별개로 보존한다. 최초 GBS는 PoC SDK provisioning에서 실패,
r2는 nested IDL fixture의 cmake 복사 누락에서 실패했다. r5 기기 실행은 새 unit
load와 부분 bootstrap cleanup에서 파괴 시나리오 전에 실패했다. 수정 runner와
native-r5의 탐색 실행은 이후 통과했다. 이 증거는 수정 이력이며 아래 최종 설치
r6 결과를 대신하지 않는다.

명령·실패 시도·subprocess exit·journal·출력은 빌드 호스트의
`/var/tmp/consent-artifacts/consent-smoke-01/`에 보존한다. Source hash manifest는
`source-r6.json`, build 출력은 `consent-smoke-gbs-r6.log`다. 실제 CE API,
CM 제품 내부 AUTHORIZE, abrupt reboot, commit 중단, request-cache-hit coverage를
입증하지 않는다. Native snapshot 고정 후 최종 가이드만 수정했다.

최종 r6 일치 패키지 재실행: GBS exit0 (CTest26: PASS22, root-only SKIP4),
native/runner 파일은 `source-r6.json`과 일치했다. 동일 NVR r6 재설치는 변경
파일 충돌로 최초 exit3이었다. 같은 runtime/devel/test 패키지에 명시적
`--replacepkgs --replacefiles`를 적용한 재설치는 exit0이다. 설치 runner SHA256:
`912502e58186dd4b82e093c1fe2ab4c51b59bb76520dd415941e8acc73e67bf0`.

| 설치 r6 실행 | Seed | 내부 / 외부 exit | 증거 파일 |
| --- | --- | --- | --- |
| 전체 개발자 smoke | 20260930 | 0 / 0 | `device-r6-seed20260930.log` |
| 전체 smoke 뒤 strict product negative | 20260931 | 1 / 1 (기대값) | `device-r6-strict-seed20260931.log` |
| 최종 명시적 cleanup | — | 0 / 0 | `final-cleanup-services-r6.log` |

기본 순서는 running-delete/corrupt/stopped-delete, strict 순서는
stopped-delete/corrupt/running-delete다. 두 실행 모두 생산·PoC 상태 metadata
불변을 확인했다. 생산 consentd는 Release18, active/success, 설치·시험 전후
MainPID31569였다. PoC는 inactive/success, MainPID0을 유지했다. 최종 cleanup 뒤
smoke unit 둘 다 not-found/MainPID0이고 네 fixture 디렉터리는 모두 없었다.
Runtime/devel/tests는 Release19이며 생산 daemon RPM/service와 기존 PoC 설치는
변경하지 않았다. r6 native build를 고정한 후 예제 호출·증거만 문서에서 수정했다.

최종 설치 (`install-r6-replace.log`, INSTALL_EXIT0):

```sh
systemd-run --quiet --wait --pipe --unit=consent-smoke-install-r6-replace \
  -p SmackProcessLabel=System::Privileged rpm -Uvh \
  --replacepkgs --replacefiles /tmp/consent-smoke-runtime.rpm \
  /tmp/consent-smoke-devel.rpm /tmp/consent-smoke-tests.rpm
```

최종 명령은 각각 `sdb -s emulator-26101 shell`을 통해 실행했다.

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

두 전체 실행 사이에도 명시적 cleanup을 했다. SDB transport는 기대된 strict
실패에서도 exit0이므로 내부/외부 marker의 exit1을 기준으로 판정한다.
r6 RPM 경로는 `/path/to/GBS-ROOT/local/repos/tizen_10_1_emulator/x86_64/RPMS/`다.
`consent`, `consent-devel`, `consent-tests`, 빌드만 한 `consentd` 모두
`0.1.0-19.x86_64.rpm`이다. 증거 디렉터리는 설치한 세 RPM과 SHA256을 r5/r6로
구분하여 보존한다.

<a id="guide-13-checkpoint"></a>

## 가이드 13 검증 상세

## 검증된 Release21 snapshot (2026-09-30)

CONSENT-INTEGRATION-02 구현 r4는 baseline `a569363`와 개발자 예제 변경입니다.
`source-r4.json`의 native/build/runner21개 파일은 실제 실행·설치한 Release21
snapshot과 일치합니다. 게시 시 runner의 trailing space 세 곳만 제거했으며 Python
AST는 동일하지만 게시된 byte hash는 설치 r4와 다릅니다. 원래 로그/RPM을 유지하고
외부 `publication-whitespace.json`에 old/new hash를 기록했습니다. 이 mechanical
게시 수정으로 package를 다시 빌드하지 않았습니다. 검증 후 문서에 증거를 추가합니다.
증거는 `/var/tmp/consent-artifacts/consent-integration-02/`에 보존합니다.

| 증거 | 실제 결과 |
| --- | --- |
| `gbs-r4.log`, `gbs-r4.exit` | 위 exact build 명령; exit0, CTest27개 중23 PASS +4 root-only SKIP |
| `rpms-r4/`, `rpm-r4-sha256.json` | Release21 RPM 보관 및 matching source manifest |
| `install-r4.log`, `install-r4.exit` | 정상 upgrade exit0, runtime/devel/tests만 설치 |
| `installed-r4-hash.log` | 설치 smoke payload17개 hash가 보관 RPM과 일치; runner도 실행한 source-r4와 일치 |
| `installed-tools-seed20261002-r4.log` | SMOKE_EXIT0, TOOLS_OUTER_EXIT0; 모든 도구 시나리오 및3개 복구 PASS |
| `installed-strict-seed20261003-r4.log` | 도구 시나리오 전부 PASS 후 명시적 product gate; SMOKE_EXIT1, STRICT_OUTER_EXIT1 |
| `installed-default-seed20261004-r4.log` | 기본 smoke01 회귀 SMOKE_EXIT0, DEFAULT_OUTER_EXIT0 |
| `final-cleanup-r4.log`, `final-services-r4.log` | 최종 cleanup0; smoke unit2개 not-found, fixture directory4개 없음 |
| `device-before-r2.log`, `final-services-r4.log` | 제품 PID31569 active/success; PoC PID0 inactive/success; 전체 상태 metadata fingerprint 동일 |

`results-r4.json`은 exit와 recovery case를 요약하고 `commands.txt`는 모든 exact
host/device 명령을 기록합니다. SDB host는 strict 원격 실패에서도0이므로 원격
SMOKE_EXIT와 OUTER marker로 판정합니다. 발견한 emulator는 `emulator-26101`,
architecture x86_64, profile `tizen_10_1_emulator`입니다. 설치된
`consent`, `consent-devel`, `consent-tests`는0.1.0-21이고 제품 `consentd` 및
`consent-poc`는0.1.0-18을 유지합니다. GBS가 만든 제품 daemon RPM은 보관만 하고
설치하지 않았습니다. CM은0.1.0-15, JSON-GLib는1.8.0입니다.
패키지 의존성 audit에서 JSON-GLib는 tests에만 존재합니다.

선택한 emulator에서 사용한 최종 설치/실행 명령:

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

실행 순서/옵션은 `--tools --seed 20261002` → `--cleanup` →
`--tools --require-product --seed 20261003` → `--cleanup` →
`--seed 20261004` → `--cleanup`입니다. 보존된 명령에는 device shell 종료 전
원격 outer exit 출력 및 assertion도 있습니다.

실제 parser가 `cli:smoke-tool`을 게시하고 owner/executable을 검증했습니다.
Public CM preflight는 `product_public_api/BLOCKED create_status=-2
handle=null`, PREFLIGHT_EXIT3입니다. stderr-only 및 nonzero exit7 native reply는
성공하며 유효 native error는 `native_error`, timeout/malformed/conflict/NUL은
`unknown`입니다. 각 retry는 count1로 deduplicate됩니다. CE level1–3 각각 first는
정확히 `state=succeeded count=1`, retry는
`deduplicated state=succeeded count=1`입니다. 두 CM/CE tool actor는 stopped-delete,
corrupt, running-delete에서 fresh 승인 후 실제 실행/조회를 수행했고, 세 번의
정의/grant/epoch readback을 기록했습니다. Strict도 이를 모두 통과한 후 product
integration 부재만을 이유로 기대한 실패를 반환했습니다.

실패/수정도 보존합니다. `installed-tools-seed20261002-r2.log`는 CElevel2에서
다른 level과 `context-once` immutable tuple을 재사용해 원격1로 실패했고,
`cleanup-failed-r2.log`는0입니다. R3에서 level별 operation namespace를 구분했으며
daemon policy를 바꾸지 않았습니다. `gbs-r1/r2/r3.log`는 모두 성공했습니다.
`install-r3.log`의 원격3은 Tizen MSM이 같은 Release20의 변경된 runner를
`--replacepkgs`에서도 거부한 결과입니다. Release21 정상 upgrade로 force-file
replacement 없이 해결했습니다. 너무 긴 SDB hash 명령의 service-name-too-long은
`installed-r4-hash-command-size-failure.log`에 보존하고, 짧은 read-only 수집으로
17개 파일 hash를 검증했습니다. 설치 ldconfig permission warning도 install log에
남겨 두고 transaction exit와 설치 hash를 확인했습니다.
기존 smoke01/r6 증거를 유지했으며 commit/push는 하지 않았습니다.

<a id="guide-14-checkpoint"></a>

## 가이드 14 검증 상세

## 검증한 Release23 snapshot (2026-09-30)

CONSENT-MOCK-INTEGRATION-04 최종 구현 r5는 baseline8f0c441과 검토한 변경을
사용합니다. 증거 경로:
`/var/tmp/consent-artifacts/consent-mock-integration-04/`.
source-r5.json은 실행·패키징한15개 소스 경로를 기록합니다. 게시 전11개 구현
hash는 일치했습니다(implementation-r5-postcheck.json). 게시 시 CMake 한 줄만
줄바꿈하여 native/runner10개 hash는 동일하고 CMake command 인자 token도
동일하지만 해당 파일 byte hash는 바뀝니다. 외부 publication-format.json에
old/new hash를 기록하며 이 formatting 차이에 RPM 재빌드·target 재실행은
없었습니다. 이 가이드 쌍의 최종 증거와
동시 입력 설명은 빌드 후 추가했습니다. 보존 RPM에는 이전 가이드 초안이 있으며
이후의 검증 문구가 포함됐다고 주장하지 않습니다.

consent 저장소의 정확한 빌드 명령:

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

gbs-r5.log/.exit0, CTest27 =23 PASS +root-only4 SKIP, 실패0,73.41초입니다.
rpms-r5/와 rpms-r5.json에 Release23 RPM/hash를 보존합니다. 운영 daemon RPM도
생성됐지만 설치하지 않았습니다. 발견한 emulator-26101 x86_64에서 검증된
System::Privileged transaction으로 consent/consent-devel/consent-tests23만 정상
업그레이드했습니다(install-r5.log INSTALL_EXIT0). ldconfig 권한 진단은 그대로
보존했습니다. installed-hash-r5.log의19개 설치 smoke payload hash가 tests RPM과
일치하며 현재 runner와 두 서비스 executable을 포함합니다.

개별 명령은 commands.jsonl에 기록하며 target 실행 형태는 다음과 같습니다.

```sh
systemd-run --quiet --wait --pipe -p User=root -p SmackProcessLabel=System \
  /usr/bin/python3 /usr/libexec/consent/smoke/emulator-smoke.py \
  --mock-services --seed 20261005
```

| 설치본 시나리오/log | 실제 remote 결과 |
| --- | --- |
| installed-mock-seed20261005-r5.log | SMOKE_EXIT0, MOCK_OUTER_EXIT0 |
| installed-tools-seed20261006-r5.log | SMOKE_EXIT0, OUTER_EXIT0 |
| installed-default-seed20261007-r5.log | SMOKE_EXIT0, OUTER_EXIT0 |
| cleanup-after-mock-r5.log | SMOKE_EXIT0, CLEANUP_EXIT0 |
| cleanup-after-tools-r5.log | SMOKE_EXIT0, CLEANUP_EXIT0 |
| cleanup-final-r5.log | SMOKE_EXIT0, CLEANUP_EXIT0 |

SDB hostexit0만으로 remote 성공을 판단하지 않고 runner/outer marker를 모두
검사했습니다. mock은 fixture catalog를 사용해 제품 create/parser에 의존하지
않았습니다. 필수 기존 tools 회귀에서는 실제 설치 parser/catalog와 예상된
public preflight 차단을 검증했습니다. 이 checkpoint에서 선택적 actual mock
catalog나 strict-product target 실행을 했다고 주장하지 않습니다.

mock 증거는 CM8/CE4 role별 discover, 실제 권한 오류, 잘못된 params/byte frame,
효과 없는 QUERY, 승인된 CM payload와 CE0..3, 다른 RPC id의 dedup, 불변 tuple
충돌, 알 수 없는 이전 receipt 차단, 철회된 receipt STALE(-116), 새 operation의
CONSENT_REQUIRED, 실제 argo/UI DENIED callback 뒤 admissions1 불변 상태의
CONSENT_REQUIRED, native_error/timeout 결과 재사용과 추가 admission 억제입니다.
복구 순서는 running-delete/corrupt/stopped-delete이며 mock PID113749/113753을
유지했습니다. 매번 새 승인 전 integrity=ok/schema2/definitions12/grants0/
cleanup_unknown1을 확인하고 새 승인 후 실제 fixture 효과를 실행했습니다.
daemon stop 후 옛 handle 거부와 명시 재생성을 확인하며 running-delete는 원래
handle을 사용합니다. registry loss는 시작에 실패하고 두 서비스 실행을 차단합니다.

초기 gbs-r1/r2 성공 기록을 보존했습니다. gbs-r3.exit1은 owner가 이전 GBS 정리
도중 새 빌드를 시작한 순서 오류이며 사용 중인 mounted root를 안전하게 거부한
결과입니다. 수동 unmount 우회는 없고 이후 빌드는 순차 실행했습니다.
gbs-r4.exit0 및 installed-mock-seed20261005-r4 remote1도 보존했습니다. 실제
DENIED callback은 정확했고 runner의 durable DENIED 기대가 잘못됐습니다.
cleanup-failed-r4.log는0입니다. 최종 assertion 수정과 정상 Release23 업그레이드로
daemon 계약을 유지했습니다.

device-before-r5.log/device-after.log의 서비스/package 및 보호 상태 inode/size/
mtime/uid/mode fingerprint가 완전히 같습니다. 운영 consentd18 PID31569 active,
PoC18 PID0 inactive, 제품 CM15 설치 상태가 유지됐습니다. 최종 smoke unit 두 개는
not-found이며 fixture 네 경로가 없습니다. 기존 smoke01 r6, Integration02 r4와
CM prerequisite 증거는 변경하지 않았습니다. results-r5.json에 결과를 요약합니다.
제품 CM principal/provisioning/transport/consent enforcement와 최신 CE server/
taxonomy는 외부 gate로 남습니다.

<a id="guide-17-checkpoint"></a>

## 가이드 17 검증 상세

### 실행된 r14 checkpoint (2026-10-01)

증거는 `/var/tmp/consent-artifacts/consent-ui-smoke-10`에 보존합니다. 실제 개발 빌드는
다음 명령입니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all
```

`gbs-r14.log`와 `gbs-r14.exit`는 exit 0 / Done, CTest 34개 중 30 PASS와
root-only 4 SKIP를 기록합니다. managed binding/review/period-choice/worker lifetime
검사도 통과했습니다. `host-tests-r14.log`는 호스트 검사 58개 PASS를 기록합니다.
`rpms-r14/`에는 source/debug를 포함한 RPM 11개를 보존하며,
`build-audit-r14.json`에 RPM SHA256과 frozen 파일 21개의 current source /
`source-export-r14.tar.gz` 정확한 일치를 기록합니다.

선택한 target은 실제 실행 중인 SDK emulator와 같은 PID의 SDB serial log로 확인한
`emulator-26101`, x86_64입니다. 다음은 경로를 이식 가능한 자리표시자로 바꾼 예입니다. 원래 명령은 외부 로그에
보존하며 runner 기본값으로 쓰지 않습니다.

```sh
BASE=/var/tmp/consent-artifacts/consent-ui-smoke-10
AURUM=/path/to/aurum-ui
CACHE=/path/to/existing/aurum-cache
python3 scripts/consent-ui-smoke.py \
  --build-dir "$BASE/rpms-r14" --serial emulator-26101 \
  --aurum-cli "$AURUM" --aurum-cache "$CACHE" --port 55059 \
  --scenario all --seed 20261015 --output "$BASE/attempt-r14-02"
```

| Output | 시나리오 / seed | 실제 결과 |
| --- | --- | --- |
| `attempt-r14-01` | generation / 20261014 | exit 0 |
| `attempt-r14-02` | all / 20261015 | exit 0, 4개 subrun 모두 PASS |
| `attempt-r14-03` | functional / 20261016 | exit 0, focused repeat PASS |

generation-only와 repeat 명령은 위와 동일한 입력에서 표에 따라 `--scenario`,
`--seed`, `--output`만 변경합니다. 각 명령 옆의 `.log`/`.exit`,
attempt-level `result.json`/`proof.json`과 phase별 `scenario.json`/`finally.json`에
실제 결과를 보존합니다.

두 functional 실행 모두 기본 ONCE의 실제 CE count 1, ON/OFF 각각의 reset probe를
포함한 무효과 거절, checked 승인 count 2, 추가 consent popup 없는 새 operation의
persistent 재사용 count 3, 철회 후 거절 count 3 유지, checkbox 비활성화된 ONCE-only
CM count 4를 증명했습니다. 각 functional phase에 실제 UI와 CM/CE worker의 mapped
device/inode/SHA 증거를 보존합니다. checkbox는 설치된 NUI theme의 orange control이며
Samsung runtime theme 적용을 주장하지 않습니다.

all-run restart readback은 PERSISTENT grant 1개를 유지했고 추가 승인 없이 새 실제
CE operation이 성공했습니다. DB 삭제 readback은 integrity `ok`, schema 2,
definition 2개, grant 0, `cleanup_unknown=1`이며, saved operation은 consent required로
차단된 뒤 fresh 실제 UI 승인과 실행이 이뤄졌습니다. owned helper generation 변경도
fresh consent를 요구하고 saved old operation을 정확한 STALE -116으로 거부한 후
새 UI 승인 CE operation/receipt를 생성했습니다. gate-only receipt와 worker 실행
receipt는 별도로 기록합니다. old process는 stop/drain하고 새 authenticated handle을
만들었으므로 same-handle recovery 또는 TPK 재설치 증거가 아닙니다.

성공한 transaction 6개(generation-only, all-run 4 phase, functional repeat)는 모두
finally errors가 비어 있고 owned cleanup exit 0이며 원래 `package_files`,
`protected_trees`, `units`의 before/after가 정확히 같습니다. production
`consentd.service`는 PID 31569 active, 원래 PoC service 2개는 PID 0 inactive를
유지했습니다. global RPM/library/policy를 설치하지 않았습니다. 원래 TPK 복원과
허용된 app inode/time 변경은 별도로 검증했고 runtime parent의 보호된 identity,
mode/label은 유지하며 예상 timestamp 변경을 기록했습니다. state/authority/endpoint와
unit 정리 후 protected inventory/FD 삭제로 owned payload도 제거했습니다.

실행 package는 이후 evidence prose가 아니라 frozen `source-r14.json`과 일치합니다.
출판 소스는 16개 파일이 byte-identical입니다. Python 3개 파일에는 승인된 출판
formatting만 적용했습니다. `ui.py`의 trailing space 1개 제거, `aurum_tree.py`의
comprehension 1개 wrap, `emulator-ui-native.py`의 3줄 wrap입니다.
`publication-format-r14.json`에 실행/current SHA256, 동일한 AST와 실행 bytes를
정확히 복원하는 reversal proof를 기록합니다. 이 Guide 17 한·영 pair가 나머지
2개 later evidence-prose 변경입니다. 출판 수정 때문에 빌드나 기기 실행을
반복하지 않았습니다. 이전 UI09 수동 검증은 repository runner 결과와 별도로
유지합니다.

### 보존된 실패와 한계

모든 이전 command output과 복원 증거는 최종 checkpoint 옆에 보존하며 나중에 PASS로
바꾸지 않습니다.

| Attempt | 보존된 결과 / 수정 |
| --- | --- |
| r1-01 | generated audit mode 누락과 읽기로 바뀐 atime 검사, 정확한 owned FD 복구 cleanup 0 보존 |
| r3-01 | 알려진 Aurum bootstrap 진단 prefix 때문에 JSON 처리 실패 |
| r4-01 / r5-01 | 빈 startup tree, 실제 settings pixels 보존 후 source-backed registered-root 경로 확인 |
| r7-01 | 정상 active taskbar 차단, 검증된 platform identity/control-center 충돌 검사 추가 |
| r8-01 / r8-02 | dump package 누락 거부, 무입력 제한 진단으로 exact-ID own package omission 증명 |
| r9-01 | 실제 보관 문구의 “최대”가 검사에서 누락, Allow 미전송 |
| r10-01 | 첫 실제 CE 실행 후 colored journal JSON 거부 |
| r11-01 | timing 검토를 위한 owned host 중단, runtime 실패나 PASS 아님 |
| r12-01 | OFF probe의 terminal DENIED 미관측, 원인 미확인 |
| r13-01 | 마지막 generation에서 첫 관측까지 Next page 미전환, 전체 all-run 실패 |

post-Deny 진단과 bounded single-input page wait는 증거와 semantic 관측을 보완한
것이며 native refresh/input race 수정의 증명이 아닙니다. matching full run과 focused
functional repeat는 통과했지만 r12 미확인 실패는 재현성 한계로 남깁니다. 알 수 없는
UI/collector 오류는 PENDING 수용, 재클릭, 검토 skip 대신 계속 실패 처리합니다.

실제 consent IPC/native UI 위에서 동작하는 isolated 개발자용 mock CM/CE 예제입니다.
제품 CM/CE adapter, profile provisioning/privilege, 신뢰된 deployment UI/argo role,
실제 holder의 물리적 정리는 외부 통합 작업입니다. Installer hook은 기존 publisher/
generation 계약 아래 각 plugin의 책임입니다.

<a id="guide-15-checkpoint"></a>

## 가이드 15 검증 상세

실제 build 명령은 다음과 같습니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

증거는 `/var/tmp/consent-artifacts/consent-profile-05/`에 보존합니다.
`source-r8.json`은 실행된 Release25 snapshot이며 target 검증 전 29개 파일이 모두
일치했습니다(`source-r8-postcheck.json`). 이 문서 쌍의 최종 증거 문장은 이후 추가한
것이며 나머지 implementation/build/test/runner/navigation 27개는 실행 hash를
유지합니다. RPM·로그는 그대로 보존하고 설치 패키지 문서는 이전 draft입니다.
문서만을 위한 rebuild를 주장하지 않습니다.

| 증거 파일 | 실제 결과 |
| --- | --- |
| `gbs-r8.log`, `gbs-r8.exit` | exit0; CTest 25 PASS + root-only 4 SKIP |
| `rpms-r8/`, `rpms-r8.json` | matching Release25 runtime/devel/tests와 전체 build 산출물 보존 |
| `install-r8.log` | remote INSTALL_EXIT:0; runtime/devel/tests만 upgrade |
| `installed-r8-hash.log` | 설치 payload 25 hash가 RPM과 일치, HASH_EXIT:0 |
| `installed-profiles-seed20261010-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0 |
| `installed-strict-seed20261011-r8.log` | 모든 시나리오 PASS 뒤 product profile provisioning/privilege 미검증만으로 SMOKE_EXIT1 / OUTER_EXIT:1 |
| `installed-mock-seed20261012-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0 |
| `installed-tools-seed20261013-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0, 실제 설치 CM parser/catalog 수행 |
| `installed-default-seed20261014-r8.log` | SMOKE_EXIT0 / OUTER_EXIT:0 |
| `cleanup-final-r8.log` | SMOKE_EXIT0 / CLEANUP_EXIT:0 |
| `device-before-r7.log`, `device-before-r8.log`, `device-after-r8.log` | 전체 production/PoC fingerprint 일치; production daemon18 active PID31569, PoC18 inactive PID0; smoke unit2 not-found, fixture directory4 absent |
| `native-readonly-preflight-r8.log` | security_fw/System, 명시적 synthetic 설정 UID1: library file observation0, native NameHasNoOwner/BLOCKED3 |
| `verification-r8.json`, `commands.jsonl` | 종합 결과와 실제 device command argv |

profile 시나리오는 warm A 동기·비동기 request source=DAEMON, 누락/unknown/
undelegated/inactive 거부, terminal INVALIDATED callback 정확히 1회, late UI 거부,
A→B→A에서 철회하지 않은 CE receipt 무효화와 새 A persistent 승인 재사용, old session
미복원, owner gap 승인 철회, 세 DB loss의 integrity/schema2/definitions12/grants0/
cleanup_unknown1 검사와 fresh approval/payload effect, 같은 CM/CE service PID 및
명시적 새 handle, total registry loss 안전 차단을 검증합니다. 권한 검사/API 오류는
effect가 없으며 process-local mock receipt가 현재 AUTHORIZE를 대체하지 않습니다.
private-bus native 테스트는 실제 adapter의 sender/owner/init/stale 성공 reply/removal/
ACK timeout/shutdown을 검증합니다. socketpair는 실제 blocked/partial sensitive
응답이 fence 후 완료되지 않음을 검증합니다.

실패 이력도 보존합니다. r1 compiler signedness 실패, r2/r3 build 성공, r4 compiler
dangling-else fixture 실패, r5/r6 잘못된 test envelope 실패, r7 build/profile0/strict1
성공 뒤 legacy mock helper 변수 NameError1 및 owned cleanup0입니다. r8은 기존 동적
definition-count assertion을 복원하고 Release25로 정상 upgrade합니다. protocol bound,
role policy, production fatal 동작을 완화하지 않았습니다.

native product activation은 미검증입니다. 이 emulator에는 `org.tizen.sessiond` owner가
없지만 library 파일 getter는0을 반환했습니다. 파일 getter가 ready 근거가 아님을
보여줍니다. UID1은 synthetic fixture 설정이며 발견·provision된 product account
mapping이 아닙니다. 제품 mapping/privilege, 실제 native switch/provider 조율, 실제 CM
authorization adapter와 최신 CE server 통합은 외부 gate입니다. 실제 account switch,
production daemon 설치나 policy 변경은 수행하지 않았습니다.
기존 mock 경계는 [가이드 14](../guides/14-mock-services.ko.md)를 참고하십시오.

repository cleanup 테스트는 소유 session/receipt로 A artifact를 등록한 뒤 B로
전환하여 A data use를 거부하고, inactive A의 인증된 cleanup list/ACK를 허용하는지
확인합니다. 일치하는 ACK로 CLOSING에서 CLOSED로 진행하며 A 복귀로 session을
되살리지 않습니다. 이는 제어 metadata 증거이며 물리 데이터 삭제 증거가 아닙니다.

<a id="guide-8-checkpoint"></a>

## 가이드 8 검증 상세

### UI09 실행 r7 증거 (2026-09-30)

증거는 `/var/tmp/consent-artifacts/consent-ui-native-09/`에 보존했습니다.
실행한 34개 파일의 `source-r7.json`은 frozen/exported build 입력과 일치합니다.
최종 Guide08/10 한·영 증거 문단은 실행 후 작성했으며 나머지30개 파일은 byte가
동일합니다. 이후 문서 작성에 대해 다시 build했다고 주장하지 않습니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all
```

`gbs-r7-command.json`, `gbs-r7.log`, `gbs-r7.exit`은 exit0과 CTest33개
(29 PASS, root 전용4 SKIP)를 기록합니다. `rpms-r7/`에 Release27 runtime,
devel, daemon, PoC, tests RPM을 보존했습니다. 이번 실행은 전역 RPM 설치가
아닙니다. 일치하는 tests helper를 보호된 소유 fixture에 추출하고 동일 서명 main
TPK의 library byte를 UI와 private unit bind에 사용했습니다. 기존 설치본은
runtime/devel/tests26, daemon/PoC18입니다. 기존 main TPK를 backup하고
인증서 신원 일치를 확인한 뒤 복원했습니다.

`host-provenance-r7.json`, `source-postcheck-r7.json`,
`tpk-native-audit-r7.json`에 입력·ELF 증거가 있습니다. 서명 TPK library는
전체 native TARGET_FILE hash와 정확히 일치합니다. RPM-packaged library는 byte hash와 GNU build ID가 다르며 검사한 네 section
`.text`, `.rodata`, `.dynsym`, `.gnu.version`이 일치합니다. Postprocessing 설명은
확인한 원인이 아니라 추론입니다.
동일 build ID나 RPM byte 일치를 주장하지 않습니다.
`attempt-r7-02/interactive/65-result.json`의 실제 `/proc` maps는 UI와 CM/CE
worker 모두 staged device/inode/hash가 일치함을 증명합니다. daemon 실행 파일의
provenance는 별도로 기록했습니다.

증거 디렉터리에서 실행한 host 명령은 `python3 interactive_r7_02.py`,
`python3 interactive_r7_06.py`, `python3 interactive_r7_09.py`입니다.
명시적으로 선택한 emulator-26101의 x86_64를 확인했습니다. 각 controller는
Aurum 시작·forward·종료, 고정 unit dispatch와 TPK 복원을 finally로 소유합니다.
팝업60초와 action window600초 제한은 유지했습니다.

- `attempt-r7-02`: exit0. 실제 기본 해제 ONCE 승인으로 CE count0→1,
  거절 후1 유지, 체크·전체 검토 PERSISTENT 승인으로2, 동일 전체 tuple의 새
  operation은 추가 승인 팝업 없이 fresh CE receipt로3입니다. 철회 후 거절은
  3을 유지합니다. 신뢰된 ONCE-only device 정책은 체크박스를 비활성화하며
  실제 CM action으로4가 됩니다. Screenshot과 `actions-journal-final.log`는
  선택된 기간과 변경되지 않은 base ONCE를 구분합니다.
- `attempt-r7-06`: outer1이며 일부 lifecycle 증거를 보존했습니다. 정상 restart는
  PERSISTENT1을 유지하고 fresh receipt를 반환합니다. 소유 stopped DB 삭제 후
  schema2/definitions2/grants0/cleanup_unknown1이며 실제 새 UI 승인으로 CE가
  실행됩니다. generation retirement 후 새 operation은 REQUIRED, 저장한 이전 operation retry는
  STALE(-116)입니다. 이 시도에서 최종 generation 후 새 UI 성공은 증명하지 못했습니다.
- `attempt-r7-09`: host exit0으로 generation 후 실제 성공을 완료했습니다.
  소유 installation-authority helper가 generation을 변경하고 인증된 등록은
  definition2개를 반환합니다. 새 operation은 승인이 필요하고 저장한 이전
  operation은 STALE(-116)입니다. 실제 기본 해제 팝업→체크→전체 검토→승인 후
  CE operation `feature-7f244d44-...`가 새 effect receipt `3334dd94...`,
  새 coordinator epoch의 count1/retry0을 기록합니다. 별도 공개 gate receipt
  `5737...`는 권한 검사만 증명합니다. 정확한 값은
  `generation-positive-summary.json`에 있습니다. 이는 소유 helper generation
  변경이며 **TPK 재설치 lifecycle 시험이 아닙니다**.

완료한 mutation 시도의 finally·복원 로그를 보존했습니다. 성공02/09와 일부06의
finally는 `errors=[]`이며 기존 package file·보호된 운영/PoC tree·unit이 정확히
일치합니다. Runtime parent의 보호 metadata는 동일하고 예상 timestamp 변경은
별도 기록했습니다. TPK 교체·복원의 inode/time 변경도 명시했습니다.
Production18 PID31569는 active, 기존 PoC는 inactive를 유지했습니다. 소유 unit,
endpoint, state/authority와 유한 payload를 제거하고 Aurum bootstrap·forward를
종료했습니다. 전역 graphics·정책·account는 변경하지 않았습니다.

이전 실패도 유지합니다: r1 SDK 입력, r2 private fixture 호출, r3 동일 버전 정책
fixture, r5 unit parent 보호 검사, r6 초기 lifecycle lock 부재, r7-01 capture 경로,
r7-03 legacy check 금지 필드, r7-04 JSONL artifact parser, r7-05 retained-root
preflight, r7-06 timeout/black capture, r7-07 queue method, r7-08 gate boolean 누락.
생성된 artifact Python cache 정리는 실행 helper hash와 이후 미실행 ROOT-FD
검사를 구분했습니다. 이를 위해 native 검증을 완화하지 않았습니다. CM/CE는 실제
격리 consent API를 사용하는 synthetic provider이며 제품 adapter 검증이 아닙니다.

<a id="guide-16-checkpoint"></a>

## 가이드 16 검증 상세

근거: `/var/tmp/consent-artifacts/consent-style-06/`.
순차 실행한 `gbs-r1.log/.exit`와 최종 `gbs-r2.log/.exit`는 모두 exit0,
31개 테스트 중 27 PASS·문서화된 root 전용 4 SKIP입니다. 문자열 오류,
spawn 이후 kill/reap과 실제 private bus barrier 예외 회귀가 통과했습니다.
마지막 검증은 제품 어댑터의 private 테스트 bus 경로이며 실제 sessiond
사용자 전환은 하지 않았습니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator \
  --include-all --define '_without_poc 1'
```

`source-r2.json`, `export-r2.json`, `inputs-r2/`는 실행한 25개 파일과
export 입력을 보존합니다. `rpms-r2/`의 생성 RPM 9개 해시는
`rpms-r2.json`에 있습니다. 설치는 `consent-0.1.0-26.x86_64.rpm`,
`consent-devel-0.1.0-26.x86_64.rpm`, `consent-tests-0.1.0-26.x86_64.rpm`
세 개만 선택했고 제품 daemon/PoC RPM은 제외했습니다.
`rpm-dependencies-r2.log`는 의존성 검사를 보존합니다. libsessiond·dbus·
JSON-GLib은 테스트 패키지 의존성이며 새 제품 의존성으로 추가하지 않았습니다.

발견한 `emulator-26101`, `x86_64`에서 `install-r2.log`는 INSTALL_EXIT0입니다.
`installed-payload-r2.log`는 설치된 일반 파일 162개를 archive RPM과
비교해 VERIFY_EXIT0을 기록합니다. 설치된 설정·프로세스 네이티브 회귀도
`native-config-r2.log`, `native-process-r2.log`에서 각각 NATIVE_EXIT0입니다.

| 설치 runner 모드 | Seed | SMOKE_EXIT / OUTER_EXIT | 근거 로그 |
| --- | --- | --- | --- |
| profiles | 20261020 | 0 / 0 | installed-profiles-seed20261020-r2.log |
| profiles --require-product | 20261021 | 1 / 1, 예상 결과 | installed-strict-seed20261021-r2.log |
| mock-services | 20261022 | 0 / 0 | installed-mock-seed20261022-r2.log |
| tools | 20261023 | 0 / 0 | installed-tools-seed20261023-r2.log |
| default | 20261024 | 0 / 0 | installed-default-seed20261024-r2.log |

정확한 명령은 `commands.jsonl`, host driver는 `target-owner.py`입니다.
runner는 `systemd-run --quiet --wait --pipe`, `User=root`,
`SmackProcessLabel=System`과
`/usr/bin/python3 /usr/libexec/consent/smoke/emulator-smoke.py`에 표의
모드·seed 인자를 전달했습니다. strict는 프로필 시나리오를 완료한 뒤 오직
`product profile provisioning/privilege unverified`로 실패했습니다.
예상한 registry 전체 손실 시 서비스 start 실패는 보호 차단이며 시나리오 실패가
아닙니다. 두 프로필 실행은 begin/reconnect/new async/new pending과 admission
불변을 확인합니다. 이전 콜백은 Pending 정리 이전에 detach됩니다.

모든 중간 cleanup과 `cleanup-final-r2.log`는 cleanup0입니다.
`device-before-r2.log`, `device-after-r2.log`의 전체 제품·PoC metadata 지문은
정확히 같습니다. 제품 daemon18은 PID31569로 active, PoC18은 PID0으로
inactive, CM15도 그대로입니다. 최종 smoke unit 두 개는 not-found이고
fixture 디렉터리 네 개는 absent입니다. 제품 활성화·정책 변경·실제 계정 전환·
외부 참조 저장소 변경은 하지 않았습니다.

실행 소스와 publication 구분: r2 export 이후 `repository.cc`의 인접 표준
`<type_traits>`/`<utility>` include 순서만 바뀌었습니다. 역교환하면 실행 해시가
정확히 재현되며 두 해시와 검증은 `publication-include-order.json`에 있습니다.
당시 나머지 24개 파일은 byte 일치했습니다. 최종 publication은 이 한·영
가이드의 나중 근거 서술도 포함합니다. 따라서 나머지 22개 파일은 byte 일치,
한 개는 승인된 include 순서 변경, 두 개는 나중 서술입니다. 이 publication
차이에 대한 RPM 재빌드나 설치본과의 해시 일치를 주장하지 않습니다.
이전 r1 소스·RPM 근거도 보존했습니다.

<a id="guide-8-settings-history"></a>

## 과거 build26 검증

고정26에서 GBS 컴파일/managed 시험, 실제 TPK 설치와 소켓 신원 검사가 통과했습니다.
실제 한영 페이지 검토·언어 변경 초기화·허용/거절·긴 연속 문자열·취소·시간 초과·
Back을 검증했고 허용 뒤 QUERY/ONCE 승인·재시도 및 holder 정리가 완료됐습니다.
Hash, 화면, 정확한 근거와24/25 실패 구분은 [Guide 07](../guides/07-verification.ko.md)을
참고합니다. 검증 후 PoC daemon/socket과 actor는 중지하고 명시 PoC state/roles와
설치 TPK는 검토용으로 보존했습니다. Common Emulator PoC이며 제품 role/UI/Installer
연동이나 실제 TV 수용 완료 주장은 아닙니다.

## 기능 설정 연동

최초 DEFAULT app-control은 선택을 변경하지 않고 설정을 엽니다.
`consent-poc-launch org.tizen.consentui --settings ko-KR`가 이 경로이며 VIEW는
기존 특정 승인 요청을 엽니다. launch 인자로 기능 선택·저장·실행을 하지 않습니다.
선택, 부족분 승인, 실행 계약은 [Guide 10](../guides/10-feature-approval.ko.md)을 참고합니다.

별도 `libconsent-feature-poc.so.0`는 `consent-feature-poc.socket` 및
`consent-feature-poc.service`가 활성화하는
`/opt/var/lib/consent-feature-runtime/argo.sock`에만 연결합니다. UI는 root 보호
경로, PID1/UID0, kernel 원래 bind 주소, inode, `System::Privileged` listener
label을 검증합니다. coordinator는 실제 UI UID, 보호된 loader, 정확한 package
label을 검증합니다. 선택 target의 앱은 root coordinator process identity를
읽을 수 없으므로 cross-UID executable 조회나 UI capability 추가를 주장하지
않습니다. 전용 runtime leaf만 root:root 0755/SMACK `_`로 준비하며 socket은
root:users 0660입니다.

argo actor가 immutable catalog, 선택 CAS, coordinator epoch를 소유합니다.
저장한 SESSION 또는 30분 선택은 설정을 닫아도 대화/선택 기간 안에서 유지됩니다.
명시적인 이번 작업만 ONCE 선택은 저장 설정과 별개입니다. 모든 변경은 표시한
catalog hash, epoch, expected revision, stable command ID를 전달합니다. 응답이
불확실하면 immutable submission을 보존하여 같은 명령 재시도를 명시적으로
제공합니다. coordinator 재시작 뒤에는 과거 명령을 거부하고 새로 확인한 선택을
요구합니다. 모든 조건이 통합 prompt 예산에 들어가야 하며 일부 승인이나 자동
분할을 하지 않습니다.

기존 PoC 신원·generation 설정 뒤 새 host artifact 디렉터리와 확인된 emulator
serial을 사용합니다.

```sh
python3 scripts/emulator-feature-flow.py --serial "$CONSENT_SERIAL" \
  --artifact-dir /var/tmp/consent-feature-run start --locale ko-KR
# 실제 앱에서 설정과 승인 페이지를 확인하고 조작합니다.
python3 scripts/emulator-feature-flow.py --serial "$CONSENT_SERIAL" \
  --artifact-dir /var/tmp/consent-feature-run collect
# 명시적인 대화 종료 작업을 선택하고 holder 정리부터 확인합니다.
python3 scripts/emulator-feature-flow.py --serial "$CONSENT_SERIAL" \
  --artifact-dir /var/tmp/consent-feature-run stop
```

Driver는 대응 보호 준비 script를 복사하여 root/System::Privileged 문맥에서
실행합니다. mock CM/CE의 enforcer를 분리하고 stop에서 원 PoC 역할을 복원합니다.
증거를 수집할 뿐 요청 승인이나 화면 성공을 대신 판정하지 않습니다. 일반 PoC의
최종 종료는 여전히 `emulator-poc-setup.sh stop`을 사용합니다. holder ACK 없이
강제 종료한 것은 데이터 정리 성공이 아닙니다.

별도 `consent-feature-gate-*` 실행파일은 tests에만 설치됩니다.
`scripts/emulator-feature-gate.py`는 보호된
`/etc/systemd/system/consent-feature-poc.service.d/gate.conf` override를 명시적으로
준비하고 실제 AUTHORIZE receipt/operation/job/PID를 기록하며 actor를 막지 않고
동작 dispatch를 최대 30초 보류합니다. prepare/wait/release/audit/cleanup 단계는
실제 UI 선택 해제를 요구하고 해당 작업 취소와 action event 0개를 검증한 뒤
원 역할/unit 설정을 복원합니다. 일반 coordinator에는 gate나 runtime switch가
없습니다. native completion 주입 증거와 실제 target 시험은 Guide 07에서 구분합니다.

`scripts/emulator-feature-endpoint-test.py`는 설치된 private bridge와 endpoint
fixture로 직접 bind한 위조 서버 및 기대 경로로 rename한 다른 PID1 socket을
요청 byte 송신 전에 거부하는지 검사합니다. 보호된 `/etc/systemd/system` 임시
unit을 사용하고 기록한 feature unit 상태만 복원합니다. 별도 negative .NET
package도 bridge를 조회합니다. 해당 PID의 coordinator
`feature-peer-rejected` / `ui-peer-rejected` 로그와 consent daemon의 역할 거부
증거를 함께 확인하며 transport 오류만으로 인증 거부를 주장하지 않습니다.

별도 재사용 경계는 `prepare --kind reuse`로 전용 test coordinator의 계측만
선택합니다. 기본값은 `--kind acquisition`입니다. 취득 증거는
`proof_kind=acquisition-receipt`, 재사용 증거는 실제 `reuse-data` 권한 조회 뒤
기록한 `proof_kind=artifact-permit`이며 artifact, session/generation, 정확한
문맥의 canonical SHA-256, operation/job/PID에 결합합니다. 새 취득 receipt가
아닙니다. 두 종류 모두 actor 최종 선택 검증 및 start dispatch 전에 보류합니다.
다음 run을 준비하기 전에 이전 gate 자료를 증거로 보존해야 하며 script는 기존
기록 덮어쓰기를 거부합니다.


worker 채널은 보호된 feature runtime 아래에 일시적인 root 소유0600 pathname을
만든다. coordinator가 fork 전에 connect/accept하고 양쪽 kernel PID가 자신인지,
UID0/GID0 및 System label이 일치하는지 검사한다. 이후 listener 경로를 제거하고
검증한 실행파일에 연결 FD만 전달한다. worker의 부모 실행파일 inode/starttime/
수명 검사는 유지한다. 선택한 커널의 socketpair는 peer label이 비어 있으며,
이를 허용하는 fallback은 없다. 시작/종료 진단은 단계와 status만 포함한다.
inode를 증명하지 못한 잔여 node는 생성된 경로를 기록하고 무조건 unlink하지 않는다.

matching tests RPM 설치와 보호 feature runtime 준비 후 실제 SMACK 근거를
필수로 검사하는 target 명령은 다음과 같다.

```sh
# 선택한 target의 privileged 시험 준비 문맥에서 실행:
systemd-run --wait --pipe -p User=root -p Group=root \
  -p SmackProcessLabel=System -p CapabilityBoundingSet=CAP_SYS_PTRACE \
  -p AmbientCapabilities=CAP_SYS_PTRACE -p NoNewPrivileges=yes \
  /usr/libexec/consent/tests/consent-feature-test --worker-channel
```

이 명시 모드는 양쪽 kernel 신원, Parcel 전달, 다른 부모 실행파일 거부,
일시 stat 오류 재시도 및 자기 node 정리를 반드시 통과해야 하며 SMACK 부재를
SKIP하지 않는다. 일반 host/GBS 시험은 해당 플랫폼 양성 부분만 SKIP으로 구분하고
다른 kernel PID 거부는 실행한다. 실제 빌드와 target 결과는 Guide07에 기록한다.

### 검증된 선택 기능 흐름(build29)

정확한 build29 TPK/RPM으로 실제 KO SESSION 및 EN 30분 TIMED 선택·승인,
앱 닫기·재실행 중 heartbeat 유지, 같은 대화 artifact 재사용, 부족한 기기 권한만
표시한 popup 및 일정+기기 실행을 완료했습니다. 넓은 일회 일정 요청을 거부하면
저장 선택과 실행 count가 유지되며, 별도로 명시한 대안은 기존 좁은 artifact를
재사용합니다. 빈 선택 저장 및 명시적 대화 종료에서 holder wipe/ACK와
CLOSED/pending0을 확인했습니다. [Guide 07](../history/07-verification-history.ko.md#build-29-선택-기능-사전승인과-실제-작업-실행)에
정확한 해시, request/job/PID/revision 문맥, 화면·명령·제한을 기록했습니다.
격리 mock provider의 Public Common Emulator 수용 결과이며 TV 하드웨어 또는
제품 Settings/argo 연동 완료가 아닙니다.

설치 가능한 positive TPK는
`/var/tmp/consent-artifacts/gbs-build-29/org.tizen.consentui-0.1.0.tpk`입니다.
같은 폴더의 runtime/daemon/PoC RPM 및 위 setup 절차와 함께 사용하세요.
TPK만으로 신뢰 역할이나 generation authority가 준비되지는 않습니다.
실제 화면과 단계 로그는
`/var/tmp/consent-artifacts/emulator-build-29/feature-main/`에 있습니다.
`prompt-ko-1.png`~`prompt-ko-3.png`는 정확한 일정 범위와 별도 보관기간,
`missing-device-ko-1.png`~`-3.png`는 부족한 한 조건,
`timed-prompt-en-1.png`~`-5.png`는 30분 선택을 보여줍니다.
넓은 요청 거부 후에는 **이번 작업만 한 번** 체크를 해제하고 이미 승인된 좁은
대안을 선택하세요. 일회 작업을 설정에 저장하는 동작이 아닙니다.

실제 dispatch 경계 시험은 일치하는 script를 선택 개발 emulator에 복사한 뒤
각 단계를 명시적 root privileged unit으로 실행합니다. 다음은 target 명령입니다.

```sh
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py prepare --kind acquisition
# 실제 설정/승인 UI를 조작하고 검토한 일정 작업을 요청합니다.
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py wait --timeout 10
# 기존 30초 gate 안에서 일정 체크 해제→검토→저장을 수행합니다.
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py release
systemd-run --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/bin/python3 /tmp/consent-feature-gate.py audit
```

다음 `prepare` 전에 완료된 근거 폴더를 보존하고, 끝나지 않은 run의 기록을
덮어쓰지 마세요. 별도 재사용 시험은 `--kind reuse`로 시작하여 먼저 정상적으로
artifact 한 개를 취득한 뒤 다시 요청합니다. release 후 audit/unit 복원 전,
살아 있는 holder에서 **대화 종료**를 선택하여 CLOSED/pending0을 확인하세요.
중단 시 script의 `cleanup` 단계를 실행하되 강제 중지를 실제 삭제 근거로 보지
않습니다. actual29 두 gate는 해당 action event0으로 통과했고 재사용 기준 상태의
최초 취득 1건은 그대로 남았습니다. 첫 취득 시도는 확인 중 기한 만료로 실패하여
별도 보존했습니다. Native 지연 결과/retry/epoch 주입 회귀는 실제 UI gate와 다른
근거이며, 이미 시작된 효과를 나중 선택 해제로 되돌린다고 주장하지 않습니다.

최종 actual29 정리에서는 일반 PoC 역할 복원, feature/PoC/test unit 중지,
소유 gate FIFO/override/임시 소켓 제거를 확인했습니다. production은 default-deny와
기존 DB/registry inode를 유지하며 active입니다. 설치 패키지와 보호된 일반 근거 파일은
검토용으로 남겼으며 소유 Aurum bootstrap/forward는 종료했습니다. 최종 Guide07/08
근거와 Guide10 정리만 archive 이후 문서이며, 소스와 시험 TPK는 exact29를 유지합니다.


<a id="guide-10-checkpoint"></a>

### 실행한 UI09 검증

동일 Release27/r7 native build의 CTest33개(29 PASS, root 전용4 SKIP)가
통과했습니다. Managed 선택·검토·geometry 검사도 build에 포함됩니다. 실제 선택한
x86_64 emulator에서 UI와 private namespace의 CM/CE worker 모두 서명 TPK의
정확한 전체 library byte를 사용했습니다. 전역 RPM upgrade 대신 소유 tests helper를
추출했습니다. RPM-packaged byte hash/build ID는 별도로 기록하며 TPK와 같다고
주장하지 않습니다.

`/var/tmp/consent-artifacts/consent-ui-native-09/attempt-r7-02/`는 실제 기본 해제
ONCE, 거절·무효과, 체크·전체 검토 PERSISTENT, fresh CE receipt를 갖는 새 operation
재사용, 철회·거절, ONCE-only CM 실행을 기록합니다. 선택 승인이 PERSISTENT여도
base `grant_mode`는 ONCE로 유지됩니다. 실제 기본 NUI checkbox는 주황색이며
작은 배치 구현이 Samsung runtime theme 설치를 의미하지 않습니다.

일부 `attempt-r7-06/`은 정상 restart의 승인 유지와 소유 DB 손실
(grants0, cleanup_unknown1) 이후 실제 새 승인·효과를 증명합니다. 최종 generation
후 새 UI는 timeout으로 성공에 포함하지 않습니다. 별도 `attempt-r7-09/`가 해당
성공을 완료했습니다: 실제 소유 helper generation 변경, 새 REQUIRED/이전
STALE(-116), 기본 해제 새 팝업, 체크·전체 페이지 검토, 새 coordinator epoch의
fresh CE action/receipt와 authoritative ALLOWED gate입니다. 실제 TPK 재설치
lifecycle 증거가 아닙니다. Gate-only receipt와 실제 효과 receipt는 구분합니다.

성공02/09 host exit0/finally errors[]와 일부06 복원은 기존 전역 package/state/
config/unit의 정확한 일치를 보존하며 예상 package inode/time과 runtime parent
timestamp 변경은 별도 기록합니다. 소유 fixture와 Aurum을 정리했습니다.
Production18 PID31569와 기존 inactive PoC는 유지됐고 이전 build/capture/controller
실패도 보관했습니다.

[Guide08 UI09 증거](../history/07-verification-history.ko.md#guide-8-checkpoint)에
정확한 명령·파일·한계가 있습니다. `source-r7.json`은 실행한34개 snapshot이며
이후 최종 Guide08/10 한·영 문장만 변경하고 나머지30개 byte는 동일합니다.
실제 consent API를 쓰는 synthetic CM/CE 참여자이며 제품 CM/CE adapter나 배포된
제품 UI를 의미하지 않습니다.
