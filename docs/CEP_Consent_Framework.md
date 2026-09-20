# CEP: Tizen 사용자 승인 프레임워크 — consent / consentd

| 항목 | 내용 |
|---|---|
| 문서 상태 | Draft — 설계 검토용 |
| 문서 버전 | 0.5 |
| 작성일 | 2026-09-20 |
| 대상 시스템 | Tizen AI OS |
| 라이브러리 | `consent` — 공개 C API, 내부 C++ 구현 |
| 데몬 | `consentd` — C++ 구현 |
| 주요 연동 | argo, Capability Manager, Context Engine, Installer, 승인 UI |
| 저장소 | SQLite3 기반 `consent.db` |
| 이벤트 루프 | GLib `GMainLoop` / `GMainContext` |
| IPC endpoint | Unix domain stream socket `/run/.consentd.sock` |
| IPC 직렬화 | bundle의 `parcel` library와 C++ `Parcelable`, 공통 IDL 기반 생성 코드 |
| 기동 | `consentd.socket` → `consentd.service`, systemd socket activation |
| CEP 식별자 | 미배정 |
| 근거 | 본 대화에서 논의한 요구와 설계안 |

> 이 문서는 대화 내용을 통합한 설계 제안서이다. 사내 CEP 양식이나 CEP의 공식 약어 정의는 제공되지 않았으므로 일반적인 기술 제안서 구조를 사용한다. 사용자 명시 요구, 대화 중 제안한 설계, 구현 전 결정 사항을 구분한다. 코드·스키마·필드 이름은 별도 표시가 없으면 검토용 초안이며 구현 완료를 의미하지 않는다.

**읽기 안내:** 요구·책임은 1~5절, 승인·세션 수명은 6절, C API는 7절, 시나리오는 8~10절, 다국어는 11절, 캐시·대화 데이터·삭제는 12절, DB는 13절, 운영·검증·미결정 사항은 14~22절에 정리한다. v0.2의 주요 추가 내용은 6.1~6.8, 7.7, 9.6~9.10, 12.5~12.11 및 세션·데이터 관련 SQL이다.

v0.3은 4.2~4.10의 GLib 실행·스레드·잠금·소켓 프로토콜, 14.5~14.8의 systemd unit·FD 수명·종료, 15.1~15.4의 ucred·로그·인증에 대한 구현 지침을 추가한다. GMainLoop, 서브스레드 연결 처리, endpoint, socket activation과 peer credential 사용은 사용자 확정 요구다. 구체적인 스레드 개수·unit의 계정·큐 상한은 제품 환경에 맞춰 확정할 제안값이다.

v0.4는 사용자 지시에 따라 client–consentd 통신에 `~/tizen/platform/core/base/bundle`의 parcel library를 사용하도록 확정한다. 기존 JSON wire 제안과 구현 중 검토한 GVariant wire 선택을 대체한다. 사용자가 허용한 통신 IDL과 간단한 compiler는 PO가 공통 메시지 규격을 유지하는 방법으로 채택했다. 4.8절의 parcel·IDL 계약은 구현 지침이며, 생성기·새 wire·GBS·emulator 검증 완료를 뜻하지 않는다. 상세 결정과 실제 검증 상태는 별도 결정 기록과 개발 가이드에서 구분한다.

v0.5는 11.5절에 기존 승인 필드와 결합한 typed template v1의 구현 계약을 추가한다. 일반적인 복합 scope 언어나 전체 국제화 formatter의 구현 완료를 의미하지 않는다. PO 결정은 `decisions.ko.md`의 D-10, 실제 검증은 `verification.ko.md`에서 구분한다.

## 1. 개요

Tizen AI OS에서 사용자 승인을 요청·확인·저장하는 공통 프레임워크를 도입한다. `consentd`가 정책, 승인 요청, 사용자 결정, SQLite3 저장소를 소유한다. 각 프로세스는 C API 라이브러리 `consent`를 통해 데몬과 통신한다.

사용자 승인 요청은 `argo`만 시작한다. Capability Manager와 Context Engine은 실제 요청에 필요한 승인 조건을 확인하고, 충족하면 처리하며 충족하지 않으면 실행을 차단하고 사유를 반환한다. 승인 UI는 데몬이 관리하는 요청을 표시하고 사용자의 선택을 제출한다.

승인 요청과 확인에 동기·비동기 API를 모두 제공한다. 동기 API는 기본 이름을 사용하며 비동기 API에만 `_async`를 붙인다. 비동기 API는 기존 승인으로 즉시 허용·거부가 가능하더라도 결과를 callback으로 전달한다.

‘항상 허용’ 승인에는 프로세스 내부 캐시를 적용한다. 캐시는 반복 승인 요청의 IPC를 줄이며, 최종 데이터·기능 접근은 해당 서비스가 데몬의 최신 상태로 확인한다. 기능 실행, 앱 데이터, Context 데이터, 데이터 전달을 개별 승인 조건으로 모델링하고, 실행 중 발견되는 추가 승인도 같은 작업에 연결한다.

v0.2부터 사용자와 agent의 논리적 대화 세션, 실제 connection, 세션 승인과 대화 데이터 보관을 구분한다. 같은 대화에서 승인된 데이터를 후속 질문에 사용할 수 있도록 SESSION 승인과 세션 캐시를 기본 범위에 포함한다. 데이터 접근 승인 기간과 조회 결과의 보관·사용 기간은 독립적인 정책으로 관리한다. consentd는 정책·세션·허가 메타데이터를 관리하고, argo와 데이터 보유자가 실제 데이터·요약·모델 문맥의 수명을 집행한다.

데몬은 C++에서 GLib/GIO API를 사용한다. main thread의 GMainLoop가 listener와 생명주기를 관리하고, 클라이언트 연결의 읽기·쓰기·peer credential 처리는 고정된 I/O 서브스레드에서 수행한다. 짧은 계산 작업에는 상한이 있는 GThreadPool을 사용한다. 공유 critical section은 소유 스레드 원칙과 GMutex 중심의 잠금으로 보호한다. 통신은 `/run/.consentd.sock`의 AF_UNIX/SOCK_STREAM이며, systemd가 생성한 listener FD를 인계받는다.

## 2. 결정 상태와 범위

### 2.1 사용자 명시 요구

| ID | 요구 |
|---|---|
| U-01 | 라이브러리 이름은 `consent`, 데몬 이름은 `consentd`이다. |
| U-02 | 데몬은 C++, 라이브러리는 C API와 내부 C++ 구현을 사용한다. |
| U-03 | 설치·등록, 삭제, 업데이트, 사용자 승인 요청, 승인 확인, SQLite3 관리를 지원한다. |
| U-04 | 사용자 승인 요청은 argo만 수행한다. Capability Manager와 Context Engine은 확인 후 처리·거부한다. |
| U-05 | 승인 UI에 표시할 문구의 다국어 지원이 필요하다. |
| U-06 | 승인 요청과 승인 확인 모두 SYNC/ASYNC를 제공한다. 동기 함수에는 `_sync`를 붙이지 않는다. |
| U-07 | ASYNC의 ALLOWED·DENIED 결과도 callback으로 받는다. |
| U-08 | ‘항상 허용’은 API 내부 프로세스 캐시로 바로 확인할 수 있어야 한다. |
| U-09 | Context Engine의 기존 정보 등급 0~2를 고려하고 민감 정보를 한 단계 더 구분할 수 있어야 한다. |
| U-10 | Capability Manager가 연계하는 앱 데이터 권한과 승인 후 추가 승인을 확장 가능하게 지원한다. |
| U-11 | 사용자와 agent의 대화를 유지하기 위해 세션에 연결된 승인과 이미 승인받아 조회한 데이터를 일시적으로 유지·재사용한다. |
| U-12 | connection 종료·재연결, 논리적 세션 종료, 데이터 보관·삭제 책임을 구체화한다. |
| U-13 | consentd는 GMainLoop를 사용하며 client 연결 처리를 서브스레드에서 수행한다. async dispatch나 thread pool을 활용할 수 있다. |
| U-14 | 공유 critical section을 mutex 또는 필요한 경우 recursive mutex로 보호한다. |
| U-15 | Unix domain socket 경로는 `/run/.consentd.sock`으로 한다. |
| U-16 | systemd service와 socket unit으로 socket activation을 지원한다. |
| U-17 | socket의 peer credential/ucred를 이용해 client PID·UID 등의 정보를 얻고 로그로 출력한다. |
| U-18 | GLib/GIO API를 활용해 이벤트·스레드·동기화·I/O·종료를 구현한다. |
| U-19 | client와 consentd 사이의 통신은 bundle의 parcel library로 Parcelable 데이터를 교환한다. |
| U-20 | 필요한 통신 IDL과 간단한 compiler를 검토·반영할 수 있으며 설계 문서를 정비해 개발을 계속한다. |

### 2.2 본 CEP의 제안

- Level 3을 고민감 정보 등급으로 추가한다. 기존 Level 0~2의 정확한 의미는 Context Engine 정의와 정합한다.
- `consentd`만 DB에 접근한다. 캐시 무효화 이벤트와 승인 리비전을 제공한다.
- `check`를 상태 조회 QUERY와 실행 직전 AUTHORIZE 모드로 구분한다.
- 복수 필수 승인 조건은 AND로 평가한다. 추가 승인은 독립된 결정으로 저장한다.
- 승인 문구는 등록 리소스로 관리하고, 공통 UI 문구는 승인 UI가 관리한다.
- 일회성 승인 사용과 복수 조건의 최종 확인을 하나의 DB 트랜잭션에서 처리한다.
- SESSION 승인과 세션 승인 캐시를 기본 기능으로 제공한다. connection과 논리적 대화 세션을 구분한다.
- 데이터 취득 권한과 취득한 결과의 보관·재사용 권한을 분리하고, 원본·파생 데이터의 출처를 추적한다.
- 세션 종료 시 접근·생성을 먼저 차단하고 데이터 삭제 완료는 보유자별 ACK로 추적한다.
- 장기 client 연결은 고정 I/O 서브스레드의 GMainContext에서 multiplex하고, 짧은 작업만 GThreadPool에 배정한다.
- 공유 상태는 GMutex와 소유 스레드 원칙으로 보호하며 GRecMutex는 필요한 재진입 구간에 제한한다.
- U-19를 구현하는 공유 메시지는 작은 IDL을 원본으로 두고 C++ Parcelable 코드를 결정적으로 생성한다. 이는 U-20에 따라 PO가 채택한 구현 결정이며 범용 RPC compiler 도입을 뜻하지 않는다.

### 2.3 목표

1. 사용자 선택과 실제 실행 허용 판단을 일관되게 연결한다.
2. UI 대기 때문에 argo나 데몬의 이벤트 처리가 정지하지 않도록 한다.
3. 지속 승인 요청의 반복 비용을 줄인다.
4. 새로운 앱 데이터와 자원을 추가할 때 공통 승인 API를 유지한다.
5. 언어 변경, 앱 업데이트, 승인 철회, 프로세스 재시작에도 의미가 일치하도록 한다.

### 2.4 비목표

- consent를 통해 기존 플랫폼 보안 정책, 샌드박스, 접근 제어를 우회하지 않는다. 사용자 승인과 플랫폼 접근 권한이 모두 충족되어야 한다.
- consentd는 Action 실행기, Context 저장소 또는 에이전트 추론 런타임이 아니다.
- 앱 내부의 임의 메모리·파일 접근을 자동 감시하거나 통제하는 기능을 제공하지 않는다.
- 여러 앱의 실행을 분산 트랜잭션으로 만들거나 정확히 한 번 실행을 보장하지 않는다.
- 번역을 LLM이 런타임에 생성하는 기능은 본 범위에 포함하지 않는다.

## 3. 용어와 신원 모델

| 용어 | 정의 |
|---|---|
| User / Profile | 승인의 적용 대상 사용자 또는 프로필 |
| Requester | 승인 요청을 시작하는 인증된 IPC 호출자. 사용자 승인 요청은 argo로 제한 |
| Subject | 실제로 승인 권한을 사용하는 앱·에이전트·서비스 |
| Provider | 기능 또는 데이터를 제공하는 앱·서비스 |
| Recipient | 데이터가 전달되는 별도 수신 주체. 전달 작업에서 사용 |
| Definition | 등록된 승인 항목. 자원, 정책, 등급, 문구, 허용 scope를 정의 |
| Requirement | 특정 작업이 충족해야 하는 하나의 구체적인 승인 조건 |
| Request | 사용자 결정을 얻기 위한 수명 있는 승인 요청 |
| Decision | 허용·거부 등 확정된 판단. 사용자 선택 또는 기존 정책에 의해 결정 |
| Grant | 재사용 조건을 가진 허용 결정. 일회성·기간·세션·지속 허용을 포함 |
| Operation | 사용자 의도에 따른 실제 작업. 하나 이상의 requirement를 포함 |
| Continuation | 서비스가 보류 중인 작업을 안전하게 재개하기 위한 식별자 |
| Conversation Session | 사용자·프로필·Subject와 결합한 논리적 대화 단위. 여러 Operation을 포함 |
| Connection | 사용자 접점의 인증된 실제 연결. consentd 내부 IPC 연결과 구분 |
| Session Generation | 재연결·중단·종료 등 상태 전이에서 증가하는 세대. 오래된 실행·이벤트 구분 |
| Data-use Permit | 적법하게 취득한 특정 데이터의 보관·재사용을 허용하는 메타데이터 |
| Artifact | 보관 중인 도구 결과, 대화 메시지, 요약, 추출 사실 등 데이터 객체 |
| Holder | Artifact의 실제 메모리·저장소를 소유하며 접근 차단과 삭제를 수행하는 주체 |

Requester와 Subject는 같을 수도 있지만 반드시 같지는 않다. argo가 앱을 대신해 요청하는 경우, Subject는 인증된 위임 관계에서 결정한다. 외부 입력의 `subject_id`, `profile_id`, `provider_id`를 그대로 신뢰하지 않는다.

## 4. 전체 구조와 책임

| 구성 요소 | 책임 | 허용하지 않는 동작 |
|---|---|---|
| argo | 요구 조건 수집, 승인 요청, 작업 재개·취소, 대화 세션 연계, 대화 데이터·모델 문맥의 사용 및 삭제 집행 | 사용자 선택을 임의로 생성하거나 승인 결정 직접 저장 |
| consent 라이브러리 | C ABI, IPC, SYNC 대기, ASYNC callback, 캐시, 결과 수명 관리 | 클라이언트에서 DB 직접 수정 |
| consentd | 호출자 인증, 정의·세션·보관 정책 관리, 승인 평가, UI 연동, 결정·사용·정리 상태 저장, 변경 이벤트 | 실제 Action 실행 또는 원문 대화 데이터 저장 |
| 승인 UI | 사용자 언어 조회, 등록 문구 표시, 사용자 선택 제출 | 독립적인 승인 요청 생성 또는 승인 범위 확대 |
| Capability Manager | Action 및 알려진 앱 데이터 요구 조건 평가, 실행 직전 확인, 부족한 조건 반환 | 승인 팝업 직접 요청 |
| Context Engine | 데이터 분류, 요구 범위 산출, 접근 직전 확인, 실제 데이터 접근 통제 | argo 대신 사용자 승인 요청 |
| Installer | 패키지 소유권과 등록 자료 검증, 등록·갱신·삭제 요청 | 사용자의 허용 결정을 임의로 생성 |
| 앱의 데이터 제공 지점 | 동적 데이터 접근 요구 산출, 차단, 재개, 필요 시 check 호출 | 미승인 데이터를 먼저 읽고 나중에 승인 요청 |
| 승인 설정 UI | 승인 목록 조회·철회. 향후 관리 경로 | 데이터 접근을 수반하는 새 승인 요청 생성 |
| 신뢰하는 세션 제어 주체 | argo 또는 플랫폼 대화 관리 서비스. 인증된 접점 연결과 논리적 세션 결합·종료 통지 | 단순 클라이언트 주장으로 다른 사용자 세션에 참가시키기 |
| Data holder / Context assembler | 데이터 출처·보관 수명 관리, 후속 모델 입력 구성, 정리 ACK | 허가가 만료된 데이터를 요약·프롬프트에 재삽입 |

기본 통신 경로는 `프로세스 → consent(C API/C++) → Unix domain socket /run/.consentd.sock → consentd`이다. systemd socket activation을 사용하며, 접수된 연결의 SO_PEERCRED로 커널이 제공하는 peer PID·UID·GID를 얻는다. C callback 포인터는 프로세스 밖으로 보내지 않는다. peer credential을 앱 역할·위임·SMACK 정책과 연결하는 상세 규칙은 플랫폼 담당 조직과 정합한다.

### 4.1 consentd 내부 모듈 제안

| 모듈 | 책임 |
|---|---|
| IpcEndpoint | 접수 응답, 완료 이벤트, 호출자 신원 확인 |
| DefinitionRegistry | 설치·업데이트·삭제 및 자원 소유권 검증 |
| PolicyEvaluator | 플랫폼 정책, 사용자 결정, 범위, 등급, 버전 평가 |
| RequestCoordinator | 요청 상태, 중복 방지, 기한, 취소, 추가 승인 관계 |
| PromptService | 요청에 고정된 정책·문구·인자의 UI 조회와 응답 검증 |
| ConsentRepository | SQLite3 트랜잭션, 마이그레이션, 복구 |
| ChangePublisher | 데몬 epoch, 리비전, 캐시 무효화와 결정 이벤트 |
| SessionManager | session·connection 결합, 중단·재개·종료·유휴 및 최대 수명 |
| DataUseCoordinator | 데이터 사용 허가, 보유자 등록, 출처 관계, 정리 이벤트와 ACK |

DB 쓰기는 초기 구현에서 하나의 직렬화된 작업 경로를 사용한다. 사용자 응답을 기다리는 동안 DB 트랜잭션이나 데몬 작업 스레드를 점유하지 않는다.

### 4.2 GLib 실행 모델

| 실행 영역 | 기본 구성 | 책임 | 금지하는 장기 대기 |
|---|---|---|---|
| Main thread | GMainContext + GMainLoop | listener 접수, 시작·종료, signal, 정책·요청·session 상태 투영, timer, UI 연동 조정 | client read/write, 사용자 응답, SQLite commit 대기 |
| I/O 서브스레드 | 고정 개수 GThread, 각자 GMainContext + GMainLoop | 연결 소유, ucred 취득, async read/write, frame 조립, 송신 큐, 단절 감지 | 사용자 응답·DB 결과를 blocking wait |
| 작업 pool | 상한 있는 GThreadPool | parcel payload·IDL schema 검증, scope 정규화 등 짧고 독립적인 작업 | 연결 수명 전체 점유, 승인 팝업 대기 |
| DB executor | 전용 GThread + GAsyncQueue | SQLite 연결 소유, 모든 초기 read/write 직렬화, transaction·복구 | 사용자 응답, socket I/O |

기본안은 I/O 서브스레드 1개에서 여러 연결을 비동기로 관리하고, 필요할 때 고정 개수를 늘리는 것이다. 작업 pool 크기는 실측으로 정한다. 연결 수만큼 스레드를 무제한 생성하지 않는다. 접수 자체는 main에서 수행할 수 있지만, incoming handler에서는 짧은 admission·참조 전달만 하고 실제 client 프로토콜 처리는 서브스레드로 넘긴다.

GSocketService는 incoming signal로 연결을 전달한다. handler에서 대기하지 않고 처리 소유권을 넘기는 방식으로 사용한다. [GIO GSocketService](https://docs.gtk.org/gio/class.SocketService.html)

### 4.3 GMainContext와 객체 소유권

1. main context를 초기화하고 필요한 thread-default context를 지정한 상태에서 GSocketService를 생성한다.
2. 각 I/O worker는 `g_main_context_new()`와 `g_main_loop_new()`로 독립 context·loop를 만들고 자신의 thread-default로 push한 뒤 실행한다.
3. main incoming handler는 GSocketConnection의 참조를 확보해 I/O worker에 넘긴다. dispatch 실패 시 참조와 연결을 정리한다.
4. I/O worker가 연결을 등록한 이후에는 같은 worker만 read/write 시작, stream 상태, send queue, close를 변경한다. 다른 스레드는 명령·결과 메시지를 게시한다.
5. DB job과 worker job은 immutable request snapshot을 받는다. 완료 이벤트에는 client_instance_id, protocol_request_id, session_generation과 필요한 정책 revision을 포함한다.
6. main은 완료 결과를 현재 요청·session 상태와 비교하고, 유효한 결과만 해당 I/O owner에 게시한다. 종료된 연결에 결과를 새 연결로 잘못 전달하지 않는다.

GObject의 참조 수 관리만으로 객체의 모든 메서드가 thread-safe가 되는 것은 아니다. 소유권 이관 전·후에 두 스레드가 같은 stream을 동시에 조작하지 않는다. C++ RAII wrapper로 GObject, GError, GSource, GMainContext 참조를 관리하고 수명 종료 스레드를 명시한다.

### 4.4 cross-thread dispatch와 callback

일반적인 context 이동에는 `g_main_context_invoke_full()`을 사용할 수 있다. 다만 이 계열 함수는 호출 스레드가 context를 소유하는 경우 함수를 직접 실행할 수 있으므로, 항상 비동기 큐잉된다고 가정하지 않는다. [GLib MainContext.invoke](https://docs.gtk.org/glib/method.MainContext.invoke.html)

공개 ASYNC API의 ‘호출 반환 이후 callback’ 계약에는 명시적으로 GSource를 만들어 지정 dispatcher에 attach하는 경로를 사용한다. 아래 helper는 attach 자체에서 handler를 호출하지 않는다. dispatcher의 소유 스레드에서 ASYNC API를 호출하고, 그 호출 중 nested main-loop iteration을 하지 않는 7.5의 계약과 함께 사용해야 함수 반환 뒤 callback이 보장된다. 다른 스레드의 루프에 attach한 source는 곧바로 실행될 수 있으므로 attach만으로 반환 순서를 보장한다고 해석하지 않는다.

```cpp
#include <glib.h>

// context의 수명은 호출자가 보장한다. callback data의 해제는 destroy가 담당한다.
static void post_to_context(GMainContext* context,
                            GSourceFunc callback,
                            gpointer data,
                            GDestroyNotify destroy)
{
    GSource* source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(source, callback, data, destroy);
    g_source_attach(source, context);
    g_source_unref(source);
}
```

일회성 callback은 G_SOURCE_REMOVE를 반환한다. 운영 구현은 게시 admission 상한, 종료 여부, source 등록부를 추가한다. 중단된 loop에 source를 무한 게시하지 않으며 종료 때 남은 source를 destroy해 data를 해제한다. GDestroyNotify는 특정 스레드에서 실행된다고 가정하지 않고, 스레드 종속 객체의 최종 해제는 해당 owner에 명시적으로 위임한다.

타이머와 signal도 목적 context에 명시적으로 attach한다. `g_timeout_source_new()` 또는 `g_timeout_source_new_seconds()`, `g_unix_signal_source_new()`를 사용하고, worker에서 전역 default context로 잘못 등록되는 편의 함수를 무심코 사용하지 않는다.

### 4.5 client 연결 처리 순서

| 단계 | 위치 | 동작 |
|---|---|---|
| 1 | main incoming | 종료·전체 연결 admission 확인, client instance ID 발급, 연결 ref 확보 |
| 2 | I/O worker | 실제 accepted socket에서 SO_PEERCRED 취득, PID·UID·GID 로그 |
| 3 | I/O worker / 검증 job | peer 신원에 따른 연결·역할 quota와 protocol handshake 검증 |
| 4 | I/O worker | 길이 제한 내 frame 수신, immutable payload 생성 |
| 5 | 작업 pool | payload schema·scope 정규화. 결과만 main에 게시 |
| 6 | main → DB executor | 요청 의미·현재 상태 확인, 최종 policy/transaction 작업 예약 |
| 7 | DB executor → main | commit된 revision과 결과 게시. main projection 갱신 |
| 8 | main → I/O worker | receipt·완료·변경 이벤트를 해당 연결의 송신 큐에 넣음 |
| 9 | I/O worker | async write로 순서대로 전송. 오류·EOF 시 소유 연결 정리 |

접수 후 request가 PENDING이면 관련 작업 job은 반환한다. 승인을 기다리는 것은 DB의 요청 상태·타이머·이벤트이며 GThreadPool thread가 아니다. 같은 연결에서 취소·check·변경 구독 메시지를 계속 처리할 수 있어야 한다.

### 4.6 GThreadPool과 대안

`g_thread_pool_new()`와 `g_thread_pool_push()`를 사용해 짧은 CPU 작업을 위임한다. thread 수 상한과 큐 길이 상한은 다르다. pool thread 수만 제한해도 job이 무한 대기열에 쌓일 수 있으므로 별도 admission counter를 mutex로 보호하고, 상한 초과 시 BUSY 계열 오류를 반환한다. [GLib ThreadPool](https://docs.gtk.org/glib/struct.ThreadPool.html)

GThreadedSocketService는 연결을 worker에서 처리하는 간단한 대안이다. 그러나 장기 구독 연결이 worker를 계속 점유하면 모든 worker 사용 중 새 접수를 멈춘다. 승인 UI의 새 연결까지 지연될 수 있으므로 현재의 장기 연결·캐시 무효화 구독 기본안에는 고정 I/O loop 방식을 사용한다. 연결 수가 작고 수명이 짧은 제품 변형에서는 별도 용량 검증 후 사용할 수 있다. [GIO ThreadedSocketService](https://docs.gtk.org/gio/class.ThreadedSocketService.html)

GTask는 결과를 특정 context로 돌려주는 작업 추상화로 선택할 수 있다. 직접 생성한 async job이나 std::async를 사용하더라도 동일한 thread·queue 상한과 취소·join 계약이 필요하다. 기본 구현에서는 GLib 중심의 thread pool을 우선하며, fire-and-forget으로 객체 수명을 잃는 작업은 만들지 않는다.

### 4.7 mutex·recursive mutex·critical section

소유 스레드에서만 접근하는 상태는 메시지 전달로 직렬화하고, 여러 스레드가 직접 공유하는 저장소·counter·snapshot만 mutex로 보호한다. 기본은 GMutex이다. 같은 스레드에서 재귀 진입이 설계상 필요한 좁은 영역에 한해 GRecMutex를 사용할 수 있으며 이유를 코드에 명시한다. GRecMutex는 다른 스레드와의 교착이나 잠금 순서 문제를 해결하지 않는다. [GLib RecMutex](https://docs.gtk.org/glib/struct.RecMutex.html)

| 상태 | 보호 방식 | 잠금 중 하지 않는 작업 |
|---|---|---|
| client registry·admission counter | GMutex + RAII guard | socket read/write, worker 종료 대기 |
| 공유 request/session snapshot | main owner. 공유 조회 snapshot 교체 시 GMutex | callback, UI 호출, DB commit |
| consent 라이브러리 승인 캐시 | GMutex, entry 복사·revision 갱신을 짧게 수행 | consentd IPC, 사용자 callback |
| client pending map | 라이브러리 I/O owner 또는 좁은 GMutex | callback 호출 |
| connection send queue·stream | 해당 I/O worker만 접근 | 다른 스레드에서 stream 직접 조작 |
| SQLite connection·statement | DB executor 단독 소유 | 다른 worker의 동일 connection 사용 |
| 작업 admission·shutdown flag | GMutex 또는 명시된 atomic | join·condition wait 전에 필요한 lock 유지 |

중첩 잠금은 기본적으로 피한다. 불가피하면 `lifecycle → registry → client-local` 순서를 문서화하고 반대 순서를 금지한다. DB queue 작업은 잠금 밖에서 게시하고 transaction 동안 위 process mutex를 유지하지 않는다. GAsyncQueue 자체 잠금과 사용자 mutex의 중첩도 피한다.

```cpp
#include <glib.h>

class GMutexGuard final {
public:
    explicit GMutexGuard(GMutex& mutex) : mutex_(mutex) {
        g_mutex_lock(&mutex_);
    }
    ~GMutexGuard() { g_mutex_unlock(&mutex_); }
    GMutexGuard(const GMutexGuard&) = delete;
    GMutexGuard& operator=(const GMutexGuard&) = delete;
private:
    GMutex& mutex_;
};
```

GMutex는 공유 전에 초기화하고 모든 사용자·job 종료 후 clear한다. C++ 객체 복사로 mutex를 복제하지 않는다. GCond를 사용하는 제한된 SYNC wait에서는 predicate를 while로 검사하고 기한을 적용한다. recursive mutex를 condition wait의 대체로 사용하지 않는다. C/C++ callback 경계에서 예외를 잡아 오류로 변환하고 예외가 GLib의 C stack 밖으로 전파되지 않게 한다.

### 4.8 UDS framing·동시 요청·backpressure

endpoint는 pathname 형식의 AF_UNIX/SOCK_STREAM `/run/.consentd.sock`이다. 이름의 점은 파일 이름의 일부이며 abstract socket을 뜻하지 않는다. SOCK_STREAM은 message 경계를 보존하지 않으므로 한 번 read한 데이터가 한 요청이라고 가정하지 않는다. [Linux unix(7)](https://man7.org/linux/man-pages/man7/unix.7.html)

wire 형식은 `4-byte unsigned big-endian payload length + parcel payload`로 한다. payload는 bundle의 `tizen_base::Parcel`과 `tizen_base::Parcelable`을 사용하는 공통 메시지다. protocol version, message kind, protocol_request_id, method·params 또는 result·error를 명시적으로 표현한다. 정수와 길이는 `Parcel::SetByteOrder(true)`로 big-endian을 지정하며 native C 구조체의 메모리 표현을 전송하지 않는다. JSON/GVariant blob을 감싼 것을 Parcelable 메시지 구현으로 취급하지 않는다. 아래 JSON 예시는 도메인 모델 설명용이며 IPC 인코딩 규격이 아니다.

- header·body의 분할 수신, 여러 frame의 합쳐진 수신, EOF 중간 종료를 처리한다.
- 크기 검증 전 payload 길이만큼 메모리를 할당하지 않는다. 최대 frame bytes·IDL record 깊이·문자열 길이·배열 및 requirements 개수·연결별 in-flight 수를 제한한다.
- 한 연결은 하나의 reader와 하나의 직렬 송신 큐를 사용한다. 여러 thread가 같은 stream에 frame을 직접 write하지 않는다.
- 읽기·쓰기 각각 하나의 진행 중 async 작업을 기본으로 하고 callback 완료 후 다음 단계로 간다. read와 write는 별도의 방향으로 진행할 수 있다.
- 접수 응답과 최종 완료가 역전되지 않게, 같은 request의 receipt를 먼저 송신 큐에 넣는다. 이미 완료된 결과도 이후 frame으로 큐잉한다.
- 최종 결과, 무효화 이벤트, 취소 응답은 protocol_request_id·event revision으로 구별한다. caller PID를 payload의 신원으로 사용하지 않는다.
- slow reader의 출력 큐 bytes가 상한을 넘으면 그 연결을 종료한다. 캐시 consumer는 단절을 감지하면 UNSYNCED가 되어 데몬 재확인 전 허용 cache를 사용하지 않는다.
- handshake·불완전 frame에는 별도 기한을 둔다. 유효한 장기 구독 연결의 단순 idle을 사용자 승인 기한과 혼동해 닫지 않는다.

GInputStream/GOutputStream의 async read/write 계열과 GCancellable을 사용한다. 송신 버퍼는 async 완료까지 보관한다. partial I/O를 직접 구현할 경우 offset과 WOULD_BLOCK 처리를 유지한다. request마다 GCancellable을 무분별하게 공유하지 않고, 연결 단위 취소와 operation 취소의 범위를 구별한다.

#### 4.8.1 Parcelable 메시지와 경계 검사

기존 AUL `ac581e7`의 `src/aul/socket/packet.hh`처럼 메시지 객체가 `Parcelable`을 구현하고 `WriteToParcel()`/`ReadFromParcel()`에서 필드 순서를 정의한다. `Parcel::WriteParcelable()`과 `GetData()`/`GetDataSize()`로 frame을 구성한다. 공개 C API는 내부 C++ 타입이나 wire buffer의 소유권을 호출자에게 노출하지 않는다.

- 생성된 reader는 모든 정수 읽기의 오류를 확인하고 길이·개수를 할당/반복 전에 검사한다. 문자열은 wire 길이, 남은 바이트, 끝 NUL, 내부 NUL 부재, UTF-8을 확인한다.
- 조사한 parcel 구현의 `ReadString()`은 선언 길이에 따라 먼저 할당하며 안전한 문자열 종료를 별도로 검증하지 않는다. 비신뢰 frame에는 이를 직접 적용하지 않고 Parcel의 정수·byte 읽기를 사용하는 제한된 공통 reader를 둔다.
- 조사한 `ReadParcelable()`은 가상 `ReadFromParcel()` 호출 후 성공을 반환한다. 따라서 생성 객체가 decode 오류/유효성을 별도로 보존하고 호출자가 검사해야 한다. 이 반환값만으로 메시지를 신뢰하지 않는다.
- 모든 필드를 읽은 뒤 reader 위치와 payload 크기가 일치해야 한다. 잘린 필드, 알 수 없는 버전·메시지 종류, 중복 필드, 남은 바이트, 잘못된 correlation은 거부한다.
- 정렬과 직렬화 순서를 고정해 작업 fingerprint를 안정적으로 계산한다. 임의 padding이나 포인터 값은 직렬화하지 않는다. 초기 primitive는 고정 폭 정수와 길이가 제한된 문자열로 시작하며 bool/float/native struct의 ABI 표현에 의존하지 않는다.
- Parcel과 STL의 메모리 할당 예외는 C ABI·GLib callback 경계 안에서 처리한다. 초기화 실패나 부분 decode 객체를 성공 결과로 노출하지 않는다.

#### 4.8.2 작은 IDL과 compiler

IDL은 공유 메시지의 버전·필드·타입·상한에 대한 단일 원본이다. 단순한 선언 문법 또는 JSON 형식의 IDL로 고정 폭 정수, bounded UTF-8 문자열, record, bounded array를 표현한다. Python 표준 라이브러리 기반의 작은 compiler로 C++ Parcelable 선언·정의와 검증 경로를 생성한다. 범용 서비스 실행기나 네트워크 호출 코드는 생성하지 않는다.

IDL과 compiler 소스는 `src/` 아래에 둔다. CMake가 빌드 디렉터리에 생성물을 만들고 client와 consentd가 같은 결과를 링크한다. Python은 빌드·테스트 의존성으로 제한하며 설치된 라이브러리와 데몬의 실행 시 의존성이 아니다. 생성 코드에도 기존 appfw 형식의 copyright와 Apache-2.0 전체 고지를 넣는다.

compiler는 중복 이름/필드, 알 수 없는 타입, 잘못된 상한, 지원하지 않는 재귀 구조를 오류로 처리한다. 입력에서 임의 코드를 평가하거나 실행하지 않는다. 동일한 IDL은 timestamp 없이 동일한 출력을 생성해야 하며 실패 시 부분 생성물을 정상 산출물로 남기지 않는다. 정확한 문법·메시지 목록·필드 번호/순서·호환성 규칙은 실제 schema와 함께 문서화한다. wire 변경 시 version과 시험 벡터를 갱신하며 기존 payload를 새 형식으로 추측해 읽지 않는다.

생성 코드의 roundtrip 외에도 잘림, 초과 길이/개수, 잘못된 문자열 종료, 중복, trailing bytes, 잘못된 version, 분할 frame을 검증한다. compiler의 결정성·오류 진단과 CMake 재생성도 시험한다. 호스트 생성 성공만으로 대상 parcel ABI·GBS 빌드·emulator 통신 검증을 대체하지 않는다.

### 4.9 DB와 main projection의 일관성

DB executor는 GAsyncQueue에서 job을 꺼내 하나의 SQLite connection으로 처리한다. queue 자체는 자동으로 유한 용량이 되는 것이 아니므로 admission 상한을 둔다. 초기에는 read도 같은 executor로 처리해 transaction 일관성과 구현 단순성을 확보한다.

worker에서 미리 검증한 snapshot은 성능을 위한 사전 판단이다. 최종 AUTHORIZE·사용 기록·철회·세션 종료는 DB transaction 안에서 최신 상태와 generation을 다시 검사한다. DB commit 순서대로 revision이 증가하며 main은 결과 revision에 따라 투영 상태를 갱신한다. 오래된 job 결과로 새로운 세션 종료·철회를 덮어쓰지 않는다.

DB 결과를 main으로 보내기 전에 DB mutex 또는 transaction을 끝낸다. main은 DB 응답을 blocking wait하지 않는다. DB executor도 main callback의 완료를 기다리지 않는다. 기존 13절의 단일 writer와 transaction 경계는 이 executor에서 구현한다.

### 4.10 리소스 예산과 장애 격리

연결 수, UID별 연결 수, 인증 전 연결 수, pending request 수, worker job 수, DB queue 길이, 송신 queue bytes를 각각 제한한다. 장기 연결을 한 I/O loop에서 multiplex하므로 client 수가 thread 수가 되지 않는다. UI·Installer 등 필요한 제어 경로가 일반 요청 폭주로 완전히 막히지 않도록 인증된 역할에 대한 admission 예산과 공정 스케줄링을 마련한다.

상한값은 TV의 실제 메모리·동시 서비스 수로 결정한다. 소켓 파일의 DAC group만으로 role을 추정하지 않는다. 모든 연결에서 credential 확인을 먼저 수행하고 protocol role을 별도 검증한다. 파싱 오류·프레임 초과·credential 실패는 해당 연결을 종료하며 daemon 전체에 g_error 같은 fatal logging을 사용하지 않는다.

## 5. 자원과 승인 요구 모델

### 5.1 공통 조건

| 필드 | 의미 |
|---|---|
| `requirement_id` | Operation 안의 개별 요구 조건 식별자 |
| `resource_type` | `capability`, `app-data`, `context-data` 등 등록된 타입 |
| `provider_id` / `resource_id` | 제공 주체와 실제 자원 |
| `operation` | 실행·읽기·수정·삭제·전달 등 자원별 허용 작업 |
| `scope` | 날짜 범위, 레코드, 사용자, 대상 기기 등 구체적 접근 범위 |
| `purpose` | 등록·검증 가능한 사용 목적 |
| `recipient_id` | 전달 대상. 비전달 작업은 없음 |
| `sensitivity_level` | 신뢰하는 데이터 소유자·정책으로 확정한 등급 |
| `policy_version` | 승인 의미를 정의하는 버전 |

Level이나 자원 ID 하나만으로 접근을 허용하지 않는다. 사용자, Subject, Provider, Operation, Scope, 목적, 수신자, 버전과 유효성을 함께 비교한다.

### 5.2 정적 요구 조건 등록 예시

```json
{
  "capability_id": "calendar-event-list",
  "consent_requirements": [
    {
      "requirement_id": "execute-calendar-list",
      "resource_type": "capability",
      "provider_id": "org.example.calendar",
      "resource_id": "calendar-event-list",
      "operation": "execute"
    },
    {
      "requirement_id": "read-calendar-events",
      "resource_type": "app-data",
      "provider_id": "org.example.calendar",
      "resource_id": "calendar-events",
      "operation": "read",
      "scope_binding": "requested-date-range"
    }
  ]
}
```

`scope_binding`은 등록된 검증·매핑 규칙의 식별자다. 임의 코드나 LLM 생성 문장을 실행하는 필드가 아니다. Capability Manager는 실제 Action 인자에서 구체적인 요구 범위를 생성한다. 자원별 scope 정규화·비교기는 등록된 구현을 사용하며, 초기 구현은 정확 일치를 기본으로 한다.

### 5.3 복수 조건 평가

- 모든 필수 조건이 ALLOWED인 경우에만 전체 결과를 ALLOWED로 반환한다.
- 하나라도 확정 DENIED이면 전체 DENIED다. 승인 가능한 나머지 조건 때문에 불필요한 팝업을 띄우지 않는다.
- DENIED가 없고 부족한 조건이 있으면 전체 CONSENT_REQUIRED다.
- 결과에는 전체 판단, 조건별 판단, 부족한 조건 목록을 포함한다.
- 평가 자체가 불가능한 통신·저장소 오류는 승인 결정과 분리한다. 실행은 차단한다.
- 여러 조건을 한 화면에 표시하더라도 각 조건의 의미와 허용 기간을 구별한다. UI 버튼 하나가 숨겨진 추가 범위를 허용해서는 안 된다.

## 6. 정보 등급과 승인 방식

다음 매핑은 제안이다. 기존 Context Engine의 Level 0~2 정의를 덮어쓰지 않으며, 실제 분류표와 정합한 뒤 확정한다.

| Level | 제안 분류 | 예시 | 기본 승인 방식 제안 |
|---|---|---|---|
| 0 | 공개·비개인 정보 | 공개 기기 기능 정보 | 별도 사용자 승인 없이 플랫폼 정책으로 판단 |
| 1 | 일반 개인화 정보 | 일반 설정·선호 정보 | 최초 승인 후 지속 허용 가능 |
| 2 | 민감 정보 | 상세 시청 기록·개인 일정·생활 패턴 | 목적·범위 명시, 기간 또는 세션 제한 우선 |
| 3 | 고민감 정보 | 정밀 위치 이력·건강 정보·사적 대화 원문 | 요청별 명시 승인, 일회성·최소 범위 우선 |

실제 내용이나 데이터 결합에 따라 등급이 달라질 수 있다. Level 3 승인이 하위 등급의 모든 자원을 허용하지 않는다. 인증 비밀 등 플랫폼이 제공을 금지한 데이터는 사용자 승인으로 허용할 수 없다.

승인 방식은 `ONCE`, `SESSION`, `TIMED`, `PERSISTENT`를 후보로 둔다. Level과 승인 방식은 별도 필드로 저장하되 정책이 허용 가능한 조합을 제한한다. ‘항상 허용’은 PERSISTENT이며 철회·정책 변경·삭제에도 무조건 유효하다는 의미가 아니다. SESSION의 범위와 수명은 명시된 플랫폼 세션 ID로 정의해야 한다.

### 6.1 승인 수명과 데이터 수명의 분리

| 축 | 질문 | 값의 예 |
|---|---|---|
| 접근 승인 | 원본을 언제까지, 몇 번 조회할 수 있는가? | ONCE / SESSION / TIMED / PERSISTENT |
| 결과 보관 | 가져온 데이터를 언제까지 보유할 수 있는가? | OPERATION / SESSION / TTL |
| 결과 사용 | 어디에서 어떤 목적으로 재사용 가능한가? | 이번 답변, 같은 대화의 후속 답변, 지정된 처리 |
| 저장 매체 | 재부팅을 넘어 디스크에 남길 수 있는가? | 초기 MEMORY_ONLY, 별도 정책의 보호된 저장 |

ONCE로 원본을 한 번 조회하고 결과는 SESSION 동안 사용하는 조합이 가능하다. PERSISTENT로 원본 조회를 허용해도 데이터 결과는 SESSION 종료 시 삭제하도록 할 수 있다. 이는 사용자가 이해할 수 있는 승인 문구와 자원 정책에 함께 명시해야 한다. ONCE 조회 승인을 근거로 자동 무제한 보관을 허용하지 않는다.

기본 데이터 보관 상한은 관련 정책의 최소값이다. 예를 들어 SESSION 보관 + 10분 TTL이면 세션 종료와 10분 만료 중 먼저 오는 시점까지다. 여기서 숫자는 구조 설명 예시이며 제품 기본값이 아니다. 취득 승인의 정상 소비·정상 만료와 명시적 철회를 구분한다. 허용된 결과 보관은 ONCE 소비 후에도 유지될 수 있지만, 명시적 철회·정책 무효화는 연결된 결과 사용 허가도 무효화하는 것을 기본으로 한다.

### 6.2 세션 식별과 connection의 구분

| 식별자 | 생성·검증 주체 | 의미 |
|---|---|---|
| session_id | consentd가 인증된 세션 제어 주체의 요청으로 발급 | 사용자 대화 세션. 임의 문자열만으로 권한 획득 불가 |
| connection_id | 신뢰하는 접점 계층이 발급·검증 | 사용자 단말·UI의 접속 인스턴스 |
| ipc_connection_id | 데몬 IPC 계층 | argo·CM·Context Engine과 데몬 사이의 통신 연결 |
| operation_id | 검증된 호출 문맥의 argo/실행 서비스 | 대화 내 개별 작업 |
| session_generation | consentd | 세션 상태 변경을 구별하는 증가값 |

argo, CM, Context Engine은 서로 다른 IPC 연결을 통해 동일한 session_id를 사용한다. 각 서비스가 해당 session을 대신 처리할 권한은 인증·위임으로 확인한다. IPC 재접속만으로 대화가 새로 생성되거나 다른 세션의 승인이 적용되지 않는다. 채팅 기록을 다시 여는 행위와 과거의 활성 승인 세션을 재개하는 행위도 구분한다.

### 6.3 연결 정책

| 정책 | 연결 끊김 시 | 용도 |
|---|---|---|
| CONNECTION_BOUND | 권한 있는 사용자 접점의 연결 종료 감지 시 세션 종료 시작 | 엄격한 연결 수명 기반 작업 |
| RESUMABLE_CONVERSATION | SUSPENDED 전이 후 제한된 유예시간 내 인증된 재연결만 허용 | 대화 연속성이 필요한 agent |

초기 모델은 하나의 primary 사용자 접점 연결을 기준으로 한다. CM·Context Engine의 support 연결은 세션을 살려두는 사용자 활동으로 간주하지 않는다. 여러 사용자 단말이 같은 세션을 동시에 활성화하는 기능은 별도 확장으로 두고, 초기에는 primary 연결 교체 시 이전 generation의 연결을 차단한다.

물리적 연결 끊김의 즉각 감지는 보장할 수 없다. 연결 종료 통지와 lease/heartbeat 만료를 조합하고 감지 지연 상한을 제품 요구로 정한다. heartbeat는 생존 신호이며 사용자 활동이 아니다. 반복 heartbeat로 유휴 만료나 절대 최대 수명을 무한 연장하지 않는다.

### 6.4 상태 모델

| 상태 | 신규 조회·데이터 재사용 | 데이터 보유 | 전이 |
|---|---|---|---|
| ACTIVE | 범위·정책·허가 확인 후 가능 | 허용 정책 내 가능 | 연결 상실 시 SUSPENDED 또는 CLOSING |
| SUSPENDED | 금지. 신규 모델 입력·후속 데이터 출력도 보류 | 유예시간·TTL 안에서 메모리 보존 가능 | 인증 재개 성공 시 ACTIVE, 만료 시 CLOSING |
| CLOSING | 금지. 새 승인·접근·재개 금지 | 사용 불가 상태로 삭제 작업 수행 | 보유자별 정리 완료 후 CLOSED |
| CLOSED | 금지 | 세션 임시 데이터 정리 완료가 확인된 상태 | 같은 세션 ID의 부활 금지 |

종료 원인은 USER_END, DISCONNECTED, IDLE_TIMEOUT, MAX_LIFETIME, LOGOUT, PROFILE_CHANGED, REVOKED, OWNER_LOST, RECOVERY_INVALIDATED 등 별도 필드로 보관한다. 한 자원에 대한 승인 철회는 기본적으로 해당 grant·데이터만 무효화한다. 세션 전체 철회·사용자 로그아웃은 모든 관련 데이터를 무효화한다.

ACTIVE 또는 SUSPENDED에서 CLOSING으로 전이하는 DB 커밋이 신규 권한 차단의 기준점이다. close API 성공은 이 차단과 정리 작업 접수가 완료됐다는 의미이며, 모든 프로세스의 데이터가 이미 삭제됐다는 의미가 아니다. 실제 정리 결과는 세션 상태·cleanup API 또는 이벤트로 확인한다.

### 6.5 재연결과 세션 세대

1. primary 접점 연결 종료를 검증하고 SUSPENDED로 전이한다. generation을 증가시키고 새 접근·사용을 차단한다.
2. argo는 진행 중 inference·스트리밍을 정지 또는 취소하고 새 입력 구성을 중단한다. 단순히 이전 generation의 결과를 나중에 출력하지 않는다.
3. 재연결 요청은 같은 사용자·프로필·Subject, 인증된 접점, 유예기간, 현재 정책, 서버가 검증하는 resume 증명을 충족해야 한다.
4. 성공하면 새 connection을 연결하고 generation을 다시 증가시킨다. 기존 연결·이전 generation의 callback, prompt, continuation은 현재 작업으로 자동 수용하지 않는다.
5. 승인 캐시를 재동기화하고 결과 데이터의 사용 가능성을 다시 확인한다. 살아 있는 데이터라도 TTL이 지났으면 재사용하지 않는다.

재연결 토큰은 선택한 인증 방식의 구현 항목이다. 사용한다면 원문을 로그에 남기지 않고, 서버 검증값·만료·재사용 방지·회전 정책을 둔다. 토큰 문자열 하나 또는 session_id를 안다는 이유로 재개하지 않는다. 재개가 실패하거나 유예시간이 끝나면 종료하고 새 세션에서 필요한 승인을 다시 얻는다.

### 6.6 세션 기한

| 기한 | 기준 | 갱신 |
|---|---|---|
| idle_timeout | 검증된 마지막 사용자 활동 | 승인된 사용자 활동만 갱신 |
| max_lifetime | 세션 최초 생성 | 기본적으로 갱신하지 않음 |
| reconnect_grace | SUSPENDED 시작 | 같은 중단의 반복 재시도로 연장하지 않음 |
| request_deadline | 각 사용자 승인 요청 | 세션 유예와 별개 |
| data_max_age | 데이터 취득 시각 | 읽기·요약·재연결로 재설정하지 않음 |

정지 상태에서도 보관 TTL과 절대 수명은 흐른다. SESSION grant가 유효해도 실제 세션이 ACTIVE가 아니면 접근할 수 없다. 세션이 살아 있어도 개별 승인이나 데이터 TTL이 끝나면 해당 자원은 사용할 수 없다.

### 6.7 재시작 기본 정책

초기 구현은 MEMORY_ONLY 대화 데이터와 실행 중 프로세스 세션을 기준으로 한다. argo 세션 소유 인스턴스가 소실되거나 기기가 재부팅되면 과거 세션을 자동 ACTIVE로 복원하지 않는다. consentd 재시작 시에도 이전 활성 세션을 보수적으로 종료·무효화하고 생존 holder가 정리하도록 한다. 이는 일반 접점 연결의 짧은 단절과 구분한다.

세션 메타데이터와 cleanup 작업을 DB에 보존하는 목적은 복구·차단·정리이며, DB 행의 존재가 세션 재활성화를 허용하지 않는다. PERSISTENT grant는 별도 정책에 따라 유지될 수 있어 새 세션에서 다시 사용할 수 있지만, 이전 세션의 임시 데이터는 자동 복원하지 않는다.

### 6.8 정책 객체 예시

다음 예시는 구조 설명용이다. 모든 시간 값은 제품 기본값이 아니며, 설치된 정책과 사용자 선택에서 검증·확정한 값만 실제 적용한다.

```json
{
  "session_policy": {
    "lifecycle_mode": "RESUMABLE_CONVERSATION",
    "idle_timeout_ms": 600000,
    "max_lifetime_ms": 3600000,
    "reconnect_grace_ms": 30000,
    "primary_connections_max": 1
  },
  "requested_consent": {
    "grant_mode": "ONCE",
    "resource_type": "app-data",
    "resource_id": "calendar-events",
    "operation": "read",
    "retention": {
      "scope": "SESSION",
      "max_retention_ms": 600000,
      "storage_class": "MEMORY_ONLY",
      "reuse_purpose": "answer-calendar-followups",
      "on_revocation": "BLOCK_AND_CLEANUP",
      "allow_cross_session": false
    }
  }
}
```

위 조합은 원본을 한 번 읽은 결과를 동일 대화에서 최장 10분 동안 후속 답변에 사용한다는 의미다. 세션 종료·철회가 먼저 오면 즉시 사용을 차단한다. 유예시간 동안 보존할 수 있어도 새 질문 처리·외부 전달은 중단한다. 재연결이나 요약은 10분을 다시 시작하지 않는다.

## 7. C API와 실행 계약

### 7.1 공개 이름

| 기능 | 동기 | 비동기 |
|---|---|---|
| 사용자 승인 요청 | `consent_request()` | `consent_request_async()` |
| 승인 여부 확인 | `consent_check()` | `consent_check_async()` |

동기 API에 `_sync` postfix를 사용하지 않는다. 초기 대화의 `consent_request_sync()`와 `consent_check_sync()`는 본 문서의 공개 API가 아니다.

### 7.2 인터페이스 초안

아래 선언은 ABI 방향을 보여준다. 구조체 접근자·생성자 전체와 오류값 숫자는 구현 설계에서 확정한다. 객체는 opaque handle로 제공해 필드 추가 시 ABI 변경을 줄인다.

```c
#ifndef TIZEN_CONSENT_H
#define TIZEN_CONSENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct consent_client *consent_client_h;
typedef struct consent_request_params consent_request_params_t;
typedef struct consent_check_params consent_check_params_t;
typedef struct consent_result consent_result_t;
typedef struct consent_check_result consent_check_result_t;
typedef struct consent_request_handle *consent_request_handle_t;
typedef struct consent_check_handle *consent_check_handle_t;

typedef enum {
    CONSENT_DECISION_ALLOWED,
    CONSENT_DECISION_DENIED,
    CONSENT_DECISION_CANCELLED,
    CONSENT_DECISION_EXPIRED,
    CONSENT_DECISION_INVALIDATED
} consent_decision_e;

typedef enum {
    CONSENT_CHECK_ALLOWED,
    CONSENT_CHECK_CONSENT_REQUIRED,
    CONSENT_CHECK_DENIED
} consent_check_decision_e;

typedef enum {
    CONSENT_CHECK_QUERY,
    CONSENT_CHECK_AUTHORIZE
} consent_check_mode_e;

typedef void (*consent_result_cb)(
    int status, const consent_result_t *result, void *user_data);
typedef void (*consent_check_cb)(
    int status, const consent_check_result_t *result, void *user_data);

int consent_request(
    consent_client_h client,
    const consent_request_params_t *params,
    unsigned int wait_timeout_ms,
    consent_result_t **result);

int consent_request_async(
    consent_client_h client,
    const consent_request_params_t *params,
    consent_result_cb callback,
    void *user_data,
    consent_request_handle_t *request);

int consent_check(
    consent_client_h client,
    const consent_check_params_t *params,
    unsigned int wait_timeout_ms,
    consent_check_result_t **result);

int consent_check_async(
    consent_client_h client,
    const consent_check_params_t *params,
    consent_check_cb callback,
    void *user_data,
    consent_check_handle_t *check);

void consent_result_free(consent_result_t *result);
void consent_check_result_free(consent_check_result_t *result);

#ifdef __cplusplus
}
#endif
#endif
```

### 7.3 공통 파라미터와 결과

| 객체 | 필수 의미 정보 |
|---|---|
| Request params | client_request_id, operation_id, session_id·검증된 generation, 사용자·Subject 문맥, requirements, 요청하는 보관·재사용 범위, 사용자 응답 기한, 선택적 parent_request_id |
| Check params | check mode, 사용자·Subject·session 문맥, requirements, AUTHORIZE의 operation_id·실행 단계 ID. 기존 데이터 재사용이면 artifact·permit 참조 |
| Request result | 최종 decision, reason, 조건별 결정, 확정 scope·보관 정책, 승인 ID·리비전·정책 버전, session 문맥, 캐시 가능 여부, 결과 출처 |
| Check result | 전체·조건별 decision, missing requirements, reason, AUTHORIZE의 사용·허가 식별 정보, 현재 session generation·정책 revision |

전달된 신원과 등급은 주장 값이다. 데몬은 인증된 호출자와 위임·소유권에 따라 검증한다. 불필요한 원문 데이터는 승인 요청에 포함하지 않는다.

보관을 요청할 수 있는 필드는 retention_scope(OPERATION/SESSION/TTL), max_retention_ms, storage_class, reuse_purpose, allowed_recipient 등이다. 호출자가 원하는 값과 실제 허용된 값을 구분하고 반환된 정책만 적용한다. 대화 세션 사용 여부가 없는 시스템 작업은 세션 없는 별도 정책으로 평가하며 SESSION 승인으로 취급하지 않는다.

### 7.4 SYNC/ASYNC 의미

SYNC는 최종 결과 또는 대기 오류까지 호출 스레드에서 기다린다. ASYNC는 접수 확인 이후 반환하고 모든 최종 결과를 callback으로 전달한다. ASYNC 내부의 짧은 접수 IPC는 허용하지만 사용자 응답을 기다리지 않는다.

ASYNC 함수의 `0` 반환은 로컬 또는 데몬 요청 접수 성공이다. 허용 결정을 뜻하지 않는다. 접수 전에 확정 실패하면 오류를 반환하고 callback을 호출하지 않는다. 접수 후 장애는 callback의 `status`로 전달하며 사용자 DENIED로 바꾸지 않는다.

접수 IPC 응답이 유실되면 원격 접수 여부가 불명확할 수 있다. `OUTCOME_UNKNOWN` 계열 API 오류로 구분하고, 동일 client_request_id로 조회·재접수한다. 이 동기 오류 경로에서는 callback을 등록 해제한다. 이미 생성된 원격 요청은 중복 방지 ID로 회수한다.

`status == 0`이면 결과 객체가 유효하다. `status != 0`일 때 `result`는 NULL일 수 있으며 결정 필드를 사용하지 않는다. SYNC의 결과는 호출자가 free한다. callback 결과는 호출 동안만 유효하며 보관이 필요하면 별도 clone API를 사용한다. ASYNC 입력은 함수 반환 전에 복사하거나 동등한 수명 보장을 제공한다.

### 7.5 callback과 스레드

- ASYNC API는 지정된 callback dispatcher의 소유 스레드에서 호출하는 계약을 기본안으로 한다. 다른 스레드는 dispatcher에 호출 작업을 게시한다.
- callback은 같은 dispatcher의 다음 실행 기회에 큐잉한다. 즉시 허용·캐시 적중도 함수 내부에서 callback을 직접 호출하지 않는다.
- IPC I/O 수신은 독립적인 내부 경로에서 처리한다. SYNC는 결과 dispatcher를 막지 않는 작업 스레드에서 사용한다.
- SYNC를 자신이 기다릴 완료 경로의 dispatcher 스레드에서 호출하면 WOULD_DEADLOCK 오류로 거부한다.
- 라이브러리 잠금을 잡고 callback을 호출하지 않는다. 재진입·종료·취소 경쟁을 명시한다.
- 살아 있는 하나의 로컬 등록에는 완료 callback을 최대 한 번 전달한다. 프로세스 종료를 가로지르는 callback 전달 보장은 하지 않는다.
- 클라이언트 종료는 dispatcher에서 등록을 해제하고, 진행 중 callback 종료와 조율한다. 종료 후 해제된 user_data를 참조하지 않는다.

### 7.6 관리·UI·복구 API 후보

| 후보 API | 호출 주체와 목적 |
|---|---|
| `consent_register()` | Installer: 정의·문구 일괄 등록 |
| `consent_update()` | Installer: 정책·문구 버전 갱신 |
| `consent_unregister()` | Installer: 정의 비활성화·승인 무효화 |
| `consent_get_prompt()` | 인증된 승인 UI: 요청·locale로 표시 자료 조회 |
| `consent_respond()` | 인증된 승인 UI: 화면 revision에 연결된 사용자 선택 제출 |
| `consent_get_request_result()` | 소유 요청 ID 또는 client_request_id로 결과 복구 |
| `consent_cancel_request()` | argo: PENDING 요청 취소 |
| `consent_revoke()` | 승인 설정 UI: 승인 철회 |
| `consent_session_open()` | 인증된 세션 제어 주체: 사용자 접점 문맥으로 논리적 세션 생성 |
| `consent_session_suspend()` | 세션 제어 주체: 검증된 접점 연결 상실 보고. 데몬의 lease 만료 경로도 제공 |
| `consent_session_resume()` | 세션 제어 주체: 인증된 재연결 증명으로 접점 교체·재개 |
| `consent_session_close()` | 세션 제어 주체: 신규 사용 차단 및 비동기 정리 시작 |
| `consent_session_get_state()` | 허용된 참여자: 상태·generation·기한·정리 진행 조회 |
| `consent_session_subscribe()` | 허용된 참여자: 상태·generation 변경 이벤트 구독 |
| `consent_data_register()` | 인증된 provider/holder: 취득 허가와 반환 범위를 검증해 결과 데이터 permit 및 holder 등록 |
| `consent_data_register_derived()` | holder: 부모 artifact를 참조해 요약·가공 결과 등록 |
| `consent_data_release()` | holder: 특정 데이터 사본의 사용 중단·정리 결과 ACK |
| `consent_cleanup_get_state()` | 권한 있는 세션 제어 주체: holder별 cleanup 완료·실패 조회 |

위 API를 같은 라이브러리의 별도 헤더로 구분할 수 있으나, 실제 권한은 데몬의 IPC 인증으로 강제한다. `check`와 승인 상태 조회는 사용자 팝업을 생성하지 않는다.

### 7.7 세션·데이터 관리 API의 완료 의미

관리 API 이름은 후보이며 상세 C ABI는 후속 확정한다. 원래 합의한 request/check의 SYNC·ASYNC 쌍은 유지한다. 세션 관리 호출은 상태 전이의 짧은 접수·커밋을 기다리고, 오래 걸리는 데이터 정리는 이벤트·상태 조회로 분리한다.

`consent_session_close()`가 성공한 후 새 접근은 거부되어야 한다. 일부 holder의 정리가 실패해도 세션을 ACTIVE로 되돌리지 않는다. `consent_data_register()`는 원문 데이터를 consentd에 업로드하는 함수가 아니다. 검증된 취득 허가, 실제 반환 scope, 보관 정책, holder identity와 불투명 artifact 참조를 등록한다.

request/check의 기존 데이터 사용은 `operation=reuse-data`와 artifact·permit 참조로 표현한다. 초기에는 추가 공개 check 함수를 만들지 않는다. 부모의 일회성 조회 grant를 다시 소비하지 않고, 적법한 취득 사실과 현재 유효한 data-use permit·session·목적을 검사한다. 재조회는 별도의 원본 read 권한을 다시 평가한다.

ASYNC 요청이 진행 중인 세션이 종료되면 해당 요청을 INVALIDATED로 완료한다. SUSPENDED에서는 신규 prompt와 승인 적용을 중지한다. 초기 정책은 표시 중 prompt를 닫고 관련 PENDING 요청을 INVALIDATED로 종료하며, 재개 후 필요하면 새 요청으로 시작한다. 과거 UI 응답을 새 generation에 자동 적용하지 않는다.

이미 비활성 세션에 새 request/check를 시도하면 SESSION_INACTIVE 또는 SESSION_CLOSED 계열 API 오류를 반환한다. 이는 승인 부족을 뜻하는 CONSENT_REQUIRED가 아니며 argo가 새 팝업으로 해결하려고 반복하지 않는다. 같은 오류는 SYNC 반환 status와 ASYNC callback status에서 동일 의미를 가진다. ACTIVE 세션에서 TTL이 지난 artifact를 사용하는 경우에는 DATA_EXPIRED 사유를 제공하고, 기존 데이터 복구가 아니라 별도의 원본 재조회·승인 경로를 선택하게 한다.

## 8. 요청 상태와 기한

| 상태 | 의미 |
|---|---|
| PENDING | 접수 완료, 정책 평가 또는 사용자 선택 대기 |
| ALLOWED | 현재 요청을 허용하는 결정 완료 |
| DENIED | 정책·기존 거부·사용자 선택으로 거부 |
| CANCELLED | 취소가 최종 확정됨 |
| EXPIRED | 사용자 응답 기한 만료 |
| INVALIDATED | 정의 삭제, 정책 변경 등으로 기존 요청 의미가 무효화됨 |

외부 결과는 ALLOWED로 통일한다. 초기 대화의 APPROVED와 ALLOWED는 별도 상태로 남기지 않는다. UI 필요 여부는 상태와 별도의 필드로 표현한다.

PENDING에서 최종 상태로의 전이는 조건부 갱신으로 한 번만 확정한다. 승인·취소·타임아웃이 경쟁하면 먼저 확정된 최종 상태를 반환한다. 최종 상태를 다른 결정으로 덮어쓰지 않는다. 이후 승인 철회는 해당 grant의 유효성을 변경하며 과거 요청 결과를 소급 수정하지 않는다.

`request_deadline`은 요청 자체의 기한이다. 초기 검토값으로 1분·5분을 둘 수 있으나 항목별 정책으로 확정한다. `wait_timeout_ms`는 SYNC 호출의 대기 시간이다. 대기 시간 초과는 자동 사용자 거부나 원격 취소가 아니다. 호출자는 안정된 client_request_id로 결과를 조회하거나 명시적으로 취소한다.

재부팅 시 PENDING 요청을 INVALIDATED로 종료하고 새 요청을 요구하는 것을 초기 복구 정책으로 제안한다. UI 응답을 복원된 요청에 연결하는 복잡한 재개는 후속 확장으로 둔다. 지속 승인은 DB에서 복원하되, 이전 활성 세션과 세션 데이터는 6.7의 종료·정리 정책을 적용한다.

## 9. 처리 시나리오

### 9.1 최초 사용자 승인

| 순서 | 주체 | 동작 |
|---|---|---|
| 1 | Capability Manager / Context Engine | QUERY 또는 사전 정책 확인으로 부족한 조건을 산출 |
| 2 | argo | 부족한 조건에 대해 consent_request_async 호출 |
| 3 | consent 라이브러리 | callback 등록·중복 방지 ID 준비 후 데몬 접수 |
| 4 | consentd | 요청 생성·접수 확인. 현재 결정이 없으면 UI 표시 작업 예약 |
| 5 | 승인 UI | locale로 prompt 조회, 고정된 scope·목적·등급·승인 방식 표시 |
| 6 | 사용자 / UI | 선택을 request_id와 prompt revision에 연결해 제출 |
| 7 | consentd | 신원·기한·버전 재확인, 결정 저장과 요청 완료를 원자적으로 확정 |
| 8 | consentd / 라이브러리 | 완료 이벤트를 전달하고 dispatcher에서 callback 호출 |
| 9 | argo | ALLOWED이면 실제 서비스 요청을 재개 |
| 10 | 실행 서비스 | AUTHORIZE로 현재 조건 확인 후 접근·실행 |

### 9.2 기존 승인 또는 거부

캐시 적중이면 라이브러리가 로컬 결과를 만든다. 캐시 미적중이면 consentd가 기존 결정·정책을 확인한다. 두 경로 모두 ASYNC는 callback, SYNC는 함수 출력으로 완료한다. UI는 생성하지 않는다.

캐시 적중만으로 완료된 요청은 데몬 request_id가 없을 수 있다. 로컬 호출 handle과 원격 request_id를 구분하고 결과 출처를 CACHE로 표시한다. 캐시 결과에 가짜 원격 ID를 만들지 않는다. 로컬 캐시 호출의 재연결 복구 대신 실제 실행 서비스의 AUTHORIZE 확인을 사용한다.

### 9.3 check

| 결과 | 의미 | 후속 동작 |
|---|---|---|
| ALLOWED | 필요한 조건이 충족됨 | QUERY이면 사전 판단, AUTHORIZE이면 실행 시작 가능 |
| CONSENT_REQUIRED | 승인 없음·만료·범위 변경 등 | 서비스가 차단 후 argo에 부족한 조건 반환 |
| DENIED | 정책 또는 유효한 명시 거부 | 실행 거부, 반복 팝업으로 자동 전환하지 않음 |
| API 오류 | 통신·DB·검증 처리 실패 | 실행 차단, 원인에 맞게 복구 |

### 9.4 복수 승인과 추가 승인

예: 일정 Action 실행은 허용되어 있지만 일정 내용 읽기는 미승인인 경우, 데이터 읽기 조건만 추가 요청한다. 이후 읽은 내용을 다른 앱에 전달하려면 전달 목적·수신자를 포함한 별도 조건이 필요할 수 있다.

1. 처음부터 알려진 요구 조건은 실행 전에 모두 수집한다.
2. 실행 중 추가 조건을 발견하면 해당 접근 전에 보류한다. 미승인 데이터를 먼저 읽거나 전달하지 않는다.
3. 앱은 continuation_id와 요구 조건을 자신의 서비스 경로로 반환한다. Capability Manager가 소유권·등록 범위를 검증한다.
4. argo는 operation_id를 유지하고 parent_request_id로 이전 승인 요청과 연결한다.
5. 추가 승인 결과를 받으면 보류된 단계의 조건을 다시 AUTHORIZE한다.
6. 성공 시 저장된 continuation을 재개한다. 전체 Action을 무조건 처음부터 다시 실행하지 않는다.

parent_request_id는 추적 관계이며 권한 상속을 의미하지 않는다. continuation은 소유 주체·operation·scope·만료·서비스 인스턴스에 연결해 다른 작업에 재사용하지 못하게 한다. 원문 데이터나 비밀을 continuation ID에 담지 않는다.

무한 추가 승인과 반복 팝업을 막기 위해 같은 조건의 반복 요구를 식별하고, 작업별 승인 라운드·전체 기한·요구 조건 수에 상한을 둔다. 수치는 제품 정책으로 확정한다. 앱이 실행 중 내부 접근을 통제할 수 없다면 해당 Action의 안전한 재개 지원 여부를 명시하고 사전 승인 가능한 범위로 제한한다.

### 9.5 실행 도중 철회

AUTHORIZE는 승인 판단이 확정되는 실행 시작 시점을 제공한다. 그 이후 이미 수행된 작업을 되돌린다는 보장은 하지 않는다. 장시간 Context 구독·스트리밍은 새 데이터 제공 단위의 재확인 또는 철회 이벤트에 따른 중단 계약이 추가로 필요하다. 이를 단발성 Action의 승인과 구분한다.

### 9.6 대화 내 후속 질문

| 순서 | 사용자 또는 시스템 | 처리 |
|---|---|---|
| 1 | 사용자: “오늘 일정을 알려줘.” | ACTIVE 세션 S에서 일정 조회·보관 요구 조건 구성 |
| 2 | UI: “이번 대화 동안 일정에 접근하고, 조회한 내용을 대화에 사용하도록 허용” | 실제 정책이 허용하는 선택지만 표시 |
| 3 | 사용자 승인 | SESSION 조회 grant와 SESSION 보관·동일 목적 재사용 정책 확정 |
| 4 | 앱/Context Engine | AUTHORIZE 후 원본 조회. 실제 반환 범위를 취득 허가와 결합 |
| 5 | argo holder | 결과를 메모리에 보관하고 artifact A 및 permit 등록 |
| 6 | 사용자: “그중 오후 일정만 알려줘.” | artifact A의 session·목적·TTL·철회 상태를 AUTHORIZE로 확인 |
| 7 | Context assembler | 허용된 데이터만 입력에 넣어 후속 답변. 원본 재조회·팝업 불필요 |
| 8 | 사용자 대화 종료 | S를 CLOSING으로 전환, 신규 사용 차단, A와 관련 요약·문맥 정리 |

승인 상태가 유효하다는 것과 일정 내용이 최신이라는 것은 다르다. 최신 일정이 필요하거나 데이터가 변경되었을 가능성이 중요한 요청은 원본 freshness 정책에 따라 다시 조회한다. 원본 재조회에는 현재 read 승인이 필요하다.

### 9.7 한 번 조회하고 대화 중 재사용

ONCE 조회 + SESSION 보관·reuse를 명시적으로 승인한 경우, 원본 조회 시 ONCE를 소비한다. 이미 얻은 artifact는 permit 범위에서 여러 후속 질문에 사용할 수 있다. 새 일정이나 다른 날짜를 다시 읽으려면 새 조회 승인이 필요할 수 있다. Level 3은 별도 정책이 이를 허용할 때만 SESSION 보관을 지원하며, 기본값을 일괄 완화하지 않는다.

### 9.8 연결 단절 후 이어서 대화

RESUMABLE_CONVERSATION 세션에서 접점이 끊기면 SUSPENDED가 된다. holder는 아직 유효한 데이터를 메모리에 보존하되 사용을 정지한다. 유예시간 안에 같은 사용자·프로필로 재개하면 generation을 갱신하고 캐시·permit을 확인한 뒤 후속 질문을 처리한다. TTL이 지난 데이터는 복구하지 않는다. CONNECTION_BOUND 세션은 이 재개 경로를 사용하지 않는다.

### 9.9 다른 앱으로 전달·새 목적 사용

같은 대화에서 보관 중인 일정이라도 “이 내용을 다른 앱에 보내줘”는 recipient·목적이 달라진다. 기존 reuse 허가에 포함되지 않으면 추가 승인을 요구한다. 필요 시 새 data-use permit을 발급하지만 원본 데이터의 최대 보관 기한은 임의로 연장하지 않는다.

### 9.10 대화 종료와 데이터 정리

세션 제어 주체가 종료를 요청하면 consentd는 generation을 올리고 session을 CLOSING으로 바꾼다. SESSION grant·관련 permit·미완료 prompt를 무효화한다. 정리 이벤트를 받은 argo는 생성·출력을 중단하고 해당 데이터와 파생 결과를 다음 입력에서 제외한다. holder별 삭제 ACK가 확인되면 CLOSED로 전이한다. 정리 실패는 별도 오류로 남기고 재시도하며, 세션을 다시 허용하지 않는다.

## 10. QUERY / AUTHORIZE와 일회성 승인

| 모드 | 의미 | DB 변경 |
|---|---|---|
| QUERY | 현재 승인 존재와 범위를 조회 | 승인 사용 횟수 변경 없음 |
| AUTHORIZE | 실행 직전 필요한 모든 조건 검증 | 필요한 일회성 사용을 원자적으로 기록 |

일회성 승인은 argo의 request 또는 사전 QUERY에서 소비하지 않는다. 실제 접근을 수행하는 서비스의 AUTHORIZE에서 사용한다.

### 10.1 원자적 평가

하나의 실행 단계에 필요한 조건 집합을 트랜잭션 안에서 평가한다. 하나라도 부족하거나 거부되면 해당 평가로 어떤 일회성 승인도 소비하지 않는다. 모두 충족되면 필요한 사용 기록을 저장하고 ALLOWED를 반환한다. 반복 호출은 같은 operation_id·실행 단계 ID·조건 fingerprint로 식별한다.

서비스가 동일 작업의 AUTHORIZE를 재시도하면 같은 사용 기록을 확인해 중복 소비하지 않는다. 재시도 시 요청 내용이 바뀌면 충돌 오류 또는 새로운 단계로 처리한다. 기존에 사용 기록이 있어도 이후 철회·무효화된 권한을 새 실행에 재사용하지 않는다. 실행 서비스는 이미 시작·완료한 작업을 자신의 operation 상태로 구분한다.

### 10.2 일회성의 단위

초기 기본안은 ‘정의된 범위의 한 실행 단계에서 한 번 접근 시작을 허용’이다. 같은 사용자 작업에서 데이터 페이지를 여러 번 읽어야 한다면 승인 범위와 실행 단계 수명을 명시해야 한다. 임의의 operation_id만 붙여 무한 재사용하는 방식은 허용하지 않는다.

Capability Manager가 데이터 조건을 사전 조회하고 앱이 실제 데이터 접근을 수행하는 경우, 실제 제공 지점이 데이터 조건을 소비한다. 동일한 데이터 승인을 Manager와 앱이 각각 소비하지 않도록 자원별 enforcement owner를 등록한다. 검증된 위임이 없는 서비스는 다른 제공자의 일회성 승인을 소비할 수 없다.

### 10.3 실행 실패와 보상

승인 사용 기록과 실제 앱 작업은 단일 SQLite 트랜잭션으로 묶을 수 없다. 승인 사용 후 앱이 실패했을 때 기본적으로 승인을 자동 환급하지 않는다. 재시도는 서비스의 멱등 처리와 실행 단계 상태에 따라 결정한다. 예약·확정·해제 모델은 필요성이 확인될 때 후속 확장으로 검토한다.

## 11. 다국어 등록과 UI

### 11.1 소유와 저장 위치

| 문구 종류 | 소유·저장 |
|---|---|
| 허용·거부·이번만 허용·요청 만료 등 공통 UI | 승인 UI의 플랫폼 번역 리소스 |
| 자원별 제목·설명·목적 안내 | 등록 패키지에서 consentd가 검증·수집해 consent.db에 저장 |
| 날짜·숫자·대상 기기 등 동적 값 | 확정된 요청 scope 또는 신뢰하는 시스템 정보 |

플랫폼 공통 자원 문구는 플랫폼이 관리한다. 앱은 자신의 자원 설명만 등록한다. 다국어 내용을 등록할 권한이 있다는 것이 승인 의미를 임의로 축소해도 된다는 의미는 아니다. 문구 소유권과 번역 검토는 설치·배포 경로에서 관리한다.

### 11.2 정의 예시

```json
{
  "owner": "org.tizen.context",
  "consent_id": "viewing-history-read",
  "resource_type": "context-data",
  "resource_id": "viewing-history",
  "policy_version": 1,
  "text_revision": 1,
  "default_locale": "en",
  "sensitivity_level": 2,
  "allowed_modes": ["ONCE", "TIMED"],
  "parameters": {
    "retention_days": {
      "type": "integer",
      "minimum": 1,
      "maximum": 365
    }
  },
  "messages": {
    "ko": {
      "title": "시청 기록 접근",
      "body": "최근 {retention_days}일 동안의 시청 기록을 읽도록 허용하시겠습니까?"
    },
    "en": {
      "title": "Viewing history access",
      "body": "Allow access to your viewing history for the last {retention_days} days?"
    }
  }
}
```

### 11.3 처리 원칙

- argo는 ID와 구조화된 인자를 보낸다. 승인 UI의 최종 문장을 생성하지 않는다.
- consentd는 인자 타입·범위와 자원 정책을 검증한다. ‘30일’ 표시는 실제 승인된 기간과 같은 원본에서 유도한다.
- UI는 현재 사용자 locale을 명시적으로 전달한다. 데몬의 프로세스 전역 locale을 요청마다 변경하지 않는다.
- consentd는 요청의 policy_version과 text_revision에 맞는 템플릿을 선택한다. UI가 검증된 값으로 최종 포맷한다.
- 문장을 조각내어 이어 붙이지 않는다. 전체 문장 템플릿과 이름 있는 변수를 사용한다. 복수형·날짜·숫자는 기존 플랫폼 포맷 기능을 활용한다.
- 템플릿에서 요구하는 변수와 인자 정의가 일치하는지 등록 시 확인한다. 앱 이름·사용자 입력은 템플릿 문법으로 재해석하지 않고 표시 데이터로 취급한다.
- 언어별 fallback은 명시적으로 구성한다. `ko-KR → ko → 기본 언어`는 예시이며 모든 언어의 script·region을 단순 제거하는 규칙으로 일반화하지 않는다.
- fallback은 번역 누락에 대한 기술 경로다. 제품이 승인 화면에 요구하는 지원 언어 조건을 충족하지 못하면 승인 불가로 처리한다. 의미 있는 설명 자체가 없으면 UI 실패로 종료하고 자동 허용하지 않는다.
- 등급, 범위, 목적, 지속 기간과 승인 가능한 방식을 화면에 일관되게 표시한다. 정책이 허용하지 않는 ‘항상 허용’ 버튼은 제공하지 않는다.
- ‘이번 대화 동안 허용’은 한 번의 조회인지 세션 중 반복 조회인지, 결과를 대화에 보관·재사용하는지 구분해 설명한다. 접근 PERSISTENT와 결과 SESSION 보관을 하나의 ‘항상’ 문구로 뭉뚱그리지 않는다.
- 재연결 유예 중 임시 보관을 지원한다면 세션 종료·유예 만료 기준을 UI 정책에 포함한다. UI의 문구는 실제 보관 정책과 동일한 정의에서 생성한다.

### 11.4 화면과 응답의 결합

prompt_token 또는 동등한 서버 관리 식별자로 request_id, policy_version, text_revision, scope fingerprint, 표시한 locale을 연결한다. UI 응답에서 이 결합을 확인한다. 언어 변경으로 다시 표시하면 새 prompt revision을 발급하고 이전 응답과의 경쟁을 처리한다.

단순 번역 수정은 text_revision, 승인 의미·범위·목적·등급 변경은 policy_version을 변경한다. 오역 수정이 사용자의 이해나 승인 의미를 바꾼다면 단순 문구 수정으로 취급하지 않는다. 표시 중 정책이 바뀌면 요청을 INVALIDATED로 종료한다. 이전 문구 revision은 관련 요청·감사 보존기간 동안 참조 가능하게 유지한다.

### 11.5 확정 구현 계약: typed template v1

첫 구현은 등록 정의의 `template_version=1`과 최대 8개
`parameter.<name>.{source,type,min,max,max_bytes}`를 사용한다. source는 요구 조건의
scope·purpose·recipient·operation 또는 정의의 retention_ms로 제한하며, 저장된 요청과
정의에서 값을 추출한다. 별도 display_args는 받지 않는다. scope의 정규 십진 정수 또는
길이 제한 UTF-8 문자열, 다른 요청 필드의 문자열, retention_ms 정수를 검증한다.
타입에 맞지 않는 제약·미등록 변수·문법 오류·과도한 출력은 명시적 오류다.
요청 source 필드는 반드시 명시한다. 문자열 누락을 빈 값으로 바꾸지 않으며,
명시한 빈 문자열만 schema 범위 안에서 허용한다.

11.2절 예시의 “최근 30일”은 조회 범위이므로 정수형 scope `30`에 결합한다.
취득한 결과를 얼마나 보관하는지 나타내는 retention_ms와 혼동하지 않는다. 모든 값은
기존 grant·retry·cache·receipt·artifact 범위 검증과 동일한 원본을 사용한다. schema나
범위 의미가 바뀌면 policy_version을 올리며 typed 요청은 일치하는 version을 명시한다.

템플릿은 전체 문장 내 `{name}`만 허용하고 모든 locale의 title/body 변수 집합과
schema를 일치시킨다. UI에는 선택한 template와 검증된 이름·타입·값을 전달한다.
`consent_prompt_format()`은 상한 있는 단일 치환으로 평문 소유 문자열을 제공하며,
인자의 중괄호·퍼센트·markup을 다시 실행하지 않는다. v1 정수는 정규 십진 표기로
제한하고 단위 자동 변환·복수형·날짜·언어별 숫자 서식은 후속 플랫폼 통합 범위다.

UI는 typed prompt를 받을 때 version 1 지원을 명시하고 응답 시 표시 locale과 최신
token을 결합한다. 구 UI에는 미지원 오류를 반환하며 기존 literal 정의는 호환된다.
언어를 바꾸어 다시 표시하면 token을 교체한다. 등록된 직접 locale fallback을 먼저
적용하고 기존 명시적 fallback과 기본 언어를 뒤에 적용한다. 연쇄·순환·미등록 대상은
거부한다. title/body 번역 쌍이 등록된 locale을 alias 출발점으로 중복하는 mapping도 등록 오류다.
번역·fallback 변경은 text_revision 증가와 표시 중 요청 무효화가 필요하다.
같은 definition ID의 text_revision은 제거·재설치에도 감소하지 않는다. 기본 locale·
번역·fallback map 변경은 policy_version을 함께 올려도 더 큰 text_revision이 필요하며,
map이 같으면 기존 text_revision을 유지할 수 있다.

## 12. 프로세스 내부 캐시

### 12.1 구조

`consent`의 C++ 구현에 프로세스 내 공유 ConsentCache를 둔다. 여러 client handle이 저장 공간을 공유할 수 있지만 사용자, Subject, Provider와 scope는 분리한다. 공개 C API 호출자는 캐시를 직접 관리하지 않는다.

대상은 consentd가 캐시 가능하다고 확정한 PERSISTENT 및 SESSION 허용 결정이다. SESSION 캐시는 이번 개정의 기본 범위에 포함한다. ONCE 조회 승인은 재사용 승인 캐시에 저장하지 않는다. ONCE로 얻은 결과를 대화 중 사용할 수 있는지는 별도의 data-use permit으로 판단한다. 초기 Level 3 정책과 DENIED 결과는 승인 캐시에서 제외하며, TIMED 캐시는 후속 확장으로 둔다.

### 12.2 캐시 항목

| 범주 | 데이터 |
|---|---|
| 범위 키 | 사용자·프로필, Subject, Provider, 자원 타입·ID, 작업, 목적, 수신자, 정규화 scope. SESSION은 session_id 필수 |
| 승인 메타데이터 | decision_id, policy_version, sensitivity_level, grant_revision, 승인 방식, expires_at |
| 동기화 메타데이터 | daemon_epoch, change_revision, 동기화 상태, SESSION의 session_generation·세션 기한 |

초기에는 정확한 범위 일치만 적중으로 인정한다. hash를 사용하더라도 원본 정규화 값의 동일성을 확인한다. 부분 범위 포함 판단은 신뢰하는 비교기가 있을 때만 사용한다. consentd가 반환한 확정 범위를 저장하며 입력 인자를 그대로 grant로 만들지 않는다.

### 12.3 무효화와 재연결

| 사건 | 처리 |
|---|---|
| 사용자 철회·정책 거부 갱신 | 관련 grant 제거 |
| 정의·제공 앱 삭제 | 관련 항목 제거 |
| policy_version·등급·범위 변경 | 해당 항목 무효화 |
| 프로필·Subject 신원 변경 | 이전 문맥과 분리하거나 제거 |
| 만료 | 조회 시 검사 후 제거 |
| IPC 연결 단절·daemon_epoch 변경 | 캐시를 UNSYNCED로 표시하고 허용 판단에 사용하지 않음 |
| 변경 sequence 누락 | 캐시 사용을 중단하고 스냅샷 재동기화 |
| 세션 SUSPENDED·CLOSING·CLOSED | SESSION 캐시의 허용 재사용 중단. 종료 시 제거 |
| 세션 재개·generation 변경 | 이전 generation 캐시를 그대로 사용하지 않고 서버 상태와 재동기화 |

구독과 스냅샷은 리비전 경계를 공유한다. 예를 들어 R 시점 스냅샷을 받고 R 이후 이벤트를 순서대로 반영한 뒤 READY가 된다. 데몬은 DB 변경 커밋과 이벤트 리비전 증가를 결합한다. 이벤트 전달은 유실 가능하므로 누락 감지와 재조회 경로가 필요하다.

### 12.4 일관성과 한계

변경 이벤트 전달 지연 동안 요청 캐시가 이전 허용을 반환할 수 있다. 이 캐시는 승인 팝업을 생략하고 작업을 재개하는 판단에 사용한다. 실제 서비스의 AUTHORIZE는 consentd 최신 상태를 확인한다. 엄격한 최신성을 요구하는 AUTHORIZE를 이벤트 기반 프로세스 캐시만으로 허용하지 않는다.

초기 check QUERY도 데몬 조회를 기본으로 한다. 향후 QUERY 캐시를 추가하더라도 결과의 신선도와 사전 조회 목적을 명시하고 AUTHORIZE와 구분한다. 일회성 사용 처리는 항상 데몬에서 수행한다.

캐시 항목 수와 메모리 상한, LRU를 적용한다. 항목 제거는 권한 제거가 아니며 다음 요청에서 데몬을 조회한다. 구체적인 크기는 Tizen 실측으로 확정한다.

PERSISTENT 캐시가 적중해도 현재 작업에 연결된 세션이 중단·종료된 경우 대화 작업을 재개하지 않는다. 이는 원본 grant가 PERSISTENT라는 사실과 별개다. request 캐시에는 문맥의 세션 사용 가능성 검사를 포함하고 최종 AUTHORIZE에서 재검증한다. IPC 단절 시 캐시는 비활성화하고 대화 데이터 재사용도 데몬 상태를 확인할 때까지 보류한다.

### 12.5 데이터 보관과 승인 캐시의 분리

| 저장 대상 | 소유자 | 내용 |
|---|---|---|
| 승인 캐시 | consent 라이브러리 | 허용 상태와 scope·version·session 메타데이터 |
| 데이터 사용 permit | consentd | 합법적 취득 출처, 보관·reuse 범위, TTL, 철회·정리 상태 |
| 실제 대화 데이터 | argo / Context 저장소 / 앱 holder | 도구 결과, 메시지, 요약, 모델 문맥 |

consentd와 consent 라이브러리의 승인 캐시에 원문 일정·대화·시청 기록을 넣지 않는다. 서비스별 holder는 정책 집행 책임을 가진다. consentd가 다른 프로세스 메모리를 직접 삭제할 수 있다고 가정하지 않는다.

### 12.6 data-use permit과 provenance

permit은 data_permit_id, source_decision_id, 취득 AUTHORIZE receipt, session_id, holder 대상, purpose·recipient, 실제 데이터 범위, retention_scope, expires_at, storage_class, policy_revision을 포함한다. 승인받은 범위보다 반환된 데이터가 넓으면 provider에서 차단하거나 잘라내야 한다.

holder가 임의로 데이터를 등록해 유효한 permit을 만들 수 없어야 한다. 데이터 제공자의 인증된 반환 문맥 또는 데몬이 검증 가능한 취득 receipt와 요청 fingerprint를 사용한다. receipt는 특정 operation·실행 단계·provider·수신 holder에 결합한다. 식별자만 복사해 다른 데이터나 세션에 적용하지 못하도록 한다.

데이터 수신과 permit 등록 사이의 결과는 격리된 임시 버퍼에 두고 모델 입력·사용자 출력·history에 공개하지 않는다. 등록 시 현재 session generation과 철회 상태를 다시 확인한다. 취득 중 세션이 종료되었거나 permit 등록에 실패하면 늦게 도착한 결과를 폐기한다. holder가 사전 등록한 반환 슬롯을 사용하면 in-flight 데이터도 cleanup 대상으로 추적할 수 있다. 어떤 방식이든 등록되지 않은 결과가 정리 대상에서 빠진 채 대화 문맥에 들어가서는 안 된다.

artifact는 holder가 보관하는 객체에 대한 불투명 ID이며 raw path, 본문, 인증 비밀을 노출하지 않는다. 원본 데이터와 취득 시점, grant·permit, 사용자, 세션, 목적을 연결한다. 승인 당시 확인된 민감도보다 파생 과정에서 민감도가 높아지면 새 분류와 정책으로 평가한다.

### 12.7 요약·추출·모델 문맥의 수명

요약으로 바꾸거나 프롬프트에 넣었다고 원본의 보관 제한이 사라지지 않는다. 파생 artifact는 부모 artifact의 의존 관계를 저장하고 다음 규칙을 적용한다.

- 허용 목적·수신자는 부모 정책의 공통 허용 범위를 넘지 않는다.
- 만료 시각은 부모 중 가장 빠른 제한을 넘지 않는다. 요약 시각을 새 취득 시각으로 사용하지 않는다.
- 등급은 기본적으로 부모의 가장 높은 등급 이상으로 평가하고, 데이터 결합으로 더 높아질 수 있다.
- 부모 하나라도 철회·만료되면 그 내용을 포함하는 파생 결과를 차단·삭제하거나 살아 있는 출처만으로 재생성한다.
- 출처를 분리할 수 없는 요약·대화 턴은 전체를 영향 대상으로 취급한다. 정리 후 남은 입력으로 모델 문맥을 재구성한다.
- 모델의 KV cache·prefix cache·agent scratchpad도 영향을 추적한다. 안전한 부분 제거가 지원되지 않으면 관련 모델 세션·캐시를 폐기하고 다시 만든다.

여기서 ‘삭제’는 관리하는 데이터의 참조 제거·저장소 정리·향후 사용 차단을 뜻한다. 할당 해제만으로 물리 메모리의 모든 잔존 바이트가 즉시 지워진다고 주장하지 않는다. 직접 관리 가능한 민감 버퍼는 가능한 범위에서 초기화하되, 라이브러리·모델 엔진의 복사본 처리 능력을 함께 검증한다.

### 12.8 대화 기록과 임시 데이터

채팅 기록 저장과 세션 임시 문맥 저장은 다른 정책이다. 이번 세션에만 사용하도록 승인된 데이터를 사용자에게 응답했다고 해서 영구 대화 기록에 자동 보관하지 않는다. 원본 tool output뿐 아니라 해당 내용을 포함한 assistant 답변·사용자 인용·요약에도 같은 출처 정책을 적용한다.

초기 구현은 세션 제한 데이터가 포함된 턴을 영구 history 저장에서 제외하거나 종료 시 정책에 맞게 제거·마스킹하는 방식을 선택한다. 일반 대화 기록까지 모두 삭제할 필요는 없으나 분리할 수 없으면 더 넓은 문맥을 정리한다. 장기 기억·embedding·검색 인덱스·디스크 checkpoint로의 저장은 별도 허용 없이는 수행하지 않는다.

사용자가 이미 본 응답을 되돌리거나 외부에서 만든 사본까지 제거한다는 보장은 하지 않는다. 본 계약은 시스템이 관리하는 향후 접근·재사용·보관을 통제한다.

### 12.9 사용 경계와 실행 중 철회

매 토큰마다 IPC하지 않고 모델 입력 구성, 도구 호출, 외부 전달, 출력 공개 등 의미 있는 경계에서 사용 조건을 배치 확인한다. 실제 데이터를 프롬프트에 넣는 holder는 reuse-data AUTHORIZE를 확인한다. 장시간 실행은 이벤트와 기한을 감시하고 generation이 달라지면 출력·다음 입력을 중지한다.

데몬 확인 시점과 실제 출력 사이에도 경합이 있을 수 있다. 엄격한 요구가 있는 출력 경로는 신뢰하는 출력 dispatcher가 generation과 허가를 검증하는 공개 경계를 정의한다. 그 경계 전에 철회가 확정되면 출력하지 않는다. 이미 허가되어 공개된 데이터의 회수 또는 분산 프로세스 간 무한히 즉각적인 철회는 보장하지 않는다. 스트리밍은 chunk 경계, 감지 지연, in-flight 중단 정책을 명시한다.

외부 LLM으로 프롬프트를 보내는 것은 데이터 전달이다. recipient·처리 목적·외부 보관 계약이 허용되어야 한다. 로컬 SESSION 보관 승인이 외부 저장 허용으로 자동 확장되지 않는다. 외부 provider의 삭제·보관 특성을 확인하지 않고 ‘세션 종료 시 모든 사본 삭제’를 약속하지 않는다.

### 12.10 종료·철회·TTL 정리 프로토콜

1. consentd가 session 또는 permit을 비활성화하고 revision/generation을 증가시킨다. DB 커밋과 cleanup 작업 생성은 결합한다.
2. 모든 등록 holder에 invalidation·cleanup 이벤트를 발행한다. 이벤트 유실은 재연결 스냅샷·미완료 작업 조회로 복구한다.
3. holder는 즉시 새 사용을 차단하고 진행 중 모델 작업·출력을 중지한다. 도구 결과·파생 요약·해당 문맥의 접근 참조를 제거한다.
4. 메모리·저장소 정리를 수행하고 cleanup_id별 ACK를 보낸다. 동일 ACK 재전송은 멱등 처리한다.
5. consentd는 holder별 완료·실패·재시도 시각을 기록한다. 모든 필수 정리가 확인되면 artifact를 정리 완료로 표시하고, 세션 종료 작업이면 CLOSED로 전이한다.

holder가 응답하지 않아도 신규 접근 차단은 유지한다. MEMORY_ONLY holder 프로세스의 종료를 플랫폼이 확인하고 다른 사본이 없다는 계약이 충족된 경우에는 소실 완료로 판정할 수 있다. 디스크·원격 사본이 있을 수 있으면 프로세스 종료만으로 정리 완료 처리하지 않는다. 삭제 실패를 ALLOWED로 되돌리거나 성공 ACK로 위장하지 않는다.

### 12.11 보관 공간의 경계

세션 artifact는 `사용자/프로필 + Subject + session_id`로 격리한다. 동일한 provider와 내용이어도 다른 대화에 자동 공유하지 않는다. 다른 세션으로 복사하려면 별도의 허용된 전달·보관 정책을 평가한다. artifact ID·generation을 유추해 접근할 수 없도록 모든 조회에 인증과 소유권을 확인한다.

메모리 압박으로 데이터를 먼저 제거하는 것은 허용한다. 이후 질문에 필요한 데이터가 없으면 freshness·현재 read 승인에 따라 다시 조회하거나 사용자에게 상태를 알린다. 디스크로 자동 spill해 MEMORY_ONLY 정책을 우회하지 않는다. 데이터 캐시 eviction은 승인 grant 자체의 철회를 의미하지 않는다.

## 13. SQLite3 관리 모델

### 13.1 소유와 논리 테이블

`consent.db`는 consentd만 읽고 쓴다. 라이브러리·Installer·UI가 DB를 직접 열지 않는다. 파일과 보조 파일의 소유권·플랫폼 보안 라벨을 데몬 경계에 맞춘다.

| 테이블 | 역할 |
|---|---|
| consent_meta | DB schema version, change revision 등 |
| consent_definition | 소유자·자원·현재 정책 버전·활성 상태 |
| consent_policy | 버전별 등급·인자 스키마·허용 승인 방식·정책 |
| consent_message | 정책·문구 revision·locale별 제목과 본문 |
| consent_request | 접수·최종 상태·기한·중복 방지·상위 요청 관계 |
| consent_requirement | 요청의 개별 조건과 확정 범위·버전·조건별 결과 |
| consent_decision | 사용자·Subject·조건에 대한 허용 또는 명시 거부, 유효성 |
| consent_usage | operation·실행 단계별 일회성 사용 내역 |
| consent_session | 논리적 대화 세션, 사용자·소유 인스턴스, 상태·generation·기한 |
| consent_connection | 세션에 결합된 사용자 접점·서비스 연결과 역할 |
| consent_authorization | 데이터 취득을 허용한 확정 receipt. 원문 없는 출처 증거 |
| consent_data_permit | 취득 결과의 보관·재사용 범위와 TTL·철회 상태 |
| consent_artifact | holder가 관리하는 원본·파생 데이터의 불투명 식별자·분류·기한 |
| consent_artifact_permit | artifact가 의존하는 하나 이상의 data-use permit |
| consent_artifact_parent | 요약·추출 등 파생 관계 |
| consent_artifact_holder | 실제 사본을 보유한 프로세스 인스턴스·저장 매체·정리 상태 |
| consent_cleanup | 세션·정책·철회·TTL에 따른 holder별 정리 작업과 ACK |

장기 보관이 필요한 결정과 수명이 짧은 pending 요청을 구분한다. 사용자 거부, 기간이 있는 거부, 이번 요청만 거부를 구분하고, 이번 요청 거부를 영구 거부로 자동 저장하지 않는다.

### 13.2 최소 관계 스키마 초안

아래 SQL은 관계와 제약을 검토하기 위한 실행 가능한 초안이다. 복잡한 scope·정책·번역 검증은 C++ 계층에서 수행한다. JSON1 확장에 의존하지 않는다. 실제 보존 정책, ID 생성, 마이그레이션 DDL은 구현 시 확정한다.

```sql
PRAGMA foreign_keys = ON;

CREATE TABLE consent_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE consent_definition (
    definition_id TEXT PRIMARY KEY,
    owner_id TEXT NOT NULL,
    consent_id TEXT NOT NULL,
    resource_type TEXT NOT NULL,
    resource_id TEXT NOT NULL,
    current_policy_version INTEGER NOT NULL,
    active INTEGER NOT NULL CHECK (active IN (0, 1)),
    UNIQUE (owner_id, consent_id)
);

CREATE TABLE consent_policy (
    definition_id TEXT NOT NULL,
    policy_version INTEGER NOT NULL,
    sensitivity_level INTEGER NOT NULL CHECK (sensitivity_level BETWEEN 0 AND 3),
    default_locale TEXT NOT NULL,
    current_text_revision INTEGER NOT NULL,
    parameter_schema TEXT NOT NULL,
    policy_json TEXT NOT NULL,
    PRIMARY KEY (definition_id, policy_version),
    FOREIGN KEY (definition_id) REFERENCES consent_definition(definition_id)
);

CREATE TABLE consent_message (
    definition_id TEXT NOT NULL,
    policy_version INTEGER NOT NULL,
    text_revision INTEGER NOT NULL,
    locale TEXT NOT NULL,
    title TEXT NOT NULL,
    body TEXT NOT NULL,
    PRIMARY KEY (definition_id, policy_version, text_revision, locale),
    FOREIGN KEY (definition_id, policy_version)
        REFERENCES consent_policy(definition_id, policy_version)
);

CREATE TABLE consent_session (
    session_id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    subject_id TEXT NOT NULL,
    owner_id TEXT NOT NULL,
    owner_instance_id TEXT NOT NULL,
    state TEXT NOT NULL CHECK (state IN
        ('ACTIVE', 'SUSPENDED', 'CLOSING', 'CLOSED')),
    generation INTEGER NOT NULL CHECK (generation > 0),
    lifecycle_mode TEXT NOT NULL CHECK (lifecycle_mode IN
        ('CONNECTION_BOUND', 'RESUMABLE_CONVERSATION')),
    created_at_ms INTEGER NOT NULL,
    last_user_activity_at_ms INTEGER NOT NULL,
    idle_deadline_at_ms INTEGER NOT NULL,
    absolute_deadline_at_ms INTEGER NOT NULL,
    reconnect_deadline_at_ms INTEGER,
    closed_at_ms INTEGER,
    end_reason TEXT,
    resume_verifier_hash TEXT,
    session_policy_json TEXT NOT NULL
);

CREATE TABLE consent_connection (
    connection_id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    generation INTEGER NOT NULL,
    peer_id TEXT NOT NULL,
    peer_instance_id TEXT NOT NULL,
    role TEXT NOT NULL CHECK (role IN ('PRIMARY', 'SUPPORT')),
    state TEXT NOT NULL CHECK (state IN ('ATTACHED', 'DETACHED')),
    attached_at_ms INTEGER NOT NULL,
    detached_at_ms INTEGER,
    lease_deadline_at_ms INTEGER NOT NULL,
    FOREIGN KEY (session_id) REFERENCES consent_session(session_id)
);

CREATE TABLE consent_request (
    request_id TEXT PRIMARY KEY,
    requester_id TEXT NOT NULL,
    client_request_id TEXT NOT NULL,
    operation_id TEXT NOT NULL,
    user_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    subject_id TEXT NOT NULL,
    session_id TEXT,
    session_generation INTEGER,
    parent_request_id TEXT,
    request_fingerprint TEXT NOT NULL,
    state TEXT NOT NULL CHECK (state IN
        ('PENDING', 'ALLOWED', 'DENIED', 'CANCELLED', 'EXPIRED', 'INVALIDATED')),
    reason TEXT,
    created_at_ms INTEGER NOT NULL,
    deadline_at_ms INTEGER NOT NULL,
    completed_at_ms INTEGER,
    UNIQUE (requester_id, user_id, profile_id, client_request_id),
    FOREIGN KEY (parent_request_id) REFERENCES consent_request(request_id),
    FOREIGN KEY (session_id) REFERENCES consent_session(session_id),
    CHECK ((session_id IS NULL AND session_generation IS NULL) OR
           (session_id IS NOT NULL AND session_generation IS NOT NULL))
);

CREATE TABLE consent_requirement (
    request_id TEXT NOT NULL,
    requirement_id TEXT NOT NULL,
    definition_id TEXT NOT NULL,
    policy_version INTEGER NOT NULL,
    text_revision INTEGER NOT NULL,
    operation TEXT NOT NULL,
    purpose TEXT NOT NULL,
    recipient_id TEXT NOT NULL DEFAULT '',
    scope_canonical TEXT NOT NULL,
    scope_fingerprint TEXT NOT NULL,
    retention_policy_json TEXT NOT NULL DEFAULT '{}',
    decision TEXT CHECK (decision IN
        ('ALLOWED', 'DENIED', 'CONSENT_REQUIRED')),
    PRIMARY KEY (request_id, requirement_id),
    FOREIGN KEY (request_id) REFERENCES consent_request(request_id),
    FOREIGN KEY (definition_id, policy_version)
        REFERENCES consent_policy(definition_id, policy_version)
);

CREATE TABLE consent_decision (
    decision_id TEXT PRIMARY KEY,
    source_request_id TEXT,
    user_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    subject_id TEXT NOT NULL,
    definition_id TEXT NOT NULL,
    policy_version INTEGER NOT NULL,
    operation TEXT NOT NULL,
    purpose TEXT NOT NULL,
    recipient_id TEXT NOT NULL DEFAULT '',
    scope_canonical TEXT NOT NULL,
    scope_fingerprint TEXT NOT NULL,
    decision TEXT NOT NULL CHECK (decision IN ('ALLOWED', 'DENIED')),
    grant_mode TEXT NOT NULL CHECK (grant_mode IN
        ('ONCE', 'SESSION', 'TIMED', 'PERSISTENT')),
    session_id TEXT,
    expires_at_ms INTEGER,
    max_uses INTEGER CHECK (max_uses IS NULL OR max_uses > 0),
    revoked_at_ms INTEGER,
    grant_revision INTEGER NOT NULL,
    created_at_ms INTEGER NOT NULL,
    FOREIGN KEY (source_request_id) REFERENCES consent_request(request_id),
    FOREIGN KEY (session_id) REFERENCES consent_session(session_id),
    CHECK (grant_mode <> 'SESSION' OR session_id IS NOT NULL),
    CHECK (grant_mode <> 'TIMED' OR expires_at_ms IS NOT NULL),
    CHECK (grant_mode <> 'ONCE' OR (max_uses IS NOT NULL AND max_uses = 1)),
    FOREIGN KEY (definition_id, policy_version)
        REFERENCES consent_policy(definition_id, policy_version)
);

CREATE TABLE consent_usage (
    decision_id TEXT NOT NULL,
    enforcer_id TEXT NOT NULL,
    operation_id TEXT NOT NULL,
    execution_step_id TEXT NOT NULL,
    requirement_fingerprint TEXT NOT NULL,
    used_at_ms INTEGER NOT NULL,
    PRIMARY KEY (decision_id, enforcer_id, operation_id,
                 execution_step_id, requirement_fingerprint),
    FOREIGN KEY (decision_id) REFERENCES consent_decision(decision_id)
);

CREATE TABLE consent_authorization (
    receipt_id TEXT PRIMARY KEY,
    enforcer_id TEXT NOT NULL,
    user_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    subject_id TEXT NOT NULL,
    provider_id TEXT NOT NULL,
    operation_id TEXT NOT NULL,
    execution_step_id TEXT NOT NULL,
    requirement_fingerprint TEXT NOT NULL,
    session_id TEXT,
    session_generation INTEGER,
    scope_canonical TEXT NOT NULL,
    authorized_at_ms INTEGER NOT NULL,
    UNIQUE (enforcer_id, operation_id, execution_step_id,
            requirement_fingerprint),
    FOREIGN KEY (session_id) REFERENCES consent_session(session_id)
);

CREATE TABLE consent_data_permit (
    permit_id TEXT PRIMARY KEY,
    source_decision_id TEXT,
    source_receipt_id TEXT NOT NULL,
    session_id TEXT,
    user_id TEXT NOT NULL,
    profile_id TEXT NOT NULL,
    subject_id TEXT NOT NULL,
    operation_id TEXT NOT NULL,
    purpose TEXT NOT NULL,
    recipient_id TEXT NOT NULL DEFAULT '',
    scope_canonical TEXT NOT NULL,
    retention_scope TEXT NOT NULL CHECK (retention_scope IN
        ('OPERATION', 'SESSION', 'TTL')),
    storage_class TEXT NOT NULL CHECK (storage_class IN
        ('MEMORY_ONLY', 'PROTECTED_PERSISTENT')),
    expires_at_ms INTEGER,
    revoked_at_ms INTEGER,
    permit_revision INTEGER NOT NULL,
    state TEXT NOT NULL CHECK (state IN ('ACTIVE', 'INVALIDATED')),
    effective_policy_json TEXT NOT NULL,
    FOREIGN KEY (source_decision_id) REFERENCES consent_decision(decision_id),
    FOREIGN KEY (source_receipt_id) REFERENCES consent_authorization(receipt_id),
    FOREIGN KEY (session_id) REFERENCES consent_session(session_id),
    CHECK (retention_scope <> 'SESSION' OR session_id IS NOT NULL),
    CHECK (retention_scope <> 'TTL' OR expires_at_ms IS NOT NULL)
);

CREATE TABLE consent_artifact (
    artifact_id TEXT PRIMARY KEY,
    session_id TEXT,
    kind TEXT NOT NULL CHECK (kind IN
        ('TOOL_RESULT', 'MESSAGE', 'SUMMARY', 'EXTRACTED_FACT', 'MODEL_CONTEXT')),
    sensitivity_level INTEGER NOT NULL CHECK (sensitivity_level BETWEEN 0 AND 3),
    created_at_ms INTEGER NOT NULL,
    expires_at_ms INTEGER,
    state TEXT NOT NULL CHECK (state IN
        ('ACTIVE', 'BLOCKED', 'CLEANUP_PENDING', 'DELETED')),
    FOREIGN KEY (session_id) REFERENCES consent_session(session_id)
);

CREATE TABLE consent_artifact_permit (
    artifact_id TEXT NOT NULL,
    permit_id TEXT NOT NULL,
    PRIMARY KEY (artifact_id, permit_id),
    FOREIGN KEY (artifact_id) REFERENCES consent_artifact(artifact_id),
    FOREIGN KEY (permit_id) REFERENCES consent_data_permit(permit_id)
);

CREATE TABLE consent_artifact_parent (
    child_artifact_id TEXT NOT NULL,
    parent_artifact_id TEXT NOT NULL,
    PRIMARY KEY (child_artifact_id, parent_artifact_id),
    CHECK (child_artifact_id <> parent_artifact_id),
    FOREIGN KEY (child_artifact_id) REFERENCES consent_artifact(artifact_id),
    FOREIGN KEY (parent_artifact_id) REFERENCES consent_artifact(artifact_id)
);

CREATE TABLE consent_artifact_holder (
    artifact_id TEXT NOT NULL,
    holder_id TEXT NOT NULL,
    holder_instance_id TEXT NOT NULL,
    storage_class TEXT NOT NULL CHECK (storage_class IN
        ('MEMORY_ONLY', 'PROTECTED_PERSISTENT')),
    state TEXT NOT NULL CHECK (state IN
        ('RESIDENT', 'BLOCKED', 'CLEANUP_PENDING', 'DELETED')),
    PRIMARY KEY (artifact_id, holder_id, holder_instance_id),
    FOREIGN KEY (artifact_id) REFERENCES consent_artifact(artifact_id)
);

CREATE TABLE consent_cleanup (
    cleanup_id TEXT PRIMARY KEY,
    artifact_id TEXT NOT NULL,
    holder_id TEXT NOT NULL,
    holder_instance_id TEXT NOT NULL,
    invalidation_revision INTEGER NOT NULL,
    reason TEXT NOT NULL,
    state TEXT NOT NULL CHECK (state IN
        ('PENDING', 'ACKED', 'RETRY', 'FAILED')),
    created_at_ms INTEGER NOT NULL,
    acknowledged_at_ms INTEGER,
    retry_count INTEGER NOT NULL DEFAULT 0,
    last_error TEXT,
    UNIQUE (artifact_id, holder_id, holder_instance_id,
            invalidation_revision),
    FOREIGN KEY (artifact_id, holder_id, holder_instance_id)
        REFERENCES consent_artifact_holder
            (artifact_id, holder_id, holder_instance_id)
);

CREATE INDEX consent_decision_lookup ON consent_decision
    (user_id, profile_id, subject_id, definition_id,
     policy_version, operation, scope_fingerprint);
CREATE INDEX consent_request_pending ON consent_request(state, deadline_at_ms);
CREATE INDEX consent_request_operation ON consent_request(operation_id);
CREATE INDEX consent_grant_session ON consent_decision(session_id, grant_mode);
CREATE INDEX consent_session_deadlines ON consent_session(state, idle_deadline_at_ms);
CREATE INDEX consent_connection_session ON consent_connection(session_id, role, state);
CREATE INDEX consent_permit_session ON consent_data_permit(session_id, state);
CREATE INDEX consent_permit_source ON consent_data_permit(source_decision_id);
CREATE INDEX consent_artifact_session ON consent_artifact(session_id, state);
CREATE INDEX consent_artifact_source ON consent_artifact_permit(permit_id);
CREATE INDEX consent_artifact_derivatives ON consent_artifact_parent(parent_artifact_id);
CREATE INDEX consent_cleanup_pending ON consent_cleanup(state, created_at_ms);
```

`current_policy_version`과 `current_text_revision`이 가리키는 데이터의 존재는 등록·업데이트 트랜잭션에서 검증한다. 현재 버전 포인터는 검증이 완료된 정책·문구 조합으로만 전환한다.

세션·사용자·Subject·scope의 일치, PRIMARY 연결 하나의 제약, 현재 generation 검증, artifact 의존 그래프의 순환 금지, 모든 artifact의 유효한 permit 존재는 C++ 서비스 계층에서 트랜잭션과 함께 검증한다. SQL의 외래키만으로 권한 경계를 보장하지 않는다. 스키마에 PROTECTED_PERSISTENT를 예약했지만 초기 정책은 MEMORY_ONLY만 허용한다.

consent_authorization은 당시 AUTHORIZE 성공을 기록한 출처 증거이며 원본 재조회에 재사용하는 무기한 bearer 권한이 아니다. permit 등록은 해당 receipt의 제공자·수신자·실제 반환 범위와 결합하고, 중복 등록이 새로운 보관 기한을 만들지 않게 한다. permit·artifact 참조가 살아 있는 동안 필요한 출처 메타데이터를 먼저 삭제하지 않는다.

DB의 artifact·holder 레코드는 실제 데이터 사본 목록을 담는 제어 메타데이터다. 원문·요약 본문·KV 값은 저장하지 않는다. 행이 DELETED라는 이유만으로 물리 사본 삭제가 완료됐다고 간주하지 않고 holder ACK와 복구 검증을 요구한다.

`consent_connection`은 논리적 session 참여·접점 결합을 기록한다. 모든 accepted UDS의 fd·peer_pid를 이 테이블에 영구 저장할 필요는 없다. 실제 IPC ClientContext는 메모리에 두고 client_instance_id, immutable ucred snapshot, 검증된 역할, I/O owner, GCancellable, send queue, pending map을 관리한다. PID·UID는 요청한 운영 로그로 출력하며, 필요 이상의 프로세스 이력을 승인 DB에 누적하지 않는다.

### 13.3 트랜잭션 경계

| 작업 | 한 트랜잭션에 포함할 변경 |
|---|---|
| 등록 | definition, policy, message 세트, change revision |
| 업데이트 | 새 정책·문구, 현재 버전 전환, 필요한 승인·PENDING 무효화, change revision |
| 삭제 | 정의 비활성화, 관련 grant 철회, PENDING 무효화, change revision |
| 사용자 선택 | PENDING 조건부 전이, 조건별 결정, 필요한 grant 저장, change revision |
| AUTHORIZE | 필수 조건 전체 평가, 일회성 사용 기록, 중복 사용 검증 |
| 승인 철회 | grant 유효성 갱신, change revision |
| 세션 중단·재개 | 상태·generation·connection 변경, 필요한 prompt 무효화, change revision |
| 세션 종료 시작 | CLOSING 전이, session grant·permit 무효화, artifact 차단, holder cleanup 작업 생성, change revision |
| 데이터 취득 등록 | receipt 결합 검증, permit·artifact·holder 등록. 부모 의존 관계 포함 |
| 데이터 철회·TTL 만료 | permit 비활성화, 직접·파생 artifact 차단, cleanup 작업 생성 |
| 정리 ACK | holder·cleanup 상태 갱신, 전체 완료 조건 검증 후 artifact 및 session 종료 확정 |

결과 이벤트는 커밋 이후 발행한다. 커밋 후 이벤트 발행 전에 데몬이 종료되더라도 결과는 DB 조회로 복구한다. 이벤트의 정확히 한 번 전달에 의존하지 않는다. 단일 요청 UI의 부분 선택을 지원할 경우 전체 ALLOWED 여부와 조건별 grant 저장 규칙을 별도로 정의한다. 초기 UI는 필수 조건 집합의 허용·거부를 기본으로 하고 개별 grant 레코드는 분리 보관한다.

### 13.4 동시성·내구성·보존

- 초기 단일 연결·직렬화 구현에서는 WAL을 필수로 두지 않는다. 읽기 연결을 분리해 병렬 조회가 필요할 때 WAL을 검토한다.
- WAL은 읽기와 쓰기의 병행을 지원하지만 동시 writer는 하나다. checkpoint와 장기 읽기 트랜잭션을 관리해야 한다. [SQLite WAL 문서](https://www.sqlite.org/wal.html)
- 지속 승인과 철회 결과를 반환하기 전에 해당 정책에서 요구하는 내구성을 확보한다. WAL 채택 시 `synchronous=FULL`을 초기 후보로 두고 플랫폼 저장장치와 함께 검증한다. WAL의 NORMAL은 전원 차단 시 최근 커밋의 지속성이 약해질 수 있다. [SQLite WAL 내구성 설명](https://www.sqlite.org/wal.html)
- foreign_keys를 각 연결에 명시적으로 활성화하고, busy timeout과 제한된 재시도를 설정한다. 무한 대기하지 않는다.
- DB 손상·디스크 부족·쓰기 실패 시 기존 요청을 임의 ALLOWED로 만들지 않는다. API 오류로 반환하고 실행은 차단한다.
- 스키마 migration은 버전으로 관리하고 전환 실패 시 서비스 준비 상태에 진입하지 않는다. 바이너리 롤백과 DB 하위 호환 범위를 릴리스 정책으로 정한다.
- pending 기한은 런타임의 단조 시계로 관리하고 기록 시각은 별도로 저장한다. 재부팅·벽시계 변경 시 기간 승인 연장 여부가 불명확하면 보수적으로 재확인한다.
- 삭제는 즉시 비활성화·철회로 적용하고, 참조 중인 정책·문구·감사 정보의 물리 삭제는 보존 정책에 따라 수행한다.
- TTL 지난 요청·사용 기록을 정리하더라도 사용된 ONCE 승인을 다시 사용 가능하게 만들지 않는다. 기록 정리는 해당 grant의 영구 무효화·삭제와 조율한다.
- 세션·permit의 DB 시각은 복구·감사 용도이고 실행 중 timeout은 단조 시계 deadline으로 관리한다. 재시작 기본 정책은 세션 무효화이므로 과거 벽시계만으로 세션을 살리지 않는다.
- 파생 그래프가 커서 정리를 나눠 처리하더라도 종료·철회 커밋부터 사용 경계에서 부모 permit의 무효화를 확인해 데이터 사용을 차단한다. 비동기 정리 지연을 권한 유효기간 연장으로 해석하지 않는다.
- 새 정책 배포나 schema v2 적용 시 기존 SESSION 행은 신뢰할 수 있는 세션 결합이 없으면 무효화한다. 기존 PERSISTENT grant는 정책 의미가 같은 경우만 보존한다. 기존 조회 결과에 보관 permit을 사후 자동 부여하지 않는다.

## 14. 설치·등록·업데이트·삭제

### 14.1 등록

1. Installer가 패키지 신원과 자원 namespace 소유권을 검증한다.
2. 승인 정의, scope 스키마, 등급, 허용 방식, 다국어 문구를 추출한다.
3. consent_register가 consentd에 등록 작업을 전달한다.
4. consentd가 소유권, 정책 조합, 기본 언어, 변수 일치와 리소스 제한을 검증한다.
5. 한 트랜잭션으로 등록하고 성공 응답과 변경 이벤트를 제공한다.

파일 이름·패키지 경로·manifest 확장 위치는 Tizen 설치 규격과 함께 결정한다. 기존 `res/*.action` 데이터와 승인 정의를 연결할 수 있으나 이 문서가 새 설치 규격을 확정하지는 않는다.

### 14.2 업데이트

번역만 바꾸면 text_revision을 올린다. 접근 대상·목적·범위·등급 변경은 policy_version을 올린다. 정책 버전이 바뀐 승인은 기본적으로 재평가·재승인을 요구한다. 변경이 명확히 동등하거나 축소된 경우의 승인 보존은 검증된 마이그레이션 규칙이 있을 때만 적용한다.

### 14.3 삭제와 재설치

자원을 비활성화하고 관련 grant·캐시·PENDING 요청을 무효화한다. 동일 앱 ID 재설치만으로 예전 승인을 자동 복원하지 않는다. 설치 인스턴스·서명 신원과 사용자 데이터 복원 정책을 함께 검증한다.

### 14.4 패키지 DB와 consent DB의 일관성

패키지 설치와 consent DB 커밋은 별도 저장소일 수 있다. 같은 로컬 트랜잭션이라고 가정하지 않는다. Installer의 transaction_id와 설치 세대 정보를 통해 작업을 멱등 처리하고, 실패 시 보상·재조정한다. 설치가 최종 확정되지 않은 정의는 실행 가능한 상태로 노출하지 않는다. 재부팅 후 패키지 인벤토리와 활성 정의를 대조하는 복구 경로를 마련한다.

### 14.5 systemd socket activation

systemd가 `/run/.consentd.sock`을 bind/listen하고 연결 요청으로 consentd를 기동한다. `Accept=no`를 사용해 단일 consentd 프로세스에 listening FD를 전달한다. client마다 데몬을 별도 실행하는 `Accept=yes` 모델은 사용하지 않는다. [systemd.socket](https://man7.org/linux/man-pages/man5/systemd.socket.5.html)

아래는 패키징 초안이다. `/run/.consentd.sock` 경로와 socket activation은 확정이다. 실행 파일 경로, 전용 계정·그룹, timeout·backlog 값은 배포 환경에서 확정한다. 예시의 `consentd` 사용자와 `consent` 그룹은 패키지가 준비해야 하며 실제 계정의 존재를 가정하지 않는다.

`consentd.socket`:

```ini
[Unit]
Description=Tizen Consent Daemon Socket

[Socket]
ListenStream=/run/.consentd.sock
SocketUser=root
SocketGroup=consent
SocketMode=0660
Accept=no
Service=consentd.service
Backlog=64
RemoveOnStop=yes

[Install]
WantedBy=sockets.target
```

`consentd.service`:

```ini
[Unit]
Description=Tizen Consent Daemon
Requires=consentd.socket
After=consentd.socket

[Service]
Type=notify
NotifyAccess=main
ExecStart=/usr/bin/consentd
User=consentd
Group=consent
Sockets=consentd.socket
StateDirectory=consentd
StateDirectoryMode=0700
UMask=0077
Restart=on-failure
RestartSec=1s
TimeoutStartSec=15s
TimeoutStopSec=10s
StandardOutput=journal
StandardError=journal
SyslogIdentifier=consentd
```

service는 foreground에서 실행하고 별도 daemonize/fork를 하지 않는다. Type=notify이므로 초기화 완료 후 main thread에서 `sd_notify(0, "READY=1")`을 보내고 실패를 검사한다. READY는 DB 복구·필수 policy·I/O worker·listener·signal source가 사용 가능한 뒤에만 보낸다. [systemd.service](https://man7.org/linux/man-pages/man5/systemd.service.5.html), [sd_notify](https://man7.org/linux/man-pages/man3/sd_notify.3.html)

배포는 socket unit을 enable한다. service에 별도 WantedBy를 두고 항상 부팅 실행시키는 것은 기본안이 아니다. socket activation은 ‘시작 방식’이며 자동 idle 종료를 뜻하지 않는다. 기존 client 연결·ACTIVE/SUSPENDED 세션·PENDING 승인·정리 작업이 있는 동안 임의로 idle 종료하지 않는다.

예시의 StateDirectory를 사용할 경우 DB 후보 경로는 `/var/lib/consentd/consent.db`다. 대상 systemd가 해당 지시어를 지원하는지 확인하고, 미지원이면 패키징 단계에서 동일 권한의 영속 디렉터리를 생성하는 방식으로 조정한다. socket 파일은 `/run`에 있고, 승인 DB를 `/run`에 배치하지 않는다. 연결해야 할 argo·CM·Context Engine·UI·Installer에 필요한 DAC 접근과 Tizen 보안 label을 각각 부여한다.

현재 선택한 emulator는 `/var`가 `/opt/var`로 연결되므로 구현 상태 경로는 `/opt/var/lib/consentd/consent.db`로 정한다. 격리 시험은 `/opt/var/lib/consent-test`를 사용한다. 위 예시 경로를 그대로 따라 보호 디렉터리의 symlink 검사를 완화하지 않는다. 실제 패키징·검증 결과는 개발 가이드에 별도로 기록한다.

### 14.6 상속 FD 초기화와 소유권

시작 순서는 다음과 같다.

1. 다른 thread를 만들기 전에 `sd_listen_fds(1)`을 호출한다. activation 환경변수를 정리하고 반환값을 검사한다.
2. 기본안은 listener 1개만 허용한다. `SD_LISTEN_FDS_START`에서 받은 FD가 AF_UNIX/SOCK_STREAM·listening 상태·정확한 pathname인지 검증한다.
3. `g_socket_new_from_fd()`로 GSocket에 소유권을 넘기고 `g_socket_listener_add_socket()`으로 listener에 등록한다.
4. DB·worker·signal 초기화가 완료되기 전에는 접수 처리를 시작하지 않는다. GSocketService 생성 직후 stop하고 초기화 완료 후 start하는 방식으로 제어할 수 있다.
5. READY 알림과 main loop 시작을 조율한다. activation FD가 없으면 기본 운영 모드에서는 실패 종료한다. 같은 경로에 몰래 bind하는 fallback을 하지 않는다.

`sd_listen_fds(1)`은 전달 FD의 CLOEXEC 처리를 포함한다. systemd도 같은 listener의 사본을 유지하므로 consentd 종료 시 자신의 FD만 close하고 listener에 shutdown을 호출하지 않는다. [sd_listen_fds](https://man7.org/linux/man-pages/man3/sd_listen_fds.3.html)

아래 함수는 FD 인계 부분을 나타내는 C++ 예시다. 전체 main·worker·오류 복구 구현은 별도로 필요하다. 이 함수는 thread 생성 전에 한 번만 호출한다.

```cpp
#include <gio/gio.h>
#include <systemd/sd-daemon.h>
#include <sys/socket.h>
#include <unistd.h>

static gboolean add_activated_listener(GSocketService* service, GError** error)
{
    const int count = sd_listen_fds(1);
    if (count != 1) {
        if (count > 0) {
            for (int i = 0; i < count; ++i)
                close(SD_LISTEN_FDS_START + i);
        }
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Expected one activation listener; result=%d", count);
        return FALSE;
    }

    const int fd = SD_LISTEN_FDS_START;
    const int valid = sd_is_socket_unix(
        fd, SOCK_STREAM, 1, "/run/.consentd.sock", 0);
    if (valid <= 0) {
        close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Invalid activation listener; result=%d", valid);
        return FALSE;
    }

    GSocket* socket = g_socket_new_from_fd(fd, error);
    if (socket == nullptr) {
        close(fd); // wrapper 생성 실패: FD 소유권은 아직 호출자에게 있음
        return FALSE;
    }
    g_socket_set_blocking(socket, FALSE);
    const gboolean added = g_socket_listener_add_socket(
        G_SOCKET_LISTENER(service), socket, nullptr, error);
    g_object_unref(socket); // 등록 성공 시 listener가 자체 참조를 보유
    return added;
}
```

pathname 검증의 length=0은 일반 파일시스템 Unix socket에 해당한다. [sd_is_socket_unix](https://man7.org/linux/man-pages/man3/sd_is_socket.3.html) GSocket 생성 성공 후 fd를 다시 close하지 않는다. 생성 실패 시에는 호출자가 close한다. [GIO Socket.new_from_fd](https://docs.gtk.org/gio/ctor.Socket.new_from_fd.html) listener 등록 성공 시 listener가 참조를 보유한다. [GIO SocketListener.add_socket](https://docs.gtk.org/gio/method.SocketListener.add_socket.html)

systemd가 소유한 pathname을 consentd가 시작·종료 과정에서 unlink하거나 chmod하지 않는다. socket unit이 파일 수명·권한을 관리한다. 개발용 직접 bind 모드를 추가하더라도 별도의 명시적 옵션과 다른 테스트 경로를 사용하고, 운영 activation 경로와 혼용하지 않는다.

### 14.7 정상 종료·signal·join 순서

SIGTERM·SIGINT는 glib-unix의 signal source를 main context에 attach해 받는다. 일반 POSIX signal handler 안에서 mutex·DB·GObject 정리를 수행하지 않는다.

1. main에서 shutdown 상태를 원자적으로 전환하고 `sd_notify(0, "STOPPING=1")`을 보낸다. 중복 signal은 멱등 처리한다.
2. GSocketService를 stop해 새 접수를 중단한다. 이 호출만으로 기존 연결·작업이 닫혔다고 간주하지 않는다.
3. 각 I/O owner에 신규 일반 요청 차단을 게시한다. 세션 중단·정리 ACK 등 종료에 필요한 제한된 제어 메시지만 기한 내 허용한다.
4. 진행 요청·session·permit에 6.7의 종료 정책을 적용하고, 필요한 상태·cleanup 작업을 DB에 커밋한다. 관련 이벤트를 큐잉한다.
5. 새 worker job을 막고 이미 수락한 짧은 job의 완료·취소를 수집한다. main 또는 I/O owner가 자신이 처리해야 하는 callback을 기다리며 join하지 않는다.
6. 송신 큐를 제한된 시간 동안 drain한다. 이후 GCancellable로 read/write를 취소하고 각 accepted connection을 owner context에서 close한다. 취소 완료 callback과 source data를 정리한다.
7. 모든 I/O owner가 정지 가능 상태를 알리면 loop 종료를 게시한다. owner 자신이 아닌 종료 coordinator에서 GThread를 join한다.
8. 생산자가 더 이상 job을 만들지 않는 상태에서 DB executor에 종료 작업을 보내 미완료 필수 commit을 마무리하고 DB 연결을 닫는다.
9. 완료된 worker·DB thread를 join하고 남은 source·queue·GObject·mutex를 해제한다. main loop를 종료한다. 실제 순서는 어떤 callback도 이미 해제된 context로 게시하지 않도록 구현한다.

종료 처리는 비동기 상태 기계로 구성해 main이 필요할 때 계속 이벤트를 처리하게 한다. GThreadPool의 drain/free를 main에서 blocking 수행하려면 해당 job이 main 응답을 기다리지 않는다는 전제가 필요하며 기본안에서는 coordinator로 분리한다. TimeoutStopSec 안에 모든 외부 holder 삭제 ACK를 기다릴 수 없으면 미완료 cleanup을 DB에 남기고 다음 기동에서 재개한다. 정리 미완료를 CLOSED 성공으로 저장하지 않는다.

소켓 unit이 살아 있으면 service 종료 뒤 새 연결로 다시 활성화될 수 있다. 운영자가 완전히 중지하려면 socket과 service를 함께 중지한다. 데몬의 자체 listener FD close는 systemd의 listener 사본을 파괴하지 않아야 한다.

### 14.8 GLib·systemd 빌드와 호환성

기본 링크 대상은 `glib-2.0`, `gio-2.0`, `gio-unix-2.0`, `libsystemd`, `sqlite3`, `parcel`이며 GLib Unix signal header는 `<glib-unix.h>`를 사용한다. parcel은 bundle 저장소에서 제공하는 실제 pkg-config 이름이며 C++ 헤더는 `<parcel/parcel.hh>`와 `<parcel/parcelable.hh>`다. 개발 빌드에서는 pkg-config로 실제 include·link flag를 가져온다. IDL compiler의 Python은 별도 빌드·시험 의존성이다. `_GNU_SOURCE`는 Linux ucred를 사용하는 translation unit에서 시스템 헤더보다 먼저 정의하거나 빌드 옵션으로 설정한다.

사용 GLib·systemd 최소 버전은 대상 Tizen SDK에서 확정한다. 최신 웹 문서의 라이브러리 버전을 대상 기기 버전으로 가정하지 않는다. 최신 편의 API 없이도 GMainLoop, GThread, GThreadPool, GMutex, GAsyncQueue, GIO async stream, Linux SO_PEERCRED와 기본 sd-daemon API로 구성할 수 있도록 한다. GLib structured logging을 채택할 경우 지원 버전을 확인하고 기본 로그 adapter와의 호환 경로를 둔다.

이 CEP의 예제는 구조·API 사용 지침이다. 실제 Tizen 빌드, systemd activation, 동시 연결 부하, SMACK label 검증은 대상 SDK·기기에서 수행해야 한다.

## 15. 접근 제어와 신뢰 경계

| 호출 종류 | 검증할 사항 |
|---|---|
| request | argo 신원, 사용자·Subject 위임, 등록된 자원, 요청 범위 |
| check QUERY | 호출자가 조회할 수 있는 Subject·자원 범위 |
| check AUTHORIZE | 실제 enforcement owner 또는 허용된 위임, operation 결합 |
| register/update/unregister | Installer 권한, 패키지 namespace·소유권 |
| get_prompt/respond | 신뢰하는 UI 신원, 요청·버전·기한·사용자 세션 결합 |
| revoke | 해당 사용자 또는 권한 있는 관리 주체 |
| result lookup | 요청 소유자와 사용자 문맥. ID를 안다는 것만으로 접근 불가 |
| session open/suspend/resume/close | 인증된 세션 제어 주체, 사용자·Subject·접점 결합, generation·재연결 증명 |
| data register/derived | 검증된 취득 receipt 또는 부모 artifact, provider·holder 신원, 정책 제한 상속 |
| reuse-data AUTHORIZE | session ACTIVE, 허용된 holder·목적·수신자, 모든 의존 permit의 현재 유효성 |
| cleanup ACK | 작업을 받은 holder 인스턴스 또는 검증된 복구 주체. 다른 사본을 대신 삭제 완료 처리 금지 |

사용자에게 표시한 범위와 실행 범위가 일치해야 한다. 앱이나 argo가 임의로 민감도를 낮추거나 플랫폼 문구를 덮어쓸 수 없다. prompt, scope, callback 결과에 포함된 표시용 문자열을 실행 명령으로 해석하지 않는다. DB·IPC·로그에 실제 민감 데이터 원문을 불필요하게 보관하지 않는다.

정책 평가의 기본 우선순위는 플랫폼 금지 → 현재 유효한 명시 거부 → 유효한 허용 → 승인 필요로 제안한다. 거부·허용이 중첩된 범위의 세부 우선순위는 정책 비교기로 일관되게 처리하며, 의미가 불명확하면 허용하지 않는다.

### 15.1 peer credential 취득

accepted client socket에서 `getsockopt(fd, SOL_SOCKET, SO_PEERCRED, ...)`를 호출해 Linux `struct ucred`의 pid·uid·gid를 얻는다. listener FD가 아니라 각각의 accepted connection FD에 수행한다. `g_socket_get_credentials()`도 Linux에서 SO_PEERCRED를 사용하는 GLib 경로이므로 wrapper로 선택할 수 있다. [GIO Socket.get_credentials](https://docs.gtk.org/gio/method.Socket.get_credentials.html)

본 초안의 예시는 사용자 요구의 ucred 구조를 직접 보여주기 위해 native getsockopt를 사용한다. 소켓·연결 수명은 GIO가 관리하며 borrowed FD를 임의로 close하지 않는다. credential 취득 실패·잘못된 반환 길이는 해당 연결을 승인 처리 경로에 넣지 않고 오류 로그 후 종료한다.

```cpp
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <gio/gio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cerrno>

// I/O owner에서 최초 protocol payload 처리 전에 호출한다.
static gboolean read_peer_credential(GSocketConnection* connection,
                                     guint64 client_instance_id,
                                     struct ucred* output)
{
    GSocket* socket = g_socket_connection_get_socket(connection);
    const int fd = g_socket_get_fd(socket); // borrowed; 직접 close하지 않음
    struct ucred peer = {};
    socklen_t length = sizeof(peer);
    int rc;
    do {
        length = sizeof(peer);
        rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0 || length != sizeof(peer) || peer.pid <= 0) {
        const int saved_errno = rc < 0 ? errno : 0;
        g_log("consentd", G_LOG_LEVEL_WARNING,
              "event=peer-credential-failed client_instance=%" G_GUINT64_FORMAT
              " errno=%d credential_size=%u",
              client_instance_id, saved_errno, static_cast<unsigned>(length));
        return FALSE;
    }

    *output = peer;
    g_log("consentd", G_LOG_LEVEL_MESSAGE,
          "event=client-connected client_instance=%" G_GUINT64_FORMAT
          " peer_pid=%ld peer_uid=%lu peer_gid=%lu",
          client_instance_id,
          static_cast<long>(peer.pid),
          static_cast<unsigned long>(peer.uid),
          static_cast<unsigned long>(peer.gid));
    return TRUE;
}
```

peer credential은 해당 연결이 형성될 때의 상대 신원 snapshot이다. 요청 payload의 pid/uid로 덮어쓰지 않는다. 연결을 다른 프로세스에 FD passing하거나 credential 변경 후 같은 연결을 재사용하는 경로는 초기 protocol에서 허용하지 않는다. 클라이언트 fork·exec 및 신원 전환 시 기존 consent client를 폐기하고 재연결하는 계약을 둔다. SO_PEERCRED만으로 매 메시지의 실제 FD 보유자가 동일함을 증명할 수 있다고 주장하지 않는다. [Linux unix(7)](https://man7.org/linux/man-pages/man7/unix.7.html)

### 15.2 peer credential과 앱 역할

pid·uid·gid는 신원 검증의 출발점이다. 같은 UID로 실행되는 앱들을 UID만으로 argo·UI·Installer라고 구분할 수 없다. 커널 credential을 플랫폼의 신뢰할 수 있는 앱·서비스 identity 및 보안 label·위임 정책과 연결해 request/respond/admin/check 권한을 결정한다.

PID는 연결 인스턴스 로그용 식별자로 사용하고 영구 권한 키로 저장하지 않는다. PID 재사용이 가능하므로 `/proc/PID`의 현재 이름만 확인해 오래된 연결을 새 프로세스로 오인하지 않는다. 후속 앱 identity 확인 경로는 프로세스 수명 결합과 실패 시 차단 정책이 필요하다. 로그를 먼저 추가하는 초기 구현 단계에서도 미확인 역할에 privileged API를 허용하지 않는다.

client가 server identity를 확인하는 경우, systemd가 생성한 listener를 상속한 socket에서는 peer credential이 항상 consentd 실행 사용자·PID와 같을 것이라고 가정하지 않는다. `/run` pathname의 신뢰 가능한 소유권, systemd activation 경로, 플랫폼 서비스 identity 검증을 함께 설계한다. 초기 handshake의 self-declared server name만으로 신뢰하지 않는다.

대상 `/run`의 `root:system_share 0775`는 해당 경로에 한한 예외로 처리한다. root-owned socket의 연결 전후 device/inode 확인에 더해, 연결된 `SO_PEERCRED` UID 0/PID 1과 `getpeername()`의 정확한 `/run/.consentd.sock` 주소, 관측으로 확정한 `SO_PEERSEC`를 모두 확인한 뒤 통신한다. 원래 bind 주소 확인 없이 PID 1과 공통 라벨만 사용하면 다른 systemd socket을 rename한 경우를 구분하지 못한다. `getpeername()`은 family·반환 길이·NUL까지 검증한다. 이는 신뢰하는 system manager와 unit 정책에 의존하며, 쓰기 가능한 부모 경로의 서비스 거부를 막는다는 주장은 아니다. 상세 결정과 시험 조건은 `decisions.ko.md` D-07을 따른다.

### 15.3 로그 요구

| 시점 | 필수 로그 정보 |
|---|---|
| 데몬 시작 | daemon instance/epoch, endpoint, activation FD 검증 결과, 설정된 thread·queue 상한 |
| client 연결 | client_instance_id, peer_pid, peer_uid, peer_gid, 담당 I/O worker |
| client 역할 확인 | client_instance_id, 검증한 app/service identity, 허용 role 또는 거부 이유 |
| request·check 처리 | correlation ID, API 종류, 승인 결정·오류 코드, 처리 지연. 민감 인자는 제외 |
| 연결 종료 | client_instance_id, 저장된 peer credential, 종료 이유·잔여 작업 처리 |
| shutdown | 단계, drain·join 상태, 미완료 cleanup 수와 기한 초과 사유 |

초기 필수 구현은 위 예시처럼 연결 시 PID·UID·GID를 일반 로그 레벨에서 출력한다. debug level에서만 출력해 운영 시 사라지지 않게 한다. GLib log를 공통 adapter로 감싸 journald 또는 Tizen의 채택된 로그 backend에 연결한다. 제공한 service unit은 stdout/stderr를 journal로 수집한다. 사용자 data·resume token·전체 요청 JSON은 로그하지 않는다.

같은 오류의 반복은 rate limit을 적용한다. 로그 출력 전에 공유 상태를 snapshot으로 복사하고 critical section 밖에서 기록한다. fd 숫자는 재사용될 수 있으므로 주 correlation 키로 사용하지 않는다. credential 로그는 관측 수단이며 그 자체가 authorization 검사를 대신하지 않는다.

### 15.4 socket·IPC 연결과 대화 session

SO_PEERCRED가 식별하는 것은 consent 라이브러리를 통해 접속한 프로세스다. 사용자가 agent와 대화하는 frontend connection이나 논리적 session_id와 동일하지 않다. CM·Context Engine의 별도 UDS 연결도 검증된 session 문맥을 참조할 수 있다.

argo와 consentd 사이 IPC 단절은 먼저 캐시 UNSYNCED와 신규 접근 중단을 발생시킨다. 단순 IPC 재연결을 사용자 대화 세션 종료·재개로 자동 변환하지 않는다. 사용자 접점 연결 상실은 신뢰하는 세션 제어 주체의 보고·lease에 따라 6절 정책으로 처리한다. argo 프로세스 자체 소실이나 consentd 재시작은 기존 6.7의 보수적 세션 무효화 정책을 따른다.

## 16. 장애와 경쟁 조건

| 상황 | 요구 동작 |
|---|---|
| callback 수신 전에 argo 종료 | 원격 결과 유지, 재연결·재시작 시 ID로 조회. 사용자 작업 자동 재실행 금지 |
| request 접수 응답 유실 | client_request_id로 중복 생성 방지, 다른 payload이면 충돌 |
| 승인과 타임아웃 동시 발생 | PENDING 조건부 갱신으로 최종 결정 하나만 확정 |
| UI 응답 중 정책 업데이트 | 버전 확인 후 INVALIDATED, 새 정책에 이전 응답 적용 금지 |
| 데몬 재시작 | epoch 변경, 캐시 UNSYNCED, PENDING 복구 정책 적용 |
| 캐시 무효화 이벤트 지연 | request 사전 판단은 낡을 수 있음. AUTHORIZE 최신 확인으로 실행 통제 |
| 일회성 승인 동시 사용 | 트랜잭션에서 한 사용만 수락, 동일 실행 재시도는 중복 소비 방지 |
| 복수 조건 중 일부 미승인 | 전체 AUTHORIZE 실패, 해당 평가에서 부분 소비 금지 |
| 추가 승인 거부 | 보류 단계 취소·종료. 이미 수행한 변경은 서비스별 보상 정책 적용 |
| UI 표시 불가·번역 누락 | 오류 또는 명시된 실패 상태, 자동 허용 금지 |
| continuation 소유 서비스 재시작 | 유효한 복구 상태가 없으면 continuation 무효화 |
| 프로세스 캐시 사용 후 fork | 상속된 IPC·잠금·캐시를 사용하지 않고 새 클라이언트 초기화 |
| primary 접점 연결 상실 | 정책에 따라 CLOSING 또는 SUSPENDED. support IPC가 수명을 연장하지 않음 |
| 재연결 인증 실패·다른 프로필 재개 | 이전 승인·데이터 제공 금지 |
| 재개 후 이전 generation 이벤트 도착 | 현재 작업 결과로 적용 금지. ACK·결과 조회는 해당 과거 작업으로만 처리 |
| 데이터 TTL 만료 후 같은 세션 질문 | 데이터 사용 차단·정리. 필요 시 현재 조회 승인으로 재취득 |
| 일회성 조회 소비 후 후속 질문 | 별도 SESSION data-use permit이 있으면 결과만 재사용. 원본 재조회는 별도 평가 |
| inference 중 세션 종료·철회 | 신규 입력·출력 차단, 진행 작업 취소, 영향받은 문맥·캐시 정리 |
| cleanup 이벤트 유실·holder 무응답 | grant·permit 차단 유지, 작업 재조회·재전송·ACK 추적 |
| 혼합 출처 요약 중 한 출처 철회 | 요약 차단·폐기 또는 허용된 출처만으로 재생성 |
| 세션 제한 데이터를 영구 history에 저장 시도 | 저장 정책 위반으로 차단 또는 민감 내용 제외 |
| 메모리 압박 | 허용된 데이터도 조기 eviction 가능. 임의 디스크 spill 금지 |
| 잘못된 activation FD·endpoint | 초기화 실패, 자체 bind fallback·기존 socket unlink 금지 |
| client credential 취득 실패 | 해당 연결 종료·오류 로그, 승인 경로에 진입하지 않음 |
| stream frame 분할·합쳐짐·중간 EOF | framing 상태로 처리, 불완전 요청 실행 금지 |
| pool·DB·send queue 상한 초과 | 제한된 BUSY 또는 연결 종료, 큐 무한 증가 금지 |
| 오래된 연결의 job 완료 | client_instance_id·generation 검증 후 폐기 또는 해당 과거 요청에만 반영 |
| SIGTERM 중 async callback 완료 | owner context를 살아 있게 유지해 정리, 해제된 객체 접근 금지 |
| UID가 같은 다른 앱의 privileged 요청 | 검증된 역할·위임으로 재확인, UID만으로 허용하지 않음 |

## 17. 성능·메모리·관측성

본 CEP에서 지연·메모리 수치를 확정하거나 실측 결과로 주장하지 않는다. Tizen 대상 기기에서 기준선을 측정한다.

| 측정 항목 | 의미 |
|---|---|
| request cache hit latency | 로컬 범위 검사 및 SYNC 반환 / ASYNC 큐잉 지연 |
| request acceptance latency | IPC 접수 확인 지연. 사용자 응답 시간 제외 |
| check QUERY / AUTHORIZE p50·p95·p99 | 캐시·DB·사용 기록 영향을 구분한 실행 확인 지연 |
| consentd RSS / 라이브러리 추가 메모리 | 프로세스별 캐시 포함 메모리 증가 |
| cache hit rate / invalidation lag | 캐시 이득과 이벤트 최신성 |
| pending count / UI queue time | 승인 동시성·팝업 대기 상태 |
| transaction duration / busy / storage failure | SQLite 경합과 저장 장애 |
| active/suspended session count | 실제 대화 세션 규모와 재연결 보존 비용 |
| session artifact bytes / derived count | 세션별 원본·요약·모델 캐시 메모리와 출처 그래프 규모 |
| reuse-data batch latency | 후속 질문의 데이터 사용 검증 지연 |
| close-to-block / close-to-cleanup latency | 신규 사용 차단 시간과 실제 정리 완료 시간의 분리 |
| cleanup pending / retry / failed | 응답 없는 holder·저장소 정리 실패 추적 |
| main-loop dispatch lag | main thread에 잘못된 blocking I/O·DB 대기가 있는지 확인 |
| I/O worker load·연결 수 | fixed worker의 연결 분배와 장기 구독 비용 |
| worker/DB queue depth·BUSY count | admission 상한·부하 격리 동작 |
| per-client output bytes | slow reader와 무효화 이벤트 backlog |
| credential failure·role rejection | 접속 credential 오류와 실제 역할 거부를 구분 |
| activation-to-ready·shutdown drain | socket activation 초기 지연과 기한 내 정상 종료 |

승인 캐시는 상한과 LRU를 두고, SQL은 인덱스와 prepared statement를 사용한다. 프로세스마다 전체 DB를 복제하지 않는다. 다국어 문구 전체를 각 클라이언트 메모리에 적재하지 않는다. 장기 사용자 응답 대기는 작은 요청 상태와 타이머로 유지한다.

Tizen의 메모리 제약을 고려해 세션별 실제 데이터 bytes, 전체 동시 세션 수, 원본·파생 artifact 수, suspend 중 보관량에 각각 상한을 둔다. 긴 대화의 요약은 허용하되 provenance와 만료 규칙을 유지한다. 유휴 상태에서 전체 데이터를 유지할 수 없으면 최신 작업에 필요한 데이터만 남기고 나머지는 eviction한다. 최적화 수치는 실측 전 확정하지 않는다.

로그에는 operation_id, request_id, 결과 사유, 정책 버전과 필요한 식별자만 남긴다. Scope에 개인 정보가 포함되면 로그용 마스킹·요약을 적용한다. 로그를 통해 원문 데이터가 우회 노출되지 않도록 한다.

## 18. 대안과 선택 이유

| 대안 | 평가 | 제안 선택 |
|---|---|---|
| 각 서비스가 사용자 팝업 요청 | 승인 UX·정책이 분산되고 argo 전용 요청 원칙과 충돌 | argo만 요청, 서비스는 부족 조건 반환 |
| 프로세스별 DB 직접 읽기 | IPC는 줄지만 버전·권한·일회성 소비·철회 동기화가 분산 | consentd 소유, request 캐시 |
| ASYNC에서 즉시 결과만 반환값으로 전달 | 즉시·지연 결과 처리 코드가 분기됨 | 모두 callback |
| 기능 실행 승인에 데이터 권한 포함 | 새 데이터 접근이나 전달 범위를 놓치기 쉬움 | 자원별 조건 분리, AND 평가 |
| 모든 승인에 cache 적용 | 일회성 소비와 철회 최신성을 보장하기 어려움 | PERSISTENT·SESSION request 캐시, 최종 AUTHORIZE는 데몬 확인 |
| UI 앱에 모든 항목 문구 내장 | 항목 추가마다 UI 배포 필요 | 항목 문구 등록, 공통 문구 UI 소유 |
| WAL 무조건 사용 | 초기 단일 연결에서는 이득이 명확하지 않을 수 있음 | 연결·동시성 요구에 따라 선택 |
| session을 consentd IPC 소켓과 동일시 | CM·Context Engine 연결과 argo의 대화 수명이 다름 | 논리 session과 접점 connection·내부 IPC를 구분 |
| 연결 단절 때 무조건 모든 데이터 삭제 | 대화 재개 시 조회·승인을 반복할 수 있음 | 정책 선택형 CONNECTION_BOUND / RESUMABLE_CONVERSATION |
| SESSION 승인만 저장하고 데이터 수명은 방치 | 요약·프롬프트·history에 데이터가 남을 수 있음 | 독립 data-use permit과 holder 정리 계약 |
| grant 만료 시 모든 취득 데이터를 무조건 삭제 | ONCE 취득 후 합의된 대화 재사용을 표현하기 어려움 | 정상 소비·만료와 명시적 철회를 구분하고 결과 보관 정책 별도 적용 |
| close 응답을 전체 삭제 완료로 취급 | 분산 holder의 늦은 응답·오류를 숨김 | 즉시 권한 차단과 비동기 cleanup 완료를 구분 |
| client별 무제한 thread·async launch | 장기 연결 수에 비례한 스택·스케줄링 비용 | fixed I/O loop + bounded GThreadPool |
| 모든 상태에 recursive mutex | 잠금 범위와 소유권 문제를 감추기 쉬움 | GMutex 기본, 필요한 재진입에만 GRecMutex |
| daemon 자체 bind와 systemd listener 혼용 | pathname·FD 소유권 충돌 | systemd Accept=no listener 인계만 사용 |
| payload의 PID/UID 또는 UID 단독 인증 | 위조 또는 공유 UID 앱 구분 실패 | accepted socket ucred + 검증된 플랫폼 역할 |

## 19. 구현 단계와 검증 계획

### 19.1 단계

| 단계 | 산출물·범위 |
|---|---|
| 1. 기반 | consent C ABI, GMainLoop·I/O 서브스레드·GThreadPool, GMutex 규칙, UDS framing·ucred 로그, systemd socket activation, SQLite migration, 등록·갱신·삭제 |
| 2. 승인·세션 흐름 | request/check SYNC·ASYNC, UI·다국어, session 상태·connection 인증·기한·취소·복구 |
| 3. 승인 캐시 | PERSISTENT·SESSION 캐시, revision·epoch·generation, 변경 구독·재동기화 |
| 4. 복수 자원·데이터 수명 | 앱·Context 조건, 등급, AND, 일회성 AUTHORIZE, 취득 receipt·data-use permit·holder |
| 5. 대화 재사용·추가 승인 | provenance·요약·model context, continuation, 추가 승인, 종료·철회·TTL 정리 ACK |
| 6. 제품 검증 | 재연결·프로필 전환·history 제한·전원 차단·설치 복구·메모리·지연·장기 Context 접근 |

단계별 구현 이전에도 자원·requirements·operation 식별자는 확장 가능한 형태로 설계한다. 실제 출시 순서는 제품 일정에 맞춰 조정한다.

### 19.2 수용 기준

| ID | 확인 시나리오 | 기대 결과 |
|---|---|---|
| A-01 | CM·Context Engine이 request 호출 | 데몬에서 호출 권한 거부 |
| A-02 | SYNC와 ASYNC로 동일 정책·scope 확인 | 같은 판단. ASYNC만 callback 경로 |
| A-03 | ASYNC 즉시 ALLOWED·DENIED·캐시 적중 | 함수 반환 이후 callback, inline 호출 없음 |
| A-04 | check 승인 부족 | UI 생성 없이 CONSENT_REQUIRED와 부족 조건 반환 |
| A-05 | 30일 승인으로 90일 조회 | 기존 승인 재사용 불가 |
| A-06 | ‘항상 허용’ 반복 request | 유효한 로컬 캐시로 완료, 원격 request_id 강제 생성 없음 |
| A-07 | 승인 철회·프로필 전환·데몬 재시작 | 해당 캐시 무효화·재동기화, AUTHORIZE 허용 불가 |
| A-08 | 다른 앱이 동일 자원 ID 사용 | Provider·Subject 분리로 승인 전이 없음 |
| A-09 | 추가 승인 거부 | 보호 접근 미수행, 이전 단계 중복 실행 없음 |
| A-10 | 일회성 동시 AUTHORIZE | 하나의 유효 사용만 허용 |
| A-11 | 여러 조건 중 하나 부족 | 다른 일회성 승인도 이번 평가에서 소비되지 않음 |
| A-12 | 같은 operation·단계 재시도 / 다른 payload 재사용 | 중복 소비 방지 / 충돌 처리 |
| A-13 | 승인·취소·만료 경쟁 | 최종 상태 하나, 완료 이벤트 중복에도 callback 중복 없음 |
| A-14 | 커밋 직후·이벤트 전 데몬 종료 | 저장된 결과 조회로 복구 |
| A-15 | 다국어 locale·누락·변수 타입 오류 | 정해진 fallback·실패 처리, 화면과 승인 범위 동일 |
| A-16 | 화면 표시 중 정책 변경 | 이전 화면 응답으로 새 정책 승인 불가 |
| A-17 | 앱 삭제·업데이트 중 장애 | 미확정 정의 비노출, 복구 후 설치 상태와 정합 |
| A-18 | DB 오류·디스크 부족·전원 차단 | 허용 오판 없음, 내구성·복구 정책 충족 |
| A-19 | SYNC 대기 시간 초과 | 원격 요청 상태와 구분, 안정된 ID로 조회·취소 가능 |
| A-20 | callback에서 종료·재진입 | deadlock·use-after-free 없음 |
| A-21 | SESSION 승인 후 같은 범위의 후속 질문 | 동일 세션에서 팝업 반복 없이 허용된 데이터 재사용 |
| A-22 | ONCE 조회 + SESSION 보관 | 원본 조회는 한 번만 허용, 결과는 permit 조건 내 재사용 |
| A-23 | PERSISTENT 조회 + SESSION 보관 종료 | 조회 grant는 유지될 수 있으나 이전 세션 데이터는 정리 |
| A-24 | 접점 연결 상실: 두 lifecycle 정책 | CONNECTION_BOUND는 종료, RESUMABLE은 중단·유예 |
| A-25 | SUSPENDED 중 새 조회·기존 데이터 사용 | 모두 차단. 살아 있는 TTL 내 보유만 가능 |
| A-26 | 유예 내 인증 재연결 / 다른 사용자 재연결 | generation 갱신·정책 재확인 후 재개 / 거부 |
| A-27 | 재연결 후 과거 prompt·callback·continuation | 현재 generation으로 자동 실행되지 않음 |
| A-28 | heartbeat만 반복 / 계속 활동 중 max 수명 | 유휴 만료 회피 불가 / 절대 수명 만료 적용 |
| A-29 | TTL 만료 뒤 요약 생성·재연결 | 기한 연장 금지, 원본·관련 파생 결과 차단 |
| A-30 | 한 grant 철회 | 그 grant에 연결된 permit·원본·파생 결과만 추적 무효화. 필요 시 혼합 요약 전체 재생성 |
| A-31 | 세션 종료 직전 inference 실행 | 신규 입력·출력 경계 차단, generation 변경 인지, 관련 모델 문맥 정리 |
| A-32 | cleanup ACK 유실·holder 재시작 | 멱등 재전송·복구, 완료 확인 전 성공으로 위장하지 않음 |
| A-33 | session open ID 위조·다른 세션 artifact 참조 | 신원·위임·소유권 검증으로 접근 거부 |
| A-34 | MEMORY_ONLY 데이터에 메모리 압박 | 조기 eviction 또는 실패, 자동 디스크 spill 없음 |
| A-35 | tool result를 summary·history·embedding으로 변환 | 제한 상속, 영구 저장 무단 전환 금지 |
| A-36 | 외부 모델 입력 전달 | 해당 recipient·목적·보관 정책 허용 여부 별도 확인 |
| A-37 | daemon·argo 재시작·기기 재부팅 | 과거 session 자동 활성화 없음, 임시 데이터 차단·정리 |
| A-38 | 데이터가 eviction된 상태에서 유효한 SESSION grant | 현재 정책으로 재조회 가능, 없어진 데이터를 있는 것으로 응답하지 않음 |
| A-39 | 정리 중 holder 응답 실패 | 접근 금지 유지, CLOSING과 cleanup 실패 상태 가시화 |
| A-40 | 여러 부모의 요약·새 세션 복사 | 모든 부모 제한 적용, 새 세션 자동 권한 전이 없음 |
| A-41 | 세션 종료 이후 원본 조회 결과 지연 도착 | 격리 버퍼에서 폐기, 새 permit·모델 입력·history 생성 금지 |
| A-42 | 비활성 세션에 request/check | SESSION_INACTIVE/CLOSED 오류, 새 승인 팝업 반복 없음 |
| A-43 | main과 여러 client 동시 처리 | main loop 유지, 실제 read/write·credential 처리는 I/O 서브스레드 |
| A-44 | 다수 장기 구독 연결 | client당 thread 증가 없이 처리, UI·Installer 접수 경로 유지 |
| A-45 | 사용자 승인 장기 대기 | 작업 pool·DB transaction을 점유하지 않고 다른 check 처리 |
| A-46 | frame 분할·합쳐짐·초과 길이·중간 EOF | 정확한 복원 또는 오류 종료, 부분 요청 실행 없음 |
| A-47 | client가 payload PID·UID 위조 | 로그·권한 판단에 kernel ucred 사용 |
| A-48 | SO_PEERCRED 실패·잘못된 크기 | 연결 차단과 원인 로그, ALLOWED 생성 없음 |
| A-49 | thread·DB·send queue 과부하 | 설정 상한 준수, BUSY·연결 종료 처리, 메모리 무한 증가 없음 |
| A-50 | socket unit만 실행 후 최초 client 접속 | service 활성화, READY 후 정상 처리, endpoint 정확히 일치 |
| A-51 | activation FD 없음·잘못된 type/path·여러 FD | 초기화 실패, fd leak·자체 bind·unlink 없음 |
| A-52 | service 재시작, socket unit 유지 | 상속 listener 재사용, systemd listener에 shutdown 하지 않음 |
| A-53 | 동시 cache 갱신·취소·callback 재진입 | lock 순서 준수, mutex 보유 중 callback·IPC·DB 대기 없음 |
| A-54 | cache 적중 ASYNC를 dispatcher에서 호출 | inline callback 없음, nested loop 재진입 금지 계약 준수 |
| A-55 | 공유 UID의 비인가 앱이 request/respond/admin 호출 | 앱 역할·위임 검증으로 거부 |
| A-56 | SIGTERM 중 read/write·DB job 진행 | 새 admission 중단, owner 정리·drain·join, double-close·UAF 없음 |
| A-57 | 연결 종료 직후 worker 결과 도착·FD 번호 재사용 | 다른 client에 결과가 전달되지 않음 |
| A-58 | slow reader로 무효화 송신 큐 초과 | 연결 종료 후 client cache UNSYNCED, 재동기화 전 재사용 금지 |
| A-59 | ucred 연결·종료 로그 확인 | client instance별 PID·UID·GID·이유 표시, 민감 payload 미출력 |
| A-60 | shutdown 기한 내 holder ACK 미완료 | cleanup 복구 정보 유지, 허위 CLOSED 저장 없이 다음 기동에서 복구 |
| A-61 | IDL 생성 Parcelable을 client와 consentd가 교환 | 실제 parcel library로 같은 필드·고정 byte order를 복원, GBS 및 emulator에서 확인 |
| A-62 | 잘린 parcel, 초과 길이/개수, NUL 오류, trailing bytes, 잘못된 version | 할당·접근 전에 제한 검사, 부분 객체로 요청 실행 금지 |
| A-63 | 같은 IDL 반복 생성, 잘못된 schema, IDL 수정 후 빌드 | 동일 출력, 명확한 실패, 양측 코드 재생성 및 라이선스 유지 |

성능 기준값과 지원 locale별 화면 품질 기준은 구현 전 제품 요구로 추가한다. 이 문서 작성 과정에서 위 제품 테스트가 실행되었다는 의미는 아니다.

## 20. 미결정 사항

| ID | 항목 | 결정에 필요한 정보 |
|---|---|---|
| O-01 | 기존 Level 0~2 정의와 Level 3 분리 | Context Engine 실제 데이터 분류표 |
| O-02 | peer credential의 플랫폼 역할 매핑 | UDS 경로·socket activation은 확정. 공유 UID 앱 구분·SMACK label·위임 검증 상세 확정 |
| O-03 | 플랫폼·앱별 등록 범위 | Installer 확장 규격과 자원 namespace 정책 |
| O-04 | 지속·세션·기간 승인 허용표 | 등급·자원·작업별 제품 정책 |
| O-05 | callback dispatcher 연동 | argo Rust 실행 모델과 C/C++ 이벤트 루프 |
| O-06 | QUERY/AUTHORIZE 최종 API 노출 | 호출자 오용 방지, 모드 enum 또는 별도 wrapper 검토 |
| O-07 | 승인 기한·팝업 병합·재요청 제한 | UX·사용자 작업 수명 요구 |
| O-08 | 부분 범위 비교와 동적 분류 | 자원별 scope 비교기·등급 산출 계약 |
| O-09 | 장기 구독·스트리밍 철회 | Context 전달 단위와 중단 지연 요구 |
| O-10 | DB journal·동시 연결·내구성 | 대상 SQLite 버전, 저장장치, 지연 실측 |
| O-11 | 데이터 보존·사용 기록 정리 | 감사·개인정보·제품 복구 정책 |
| O-12 | 설치 DB와 consent DB 연계 | Installer 트랜잭션·재부팅 복구 절차 |
| O-13 | 캐시 크기·지연 수용 기준 | 프로세스 수·대상 TV 메모리·부하 측정 |
| O-14 | 세션 제어 주체와 primary 접점 | argo·플랫폼 대화 서비스 책임, 연결 종료 감지 경로 |
| O-15 | 세션 timeout 값 | idle·absolute·reconnect grace·연결 lease의 제품 기준 |
| O-16 | 등급별 결과 보관·reuse 조합 | ONCE+SESSION 허용 여부, Level 3 TTL·최소 문맥 범위 |
| O-17 | 모델 엔진 정리 능력 | KV/prefix cache 폐기, inference 취소, 출력 generation gate |
| O-18 | 영구 대화 기록 처리 | 세션 제한 데이터가 포함된 턴의 제외·마스킹·삭제 정책 |
| O-19 | cleanup 완료 기준과 지연 | holder ACK, 프로세스 사망 검증, 실패 재시도·운영 경보 |
| O-20 | 외부 모델·저장소 계약 | recipient별 전달 승인·보관·삭제 특성 |
| O-21 | 세션 메모리 예산 | 동시 세션, suspend 보존량, provenance·artifact 상한 |
| O-22 | 장기 저장 확장 | PROTECTED_PERSISTENT 허용 조건·암호화·키 수명·백업 정리. 초기 범위에서는 비활성 |
| O-23 | I/O worker·pool·queue 상한 | Tizen 메모리·동시 client·UI 제어 경로 부하 실측 |
| O-24 | GLib/GIO·systemd 최소 버전 | 대상 SDK API와 unit 지시어 지원 확인 |
| O-25 | service 계정·socket group·실행 경로 | 패키징 정책, 기존 서비스 접근 권한, StateDirectory 지원 |
| O-26 | parcel IDL의 상세 schema·wire version·최대 frame·timeout | parcel 사용과 작은 생성기는 확정. 상세 필드·호환성·framing 크기·handshake·backpressure는 구현과 검증으로 고정 |
| O-27 | 종료·로그 backend | journal·Tizen 로그 adapter, shutdown budget·레이트 제한 |

## 21. 대화 요구 추적

| 대화에서 추가된 내용 | 반영 위치 |
|---|---|
| consent / consentd, C API + C++ | 1, 4, 7 |
| 설치·등록·업데이트·삭제, SQLite3 | 13, 14 |
| argo만 승인 요청, CM·Context Engine은 확인 | 4, 9, 15 |
| UI 문구 다국어 | 11 |
| 접수는 짧은 SYNC, 사용자 결과는 ASYNC 가능 | 7.4, 9.1 |
| ALLOWED·DENIED도 callback | 7.4, 7.5 |
| request/check 모두 SYNC·ASYNC | 7.1, 7.2 |
| 동기 이름의 sync postfix 제거 | 7.1 |
| Level 0~2와 추가 민감 등급 | 6 |
| 항상 허용 프로세스 내부 캐시 | 12 |
| 앱 데이터 승인과 추가 승인 확장 | 5, 9.4, 10 |
| 세션(connection) 기반 승인·대화 유지 | 6.1~6.8, 7.7, 9.6~9.10 |
| 세션 승인 캐시·데이터 보관 분리 | 12.1~12.11 |
| 원본·요약·모델 문맥 정리 | 12.6~12.10, 13, 16, 19 |
| GMainLoop·서브스레드·thread pool | 4.2~4.6, 4.9~4.10 |
| mutex·recursive mutex·critical section | 4.7, 7.5, 14.7 |
| UDS /run/.consentd.sock | 4.8, 14.5~14.6 |
| systemd socket activation | 14.5~14.8 |
| ucred의 PID·UID·GID 로그 | 15.1~15.4 |
| bundle parcel library 기반 Parcelable 통신 | 2.1, 4.8, 14.8, 19.2 |
| 통신 IDL·간단한 compiler와 설계 문서 정비 | 4.8.2, 19.2, 20 |

## 22. 참고 자료와 문서 이력

본 CEP의 제품 설계는 대화에서 도출한 제안이다. 아래 외부 문서는 SQLite 운영과 문구 포맷 원칙의 참고 자료이며 Tizen consent의 기존 구현을 증명하는 자료가 아니다.

- [SQLite Write-Ahead Logging](https://www.sqlite.org/wal.html): WAL 동시성, checkpoint, 내구성 특성.
- [ICU Formatting Messages](https://unicode-org.github.io/icu/userguide/format_parse/messages/): 전체 문장 템플릿, 변수, 언어별 포맷 처리.
- [GLib MainContext.invoke](https://docs.gtk.org/glib/method.MainContext.invoke.html): context 소유와 직접 호출 조건.
- [GLib ThreadPool](https://docs.gtk.org/glib/struct.ThreadPool.html), [GLib RecMutex](https://docs.gtk.org/glib/struct.RecMutex.html): worker pool·재귀 잠금 API.
- [GIO SocketService](https://docs.gtk.org/gio/class.SocketService.html), [GIO ThreadedSocketService](https://docs.gtk.org/gio/class.ThreadedSocketService.html): 접수·연결별 worker 처리 모델.
- [GIO Socket.get_credentials](https://docs.gtk.org/gio/method.Socket.get_credentials.html): Linux SO_PEERCRED 연동.
- [GIO Socket.new_from_fd](https://docs.gtk.org/gio/ctor.Socket.new_from_fd.html), [GIO SocketListener.add_socket](https://docs.gtk.org/gio/method.SocketListener.add_socket.html): 상속 socket의 소유권·listener 등록.
- [Linux unix(7)](https://man7.org/linux/man-pages/man7/unix.7.html): Unix socket와 peer credential.
- [systemd.socket](https://man7.org/linux/man-pages/man5/systemd.socket.5.html), [systemd.service](https://man7.org/linux/man-pages/man5/systemd.service.5.html): upstream systemd manual의 man7 배포본.
- [sd_listen_fds](https://man7.org/linux/man-pages/man3/sd_listen_fds.3.html), [sd_is_socket_unix](https://man7.org/linux/man-pages/man3/sd_is_socket.3.html), [sd_notify](https://man7.org/linux/man-pages/man3/sd_notify.3.html): activation FD 검증과 readiness.
- 로컬 `~/tizen/platform/core/base/bundle`의 `src/parcel/parcel.hh`, `parcelable.hh`, `parcel.cc`, `parcel.pc.in`: 실제 parcel API, 오류 처리, byte order, 의존성. 대상 SDK API 지원은 별도로 확인한다.
- AUL `ac581e7`의 `src/aul/socket/packet.hh`: 기존 appfw Parcelable 메시지와 소유권 스타일 참고.

| 버전 | 변경 |
|---|---|
| 0.1 | 대화 내용을 통합. 사용자 요구와 제안을 구분하고 API·상태·다국어·캐시·복수 승인·DB·복구·검증 계획을 작성 |
| 0.2 | 논리적 대화 세션·connection 수명·재연결·SESSION 캐시를 기본 범위로 추가. 접근 승인과 결과 보관·reuse를 분리하고 취득 receipt·data-use permit·provenance·holder cleanup 계약, API·DB·수용 기준을 통합 개정 |
| 0.3 | GMainLoop·고정 I/O 서브스레드·bounded GThreadPool·GMutex/GRecMutex 규칙을 추가. `/run/.consentd.sock` UDS와 systemd socket/service unit, 상속 FD·ucred 로그 C++ 예시, GLib dispatch·framing·backpressure·종료 계약 및 추가 수용 기준 반영 |
| 0.4 | 사용자 요구로 JSON/GVariant wire 선택을 bundle parcel·Parcelable로 대체. 작은 IDL/compiler, 제한된 reader, 생성·호환성·라이선스·빌드 의존성과 검증 기준을 추가. 구현 완료와 설계 결정을 구별 |
| 0.5 | 승인 필드 원본에 결합하는 typed template v1, 제한된 source/type/schema, UI 지원 명시와 locale/token 결합, 단일 평문 formatter 및 검증 범위를 추가 |
