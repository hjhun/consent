# API 01: C API 클라이언트 시작하기

[English](01-start.en.md)

현재 승인 상태를 조회하는 작은 C 프로그램을 만듭니다. 이 프로그램은 QUERY만
수행하므로 승인 화면을 열거나 보호된 데이터를 읽지 않습니다.

## 준비 사항

같은 빌드의 개발 헤더와 라이브러리가 필요합니다. 실행 파일에는 checker 역할과
subject/profile/enforcer 위임이 등록되어 있어야 합니다. root 계정만으로는
호출 권한이 생기지 않습니다. 이 프로그램은 고정된 운영 endpoint를 사용합니다. 통합자가 정확한 실행 파일을
등록해야 합니다. 가이드 12는 자체 고정 smoke 실행 파일과 전용 라이브러리만
등록하며 새 소비자 프로그램을 등록하지 않습니다.

## 1. 헤더를 포함하고 라이브러리 연결하기

`<consent.h>`와 `pkg-config consent`를 사용합니다. 아래 완전한 프로그램을
`query.c`로 저장하세요. 일곱 인자는 이미 등록된 정의와 접근 조건을 지정합니다.
프로그램은 클라이언트를 만들고 한 번 조회한 뒤 소유한 객체를 모두 해제합니다.

### 최소 C 조회 프로그램

세션 없는 예제로 동작은 read입니다. SESSION 검사는
[check.c](../../../src/examples/check.c)의 session/generation 인자를 사용합니다.
종료 0은 조회상 ALLOWED일 뿐이며 실행 receipt를 얻지 않습니다.

```c
#include <consent.h>
#include <stdio.h>

int main(int argc, char** argv) {
  consent_client_h client = NULL;
  consent_params_t* params = NULL;
  consent_result_t* result = NULL;
  int exit_status = 1;
  int status;

  if (argc != 8) {
    fprintf(stderr, "Usage: %s SUBJECT PROFILE DEFINITION POLICY_VERSION "
        "SCOPE PURPOSE RECIPIENT\n", argv[0]);
    return 2;
  }
  status = consent_params_create(&params);
  if (!status)
    status = consent_params_set(params, "subject", argv[1]);
  if (!status)
    status = consent_params_set(params, "profile", argv[2]);
  if (!status)
    status = consent_params_add_requirement(params, argv[3], "read",
        argv[5], argv[6], argv[7]);
  if (!status)
    status = consent_params_set(params, "r0.policy_version", argv[4]);
  if (!status)
    status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
  if (!status)
    status = consent_client_create(&client);
  if (!status)
    status = consent_check(client, params, 5000, &result);
  if (!status) {
    consent_decision_e decision = consent_result_get_decision(result);
    const char* name = consent_result_get(result, "decision");
    printf("QUERY: %s (advisory only)\n", name ? name : "UNKNOWN");
    exit_status = decision == CONSENT_DECISION_ALLOWED ? 0 : 3;
  } else {
    fprintf(stderr, "query: %s (%d)\n", consent_error_string(status), status);
  }
  consent_result_free(result);
  consent_params_free(params);
  if (client) {
    status = consent_client_destroy(client);
    if (status) {
      fprintf(stderr, "destroy: %s (%d)\n", consent_error_string(status), status);
      exit_status = 1;
    }
  }
  return exit_status;
}
```

```sh
cc -std=c11 -Wall -Wextra query.c -o consent-query \
  $(pkg-config --cflags --libs consent)
./consent-query SUBJECT PROFILE DEFINITION POLICY_VERSION SCOPE PURPOSE RECIPIENT
```

실제 값으로 바꾸고 빈 recipient는 `""`로 전달하세요. 종료 코드 1은 API 오류,
2는 인자 개수 오류, 3은 ALLOWED 이외의 결정입니다. QUERY가 CONSENT_REQUIRED이면
argo에 승인 요청을 맡기고, 실제 접근 직전에는 아래 AUTHORIZE 흐름을 사용합니다.

## 헤더와 소유권

| 헤더 | 함수 또는 선언 |
|---|---|
| `consent_common.h` | 핸들, enum, callback 타입, `consent_error_string` |
| `consent_client.h` | 온라인 생성, 오프라인 생성, destroy |
| `consent_params.h` | create/free/set/set_int64/set_check_mode/add_requirement |
| `consent_request.h` | request/check SYNC·ASYNC, detach, 결과 조회, cancel |
| `consent_registration.h` | register/update/unregister/revoke |
| `consent_prompt.h` | prompt 조회, 응답, 일반 텍스트 formatter |
| `consent_session.h` | open/heartbeat/suspend/resume/close/get_state |
| `consent_data.h` | data 등록·파생·해제, cleanup 상태·목록 |
| `consent_result.h` | free/clone/decision/get/size/get_at |
| `consent.h` | 모든 기능별 헤더를 포함하는 umbrella |

각 기능별 헤더는 C와 C++에서 단독으로 포함할 수 있습니다. private C++ 헤더는
설치하지 않습니다. 개발 패키지가 설치되어 있다면 다음과 같이 빌드합니다.

```sh
cc -std=c11 consumer.c -o consumer $(pkg-config --cflags --libs consent)
cc -std=c11 async-consumer.c -o async-consumer \
  $(pkg-config --cflags --libs consent glib-2.0)
```

| 객체 | 소유권과 수명 |
|---|---|
| `consent_client_h` | 소유자는 하나이며 생성 스레드에서 destroy합니다. raw handle을 쓰는 다른 호출과 destroy가 겹치면 안 됩니다. |
| `consent_params_t` | 호출자가 소유하는 단일 스레드 builder입니다. API 반환 전에 필드가 복사되므로 이후 해제·재사용할 수 있습니다. |
| 동기 결과 | 성공 시 호출자 소유, 오류 시 출력 NULL입니다. `consent_result_free`로 해제합니다. |
| 콜백 결과 | 콜백 반환까지 빌린 값입니다. callback status가 0이 아니면 NULL입니다. 보관하려면 clone하고 빌린 결과는 해제하지 않습니다. |
| 결과 필드 포인터 | 결과 수명까지 빌린 값입니다. 존재하는 빈 필드와 없는 필드는 다릅니다. |
| 포맷된 prompt | 호출자가 소유하는 UTF-8 할당이며 표준 `free()`로 해제합니다. |
| 오류 문자열 | 정적 문자열을 빌려 쓰며 해제하지 않습니다. |

출력은 NULL 또는 0으로 초기화하세요. `consent_result_get_at()`은 인자가 잘못되면
출력 포인터를 변경하지 않습니다. NULL 결과는 크기 0, decision UNKNOWN입니다.
결과를 해석하기 전에 API status를 검사하고, 해제한 객체를 다시 사용하지 마세요.


## 실행 가능한 예제 빌드

[`src/examples`](../../../src/examples/)에는 다음 네 실행파일의 소스가 있습니다.

| 소스 / 실행파일 | 목적 |
|---|---|
| [`check.c`](../../../src/examples/check.c) / `consent-example-check` | QUERY 후 안정적인 실행 ID로 AUTHORIZE |
| [`request.c`](../../../src/examples/request.c) / `consent-example-request` | ASYNC 승인, 명시적 GLib dispatcher, 결과 clone, watchdog, detach/cancel과 종료 |
| [`register.c`](../../../src/examples/register.c) / `consent-example-register` | package/app을 별도 인자로 받는 완전한 온라인 definition 등록 |
| [`offline-register.c`](../../../src/examples/offline-register.c) / `consent-example-offline-register` | 같은 register API를 통한 명시적 root 이미지 staging |

컴포넌트 CMake는 `consent`에 링크하고 request 예제에만 GLib를 추가합니다.
설치 대상은 `${libexecdir}/consent/examples`이며 소스는 `${docdir}/src/examples`에
설치합니다. 가이드는 `${docdir}/docs/guides`에 설치하여 여기의 상대 소스 링크가
유지됩니다. 실제 prefix, docdir, libexecdir은 패키지 CMake 설정을 따릅니다.
설치된 소스 디렉터리에서 직접 빌드할 수도 있습니다.

```sh
cc -std=c11 -Wall -Wextra check.c example_common.c -o consent-example-check \
  $(pkg-config --cflags --libs consent)
cc -std=c11 -Wall -Wextra request.c example_common.c -o consent-example-request \
  $(pkg-config --cflags --libs consent glib-2.0)
```

등록 예제는 첫 명령의 소스 이름을 바꿔 빌드합니다. 인자 없이 실행하면 연결이나
쓰기 없이 사용법을 표시합니다. 종료 코드는 예제의 성공 결과 0, API·초기화 실패 1,
인자 개수 오류 2, request/check의 비 ALLOWED 결정 3입니다. request의 종료 코드
0도 참고용 승인 결과입니다. check 예제는 실제 AUTHORIZE를 수행하여 ONCE를
소비할 수 있지만 보호 자원 동작 자체는 수행하지 않습니다. 컴파일 성공은 권한을
갖춘 타깃 실행의 증거가 아닙니다.

## 연동 계약

공통 신원·세션·정리·호환 계약은
[API 전체 목록](../02-c-api.ko.md#api-연동-계약)을 참고하세요. 이 소비자도 정확한
운영 실행 파일 신원을 등록해야 합니다.

---

[다음](02-registration.ko.md) · [API 전체 목록](../02-c-api.ko.md)
