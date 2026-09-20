# 검증 근거와 남은 범위

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
