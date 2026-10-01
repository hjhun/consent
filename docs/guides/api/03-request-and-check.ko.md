# API 03: 승인을 요청하고 접근 허가 확인하기

[English](03-request-and-check.en.md)

서로 다른 인증 실행 파일로 요약 도구 한 번의 접근을 처리합니다. Installer는
정의를 등록하고 Argo는 요청하며 UI는 응답하고 CM은 실행 허가를 검사합니다.
모든 역할에 같은 요구 조건을 전달해야 합니다. 인자를 바꿔 역할을 얻을 수는 없습니다.

## 준비 사항

먼저 [정의를 등록](02-registration.ko.md)하세요. 네 실행 파일에 각자의 역할과
문맥을 위임합니다. 아래 값은 가이드 13의 격리 예제이며 제품 신원이 아닙니다.

## 1. Argo 승인 입력 준비하기

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "count": "1",
  "r0.definition": "smoke.cm.tool.summary",
  "r0.operation": "execute",
  "r0.scope": "cli:smoke-tool/summary",
  "r0.purpose": "developer-tool-smoke",
  "r0.recipient": "fixture-provider",
  "r0.policy_version": "1",
  "client_request_id": "demo-approval",
  "operation_id": "demo-approval",
  "deadline_ms": "20000"
}
```

| 필드 | 타입 | request 필수 | 의미 |
| --- | --- | --- | --- |
| `subject`, `profile` | 문자열 | 예 | 명시한 위임 대상과 프로필 |
| `count` | 십진 문자열 | 예 | 모두 충족해야 하는 조건 수 |
| `r0.definition` | 문자열 | 예 | 등록한 승인 정의 |
| `r0.operation`, `r0.scope` | 문자열 | 예 | 정확한 동작과 범위 |
| `r0.purpose`, `r0.recipient` | 문자열 | 예 | 사용 목적과 수신자 |
| `r0.policy_version` | 십진 문자열 | 이 예제 지정 | 현재 정책 버전 |
| `client_request_id` | 문자열 | 예 | Argo가 유지하는 원격 요청 ID |
| `operation_id` | 문자열 | 예 | 내용이 바뀌지 않는 재시도 ID |
| `deadline_ms` | 십진 문자열 | 아니요 | 승인 기한; 이 예제는 20000 |

JSON은 문자열 입력을 보여 주며 C API가 JSON을 읽는 것은 아닙니다.
다음 완전한 함수로 공통 요구 조건을 구성합니다.

```c
#include <consent.h>

/* Caller owns *output; fields are copied by every request/check call. */
int summary_requirement(consent_params_t **output) {
  consent_params_t *params = NULL;
  int status = consent_params_create(&params);
  if (status == 0)
    status = consent_params_set(params, "subject", "smoke.subject");
  if (status == 0)
    status = consent_params_set(params, "profile", "smoke.profile");
  if (status == 0)
    status = consent_params_add_requirement(params, "smoke.cm.tool.summary",
        "execute", "cli:smoke-tool/summary", "developer-tool-smoke",
        "fixture-provider");
  if (status == 0)
    status = consent_params_set(params, "r0.policy_version", "1");
  if (status != 0) {
    consent_params_free(params);
    params = NULL;
  }
  *output = params;
  return status;
}
```
## 2. Argo에서 요청하기

아래는 인증된 Argo 클라이언트에서 사용하는 호출 부분입니다. 동기 대기는 콜백
컨텍스트를 점유하지 않은 곳에서 실행해야 합니다. Argo가 기다리는 동안 독립된
UI 프로세스가 안내를 처리합니다.

```c
consent_params_t *params = NULL;
consent_result_t *result = NULL;
int status = summary_requirement(&params);
if (status == 0)
  status = consent_params_set(params, "client_request_id", "demo-approval");
if (status == 0)
  status = consent_params_set(params, "operation_id", "demo-approval");
if (status == 0)
  status = consent_params_set_int64(params, "deadline_ms", 20000);
if (status == 0)
  status = consent_request(client, params, 25000, &result);
/* Only status 0 permits reading result; ALLOWED remains advisory. */
consent_result_free(result);
consent_params_free(params);
```

요청 ID를 대기 없이 전달하려면 `consent_request_async()`와
[완전한 콜백 예제](04-results-and-callbacks.ko.md)를 사용하세요. Argo는 원래의
subject/profile/client_request_id로 대기 요청의 ID를 조회해 UI에 전달합니다.
로컬 async ID는 UI에 넘기는 요청 ID가 아닙니다.

## 3. UI에서 표시하고 응답하기

이 흐름은 버전 미지정 legacy 승인을 사용합니다. Approval-v2에서는 표시된 기본
grant_mode와 원래 선택 문맥을 유지하고 chosen_grant_mode를 별도로 반환합니다.
협상되고 허용된 선택만 접수합니다. [가이드 10](../10-feature-approval.ko.md)과
[현재 UI](../08-consent-ui-poc.ko.md)를 참고하세요. 이 legacy 요청에 v2 필드를
섞거나 기간을 몰래 바꾸지 않습니다.

별도 UI 프로세스에서 request_id와 locale을 넣고 `consent_get_prompt()`를 호출해
모든 조건을 표시합니다. prompt를 해제하기 전에 prompt_token을 응답 params에
복사하세요. 사용자가 선택한 뒤 해당 token, decision과 허용된 grant_mode로
`consent_respond()`를 호출합니다. 상태와 최종 결정도 확인하세요. 정책이나 token이
바뀌면 의도한 승인이 무효화될 수 있습니다. UI는 Argo의 결과 조회 API를 호출하지
않습니다.

[가이드 13](../13-tool-examples.ko.md#프로토콜과-입력-예)에 동시에 실행하는
Argo/UI 명령이 있습니다. 자동 응답은 테스트에만 사용하며 제품 UI에는 사용자의
선택이 필요합니다.

## 4. CM에서 실행 직전 허가 확인하기

공개 예제 check.c와 request.c 실행 파일은 operation=read를 사용하는 일반
예제입니다. check.c는 AUTHORIZE로 ONCE를 소비할 수 있지만 자원 동작은 하지
않습니다. 이 요약 도구의 operation=execute를 그대로 구현한 실행 파일은 아닙니다.
정확한 조건은 [가이드 13](../13-tool-examples.ko.md)의 고정 smoke Argo/tool-check
소스를 사용하세요. read 승인을 execute에 재사용할 수 없습니다.

같은 요구 조건에 별도 실행 ID를 붙입니다.

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "count": "1",
  "r0.definition": "smoke.cm.tool.summary",
  "r0.operation": "execute",
  "r0.scope": "cli:smoke-tool/summary",
  "r0.purpose": "developer-tool-smoke",
  "r0.recipient": "fixture-provider",
  "r0.policy_version": "1",
  "operation_id": "demo-authorized",
  "mode": "AUTHORIZE",
  "step_id": "tool-admission"
}
```
`consent_check()` 전에 AUTHORIZE와 두 고정 ID를 설정합니다. 상태 0,
ALLOWED와 receipt를 모두 확인하기 전에는 실행하지 마세요.
[완전한 check 예제](../../../src/examples/check.c)는 모든 입력과 호출 결과를
확인하고 해제하며 리소스에 접근하지는 않습니다. 가이드 13은 이 경계 뒤에서
실제 합성 동작을 수행합니다.

## 5. 예상 결과 확인하기

| 단계 | 소스 계약에 따른 예상 결과 | 보호 동작 |
| --- | --- | --- |
| 승인 전 | CONSENT_REQUIRED | 없음 |
| 승인 후 QUERY | 참고 상태 ALLOWED | 없음 |
| 승인 후 AUTHORIZE | 상태 0, ALLOWED, receipt | 연결된 동작 실행 가능 |
| 동일 AUTHORIZE 재시도 | 유효한 기존 receipt | 조정만 수행하고 동작 중복 금지 |
| ONCE 소비 후 새 operation | CONSENT_REQUIRED | 없음 |

위 표는 계약 예제이며 새 실행 기록이 아닙니다. 소유한 결과와 입력을 각각
해제하고 생성 스레드에서 클라이언트를 종료하세요.

## 문제 해결

불확실한 AUTHORIZE를 조정할 때는 같은 operation_id/step_id와 같은 조건을
유지합니다. 같은 ID에 다른 조건을 넣으면 CONFLICT입니다. 요청 결과나 캐시
승인은 현재 AUTHORIZE를 대신하지 않습니다. check는 UI를 열지 않으며 Argo만
승인을 요청합니다.

## 참조: 필드 제한과 재시도 규칙

## 인자 구성과 재시도 식별자

필드는 NUL로 끝나는 UTF-8 문자열을 복사합니다. 일반 값은 8,192바이트, 키는
ASCII 영문자·숫자·밑줄·점·하이픈으로 128바이트 이내입니다. `v`, `id`, `method`,
`status`, `role`, `pid`, `uid`, `gid`, 빈 키와 밑줄로 시작하는 키는 예약되어 있습니다.
builder는 서로 다른 필드 253개를 허용하지만 개별 연산과 64 KiB transport body
제한이 더 작을 수 있습니다. 임의 표시 인자나 호출자가 주장한 identity를 보내지 마세요.

| 연산 | 필수 필드와 주요 선택 필드 |
|---|---|
| request | `subject`, `profile`, `client_request_id`, `operation_id`, `count`, requirements. `session` 사용 시 현재 `generation`; `deadline_ms`는 100–300000, 기본 60000. |
| check QUERY | `subject`, `profile`, requirements; `mode=QUERY`가 기본. session/generation을 사용하면 현재 값이어야 합니다. |
| check AUTHORIZE | QUERY 필드에 `mode=AUTHORIZE`, 안정적인 `operation_id`, `step_id` 추가. |
| requirement N | `rN.definition`, `rN.operation`, `rN.scope`, `rN.purpose`, `rN.recipient`; typed definition은 `rN.policy_version` 필수. 데이터 보관 receipt에는 `rN.holder` 추가. |
| register/update | package/app 별도 인자; `definition`, `enforcer`, `operation_id`, `expected_generation`, `policy_version`, `text_revision`, `level`, `modes`, `default_locale`, `message.<locale>.title/body`. 선택적으로 `retention_ms`와 아래 typed schema. |
| unregister | package 인자와 `operation_id`, `expected_generation`. 패키지 내 모든 앱에 적용하며 app ID는 불필요합니다. |
| 요청 조회·취소 | `request_id` 또는 원래 `subject`, `profile`, `client_request_id`. 요청 소유권도 검사합니다. |
| revoke | `definition`, `subject`, `profile`. |

`consent_params_add_requirement()`로 기본 다섯 필드와 `count`를 원자적으로
추가합니다. 최대 16개의 서로 다른 조건을 AND로 평가합니다. `definition`,
`operation`, `purpose`는 유효한 비어 있지 않은 식별자여야 합니다. scope는 정확히
일치해야 하고 최대 4,096바이트이며 scope/recipient는 명시적인 빈 문자열일 수
있습니다. setter 성공은 해당 필드의 검증이며 전체 schema와 identity는 daemon이 검사합니다.

| 식별자 | 의미와 재시도 규칙 |
|---|---|
| `consent_async_id_t` | 핸들 내부 콜백 등록 ID로 detach에만 사용합니다. 원격 ID로 보내지 않습니다. |
| `client_request_id` | 인증된 소유자와 subject/profile 범위의 안정적인 요청 ID입니다. 접수 결과가 불확실하면 변경 없이 재사용합니다. |
| `request_id` | daemon이 발급한 저장 요청 ID입니다. cache 결과에는 없을 수 있습니다. |
| `operation_id` + `step_id` | 하나의 실제 AUTHORIZE 실행입니다. 같은 enforcer/ID/내용은 아직 유효한 기존 receipt를 반환하여 ONCE를 중복 소비하지 않습니다. |
| 등록 `operation_id` | 완전한 등록·삭제 연산 하나입니다. 온라인은 호출자 범위이며 오프라인 ID는 이미지 spool 전체에서 고유해야 합니다. 같은 내용으로만 재사용합니다. |
| `expected_generation` | Installer가 발급한 보호된 설치 generation입니다. 패키지 버전, 설치 시각이나 임의 UUID로 대체하지 않습니다. |

제출하기 전에 제품 연산과 함께 ID를 보관하세요. 응답을 잃었다고 새 ID를 만들지
마세요. 라이브러리는 동의 소비를 중복 제거하며 애플리케이션의 외부 부작용까지
중복 제거하지 않습니다. enforcer도 실제 실행을 중복 제거해야 합니다. session open과
derived-data 생성은 현재 operation-ID 중복 제거를 지원하지 않으므로 불확실한
결과 뒤에 무조건 재호출하면 안 됩니다.



### 실행 직전 AUTHORIZE

check 예제에는 실제 설정된 identity와 문맥을 입력합니다.

```sh
consent-example-check SUBJECT PROFILE DEFINITION POLICY_VERSION \
  SCOPE PURPOSE RECIPIENT OPERATION_ID STEP_ID [SESSION GENERATION]
```

대문자는 인자 자리 표시자입니다. 명시적으로 빈 recipient는 `""`로 전달합니다.
예제의 보호 연산은 `read`이며 앞선 승인과 scope/purpose/recipient 및 선택적
session이 정확히 같아야 합니다. 핵심 호출 순서는 다음과 같습니다.

```c
status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
if (status == 0)
  status = consent_check(client, params, 5000, &result);
/* QUERY는 ALLOWED여도 참고용입니다. 소유한 결과를 해제합니다. */
consent_result_free(result);
result = NULL;
if (status == 0)
  status = consent_params_set_check_mode(params, CONSENT_CHECK_AUTHORIZE);
/* 다음 호출 전에 안정적인 operation_id와 step_id를 설정합니다. */
if (status == 0)
  status = consent_check(client, params, 5000, &result);
if (status == 0 &&
    consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED &&
    consent_result_get(result, "receipt") != NULL) {
  /* 실제 enforcer가 이 문맥에 결합된 동작을 중복 없이 실행할 수 있습니다. */
}
consent_result_free(result);
```

전체 소스는 모든 setter의 오류를 검사하고 두 ID를 제공합니다. ONCE 소비 후
재시도하면 QUERY는 CONSENT_REQUIRED여도 AUTHORIZE는 같은 실행의 유효한 기존
receipt를 반환할 수 있습니다. 따라서 예제는 참고용 QUERY로 AUTHORIZE 재조정
여부를 결정하지 않습니다. check는 UI를 열지 않으며 필요할 때 argo가 별도 승인
요청을 해야 합니다.

## 파라미터 필드

공개 ABI는 opaque `consent_params_t` builder와 문자열 필드를 사용합니다.
`consent_params_set()`은 UTF-8 입력을 복사하며 정수 setter는 십진수로
변환합니다. 프로토콜 신원 필드와 밑줄로 시작하는 내부 필드는 예약되어 있습니다.

| 작업 | 필수 필드와 의미 |
|---|---|
| register/update | `operation_id`, `expected_generation`, `definition`, `enforcer`, 양수 `policy_version`·`text_revision`, 0–3의 `level`, 쉼표 구분 `modes`, `default_locale`, `message.<locale>.title`·`.body`; 아래 타입 템플릿 스키마·locale 별칭은 선택; package/app은 별도 API 인자 |
| unregister | 패키지명은 별도 인자이며 params에 재시도 시 같은 `operation_id`와 현재 `expected_generation` 전달; app ID 불필요 |
| request | `subject`, `profile`, 안정된 `client_request_id`, `operation_id`, requirements; 선택적으로 `session`과 해당 `generation`; `deadline_ms`는 로컬 대기 timeout과 별개 |
| check | `subject`, `profile`, requirements; mode는 QUERY 또는 AUTHORIZE; AUTHORIZE에는 `operation_id`·`step_id`도 필요 |
| requirement | `consent_params_add_requirement()`로 definition·operation·정확 scope·purpose·recipient 추가; 최대 16개; 타입 있는 정의는 일치하는 `rN.policy_version`도 필수 |
| session open | `subject`, `profile`; lifecycle은 CONNECTION_BOUND 또는 RESUMABLE_CONVERSATION; 시간 상한은 데몬이 검증 |
| session transition | `subject`, `profile`, `session`, 현재 `generation`; resume에는 회전되는 `resume_token`도 필요 |

현재 버전은 scope의 정확 일치를 비교합니다. UI는 등록 문구와 실제
scope·purpose·recipient를 함께 표시해야 합니다. 타입 있는 문구 변수는 검증된
요청 필드와 연결하며, 표시되는 과거 조회 기간과 취득 후 보관 기간은 별개
값입니다. Level 3은 ONCE만 허용합니다. 정책 의미가 변경되면
`policy_version`을 증가시킵니다. `text_revision`은 definition ID마다 단조
증가하며 재설치나 policy version 증가 후에도 이 규칙이 유지됩니다.
`default_locale`·등록 문구 map·locale 별칭 map 중 하나라도 변경하면 더 큰
text revision이 필요하며, 같은 map은 기존 revision을 유지할 수 있습니다.

승인 응답은 ONCE 승인을 소비하지 않고 현재 요구 조건 전체의 AND를 다시
판정합니다. 예를 들어 A는 이미 허용된 상태에서 B에 대한 선택을 기다렸는데,
사용자가 B를 승인하기 전에 A가 만료·철회되거나 다른 작업에서 소비되면 요청은
`INVALIDATED`로 종료합니다. 조건별 결과는 현재 상태(A `CONSENT_REQUIRED`,
B `ALLOWED`)를 나타내며, 명시적으로 선택했고 여전히 유효한 B 승인은 소비하지
않고 유지합니다. 결합된 작업의 실행을 허용한 결과는 아닙니다. 기존 요청은
종료 상태이며 자동으로 승인 화면을 다시 열지 않습니다. 추가 승인이 필요하면
인증된 요청자가 새 request/operation ID로 요청을 시작해야 합니다. 실제 보호
작업은 여전히 전체 조건에 대한 `AUTHORIZE`가 필요합니다.

아래는 인증된 집행 서비스가 C API로 사전 조회하는 예입니다. 0이 아닌
status에서는 실행을 차단합니다.

위의 완전한 요구 조건 함수와 API 01 조회 프로그램을 사용하세요.
현재 정책 버전을 넣고 상태 0인 경우 decision을 확인합니다.

---

[이전](02-registration.ko.md) · [다음](04-results-and-callbacks.ko.md) · [API 전체
목록](../02-c-api.ko.md)
