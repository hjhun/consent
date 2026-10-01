# API 04: 결과와 비동기 콜백 처리하기

[English](04-results-and-callbacks.en.md)

사용자가 선택하는 동안 Argo가 다른 일을 처리해야 한다면 ASYNC를 사용합니다.
콜백 결과는 빌려 받은 객체이므로 보관하려면 복제하세요. 제한된 대기, 원격 취소와
종료까지 포함한 완전한 실행 예제는 [request.c](../../../src/examples/request.c)입니다.

완전한 request.c는 operation=read 입력을 구성합니다. 콜백과 컨텍스트 소유 규칙은
아래 API 부분에도 적용되지만 요약 도구의 operation=execute 실행 파일은 아닙니다.
정확한 요약 요청은 [가이드 13 smoke actor](../../../tests/smoke/argo.c)가 구현합니다.
동작을 바꾸면서 같은 승인을 쓸 수 있다고 가정하지 마세요.


## 준비 사항

인증된 Argo 프로세스와 [API 03](03-request-and-check.ko.md)의 입력을 사용합니다.
`consent glib-2.0`에 연결하세요. 한 스레드에서 명시적인 GLib 컨텍스트와
클라이언트를 만들고, 접수 함수가 반환한 뒤에만 컨텍스트를 실행합니다.
독립된 인증 UI가 안내를 처리합니다.

## 1. 콜백 데이터 소유자 준비하기

```c
#include <consent.h>
#include <glib.h>

struct completion {
  GMainLoop *loop;
  consent_result_t *owned_result;
  int status;
};

static void completed(int status, const consent_result_t *result, void *data) {
  struct completion *completion = data;
  completion->status = status;
  if (status == 0)
    completion->status = consent_result_clone(result,
                                             &completion->owned_result);
  g_main_loop_quit(completion->loop);
}
```
콜백 상태가 0이 아니면 결과는 NULL입니다. 복제 실패도 오류로 처리하세요.
빌려 받은 콜백 인자를 해제하면 안 됩니다. `owned_result`는 NULL로 시작하고
소유자가 결과를 읽은 뒤 한 번 해제해야 합니다.

## 2. 승인 요청 제출하기

```c
/* Fragment: client uses context; params is the complete Argo input from API 03.
 * Run on its creating thread; completion stays alive through destroy. */
struct completion completion = {.loop = loop};
consent_async_id_t local_id = 0;
int status = consent_request_async(client, params, completed, &completion,
                                   &local_id);
consent_params_free(params);
params = NULL;
/* Only after API return: run the loop with the watchdog from request.c.
 * Do not let this stack context outlive completion/detach/destroy. */
```
무제한 기다리지 말고 완전한 `request.c`의 루프와 watchdog을 사용하세요.
이 예제는 원격 기한 60000 ms와 앱 watchdog 65000 ms를 설정합니다. Watchdog이
끝나면 로컬 전달을 분리하고 루프를 벗어난 뒤 원래 요청 ID로 원격 취소를
시도합니다.

## 3. 세 결과를 구분해서 확인하기

| 값 | 의미 | 다음 동작 |
| --- | --- | --- |
| 접수 반환 0 | 로컬 접수 완료 | 콜백을 기다립니다. 승인 아님 |
| 콜백 상태 0 | 결과를 읽을 수 있음 | decision 확인 |
| ALLOWED | 승인 요청 성공 | 검사자는 여전히 AUTHORIZE 필요 |
| DENIED/EXPIRED/CANCELLED/INVALIDATED | 사용할 요청 승인 없음 | 실행 금지 |
| 접수나 콜백 상태가 0이 아님 | API·통신 오류 | 상태 조정, 동작 금지 |

표는 소스 계약의 예상 결과이며 새 실행 출력이 아닙니다. 관리 API 결과에는
decision이 없을 수 있으므로 해당 API의 결과 필드를 확인합니다.

## 4. ID 용도 구분하기

| ID | 소유자와 용도 |
| --- | --- |
| `consent_async_id_t` | 로컬 handle; async_detach로 콜백 억제 |
| `client_request_id` | Argo의 원격 결과·취소 조회 입력 |
| 반환된 `request_id` | 저장한 승인 요청; UI의 get_prompt에 전달 |
| `operation_id`, `step_id` | 검사자의 변경 불가 AUTHORIZE 재시도 키 |

분리는 원격 작업을 취소하거나 소비된 ONCE를 되돌리지 않습니다. 원격 취소도
완료된 승인을 철회하지 않습니다. 결과가 불확실해서 재시도할 때는 같은 원격
입력을 유지하세요.

## 5. 소유 순서대로 종료하기

watchdog을 제거하고 필요하면 콜백을 분리합니다. 원격 취소는 컨텍스트를 실행하는
중이 아닐 때 조정합니다. 클라이언트를 성공적으로 종료한 뒤 콜백 user_data와
컨텍스트·루프를 해제하세요. 소유한 복제 결과를 읽고 해제합니다. 다른 호출과
종료가 경합하면 안 됩니다. 콜백 안에서 종료하면 현재 빌린 결과는 콜백 반환까지
유지되고 뒤의 대기 전달은 억제됩니다.

## 문제 해결

- WOULD_DEADLOCK: 콜백 컨텍스트 점유 밖에서 동기 작업을 하세요.
- DISCONNECTED: 새 handle을 만들고 원격 재시도 ID는 유지하세요.
- 만료: 요청 취소나 보호 동작 실패를 확인했다고 가정하지 마세요.
- Fork/exec 또는 신원 변경: 새 handle이 필요합니다. 상속 handle은 쓰지 않습니다.

## 참조: 콜백 실행·오류·GIO 수명

## 비동기 승인과 dispatcher 수명

```sh
consent-example-request SUBJECT PROFILE DEFINITION POLICY_VERSION \
  SCOPE PURPOSE RECIPIENT CLIENT_REQUEST_ID OPERATION_ID [SESSION GENERATION]
```

예제는 명시적 `GMainContext`와 그 context를 사용하는 client를 생성하고,
`consent_request_async()` 제출 후 복사된 입력 builder를 해제한 다음 생성 스레드에서
`GMainLoop`를 실행합니다. 원격 승인 deadline은 60초, 애플리케이션 watchdog은
65초로 설정합니다. 라이브러리 ASYNC의 로컬 timeout은 300초입니다. 별도로 인증된
UI가 prompt를 처리해야 합니다.

제출의 `0`은 로컬 접수입니다. 콜백은 즉시 결정·cache 결과도 API 반환 이후 큐에
전달되며, 유효한 등록당 최대 한 번, 라이브러리 lock 없이 실행합니다. callback
status는 오류일 수 있고 정상 결과도 DENIED/EXPIRED/CANCELLED/INVALIDATED일 수
있습니다. request 처리는 원격 PENDING을 최종 결정 또는 로컬 timeout까지 polling합니다.
직접 결과를 조회하면 PENDING을 받을 수 있습니다.

콜백은 빌린 결과를 clone하고 loop를 종료합니다. main은 이후 소유한 clone을 읽고
해제하며, callback context를 소유한 동안 동기 호출을 하지 않습니다. watchdog이
만료되면 먼저 로컬 콜백을 detach하고 **context iteration 밖에서** 원래 요청 ID로
원격 취소를 시도합니다. 취소 시 이미 최종 결정된 요청을 볼 수 있고 기존 grant는
철회하지 않습니다. 취소 실패는 이후에도 상태 조정이 필요합니다. 마지막으로 콜백
data와 loop/context를 해제하기 전에 client를 destroy하므로 대기 중인 콜백이 수명이
끝난 스택을 참조하지 않습니다.

콜백 내부의 destroy도 지원하며 현재 빌린 결과는 그 콜백 반환까지 유효합니다.
같은 dispatcher를 다른 스레드에서 실행하거나 ASYNC 제출 중 nested iteration을
하면 안 됩니다. callback context를 소유한 스레드의 SYNC 호출은 WOULD_DEADLOCK을
반환합니다. fork/exec 또는 identity 변경 후 새 핸들이 필요합니다. 연결 단절 후에는
새 온라인 핸들을 만들되 기존 연산을 조정할 때 원래 원격 재시도 ID를 유지합니다.


## 오류와 안전한 상태 조정

`consent_error_e`와 `consent_error_string()`을 사용하세요. 표준 값은 `tizen.h`의
alias입니다. PROTOCOL, OUTCOME_UNKNOWN, SESSION_INACTIVE, SESSION_CLOSED,
CONFLICT, STORAGE는 `TIZEN_ERROR_MIN_MODULE_ERROR`부터 시작하는 모듈 내부 여섯
값이며 별도로 배정된 플랫폼 module range가 아닙니다. 소비자·daemon·라이브러리의
error ABI가 같아야 하며 이전 미공개 `-200x` 값은 폐기되었습니다.

| 결과 | 호출자의 처리 |
|---|---|
| Status 0, 비 ALLOWED decision | 실행하지 않고 요청·check 상태를 따릅니다. |
| INVALID_PARAMETER | schema·소유권을 수정하며 승인·거절로 임의 해석하지 않습니다. |
| PERMISSION_DENIED | 신뢰 identity·위임을 수정하고 offline fallback으로 바꾸지 않습니다. |
| TIMEOUT / OUTCOME_UNKNOWN | 실행하지 않습니다. 필요 시 다시 연결해 요청 상태를 조회하거나 지원되는 같은 중복 제거 키와 동일 payload로 재시도합니다. |
| DISCONNECTED | 온라인 핸들을 새로 만들고 상태 조정 시 원격 연산 ID를 유지합니다. |
| WOULD_DEADLOCK | callback context 소유를 벗어나거나 ASYNC API를 사용합니다. |
| STALE / SESSION_INACTIVE / SESSION_CLOSED | 현재 정책·session·설치 상태를 조정하며 이전 승인을 재사용하지 않습니다. |
| CONFLICT | ID가 다른 내용과 이미 결합되었으므로 덮어쓰지 말고 실제 연산을 정리합니다. |
| BUSY / TOO_LARGE / NO_SPACE / IO / STORAGE / PROTOCOL | 보호 실행을 중단하고 보고된 자원 상한·저장소·transport 실패를 처리합니다. |

헤더 문서 형식은 로컬 참조 `platform/core/api/device/include/battery.h`,
`device/doc/device_doc.h`, `app-manager/include/app_context.h`,
`app-manager/doc/appfw_app_manager_doc.h`를 따릅니다. 위 계약은 이 저장소의
client·wrapper·daemon에서 확인했으며 외부 플랫폼 version/privilege 주석은 복사하지 않았습니다.


## Client GIO 소유권과 프로세스 signal

각 online client의 `IoContext`는 전용 I/O thread, `GMainContext`,
`GMainLoop`를 소유합니다. `SocketTransport`는 nonblocking `GSocket`,
read/write source와 제한된 partial-frame buffer를 소유합니다.
`CallbackDelivery`는 호출자의 callback context와 delivery source를 보유합니다.
I/O context는 thread 시작 전에 재사용 wake source를 attach하며 stop은
단조 상태이므로 loop 진입 전 요청도 유지됩니다. 하나의 scheduler가 operation
deadline, result polling, partial-frame deadline을 합치며 idle 상태에서는
주기 timer를 실행하지 않습니다.

Close는 I/O producer를 중단하고 join한 뒤 state lock 밖에서 delivery source를
파괴합니다. 사용자 callback은 호출자 context에서 lock 없이 실행되며 callback
안에서 close해도 borrowed result는 callback 반환까지 유효합니다. Fork child의
각 owner는 상속된 GLib pointer를 abandon하고 상속 lock, source 파괴, thread
join을 피합니다. Transport는 상속 socket FD를 한 번 닫습니다. Fork 후 새 handle을
만들어야 하며 상속 handle은 사용할 수 없습니다.

GIO에는 프로세스 전체 signal 영향이 있습니다. 확인한 target GLib 2.80.5의 첫
`GSocket` 초기화는 기존 custom handler가 있어도 `SIGPIPE`를 `SIG_IGN`으로
설정합니다. 전용 target probe(PID 3558234)로 확인했으며 socket send에도 GIO의
signal 억제 동작이 적용됩니다. Client는 GIO 호출 전후 signal handler를 저장하거나
복구하지 않습니다. 다른 library를 함께 사용하는 프로그램은 이 GIO 정책을 고려해야
합니다. C ABI, wire protocol, 인증 검사는 유지하지만 기존 raw-socket client의
signal 동작까지 동일하다는 의미는 아닙니다.

Online create는 hello 전송을 시도하고 확인 응답 전에 disconnect/timeout이 발생하면
OUTCOME_UNKNOWN을 반환할 수 있습니다. 두 create 함수 모두 실패 시 output handle을
NULL로 유지하므로 새 handle로 다시 연결해야 합니다. 이 handshake 오류는 보호 업무나
승인 요청을 발행했다는 의미가 아닙니다. 인증 role 거부는 hello 중 DISCONNECTED 또는
OUTCOME_UNKNOWN으로 나타날 수 있으며 연결 실패만으로 PERMISSION_DENIED 정책
판단이라고 해석하지 않습니다.

## 공개 오류 값과 업그레이드

숫자를 복사하지 않고 `consent_error_e`의 이름을 사용합니다. 표준 오류는
Tizen 별칭을 사용하며 기존 표준 값 8개는 유지됩니다.

| 공개 이름 | 값 |
|---|---|
| `CONSENT_ERROR_NONE` | `0` |
| `CONSENT_ERROR_INVALID_PARAMETER` | `-EINVAL` (`-22`) |
| `CONSENT_ERROR_OUT_OF_MEMORY` | `-ENOMEM` (`-12`) |
| `CONSENT_ERROR_PERMISSION_DENIED` | `-EACCES` (`-13`) |
| `CONSENT_ERROR_BUSY` | `-EBUSY` (`-16`) |
| `CONSENT_ERROR_NOT_FOUND` | `-ENOENT` (`-2`) |
| `CONSENT_ERROR_TIMEOUT` | `-ETIMEDOUT` (`-110`) |
| `CONSENT_ERROR_DISCONNECTED` | `-ENOTCONN` (`-107`) |

`CONSENT_ERROR_WOULD_DEADLOCK`은 표준 `-EDEADLK` 값으로 변경합니다.
표준 오류의 공개 별칭으로 `CONSENT_ERROR_STALE` (`-ESTALE`),
`CONSENT_ERROR_TOO_LARGE` (`-E2BIG`), `CONSENT_ERROR_NO_SPACE` (`-ENOSPC`),
`CONSENT_ERROR_INVALID_OPERATION` (`-ENOSYS`), `CONSENT_ERROR_IO` (`-EIO`)도
제공합니다.

consent 고유 오류는 Tizen 모듈 로컬 범위를 사용하며, 플랫폼 전체에서 할당한
모듈 번호를 뜻하지 않습니다.

| 공개 이름 | 값 |
|---|---|
| `CONSENT_ERROR_PROTOCOL` | `TIZEN_ERROR_MIN_MODULE_ERROR + 0` |
| `CONSENT_ERROR_OUTCOME_UNKNOWN` | `TIZEN_ERROR_MIN_MODULE_ERROR + 1` |
| `CONSENT_ERROR_SESSION_INACTIVE` | `TIZEN_ERROR_MIN_MODULE_ERROR + 2` |
| `CONSENT_ERROR_SESSION_CLOSED` | `TIZEN_ERROR_MIN_MODULE_ERROR + 3` |
| `CONSENT_ERROR_CONFLICT` | `TIZEN_ERROR_MIN_MODULE_ERROR + 4` |
| `CONSENT_ERROR_STORAGE` | `TIZEN_ERROR_MIN_MODULE_ERROR + 5` |

아직 공개 배포하지 않은 v0.1의 오류 번호를 수정한 변경입니다. `consentd`,
`libconsent`와 모든 사용 프로그램을 함께 다시 빌드하고 업그레이드해야 합니다.
이전 `-200x` 오류 값과 새 값을 혼용하는 구성은 지원하지 않습니다. Native
Parcel frame과 필드 구조는 같고 숫자 status 계약이 변경되었습니다.
함께 전달된 decision과 관계없이 0이 아닌 status에서는 보호 작업을 차단합니다.

---

[이전](03-request-and-check.ko.md) · [다음](05-sessions-and-data.ko.md) · [API 전체
목록](../02-c-api.ko.md)
