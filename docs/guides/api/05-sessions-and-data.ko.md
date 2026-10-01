# API 05: 세션과 취득 데이터 관리하기

[English](05-sessions-and-data.en.md)

대화 세션을 열고 holder의 취득을 허가한 뒤 보관 메타데이터만 등록합니다.
마지막으로 세션을 닫고 실제 삭제를 완료한 holder가 ACK를 보냅니다. Consent는
데이터 본문을 저장하거나 holder의 버퍼를 대신 삭제하지 않습니다.

## 준비 사항

별도의 session-controller와 holder 신원을 사용합니다. 정의는 양수 보관 기간을
허용해야 합니다. AUTHORIZE 요구 조건에는 실제 holder의 `r0.holder`와 현재
session/generation이 필요합니다. 세션 없는 도구 예제만으로 데이터 사용 허가를
얻었다고 판단하지 마세요.

## 1. 세션 열기

JSON으로 읽기 쉽게 나타낸 문자열 입력입니다. JSON 로더는 없습니다.

```json
{
  "subject": "configured.subject",
  "profile": "configured.profile.A",
  "lifecycle": "RESUMABLE_CONVERSATION",
  "lease_ms": "30000"
}
```

| 필드 | 타입 | 필수 | 의미 |
| --- | --- | --- | --- |
| `subject`, `profile` | 문자열 | 예 | 명시한 위임 대상과 프로필 |
| `lifecycle` | 문자열 | 아니요 | 기본 CONNECTION_BOUND; 예제는 재개 가능 |
| `lease_ms` | 십진 문자열 | 아니요 | 최초 lease; 예제는 30000 |

아래 완전한 함수를 session-controller에서 실행합니다.

```c
#include <consent.h>

/* client has the session role; caller owns *result on success. */
int open_conversation(consent_client_h client, consent_result_t **result) {
  consent_params_t *params = NULL;
  *result = NULL;
  int status = consent_params_create(&params);
  if (status == 0)
    status = consent_params_set(params, "subject", "configured.subject");
  if (status == 0)
    status = consent_params_set(params, "profile", "configured.profile.A");
  if (status == 0)
    status = consent_params_set(params, "lifecycle", "RESUMABLE_CONVERSATION");
  if (status == 0)
    status = consent_params_set_int64(params, "lease_ms", 30000);
  if (status == 0)
    status = consent_session_open(client, params, result);
  consent_params_free(params);
  return status;
}
```
상태 0이면 소유한 결과를 해제하기 전에 `session`, `generation`, `resume_token`을
복사합니다. 결과에서 빌린 포인터는 결과와 함께 사라집니다. 복사한 값을 같은
소유 프로세스에서 유지하고 lease 만료 전에 갱신하세요. Heartbeat는 idle이나
최대 수명을 늘리지 않습니다.

## 2. 취득 허가를 받고 보관 메타데이터 등록하기

취득 전 holder의 현재 AUTHORIZE가 상태 0, ALLOWED와 receipt를 반환해야 합니다.
정확한 scope/purpose/recipient는 아래 등록과 같아야 합니다.

```json
{
  "receipt": "<AUTHORIZE receipt>",
  "subject": "configured.subject",
  "profile": "configured.profile.A",
  "session": "<returned session>",
  "generation": "<current generation>",
  "scope": "today",
  "purpose": "answer-calendar",
  "recipient": "conversation",
  "storage_class": "MEMORY_ONLY"
}
```

| 필드 | 타입 | 필수 | 의미 |
| --- | --- | --- | --- |
| `receipt` | 문자열 | 예 | Holder에 연결된 취득 receipt |
| `subject`, `profile` | 문자열 | 예 | 같은 위임 문맥 |
| `session`, `generation` | 문자열 | 예 | 활성 상태의 현재 세션 |
| `scope`, `purpose`, `recipient` | 문자열 | 예 | 정확한 취득 데이터 사용 조건 |
| `storage_class` | 문자열 | MEMORY_ONLY | 다른 저장 종류는 거부 |
| `requirement` | 십진 문자열 | 아니요 | Receipt 조건 번호, 기본 0 |

```c
#include <consent.h>
#include <stddef.h>

/* holder owns this receipt and buffer; session fields are current. */
int register_buffer(consent_client_h holder, const char *receipt,
                    const char *session, const char *generation,
                    consent_result_t **result) {
  const struct { const char *key; const char *value; } fields[] = {
    {"receipt", receipt}, {"subject", "configured.subject"},
    {"profile", "configured.profile.A"}, {"session", session},
    {"generation", generation}, {"scope", "today"},
    {"purpose", "answer-calendar"}, {"recipient", "conversation"},
    {"storage_class", "MEMORY_ONLY"}
  };
  consent_params_t *params = NULL;
  *result = NULL;
  int status = consent_params_create(&params);
  for (size_t i = 0; status == 0 && i < sizeof(fields) / sizeof(fields[0]); ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_data_register(holder, params, result);
  consent_params_free(params);
  return status;
}
```
상태 0이면 artifact 제어 레코드가 반환됩니다. 결과 해제 전에 `artifact`와
필요한 필드를 복사하세요. 본문은 holder가 계속 보유합니다. 만료는 취득 시점에
정의의 retention_ms를 더한 값이며 재시도로 늘리지 않습니다.

## 3. 유효한 artifact만 재사용하기

재사용은 `operation=reuse-data`로 artifact, subject/profile, session/generation과
같은 scope/purpose/recipient를 검사합니다. 다시 취득하거나 옛 receipt를 일반적인
데이터 권한으로 쓰면 안 됩니다. 파생 데이터 등록은 부모 출처를 기록하며
실제 변환을 수행하지 않습니다.

## 4. 세션 닫고 삭제한 뒤 ACK 보내기

session-controller가 현재 문맥으로 `consent_session_close()`를 호출하면 추가 사용이
차단됩니다. Holder는 `consent_cleanup_get_pending()`으로 목록을 가져와 본문을
삭제합니다. 실제 삭제 후에만 artifact와 `success=1`로 `consent_data_release()`를
호출하며 실패했다면 `success=0`을 사용합니다. 성공한 API 결과는 소유 객체이므로
각각 해제하세요. 모든 정리 페이지를 순회하고 실패한 항목은 다시 처리합니다.
ACK가 기록된 뒤에야 CLOSING이 CLOSED가 됩니다. API 성공만으로 물리적 삭제가
증명되지는 않습니다.

## 문제 해결

닫힌 세션은 재연결이나 데몬 재시작 뒤에도 되살아나지 않습니다. 오래된 세대는
현재 상태를 조회해 조정하세요. 재시작한 holder는 위임된 문맥과 `reconcile=1`로
정리만 할 수 있습니다. 데이터 사용 권한을 복구하는 절차가 아닙니다.

## 참조: 승인 기간·상한·출처·정리 페이지

## Grant mode, session과 데이터 수명

| Mode | 접근 grant 수명 |
|---|---|
| ONCE | 첫 성공 AUTHORIZE 실행에서 원자적으로 소비하며 QUERY·UI 완료는 소비하지 않습니다. |
| SESSION | 논리 session에 결합하며 suspend/close/만료 또는 오래된 generation 사용을 거절합니다. |
| TIMED | UI duration_ms 100–3600000, 기본 300000; check가 절대 만료 시각을 늘리지 않습니다. |
| PERSISTENT | 무효화·철회까지 영속적이지만 무제한 데이터 보관을 뜻하지 않습니다. |

허용된 PERSISTENT/SESSION request 결과는 최대 500 ms의 핸들별 참고 cache lease를
가질 수 있으며 로컬 접수 시점과 session deadline을 보수적으로 반영합니다.
정책·철회·generation 변경과 동기화 손실이 cache를 무효화합니다. ONCE, TIMED,
level 3은 cache 대상이 아닙니다. 보호 접근은 항상 authoritative check를 사용하며
`source=CACHE`는 실행 허가가 아닙니다.

Session open에는 subject/profile이 필요합니다. 기본 lifecycle은 CONNECTION_BOUND이고
RESUMABLE_CONVERSATION은 제한 내 suspend/resume을 허용합니다. 기본값·범위는 다음과 같습니다.

| 필드 | 기본 ms | Inclusive 범위 ms |
|---|---:|---:|
| `idle_timeout_ms` | 600000 | 100–86400000 |
| `max_lifetime_ms` | 3600000 | 100–86400000 |
| `lease_ms` | 30000 | 100–60000 |
| `reconnect_grace_ms` | 30000 | 100–300000 |

반환된 session/generation/resume_token을 보관합니다. 전환은 subject/profile,
session과 현재 generation이 필요하고 resume에는 token과 같은 소유 프로세스
인스턴스가 필요합니다. CONNECTION_BOUND의 suspend는 close이며 resumable suspend는
generation을 올리고 사용을 차단합니다. Resume은 generation/token을 회전하고
30000 ms lease를 시작하지만 원래 idle/maximum deadline을 늘리지 않습니다.
session 소유자는 만료 전에 subject/profile/session/generation으로
`consent_session_heartbeat`를 호출해 lease를 30,000ms로 갱신할 수 있습니다.
idle/absolute 수명은 늘리지 않습니다. local wait는 5,000ms이고 입력은 caller 소유,
출력은 consent_result_free로 해제합니다. 다른 owner/옛 generation/비활성·종료
session/offline handle은 거절합니다. 상태 조회나 일반 check는 lease를
갱신하지 않습니다. daemon 재시작은 이전 session을 다시 활성화하지 않습니다.
반환 deadline/expiry는 daemon의 monotonic milliseconds이며 UTC 날짜가 아닙니다.

| 데이터 연산 | 필수 schema와 동작 |
|---|---|
| `consent_data_register` | `receipt`, subject/profile, active session/generation, 정확한 scope/purpose/recipient; 선택적 requirement index 기본 0. AUTHORIZE의 rN.holder가 현재 holder와 결합되어야 합니다. MEMORY_ONLY만 허용하고 definition retention_ms는 양수여야 합니다. |
| `consent_data_register_derived` | Subject/profile, active session/generation, scope/purpose/recipient, count 1–16, 서로 다른 parent0…parentN. 같은 holder 인스턴스·문맥이며 가장 빠른 부모 만료와 provenance 합집합을 상속합니다. |
| `consent_check` 데이터 재사용 | `operation=reuse-data`, artifact, subject/profile, active session/generation과 정확한 scope/purpose/recipient. 재취득 없이 현재 보관 데이터의 provenance를 검사합니다. |
| `consent_data_release` | `artifact`, 실제 삭제 후 명시적인 `success=1`, 실패 시 `success=0`. 이전 holder 인스턴스 cleanup에는 `reconcile=1`과 일치하는 subject/profile도 필요합니다. |
| `consent_cleanup_get_pending` | Subject/profile과 선택적 `reconcile=1`; aN.artifact/session/state/error를 최대 48개 반환합니다. 삭제 ACK 후 다시 조회합니다. |
| Session/cleanup 상태 | Subject/profile/session; state와 cleanup_pending은 관찰값이며 lease 갱신이나 독립적인 물리 삭제 증명이 아닙니다. |

원본 데이터는 취득 시각에 definition retention_ms를 더한 시점에 만료하며 재시도가
늘리지 않습니다. 접근 grant 수명과 이미 취득한 데이터 보관은 별개입니다. ONCE 소비나
TIMED 접근 grant 만료만으로 독립적으로 유효한 data-use permit이 사라지지 않습니다.
철회, session close, 정책·설치 무효화, 데이터 만료는 계속 사용을 차단하고 cleanup을
요구합니다. 기록된 holder 삭제 ACK가 완료되어야 CLOSING이 CLOSED가 됩니다.
Consent는 대화 본문이 아닌 제어 metadata만 저장합니다. 제품 holder가 실제 메모리·저장소
삭제와 원격 수신자의 의무를 이행해야 합니다.


### Cleanup 페이지와 재시도 sweep

기존 count/aN을 처리하고 개별 삭제·ACK 실패와 관계없이 more/next_cursor로
뒤 페이지를 읽습니다. result 해제 전에 다음 위치를 params로 복사합니다.
more="0" 뒤 새 sweep은 cursor를 비워 실패 항목과 추가 항목을 재시도합니다.
격리 executable consent-scenario cleanup-pages는 실제 등록97개/첫48 실패 ACK를
실행하며 feature holder도 같은 continuation 계약을 사용합니다. 다음 예제는
목록 순회이며 실제 삭제·ACK는 물리삭제 근거가 있는 holder가 수행해야 합니다.

```c
int more = 0;
int status = consent_params_set(params, "cursor", "");
do {
  consent_result_t *page = NULL;
  if (status)
    break;
  status = consent_cleanup_get_pending(client, params, &page);
  if (status)
    break;
  /* 각 aN을 처리하되 삭제 실패가 뒤 페이지 순회를 막지 않게 합니다. */
  more = strcmp(consent_result_get(page, "more"), "1") == 0;
  status = consent_params_set(params, "cursor",
      consent_result_get(page, "next_cursor"));
  consent_result_free(page);
} while (more);
/* 다음 재시도 sweep 전에 cursor를 명시적으로 비웁니다. */
consent_params_set(params, "cursor", "");
```

위치는 지속 DB incarnation과 인증된 holder/process/subject/profile/reconcile
scope에 묶습니다. 같은 holder 프로세스의 정상 daemon 재시작에서는 유지되며,
새 프로세스는 reconcile=1이어도 새 sweep을 시작합니다. cache epoch와 ACK policy
revision은 별개입니다. DB reset은 STALE, caller scope 변경은
PERMISSION_DENIED를 반환합니다. 잘못된 token은 INVALID_PARAMETER입니다.
이 오류 후에는 기존 순회를
폐기하고 새 sweep을 시작하며 내부 CleanupProgress owner도 continuation을
명시적으로 초기화합니다. 이 초기화는 물리삭제 완료 근거가 아닙니다.
페이지/API 오류를 삭제 성공으로 처리하지 않습니다.

---

[이전](04-results-and-callbacks.ko.md) · [API 전체 목록](../02-c-api.ko.md)
