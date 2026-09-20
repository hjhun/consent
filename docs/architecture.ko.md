# 구조와 신뢰 경계

이 문서는 저장소의 구현 구조를 설명합니다. CEP는 목표 설계이며,
[구현 결정](decisions.ko.md)은 채택한 선택을 기록합니다.
[저장소 설계](storage-design.ko.md)와 [통신 규격](protocol.ko.md)은 각각의
형식을 설명합니다. 격리 시험 결과와 실제 플랫폼 연동 결과를 구분합니다.

## 구성 요소

```mermaid
flowchart LR
  Installer[인증된 Installer] --> API[consent C API]
  Argo[argo: 승인 요청] --> API
  Service[Capability Manager / Context Engine] --> API
  UI[승인 UI] --> API
  Holder[데이터 보유자] --> API
  API --> Wire[생성된 Parcelable envelope]
  Wire --> Socket[systemd Unix stream listener]
  Socket --> Identity[호출자 신원과 역할 검증]
  Identity --> Repository[직렬화된 정책 및 저장 엔진]
  Repository --> Registry[정의 전용 registry]
  Repository --> DB[(SQLite consent.db)]
  Authority[설치 generation authority] --> Repository
  Repository --> Events[revision 및 epoch 무효화]
  Events --> API
```

공개 C API는 opaque 파라미터·결과 객체를 사용하며 내부는 C++입니다. 등록에는
패키지명과 app ID를 명시적으로 전달합니다. 삭제에는 패키지명, 안정된 operation
ID, 예상 설치 generation을 전달하며 app ID는 필요하지 않습니다. 한 패키지의
여러 앱은 같은 설치 generation을 공유합니다. 재시도 ID는 요청 내용과 결합하여
같은 ID로 삭제 범위가 바뀌지 않도록 합니다.

컴파일러는 `src/protocol/consent.idl.json`에서 빌드 시점에 실제
`tizen_base::Parcelable` 클래스를 생성합니다. 공개 C API에는 Parcel 클래스를
노출하지 않습니다. 4바이트 big-endian 길이 뒤에 native Parcel payload가 오며,
크기를 제한한 읽기와 검증을 거친 후 요청을 처리합니다. DB·registry 내부
메타데이터는 wire와 독립된 버전 및 정규화 형식을 사용합니다.

## 실행 영역과 소유권

```mermaid
flowchart TD
  Main[Main GLib context: activation / 접수 / signal / timer]
  IO[I/O GLib context: 연결과 peer 신원]
  Parser[상한 있는 parser pool: 2 threads]
  Queue[상한 있는 DB 작업 queue]
  DB[DB thread 하나: SQLite 및 registry 소유]
  Main -->|연결 참조| IO
  IO -->|크기 제한 frame| Parser
  Parser -->|검증된 메시지| IO
  IO --> Queue
  Queue --> DB
  DB -->|커밋 결과와 snapshot| IO
  IO -->|직렬화 송신| Client[클라이언트 I/O worker]
  Client -->|source 게시| Callback[호출자 GLib callback context]
```

초기 상한은 연결 64개, parser 작업 128개, DB 작업 128개, 연결당 진행 요청
32개, payload 64 KiB, 송신 queue 256 KiB입니다. UID별 연결 상한과 인증된 제어
역할의 추가 여유를 둡니다. 이는 구현 기본값이며 제품 용량 실측 결과가 아닙니다.
parser와 DB 작업은 사용자 응답을 기다리지 않습니다. 클라이언트가 접수된 요청
ID로 결과를 조회하므로 승인 대기가 데몬 worker를 점유하지 않습니다.

연결 버퍼와 source는 I/O thread만 변경하고 SQLite 및 registry는 DB executor가
소유합니다. 결과에는 daemon/DB epoch와 revision을 포함합니다. 서버는 게시 전
최신 snapshot의 epoch를 다시 비교하여 복구 전 ALLOWED 결과가 새 DB의 승인으로
전달되지 않게 합니다. 저장소 상태가 불확실하면 클라이언트 동기화를 무효화합니다.
종료 시 접수를 중지하고 연결을 닫은 뒤 이미 받은 DB 작업과 세션·정리 상태를
저장하고, callback context의 수명을 유지하면서 worker를 합류시킵니다.

클라이언트별로 상한 있는 I/O worker와 짧은 mutex 구간을 사용합니다. ASYNC는
지정한 GLib context의 소유 thread에서 호출하며 callback은 함수 반환 뒤에
게시되고 잠금 밖에서 실행됩니다. 로컬 대기 timeout은 원격 요청 취소가 아닙니다.
fork 자식은 부모 handle이나 callback을 사용할 수 없고 새 client를 생성해야 합니다.

## 신원과 설치

역할은 커널 `SO_PEERCRED`, 커널 `SO_PEERSEC`, 보호된 절대 실행파일 경로와
device/inode, root 소유 allowlist로 결정합니다. 처리 및 응답 전에 프로세스
starttime·UID·실행파일을 재검증합니다. root UID, basename, payload 역할 주장은
권한이 아닙니다. subject, profile, package 및 enforcement 위임을 별도로 검사합니다.

확인한 emulator 커널은 4.4.35입니다. starttime을 검사하는 fallback은 연결과
최초 프로세스 조회 사이의 모든 경쟁을 제거하지는 못합니다. 새 커널의 조건부
pidfd 지원은 추가 근거이며 구형 target이 지원한다고 주장하지 않습니다. 운영
설정에는 신뢰 역할을 기본 등록하지 않습니다. 실제 argo·UI·Installer 배포는
별도 연동 사항이며 이름이 비슷한 실행파일만으로 역할을 부여하지 않습니다.

운영 등록은 pkgmgr-info로 app/package 관계를 확인하고 별도로 보호된 Installer
authority의 generation과 `expected_generation`을 비교합니다.
`consent-installation-authority`는 begin/attach/commit/remove, 내구성 있는 갱신,
재시도 fingerprint 및 tombstone을 제공합니다. pending·removed generation으로
정의를 활성화하지 않습니다. 이 도구가 기존 Installer lifecycle hook 연동 완료를
뜻하지는 않습니다. [설치 authority](installation-authority.ko.md)를 참고하세요.

시험용 데몬과 클라이언트는 endpoint·설정·영속 상태가 분리된 별도 빌드 target입니다.
실행파일·UID·SMACK 검증은 유지하고 package inventory 조회만 명시적인 시험
authority로 대체합니다. 운영 binary에는 인증을 우회하는 runtime 옵션이나
환경변수가 없습니다.

## 승인과 데이터 수명

```mermaid
sequenceDiagram
  participant A as argo
  participant L as consent
  participant D as consentd
  participant U as 승인 UI
  participant E as 실제 접근 서비스
  A->>L: consent_request_async(requirements)
  L->>D: request(안정된 client ID)
  D-->>L: PENDING 및 request ID
  U->>D: get_prompt(request ID, locale)
  D-->>U: 정책·문구 snapshot 및 prompt token
  U->>D: respond(token, 사용자 선택)
  D->>D: 결정과 grant 커밋
  L->>D: result(request ID)
  D-->>L: 최종 결정
  L-->>A: 소유 context에서 callback
  E->>D: check(AUTHORIZE, operation, step)
  D->>D: 전체 조건 검증 및 ONCE 원자적 소비
  D-->>E: 결정 및 취득 receipt
```

QUERY는 사전 조회이고 실제 접근에는 AUTHORIZE가 필요합니다. 모든 필수 조건이
충족될 때만 ONCE를 소비합니다. 같은 operation/step 재시도는 동일 fingerprint와
현재 유효성을 요구합니다. UI token은 표시한 정책·문구를 살아 있는 UI 인스턴스에
결합합니다. 실제 화면 렌더링은 외부 UI의 책임이며 이 저장소는 인증된 UI 통신을
제공합니다.

```mermaid
stateDiagram-v2
  [*] --> ACTIVE: 인증된 세션 생성
  ACTIVE --> SUSPENDED: 재개 가능한 접점 단절
  SUSPENDED --> ACTIVE: 재인증 및 새 generation
  ACTIVE --> CLOSING: 종료 / 만료 / 재시작
  SUSPENDED --> CLOSING: 유예 만료 / 재시작
  CLOSING --> CLOSED: 필수 holder 정리 ACK
  CLOSED --> [*]
```

접근 grant와 조회 결과의 보관·사용 허가는 구분합니다. receipt는 취득 범위와
holder·session을 결합하고 artifact에는 메타데이터와 출처만 저장합니다.
파생 artifact는 부모 제한을 상속합니다. 세션 종료는 정리 완료 전에 접근부터
차단하며 ACK 실패·미수신을 CLOSED 성공으로 바꾸지 않습니다. 실제 데이터,
history, prompt 및 모델 cache는 연동된 holder가 정리해야 합니다. 데몬이 다른
프로세스 메모리를 직접 삭제할 수 있다는 의미가 아닙니다.

## 복구 경계

```mermaid
flowchart TD
  Failure[DB 소실 / 교체 / 손상]
  Fence[불확실한 승인 차단 및 이전 epoch 폐기]
  Retire[handle 종료 후 DB와 sidecar 격리]
  Create[새 schema와 DB identity 생성]
  Replay[설치 generation 재검증 및 정의 재반영]
  Ready[새 epoch 및 재승인 필요]
  Failure --> Fence --> Retire --> Create --> Replay --> Ready
  RegistryLoss[registry 소실 / 손상] --> Block[차단 유지 및 명시적 재등록·재조정]
```

정의 registry가 desired state의 원본입니다. registry fsync/rename 뒤 SQLite
projection transaction을 수행하며 두 파일이 하나의 원자적 transaction이라고
간주하지 않습니다. 독립적으로 저장한 DB identity로 과거의 유효 DB 교체도
검출합니다. registry에는 사용자 결정·사용 기록이 없으므로 복구 시 되살리지
않습니다. DB 전체 소실 시 정리 메타데이터도 사라질 수 있습니다. holder는 새
epoch에서 사용을 차단하고 재조정해야 하며 물리 삭제가 완료됐다고 추정하면 안 됩니다.
