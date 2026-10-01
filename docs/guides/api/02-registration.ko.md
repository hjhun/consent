# API 02: 승인 정의 등록하기

[English](02-registration.en.md)

Installer는 승인이 필요한 동작과 UI에 표시할 문구를 등록합니다. 등록만으로
승인이 생기지 않습니다. 아래 예제는 가이드 13의 합성 요약 도구를 등록합니다.
제품 도구에는 해당 제품의 신뢰된 정책과 신원이 필요합니다.

## 준비 사항

인증된 Installer 클라이언트를 사용합니다. 테스트 환경에서 `smoke.package` 관리
권한이 위임되어야 하며 `smoke.app`이 그 패키지에 속해야 합니다. 세대는 완료된
설치 authority에서 받으세요. 호출자가 만든 UUID를 넣으면 안 됩니다.
[가이드 03](../03-installation-authority.ko.md)이 설치 순서를 설명하고, 가이드 13의
실행 도구가 예제 신원을 준비합니다.

## 1. 승인 정의 준비하기

패키지 `smoke.package`와 앱 `smoke.app`은 `consent_register()`의 별도 인자입니다.
나머지 입력은 아래와 같습니다. 숫자도 문자열 필드로 넣습니다. 세대 자리표시자는
신뢰된 발급 값으로 바꾸세요.

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "operation_id": "register-summary",
  "expected_generation": "<trusted Installer generation>",
  "definition": "smoke.cm.tool.summary",
  "enforcer": "cm",
  "policy_version": "1",
  "text_revision": "1",
  "level": "1",
  "modes": "ONCE,PERSISTENT",
  "retention_ms": "60000",
  "default_locale": "en",
  "message.en.title": "Developer smoke approval",
  "message.en.body": "Allow this isolated fixture tool operation?"
}
```

| 필드 | params 타입 | 이 예제 필수 | 의미 |
| --- | --- | --- | --- |
| `subject`, `profile` | 문자열 | 예 | 위임된 승인 대상과 프로필 |
| `operation_id` | 문자열 | 예 | 등록 재시도에서 유지할 ID |
| `expected_generation` | 문자열 | 예 | Installer가 완료한 설치 세대 |
| `definition`, `enforcer` | 문자열 | 예 | 정책 이름과 위임된 검사자 |
| `policy_version` | 십진 문자열 | 예 | 양수 정책 버전 |
| `text_revision` | 십진 문자열 | 예 | 양수 표시 문구 버전 |
| `level` | 십진 문자열 | 예 | 신뢰된 민감도 0–3 |
| `modes` | 쉼표로 구분한 문자열 | 예 | 허용할 접근 승인 기간 |
| `retention_ms` | 십진 문자열 | 이 예제 지정 | 데이터 보관 한도, 승인 기간과 별개 |
| `default_locale` | 문자열 | 예 | 완전한 기본 번역 |
| `message.en.title`, `message.en.body` | 문자열 | 예 | 표시할 승인 제목과 본문 |

JSON은 `consent_params_set()` 입력을 읽기 쉽게 보여 줍니다. 공개 JSON 등록
endpoint나 JSON 자동 로더는 없습니다.

## 2. 입력을 구성하고 register 호출하기

다음 완전한 함수는 인증된 클라이언트와 신뢰된 세대를 받습니다. 모든 설정 결과를
확인하고 오류 시 중단하며 입력 객체를 해제합니다. 클라이언트와 세대 문자열은
호출자가 계속 소유합니다.

```c
#include <consent.h>
#include <stddef.h>

/* client belongs to the authenticated Installer; generation is provisioned. */
int register_summary(consent_client_h client, const char *generation) {
  const struct { const char *key; const char *value; } fields[] = {
    {"subject", "smoke.subject"},
    {"profile", "smoke.profile"},
    {"operation_id", "register-summary"},
    {"expected_generation", generation},
    {"definition", "smoke.cm.tool.summary"},
    {"enforcer", "cm"},
    {"policy_version", "1"},
    {"text_revision", "1"},
    {"level", "1"},
    {"modes", "ONCE,PERSISTENT"},
    {"retention_ms", "60000"},
    {"default_locale", "en"},
    {"message.en.title", "Developer smoke approval"},
    {"message.en.body", "Allow this isolated fixture tool operation?"}
  };
  consent_params_t *params = NULL;
  int status = consent_params_create(&params);
  for (size_t i = 0; status == 0 && i < sizeof(fields) / sizeof(fields[0]); ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_register(client, "smoke.package", "smoke.app", params);
  consent_params_free(params);
  return status;
}
```
## 3. 성공 확인하고 클라이언트 해제하기

상태 0은 온라인 등록이 commit됐다는 뜻입니다. grant는 생기지 않으므로 접근에는
사용자 승인이 필요합니다. Installer 작업을 마친 호출자는 생성한 스레드에서
`consent_client_destroy()`를 호출하고 종료 상태도 확인하세요. 실행 가능한
[register.c](../../../src/examples/register.c)의 `main`은 생성·등록·종료와 오류 처리를
모두 보여 줍니다.

이미지를 만들 때는 명시적인 offline 생성자를 사용합니다. 성공은 활성 등록이
아니라 STAGED이며 [가이드 04](../04-offline-registration.ko.md)를 따릅니다.
온라인 권한 오류를 offline 등록으로 우회하지 마세요.

## 4. 정의 갱신하거나 제거하기

갱신에는 전체 내용을 다시 전달합니다. 의미가 바뀌면 정책 버전을, 문구가 바뀌면
text revision을 올립니다. 제거에는 패키지 이름, 예상 세대와 고정 operation ID를
전달하고 앱 ID는 넣지 않습니다. 영향을 받는 모든 앱 정의가 비활성화됩니다.
플러그인이 [Installer 계약](../06-installer-integration.ko.md)에 따라 수행합니다.

## 문제 해결

- 권한 거부: 등록된 실행 파일, 라벨과 위임 범위를 확인하세요.
- 오래된 세대: 완료된 설치 상태를 확인하고 임의 값을 만들지 마세요.
- 충돌: 원래 ID와 동일한 내용으로 재시도하세요.
- level 3: ONCE만 지원하며 다른 기간은 거부합니다.

## 참조: 정책·이미지 등록·지역화 안내

## 온라인 등록과 이미지 생성 중 등록

```sh
consent-example-register PACKAGE APP DEFINITION ENFORCER GENERATION \
  OPERATION_ID POLICY_VERSION TEXT_REVISION
consent-example-offline-register IMAGE_ROOT PACKAGE APP DEFINITION ENFORCER \
  GENERATION OPERATION_ID POLICY_VERSION TEXT_REVISION
```

두 예제는 level 1, `ONCE,SESSION,TIMED,PERSISTENT`, retention 60000 ms, 기본 locale
`en`, 영문·국문 title/body의 완전한 typed definition을 구성합니다.
`template_version=1`은 문자열 변수 `scope`, `purpose`, `recipient`를 같은 이름의
requirement 필드에 결합하며 UTF-8 byte 상한은 각각 512, 256, 512입니다. 두 언어
본문 모두 세 placeholder를 포함하므로 포맷된 prompt에 실제 요청 값이 표시됩니다.
UI는 허용된 grant mode 선택과 해당 수명도 표시해야 합니다. 선택 전에 문구가
ONCE나 PERSISTENT를 단정하지 않습니다. 가이드 08 PoC는 요청과 모든 정책이 허용할 때 approval-v2 기간 선택을
제공합니다. 제품 UI 배포는 별도 연동 작업입니다.
`consent_register()`에 package와 app을 별도로 전달합니다. 패키지나 enforcer를
추정하거나 설치 authority·사용자 승인을 만들지 않습니다. 실제 사용 전에 예제
문장과 허용 mode를 제품 정책으로 바꾸세요.

policy/text revision은 INT32_MAX 이하의 양의 정수입니다. 의미가 바뀌면 policy
version을, 문장·기본 locale·fallback alias가 바뀌면 text revision을 올립니다.
기본 번역에는 비어 있지 않은 title/body가 필요하고 메시지 필드는 최대 32개,
각각 UTF-8 4,096바이트입니다. Level 3은 ONCE만 허용합니다. update는 patch가 아닌
완전한 definition을 받고 unregister는 패키지 전체에 적용합니다. 오류와 소유권은
공개 헤더를 참고하세요.

온라인 성공은 daemon의 등록 commit을 의미합니다. 오프라인 생성에는 real/effective
root, 보호된 절대 이미지 경로, 배타적 lifecycle lock, Installer가 공급한 안정적인
generation이 필요합니다. 연결하지 않으며 온라인 실패의 암묵적 fallback이 아닙니다.
오프라인 성공은 **STAGED**입니다. 보호된 레코드는 영속화되지만 활성 등록, DB,
사용자 승인은 생성하지 않습니다. 시작 시 import가 실제 설치된 package/app 관계와
active generation을 검사합니다. register/destroy 외 핸들 연산은 INVALID_OPERATION을
반환합니다. 경로 제한, authority 공급, 레코드 상한과 쓰기 중단 후 재시도는 가이드 04를
참고하세요.


## 승인 UI와 지역화 template

UI는 request_id, 요청 locale, typed prompt 지원 시 `template_version=1`로
`consent_get_prompt()`를 호출합니다. 회전하는 prompt_token과 각 requirement의
실제 scope/purpose/recipient, definition revision, 민감도, 허용 mode, 선택 번역을
받습니다. 이 문맥을 함께 표시하세요. `consent_prompt_format(result, index,
"title"/"body", &text)`의 성공 출력을 `free()`로 해제합니다. 일반 텍스트로만
표시하고 markup, printf format이나 다시 확장할 template로 해석하지 마세요.

Typed 등록은 이름 있는 값을 권한 필드에 결합합니다.

| 필드 | 계약 |
|---|---|
| `template_version` | 정확히 `1` |
| `parameter.<name>.type` | `integer` 또는 `string` |
| `.source` | 정수: `scope` 또는 definition의 `retention_ms`; 문자열: `scope`, `purpose`, `recipient`, `operation` |
| `.min`, `.max` | 정수의 필수 inclusive signed-64-bit 범위 |
| `.max_bytes` | 문자열의 필수 UTF-8 1–512바이트 상한 |
| `locale_fallback.<requested>` | 완전한 등록 번역을 직접 지정; chain/cycle과 완전한 번역 shadow 불가 |

변수는 1–32자 ASCII 식별자 이름으로 최대 8개입니다. 모든 typed 번역은 title/body를
가지고 두 필드의 `{name}` 합집합이 선언된 변수와 정확히 같아야 합니다. 값은 검증된
`rN` 필드와 현재 정책에서 가져오며 독립적인 표시 문자열이 아닙니다. `rN.policy_version`이
없거나 오래되면 실패합니다. 정수 scope는 canonical decimal이어야 합니다(`30`은
허용, `030`·`+30`은 거절). 문자열 source 누락은 실패하지만 명시적인 빈 입력은 허용될
수 있습니다. `retention_ms` 기본값 0도 선언 범위를 만족해야 합니다. 포맷된 각 필드는
최대 8,192바이트입니다. ICU plural/date/지역별 숫자 포맷은 구현하지 않습니다.

locale은 완전한 exact 번역, 명시적인 direct alias, 기본 제공되는 `ko-KR` → `ko`
또는 `en-US`/`en-GB` → `en` fallback, 등록된 기본값 순으로 선택합니다. 그 외 locale
매핑은 명시적인 alias가 필요합니다. 최신 prompt_token, decision=ALLOWED/DENIED와 허용된 grant_mode로
응답하며 typed 응답에는 요청 locale도 그대로 보냅니다. 같은 UI 프로세스 인스턴스가
응답해야 합니다. daemon은 새 ONCE grant를 소비하지 않고 전체 AND를 재평가합니다.
UI 대기 중 기존 조건이 달라지면 ALLOWED 응답도 INVALIDATED로 끝날 수 있습니다.
다른 조건에 대해 명시적으로 승인한 유효 grant는 소비되지 않고 유지됩니다. 결과
decision을 확인하고 실행 전 AUTHORIZE하세요. 과거 요청 결과는 현재 권한이 아닙니다.

## 다국어 승인 문구

A-15 증분은 기존 변수 없는 문구와 호환되는, 크기와 타입이 제한된 `{name}`
템플릿 계약을 추가합니다. 고정된 build15로 GBS와 격리 emulator C API 시험을
통과했으며, 스냅샷과 검증 한계는 [검증 기록](../07-verification.ko.md)에서 확인합니다. 포맷터는
일반 텍스트와 정규 십진 정수를 지원합니다. ICU 문법, 복수형 규칙, 날짜,
언어별 숫자 포맷은 구현하지 않습니다.

등록 시 각 변수의 타입·상한·신뢰할 원본을 정의합니다. 변수 값은 호출자가
만든 표시 문자열이 아니라 검증된 요구 조건이나 보관 메타데이터에서 가져옵니다.
예를 들어 정수 scope `30`은 과거 30일의 조회 범위를 나타낼 수 있으며,
`retention_ms`는 취득한 결과의 보관 기간을 밀리초로 나타내는 별개 값입니다.
포맷 과정에서 단위를 변환하지 않습니다. `retention_ms`를 생략하면 정책의 실제
기본값 0을 사용하며 이 값도 선언한 범위를 만족해야 합니다. 문자열 원본의 누락은
거부하지만 명시적으로 전달한 빈 문자열은 문자열 스키마에서 허용합니다.
UI는 등록된 전체 문장과 함께
scope·purpose·recipient·민감도·허용 모드를 표시합니다.

기존 params builder로 다음 필드를 추가하여 등록합니다.

| 필드 | 계약 |
|---|---|
| `template_version` | 타입 있는 문구는 `1` |
| `parameter.<name>.type` | `integer` 또는 `string` |
| `parameter.<name>.source` | 정수: `scope` 또는 `retention_ms`; 문자열: `scope`, `purpose`, `recipient`, `operation` |
| `parameter.<name>.min`, `.max` | 정수의 필수 signed 64-bit 최솟값·최댓값, 양 끝 포함 |
| `parameter.<name>.max_bytes` | 문자열의 필수 UTF-8 byte 상한, 1–512 |
| `locale_fallback.<requested>` | title·body가 모두 등록된 직접 대상 locale |

다음은 설정된 패키지·앱에 사용할 별도의 전체 정의입니다. 위의
scope/purpose/recipient 템플릿에 변수를 추가하는 예제가 아닙니다. 문구의 유일한
placeholder와 선언 변수는 days입니다. 대응 요구 조건은 definition calendar.days,
operation read, scope "30", purpose answer-calendar, recipient conversation,
policy_version "1"을 사용하며 subject와 profile도 아래 위임 문맥과 같아야 합니다.

```json
{
  "subject": "configured.subject",
  "profile": "configured.profile.A",
  "operation_id": "register-calendar-days",
  "expected_generation": "<trusted Installer generation>",
  "definition": "calendar.days",
  "enforcer": "ce",
  "policy_version": "1",
  "text_revision": "1",
  "level": "1",
  "modes": "ONCE,SESSION,TIMED,PERSISTENT",
  "retention_ms": "60000",
  "default_locale": "en",
  "message.en.title": "Read calendar history",
  "message.en.body": "Read the last {days} days?",
  "template_version": "1",
  "parameter.days.type": "integer",
  "parameter.days.source": "scope",
  "parameter.days.min": "1",
  "parameter.days.max": "365"
}
```
실제 package/app은 별도로 전달하고 Installer와 CE 신원을 등록하세요.
JSON은 C params 예이며 로더나 이미 구성된 제품 정책이 아닙니다.

타입 있는 request·check는 policy version 누락이나 불일치를
거부합니다.
`display_args`나 `r0.arg*` 필드는 보내지 않습니다. 데몬은 호출자가 만든 표시
인자를 거부합니다. 변수 타입·범위·원본 변경에는 새 policy version이 필요합니다.

변수는 최대 8개이며 각 이름은 1–32자 ASCII 식별자입니다. 타입 있는 모든
locale에는 제목과 본문이 필요하고, 두 문구의 placeholder 합집합이 선언한
변수명과 정확히 일치해야 합니다. 템플릿은 각각 UTF-8 4,096 byte, 완성 문구는
각각 8,192 byte까지 허용합니다. 정수는 정규 십진 표기여야 합니다.
`30`은 허용하지만 `030`, `+30`, `30.0`, overflow·범위 초과 값은 거부합니다.
잘못된 등록, 번역 누락이나 인자 오류를 승인으로 처리하지 않습니다.

타입 있는 prompt를 조회할 때 UI는 `template_version=1`과 요청 `locale`을
명시합니다. 응답 최상위 `locale`은 요청한 언어이고 각 `rN.locale`은 실제
선택한 등록 번역입니다.
`consent_prompt_format(result, index, "title", &text)` 또는 `"body"` 호출로
반환된 요구 조건의 문구를 완성합니다. 성공 시 `text`는 호출자 소유의 UTF-8
문자열이며 `free()`로 해제합니다. 실패 시 출력은 NULL이고, UI는 문구 완성에
성공한 것으로 처리해서는 안 됩니다. 포맷터는 템플릿을 한 번 순회하며 치환하고,
치환된 문자열을 템플릿이나 마크업으로 다시 해석하지 않습니다. 결과는 일반
텍스트로 표시합니다.
잘못된 prompt 필드·인덱스·인자는 `CONSENT_ERROR_INVALID_PARAMETER`,
메모리 할당 실패는 `CONSENT_ERROR_OUT_OF_MEMORY`를 반환합니다.

```c
#include <consent.h>
#include <stdio.h>
#include <stdlib.h>

int print_prompt_body(const consent_result_t *prompt, unsigned int index) {
  char *text = NULL;
  int status = consent_prompt_format(prompt, index, "body", &text);
  if (status != 0)
    return status;
  puts(text);
  free(text);
  return 0;
}
```

언어는 정확히 등록된 번역, 등록 언어를 직접 가리키는 명시적 별칭 순서로
선택합니다. 이어서 지원하는 `ko-KR`→`ko`, `en-US`/`en-GB`→`en` fallback을
적용하고, 마지막으로 등록 기본 언어를 사용합니다. 별칭 연결과 순환은 허용하지
않습니다. script·region subtag를 일반적으로 제거하지 않습니다. 별칭 원본에
완전한 번역이 이미 등록되어 있으면 해당 별칭 등록을 거부합니다. 타입 있는
승인 응답에는 최상위 요청 `locale`과 현재 `prompt_token`을 그대로 전달합니다.
언어를 바꾸면 prompt를 다시 조회하며, 이전 token으로 새 화면을 승인할 수
없습니다. 별칭 변경에는 새 text revision이 필요하며 대기 prompt를 무효화합니다.

---

[이전](01-start.ko.md) · [다음](03-request-and-check.ko.md) · [API 전체
목록](../02-c-api.ko.md)
