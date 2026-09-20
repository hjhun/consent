# 가이드 09: 저장소 유지 관리와 복구 경계

Repository는 operation 재시도, 요청 결과, holder 정리 증거를 유지하면서
사용할 수 없는 메타데이터를 축소합니다. retention TTL이나 완료된
request/session/artifact 행 삭제, definitions registry 유실 복구는 구현하지
않습니다. 아래 registry 유실 절차는 별도 증분을 위한 설계입니다.

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

## Registry 유실 복구 설계 — 미구현

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
5. source/generation 검증과 격리를 완료한 뒤에만 READY를 durable하게 발행합니다.
   일반 daemon 시작 경로가 새 DB incarnation과 epoch를 만들고 pkgmgr/설치
   authority를 재검증한 뒤 준비 완료 전에 정의를 import합니다. grant, 소비
   receipt, session, artifact는 복원하지 않습니다. 보호된 복구 marker를 통해
   READY 전에 `cleanup_unknown=1`을 DB에 영속화해야 합니다. 그렇지 않으면
   registry와 DB가 모두 비어 있는 상태를 최초 설치로 잘못 판단합니다.

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
