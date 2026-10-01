# 가이드 09: 저장소 유지 관리와 복구 경계

[English](09-storage-maintenance.en.md)

Repository는 operation 재시도, 요청 결과, holder 정리 증거를 유지하면서
사용할 수 없는 메타데이터를 축소합니다. retention TTL이나 완료된
request/session/artifact 행 삭제, definitions registry 유실 복구는 구현하지
않습니다. 아래 registry 유실 절차는 별도 증분을 위한 설계입니다.

## 1. SDK 빌드에서 유지 관리 검증하기

프레임워크 유지보수 작업이며 공개 유지 관리 CLI나 C API는 없습니다.
같은 네이티브 SDK로 구성한 빌드 디렉터리에서 저장소 테스트를 실행합니다.

```sh
cd /path/to/consent-build
ctest -R '^repository-test$' --output-on-failure
```

테스트 선택이 비어 있지 않고 해당 테스트가 통과해야 합니다. 내부 테스트 명령이며
새 검증 결과나 두 번째 프로세스가 운영 DB를 열어도 된다는 뜻이 아닙니다.
살아 있는 데이터베이스는 consentd만 소유합니다.

## 2. 유지 관리 동작 해석하기

직렬 DB 실행기에서 Tick은 최대 60초에 한 번 제한된 묶음을 처리합니다.
무효 실행 허가의 payload, 오래된 UI token과 사용할 수 없는 재개 token을 줄일 수
있습니다. 재시도 ID, 유효한 receipt, 요청 상태와 holder 정리 증거는 보존합니다.
파일 크기가 줄어들 필요는 없으며 VACUUM과 TTL 삭제는 제공하지 않습니다.

BUSY, FULL이나 I/O 오류이면 불확실한 실행을 중단하고 저장소 실패를 처리합니다.
DB를 교체하거나 재시도 이력을 버리면 안 됩니다. 격리 복구 검사는
[가이드 12](12-developer-smoke.ko.md), holder 정리는
[API 05](api/05-sessions-and-data.ko.md)를 따릅니다.

## 참조: 트랜잭션과 용량 규칙


## 실행과 트랜잭션 경계

`Repository::Maintain()`은 직렬 DB executor가 소유하는 내부 C++ 메서드입니다.
공개 C API, IPC 메서드, root CLI 또는 별도 SQLite writer가 아닙니다.
`Tick()`은 기존 무결성 검사, 정의 reconciliation, 만료 처리 후 최대 60초에
한 번 한 batch를 실행합니다. Repository를 열 때 이 주기가 시작됩니다.
내부 시험은 기다리지 않고 `Maintain()`을 직접 호출할 수 있습니다.

한 `BEGIN IMMEDIATE` 트랜잭션이 네 종류를 합쳐 **최대 128개의 논리적 레코드**를
선택합니다. commit 성공 후 시작 종류를 순환하여 한 종류가 예산을 계속
독점하지 않게 합니다. authorization 하나에 grant 연결이 여러 개일 수 있으므로
물리적인 SQL 변경 행 수가 128개라는 뜻은 아닙니다. 새로 참조가 없어지는
grant는 다음 batch에서 정리될 수 있습니다.

이 제한은 선택한 변경 대상을 제한하며, 실행 시간이나 이력 조회량을 보장하지
않습니다. 후보 선택은 과거 authorization/session/grant 행을 순회할 수 있습니다.
추가된 `authorization_grant_source(grant_id)`와
`artifact_grant_source(grant_id)` 인덱스가 참조 존재 검사를 지원합니다.
SQLite는 내부 참조 검사에서 이 covering index를 사용하지만 바깥의 revoked
grant 후보 조회는 `grants`를 순회할 수 있습니다. 실제 제품의 큰 부하에서는
별도의 지연 시간·용량 검증이 필요합니다. 두 인덱스는 기존 schema 2 시작
트랜잭션에서 멱등하게 생성하며 저장된 행 형식은 바꾸지 않습니다.

## 변경하는 데이터와 보존하는 데이터

| 대상 | 축소하는 데이터 | 보존하는 데이터 |
|---|---|---|
| `valid=0` authorization | payload, `authorization_grants` 연결 | receipt ID, enforcer/operation/step 고유 키, fingerprint, 생성 시각, 무효 상태 |
| UI 잔여 정보가 있는 ALLOWED, DENIED, CANCELLED, EXPIRED, INVALIDATED request | token/UI owner/UI instance 컬럼, payload의 비공개 `prompt_token`, `ui_owner`, `ui_instance`, `_prompt_locale` | request ID, 범위가 구분된 client request 키, fingerprint, 최종 결과, 공개 context |
| CLOSED session | resume token hash | session ID, 소유 정보, 상태, generation, 정리 context |
| authorization과 artifact가 모두 참조하지 않는 revoked grant | 사용할 수 없는 grant 행 전체 | 두 관계 중 하나라도 참조하는 모든 grant |

`get_prompt`는 비공개 표시 locale과 비어 있지 않은 UI 소유 정보를 하나의
UPDATE로 저장합니다. 기존 종료 경로는 유지 관리가 컬럼과 payload를 함께
지울 때까지 소유 정보를 남깁니다. 따라서 token을 먼저 지운 취소·만료·무효화된
typed prompt도 선택됩니다. 알 수 없는 request 상태를 임의로 해석하거나
잘못된 행을 기회적으로 고치지는 않습니다.

`ReceiptValid()`는 payload를 읽기 전에 무효 authorization을 거절합니다.
고유 키와 fingerprint를 남기므로 동일한 무효 실행 재시도는 STALE,
내용이 달라진 재시도는 CONFLICT를 유지합니다. 유효 authorization의 payload와
원본 grant 연결은 유지합니다. 특히 소비된 ONCE grant와 유효 receipt는
원래 실행의 재시도를 위해 남으며 새로운 사용 횟수를 만들지 않습니다.

DELETED artifact를 포함하여 artifact 행, `artifact_grants`, `artifact_parents`,
cleanup ACK를 보존합니다. 살아 있는 derived artifact에는 원본 grant의 합집합이
별도로 저장되어 부모의 물리적 데이터 잔존 여부와 독립적으로 검증됩니다.
이번 증분은 이 합집합과 부모 이력을 모두 보존합니다. 삭제된 artifact의
등록 tombstone은 예전 receipt로 데이터가 다시 만들어지는 것을 막고,
성공한 cleanup ACK를 반복하는 동작도 유지합니다.

등록 receipt, 제거 tombstone, obsolete offline receipt, 설치 authority receipt,
offline spool 파일은 삭제하지 않습니다. 요청 결과 계약과 정리 증거의 보존
기간을 줄이지 않습니다. 내부 축소만으로 policy revision을 변경하지 않으며
`cleanup_reconciliation_required`도 해제하지 않습니다.

## 실패와 용량 처리

선택된 변경 전체를 함께 commit합니다. 쓰기 실패 시 batch의 앞선 변경도
rollback합니다. 명시적 내부 호출은 오류를 반환하고 timer는 오류를 기록한 뒤
기존 reconciliation 경로로 저장소를 fence합니다. 실패 후에도 같은 주기만큼
기다린 다음 예약된 시도를 수행합니다. 명시적 메서드는 성공을 반환하기 전에
최종 Snapshot의 epoch도 확인합니다.

BUSY, FULL, I/O 오류를 이유로 DB를 교체하거나 receipt 이력을 버리지 않습니다.
기존의 명시적인 SQLite corruption 복구는 별도입니다. 후보 조건과 보존된
tombstone이 축소를 멱등하게 만들므로 결과가 불확실한 유지 관리도 재시도할
수 있습니다.

`VACUUM`, DB 교체, 즉시 파일 크기 감소는 제공하지 않습니다. SQLite가 빈 공간을
재사용할 수 있습니다. request/session/artifact 행과 durable ledger는 여전히
늘어날 수 있고 registry/spool 용량 오류도 명시적으로 유지됩니다. 임의의 TTL로
재시도 키를 삭제하면 예전 operation이 새 실행이 되거나 제거한 등록이 살아날 수
있습니다. 전체 pruning에는 retry generation·확인 응답·만료 계약과 별도 구현이
필요합니다.

## 검증 범위

`repository-maintenance-test`는 격리된 저장소와 실제 Repository API 경로로
ONCE 재시도·소비, STALE/CONFLICT 유지, 요청 결과 보존, 부모 삭제 후 살아 있는
derived 데이터, artifact 비부활, 반복 cleanup ACK, typed prompt 정리,
닫힌 session 비재활성화를 검증합니다. 129건 fixture에서 첫 128개 대상
batch가 authorization payload 하나를 남기는지도 확인합니다.
시험 전용 SQLite interposition으로 payload UPDATE 뒤 edge DELETE 전에
IOERR/FULL/BUSY를 주입하여 앞선 변경의 rollback을 검증합니다. 시험 전용
monotonic clock 이동으로 실제 Tick의 분 단위 예약, 오류 fencing, 다음 주기
성공을 확인합니다. 제품에는 오류 주입 플래그나 시간 override가 없습니다.

새 실행 파일과 기존 repository, provenance, storage fault, offline repository
회귀를 SDK 헤더로 빌드하고 host GLib/SQLite에서 실행한 보조 native 검증입니다.
GBS/emulator, 실제 디스크 부족·장치 I/O·전원 차단·지속 부하 검증을 뜻하지
않습니다. target 실행 결과는 [검증 기록](07-verification.ko.md)에 기록합니다.

### Build 24 target 검증

GBS source tree `84042ee0d4113006872fbc334fcf3d755d72fd76`는 native
CTest 14개 통과·root 전용 4개 건너뜀을 기록했습니다. 선택한 개발 emulator에서는
정확한 RPM의 `repository-maintenance-test`가 `consent-maintenance24.service`
안에서 8개 시나리오 모두 통과하고 status 0으로 정상 종료했습니다. 설치된 실행 파일의
SHA-256은 RPM payload와 같았습니다:
`a6f296239f60f401437a8a24e266584a843abaf6d60d08356f7ae5d2e083672e`.
출력은 `/var/tmp/consent-artifacts/emulator-build-24/maintenance.log`,
source/RPM과 빌드 로그는 `/var/tmp/consent-artifacts/gbs-build-24`에 보존했습니다.
이 target fixture는 격리 state와 주입한 SQLite 오류를 사용하므로 실제 저장 공간
소진·기기 쓰기 실패·전원 차단 동작까지 입증하지 않습니다.

## Registry 유실 복구와 bootstrap 경계

Build43(Release11)은 root 보호 bootstrap receipt와 `LoadRegistrations` 전
admission 차단을 구현했습니다. Fresh 설치는 systemd invocation을 한 번
claim하며 daemon은 unit, cgroup, 정확한 MainPID를 검증합니다. 초기화된
receipt와 신뢰 가능한 registry가 있을 때 DB만 유실되면 새 DB incarnation과
durable `cleanup_unknown=1`로 자동 복구합니다. Registry 또는 receipt 유실,
부분 fresh claim, recovery-required receipt에서는 admission을 거부합니다.
계획된 복구 계약은 검증된 최신 desired-definition source 전체를 요구합니다.
현재 production `--begin`은 recovery ID를 검사한 뒤 source를 읽지 않고
저장소 변경 없이 missing-source 오류를 반환합니다. Helper는 SQLite를
직접 쓰지 않습니다. DB와
registry가 모두 없다는 사실만으로 fresh 설치로 판단하지 않습니다.

Build43 emulator의 isolated bootstrap·POC classification fixture가
통과했습니다. Release7→8 transaction 후 production·POC receipt가 모두
INITIALIZED였습니다. POC intact-pair 경로는 코드와 최종 receipt에서
추론하며 pre-upgrade POC 파일 identity는 캡처하지 않았습니다. Isolated
POC fixture의 불완전 pair 차단은 실제 absent-state
POC RPM upgrade 근거가 아닙니다. 신뢰할 desired-state producer,
import-complete fence, 물리적 holder reconciliation은 미구현입니다.
Metadata 유실로 과거 approval이나 cleanup 완료를 추정하지 않습니다.

### 향후 registry 전손 복구 설계 — 미구현

현재 구현은 DB가 남아 있는데 definitions registry가 사라지거나 손상되면
의도적으로 시작을 막습니다. 겉보기에 정상인 DB만으로 독립적인 신뢰 가능한
desired-state source를 복원하거나 예전 승인의 최신성을 증명할 수 없습니다.
이 차단을 우회하려고 두 파일을 수동 삭제하지 않습니다.

추후 root 전용 helper는 재개 가능한 2단계 복구 operation을 제공해야 합니다.

1. 명시적이고 안정적인 recovery ID, daemon 정지 확인, 기존 lifecycle EX lock을
   요구합니다. 변경 전 보호된 ancestor, 소유자, 일반 파일, 단일 링크, 정확한
   관리 파일명을 검사합니다. 살아 있는 daemon/Installer, 예상 밖 파일,
   읽을 수 없는 source가 있으면 진행하지 않습니다.
2. 파일을 옮기기 전에 daemon 쓰기 경로 밖에 root 소유 PREPARED 기록을 durable하게
   발행합니다. 미완료 복구가 있으면 SQLite open/offline import 전에 시작을
   거절합니다. 안전한 재시도를 위해 원래 설치 generation과 관리 파일의 정확한
   identity를 기록합니다.
3. DB, 해당 journal/WAL/SHM, registry, 관리되는 임시/retired 파일과 기존 offline
   spool을 격리하고 새 저장소에 승인을 복사하지 않습니다. 보호된 directory FD,
   같은 파일시스템 내 이동, 양쪽 directory fsync를 사용합니다. 동일 ID 재시도는
   기록된 identity와 완료된 이동을 확인합니다. 오류로 예전 DB를 자동 복원하거나
   빈 결과를 성공 처리하지 않습니다.
4. 신뢰 가능한 Installer가 실제 설치 패키지와 현재 필요한 정의를 확인합니다.
   이전 설치 generation을 fence한 다음 기존 offline C API로 확인된 desired set만
   새 operation으로 staging합니다. 설치 authority receipt는 보존합니다. 기존
   spool만으로 현재 desired state를 알 수 없습니다. registry tombstone이 없으면
   같은 generation에서 제거한 정의를 되살릴 수 있습니다. 신뢰 가능한 입력이
   없으면 정의를 사용할 수 없는 상태로 남겨야 합니다.
5. source/generation 검증과 격리 완료 후에만 helper `SOURCE_READY`를
   durable하게 발행합니다. 이는 daemon admission 또는 systemd `READY=1`이
   아닙니다. SQLite는 consentd만 열고 새 DB incarnation을 만듭니다.
   pkgmgr와 authority를 검증하고 `cleanup_unknown=1`을 영속화하며 모든
   정의를 import하고 `import_complete=1`을 commit한 후에만 listener,
   admission, systemd READY를 허용합니다. grant, 소비 receipt, session,
   artifact는 복원하지 않습니다. 부분 import 중 crash는 계속 차단하고
   재개할 수 있어야 합니다.

별도의 증거 기반 holder reconciliation 프로토콜로 해제할 수 있을 때까지
`cleanup_reconciliation_required`를 유지해야 합니다. 새 테이블이 비었다고
이전 물리적 데이터의 삭제를 증명하지는 못합니다. helper 배포 전에 crash,
rename/fsync 실패, 부분 격리, 안전하지 않은 경로, 과거 seed, 실제 설치 상태
변경에 대한 별도 시험이 필요합니다.

ledger 전체를 잃으면 일부 과거 operation ID에 대한 정보도 잃습니다. 현재의
응답·캐시 epoch만으로 알 수 없는 ID의 영구 deduplication을 증명할 수 없습니다.
복구는 결과 불확실성을 유지하고 새 승인을 요구해야 합니다. reset을 넘어서는
exactly-once 보장에는 추가 durable retry 프로토콜이 필요합니다.

기존 경계는 [저장소 설계](../design/04-storage-design.ko.md),
[설치 authority](03-installation-authority.ko.md),
[오프라인 등록](04-offline-registration.ko.md)을 참고합니다.

## 참조: 복구 시험과 저장소 계약

root 전용 installation authority는 `begin`, `attach`, `commit`, `remove`로
`installations.conf`를 관리합니다. begin은 세대를 회전하고 과거 앱 목록의
사용을 차단하며 attach는 패키지의 각 앱을 기록하고 commit은 완성된 목록을
활성화합니다. 한 번도 기록되지 않은 패키지에만 `absent`를 사용합니다.
명령마다 고유 operation ID와 기대 세대가 필요하며 결과가 불확실하면 같은
operation으로 재시도합니다. 실제 패키지 설치와 authority commit이 성공한
후에만 등록합니다. 삭제할 때는 authority를 먼저 removed로 바꾼 뒤 같은
기대 세대로 consent 정의를 unregister합니다. tombstone은 패키지 전체 정리가
진행되는 동안 새 권한 부여를 막습니다. 재설치는 새 세대를 사용합니다. 이 도구는
Installer 연동 지점이며 플랫폼 hook 설치 완료를 뜻하지 않습니다.

C API 실행 프로그램은 `METHOD [PACKAGE [APP]] key=value ...`, `--async`,
`--timeout-ms=N`, `--repeat=N`, `--expect-status=N`,
`--expect-decision=VALUE`를 받습니다. 결과가 다르면 0이 아닌 종료값을
반환합니다. 일치하는 위임 문맥으로 신뢰 checker를 설정한 후 다음과 같이
실행할 수 있습니다.

```sh
/usr/libexec/consent/tests/consent-api-test check \
  subject=org.example.agent profile=owner count=1 \
  r0.definition=calendar.read r0.operation=read r0.scope=today \
  r0.purpose=answer-calendar --async --expect-decision=CONSENT_REQUIRED
```

`-isolated` 도구는 별도 정적 테스트 클라이언트를 연결합니다. `consentd-test`는
같은 역할 검사와 `/tmp/consent-test`의 socket/config 경로,
`/opt/var/lib/consent-test`의 영속 state 경로를 사용하며 설치 인벤토리 adapter만
대체합니다. 운영 바이너리에는 해당 adapter를
활성화하는 런타임 switch가 없습니다. client unit test는 모의 전송 peer를
사용하므로 클라이언트 계약을 검증하며 데몬 정책이나 플랫폼 신원 검증을
대신하지 않습니다.

`scripts/emulator-scenario.sh`는 격리된 에뮬레이터 단계별 테스트를 제공합니다.
`basic`은 새 테스트 설치 상태가 필요하며 이전 결과를 임의로 삭제하지 않습니다.
후속 단계는 지속성, 실행·정지 중 DB 삭제, 손상 DB 복구, 과거 DB 교체 및
동일 UID의 역할 거부를 확인합니다. 선택한 개발 에뮬레이터에서 root와
`System` security label로만 실행합니다. `endpoint-fixture`는 실제
`/run/.consentd.sock`을 사용하는 별도 수동 endpoint 인증 도구이므로 CTest에서
제외합니다. 실행 전 [검증 증거](07-verification.ko.md)의 준비 절차를 따릅니다.

미완성 IPC 입력, UI 승인을 기다리는 요청, 실제 대기 중인 DB 작업의 종료
증거는 구분합니다. 수동 `wire-scenario --shutdown-wait`는 일부 header만 받은
연결의 종료를 확인하며 supervisor가 서비스 정상 종료와 `database-drained`
로그도 확인합니다. 이것만으로 대기 중이던 DB 작업의 완료를 입증하지는 않습니다.
UI를 기다리는 요청은 DB transaction이나 worker를 점유하지 않으므로 그 요청의
연결 단절·재시작 동작은 별도 시나리오입니다. 실제로 확인한 경계는 검증 기록을
참조합니다.

별도 실행 파일 `consentd-shutdown-test`는 DB 작업이 진행 중인 상태를 통제하는
테스트에 사용합니다. 일반 격리 데몬 설정을 사용하며
`repository_shutdown_interposer.cc` 테스트 helper만 추가합니다. 운영
`consentd`와 일반 `consentd-test`에는 이 helper가 없습니다. 제어 프로그램은
데몬 계정 소유 mode0700 `/tmp/consent-shutdown-gate`에 mode0600 FIFO
`shutdown-db-release`를 만들고 읽기·쓰기 양쪽으로 열어 둡니다. 실제 grant를
변경하는 revoke가 COMMIT 전에 멈추면 mode-0600 `shutdown-db-ready`에
`pid=N state=before-commit`을 기록합니다. PID가 테스트 데몬과 일치하는지
확인한 후 서비스 종료를 시작하고, 5초 안에 FIFO로 한 바이트 `C`를 보냅니다.
supervisor의 정상 종료·`database-drained` 검사를 요구하고 데몬이 멈춘 뒤에만
철회가 커밋되었는지 확인합니다. gate 오류나 timeout은 transaction을 실패시키며
ready 파일의 생성만으로 drain 성공을 판단하지 않습니다. 이는 테스트 절차 설명이며
실제로 통과했다는 주장은 snapshot별 검증 기록에서 확인합니다.

프로세스 강제 종료, 정상 재부팅, emulator 전원 강제 중단을 별도 시나리오로
기록합니다. 강제 DB 삭제는 격리된 테스트 상태 또는 선택한 개발 emulator의
consent 상태만 대상으로 합니다. 다른 플랫폼 DB를 삭제하지 않습니다.
승인 정보가 소실되면 새 승인이 필요하며 등록 정의 복원에는 별도로 신뢰할 수
있는 원본이 필요합니다. 권한·용량 부족·I/O 오류를 DB 삭제로 숨기지 않습니다.

초기 저장 구현은 직렬화한 SQLite 연결 하나와 `journal_mode=DELETE`,
`synchronous=EXTRA`, foreign key 및 100 ms busy timeout을 사용합니다.
rollback journal은 SQLite가 관리하므로 복구 중 따로 지우지 않습니다.
보호된 `definitions.registry`는 DB와 독립적으로 정의와 패키지 삭제 기록을
보존합니다. 별도 Installer 세대 registry와 플랫폼 `pkgmgr-info`로 정의를
현재 설치 인스턴스에 결합합니다. 복원 대상은 정의이며 사용자 승인은 복원하지
않습니다. 실행 중 DB가 없거나 교체되면 기존 handle을 닫고 새 epoch를 만듭니다.
정상 재시작에서는 pending 요청과 세션을 무효화하며 조건을 만족하는 지속 승인을
보존합니다. 삭제 증거가 수신될 때까지 holder 정리는 미완료로 남습니다.

GBS는 GCC 14.2와 `-Werror`로 native Parcel 클라이언트·데몬·C 실행
프로그램·installation authority를 빌드합니다. 패키지 검사는 클라이언트 계약,
저장 정책·복구, 저장 장애 주입, 프로세스 강제 종료 경계 및 IDL 생성을 다룹니다.
build-root 테스트와 에뮬레이터 통합 검증은 구분합니다. [검증 증거](07-verification.ko.md)에
검사한 snapshot, 실제 결과와 남은 수용 조건을 기록합니다. mock 신원 테스트가
제품 신원 연동을 입증하지는 않으며 실제 Installer·argo·UI 정책 연동이 필요합니다.

데몬은 조건을 만족하는 PERSISTENT/SESSION 승인을 최대 500 ms 동안 cache
가능으로 표시하며 timer 처리에서 설치 세대를 대조합니다. 각 클라이언트
handle은 request cache를 최대 64개 유지합니다. SESSION 항목은 서버가 확인한
session과 generation이 일치해야 하며 전달된 TTL과 session 만료를 넘지
않습니다. handle 사이에 cache를 공유하지 않습니다. 이벤트·epoch 변경·연결
단절은 항목을 무효화하며 QUERY와 AUTHORIZE는 항상 데몬을 확인합니다.
실제 승인 UI·Installer 수명주기 hook·제품 정책 연동이 필요하며 프로토콜
실행 프로그램은 운영 UI를 대체하지 않습니다.

---

[관련 작업](api/05-sessions-and-data.ko.md) · [이어 읽기](07-verification.ko.md) ·
[역할별 문서](../README.md)
