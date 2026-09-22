# 설계 03: consent IPC 프로토콜 버전 1

이 문서는 `src/common/message.hh`가 구현한 wire 형식을 설명합니다.
`platform/core/base/bundle`의 실제 Tizen `parcel` 라이브러리를 사용합니다.
`src/protocol/consent.idl.json`이 스키마이며 결정적 compiler가 native
`Parcelable` Field/Envelope 클래스를 `consent_wire.hh`에 생성합니다.
CEP v0.4의 JSON 예시는 도메인 객체를 설명하며 실제 통신은 Parcel입니다.
JSON이나 GVariant를 내부 payload로 감싸지 않습니다.

## 프레임과 인코딩

AF_UNIX/SOCK_STREAM 메시지는 **big-endian** unsigned 4바이트 본문 길이와
그 길이만큼의 native Parcel 본문으로 구성됩니다. 본문은 1~65,536바이트입니다.
송수신은 부분 헤더, 부분 본문, 한 read에 여러 프레임이 오는 경우를 처리합니다.

읽기와 쓰기 모두 `Parcel::SetByteOrder(true)`를 사용합니다. 모든 정수와 문자열
길이는 host와 무관하게 big-endian입니다. 문자열은 `WriteString`과 호환됩니다.
마지막 NUL을 포함한 u32 바이트 길이 다음에 UTF-8 바이트와 NUL이 옵니다.
Envelope 순서는 다음과 같습니다.

| Native Parcel 필드 | 타입 / 상한 |
|---|---|
| version | u32, 정확히 1 |
| kind | u32: request=1, reply=2, event=3 |
| correlation | u64: request/reply는 1..INT64_MAX, event는 0 |
| method | NUL 제외 1..128바이트 UTF-8 문자열 |
| status | i32: reply는 0 이하, request/event는 0 |
| field count | u32, 최대 256; 전체 Message도 최대 256필드 |
| 반복 key/value | key 최대 128바이트, value 최대 8192바이트 |

인코더는 필드를 사전순으로 정렬합니다. 수신자는 중복 필드, 필드 배열 속 예약
메타데이터, 잘못된 UTF-8, 중간 NUL, 마지막 NUL 누락, 초과 count, 잘린 데이터,
후행 바이트를 거부합니다. key는 비어 있지 않은 `A-Z a-z 0-9 _ . -` 조합입니다.
requirement는 최대 16개입니다. 문자열로 표현한 숫자는 후행 문자와 signed 64-bit
범위 초과를 거부합니다.

생성된 reader는 Parcel primitive 읽기 전에 길이를 검사합니다. 문자열 길이,
스키마 상한, 남은 본문을 먼저 확인하고 빌린 span의 NUL/UTF-8을 검증한 뒤
문자열을 할당합니다. 비신뢰 입력에 `Parcel::ReadString()`을 사용하지 않습니다.
배열도 남은 바이트가 각 원소의 최소 wire 길이를 수용하는지 확인한 뒤 resize합니다.
`ReadParcelable()` 반환값만으로 성공을 판단하지 않고 생성된 validation 상태와
본문 전체 소비를 확인합니다.

다음 회귀 test vector는 4바이트 헤더를 포함합니다.
추가 필드가 없는 `v=1, id=1, method=hello`입니다.

```text
00000022
00000001 00000001 0000000000000001
00000006 68656c6c6f00
00000000 00000000
```

첫 줄은 본문 크기 34입니다. 이후 version/kind/correlation, method 문자열,
status, field count 순서입니다. 공백과 줄바꿈은 가독성을 위한 표시입니다.

## Envelope와 요청 연결

각 요청은 `v=1`, 연결 내 고유한 양의 십진수 `id`, `method`를 가집니다.
reply는 `v`, `id`, `status`를 반환하며 정상 repository 응답에는 `epoch`,
`revision`이 포함됩니다. status는 처리 성공 0 또는 음수 API 오류입니다.
`ALLOWED`, `DENIED`, `CONSENT_REQUIRED`, `PENDING`, `CANCELLED`, `EXPIRED`,
`INVALIDATED` 결정은 API 오류와 별개입니다. 0이 아닌 status로 보호 작업을 허용하지
않습니다.

daemon은 accepted socket과 보호된 플랫폼 정책에서 신원을 얻습니다. caller가
보낸 role/PID/UID/underscore 접두어 private 필드는 권한 근거가 아닙니다.
public params builder는 예약 필드를 거부하고 서버도 별도로 검증합니다.
라이브러리 입력 검증 자체가 인증 경계는 아닙니다.

`request`의 `decision=PENDING` 응답에는 `request_id`가 있습니다. client I/O
thread는 새로운 transport correlation ID로 `result`를 주기적으로 조회하며,
조회 사이에는 서버 worker와 DB executor를 점유하지 않습니다. 최종 결정이 원래
로컬 operation을 한 번 완료합니다. 명시적으로 호출한 result API는 PENDING을
바로 반환할 수 있습니다. 안정된 `client_request_id`와 operation ID는 응답 유실
이후에도 사용합니다. transport `id`는 영속 중복 처리 방지 키가 아닙니다.

## 무효화와 캐시 계약

daemon은 변경 commit 후 `v=1, method=event, event=invalidate, epoch=...,
revision=...`를 broadcast합니다. 각 연결에서 해당 변경 reply보다 event가 먼저
갑니다. client는 event 또는 epoch/revision 변경 시 전체 캐시를 지웁니다.
연결이 끊기면 캐시를 지우고 비활성화합니다.

request 응답은 `cacheable=1`, `cache_ttl_ms`로 캐시를 허용할 수 있습니다.
client는 유효한 epoch/revision이 있는 ALLOWED request만 저장합니다. session
request는 응답 session/generation이 요청과 정확히 같아야 하며 daemon이 TTL을
session deadline 이내로 제한합니다. client는 로컬 operation 접수 시각부터 응답까지
경과한 시간을 차감하므로 늦게 도착한 응답이 수명을 다시 시작하지 않습니다. client 상한은 1초, handle당 LRU 64개입니다.
transport ID, request ID, operation ID, deadline을 제외한 전체 문맥을 비교합니다.
cache hit는 `source=CACHE`이고 원격 request_id를 포함하지 않습니다.
QUERY/AUTHORIZE는 항상 daemon에 문의합니다. key에는 session/generation이
포함됩니다. 여러 client handle 사이 캐시 공유는 구현하지 않았습니다.

## Production endpoint 인증

production은 컴파일된 `/run/.consentd.sock`을 사용합니다. 연결 전후 root 소유
상위 디렉터리와 root 소유/non-world-write socket, 동일 device/inode를 확인합니다.
group write 예외는 Tizen의 root:system_share 소유 정확한 /run뿐입니다.

파일 stat만으로 pathname race가 없어지지 않습니다. 연결된 socket의 kernel
SO_PEERCRED PID1/UID0, getpeername의 정확한 AF_UNIX 원래 bind 경로와 sockaddr
길이/마지막 NUL, 실제 target에서 관측한 SO_PEERSEC System::Privileged가 모두
필요합니다. credential/label 조회 실패, root 직접 bind 서버, 다른 label,
다른 systemd socket rename은 거부합니다. production에는 우회 환경변수가 없으며
별도 격리 test client는 다른 endpoint로 컴파일됩니다.

## Operation과 parameter 표현

public C params는 확장 가능한 문자열 dictionary입니다. requirement는 `count`,
`r0.definition`, `r0.operation`, `r0.scope`, `r0.purpose`, `r0.recipient`,
`r1.*`처럼 표현합니다. 등록 필드는 package/app/definition/enforcer,
operation_id/expected_generation, policy_version/text_revision/level/modes,
default_locale, `message.<locale>.title/body`, retention metadata입니다.

`consent_register(client, package, app, params)`는 package와 app을 명시적으로
복사합니다. `consent_unregister(client, package, params)`는 app 없이 package
단위로 제거합니다. 안정된 operation_id와 expected_generation으로 지연된 제거
재시도가 새 설치를 제거하는 것을 막습니다. 신뢰할 installation generation은
daemon 측 authority에서 조회합니다.

request/check는 subject/profile, 선택적 session/generation, client_request_id,
operation_id/step_id/deadline_ms와 requirement를 사용합니다. check mode는 QUERY
또는 AUTHORIZE입니다. wire method는 hello, register, unregister, request, result,
cancel, check, prompt, respond, revoke, session_open, session_suspend,
session_resume, session_close, session_state, data_register, data_derived,
data_release, cleanup, cleanup_list입니다. repository와 identity adapter가 구체적인 필수 필드와
정책을 검증합니다. 필드의 존재 자체로 신원이나 권한이 입증되지 않습니다.

`consent_cleanup_get_pending()`은 cleanup_list로 매핑됩니다. 인증된 holder가
subject/profile을 전달하면 count와 aN.artifact/session/state/error에 최대48개
pending/failed 항목을 반환합니다. 기본은 현재 process instance이며 reconcile=1은
동일 stable holder가 이전 instance의 차단된 artifact를 조회하는 cleanup 전용
경로입니다. 실제 삭제 후 consent_data_release에 artifact/success/reconcile=1과
동일 subject/profile로 ACK합니다. 사용 권한/소유권을 이전하거나 기존 instance로
새 artifact를 등록하는 기능이 아닙니다.

## 타입 있는 지역화 prompt

A-15 증분은 Parcel Envelope 버전을 유지하면서 이름 있는 평문 템플릿을
추가한다. 고정된 build15로 GBS와 격리 emulator C API 시험을 통과했으며,
소스 snapshot에 결합된 증거와 한계는 한영 검증 문서에서 확인한다.
기존 literal 정의의 호환 경로도 유지한다.

스키마는 기존 Envelope의 필드 dictionary로 표현한다.

| 등록 필드 | 값 |
|---|---|
| `template_version` | 정확히 `1` |
| `parameter.<name>.type` | `integer` 또는 `string` |
| `parameter.<name>.source` | integer: `scope`, `retention_ms`; string: `scope`, `purpose`, `recipient`, `operation` |
| `parameter.<name>.min`, `.max` | 정수의 필수 signed 64-bit 양끝 포함 범위 |
| `parameter.<name>.max_bytes` | UTF-8 문자열의 필수 1–512바이트 상한 |
| `locale_fallback.<requested>` | title/body가 등록된 직접 대상 locale |

변수 이름은 `[A-Za-z_][A-Za-z0-9_]{0,31}`이며 최대 8개다. 모든 typed locale은
제목과 본문을 모두 가지며 두 필드의 변수 합집합이 선언된 schema와 정확히
일치해야 한다. 비어 있지 않은 각 템플릿은 UTF-8 4096바이트 이하이다.
정수 제약과 값은 canonical signed decimal이며 선행 0, 양수 부호, 음수 0,
소수, overflow는 거부한다. 중괄호 escape 문법은 없다. 잘못된 중괄호,
미등록 변수/property, 선언되지 않은 인자는 오류다.

요청은 기존 `rN.scope`, `.purpose`, `.recipient`, `.operation`을 보낸다.
Typed 정의의 request/check는 현재 `rN.policy_version`을 명시해야 하며
누락·불일치는 `-ESTALE`이다. 별도 표시 인자는 보내지 않는다.
`display_args`, `display_args.*`, `rN.display_args`, `rN.display_args.*`,
`rN.arg*`를 거부한다. daemon은 typed prompt 행에 `rN.template_version=1`,
`rN.arg_count`, `rN.argM.name`, `.type`, `.value`를 추가한다. 변수명 순으로
정렬하지만 UI는 명시된 이름을 사용해야 한다. 이 목록은 검증된 표시 데이터이며
권한 문맥을 대신하지 않는다. 전체 prompt 내용은 240필드 이하이고 Envelope와
새 token 공간을 예약한 뒤 65536바이트 frame 예산 안에 들어야 한다. 초과 시
조건이나 인자를 생략하지 않고 오류를 반환한다.

등록 시 전체 문구·변수 스키마·locale alias를 함께 검증한다. 모든 변수는
검증된 요청 문맥 또는 보관 metadata의 명시 source와 타입·상한을 가진다.
독립적인 표시값으로 이 원본을 대체할 수 없다. 정수 scope `30`의 조회기간과
`retention_ms`는 다른 개념·단위이며 자동 변환하지 않는다. `retention_ms`
미지정 시 실제 정책 기본값 0을 사용하고 이 값도 schema 범위를 만족해야 한다.
문자열 source 필드 누락은 거부하며 명시된 빈 문자열은 문자열 schema가 허용한다.
`{name}`을 포함하는 전체 문장을 단일 pass로 치환하여 값 내부 중괄호를 다시
해석하지 않는다. 결과는 평문 UTF-8이며 정수는 정규 십진 표기다. ICU 문법,
복수형·날짜·locale별 숫자 포맷은 이번 계약 범위가 아니다.

Typed `prompt` 요청은 `template_version=1` 지원과 UI의 `locale`을 명시한다.
반환 prompt의 최상위 `template_version=1`, `locale`은 요청한 locale을 유지하고
각 `rN.locale`은 실제 선택한 번역이다. 정확히 등록된 번역을 우선 선택한다.
Alias는 완전한 등록 locale로 직접 연결하며 chain/cycle, 이미 완전한 번역이
등록된 source를 가리는 shadow alias는 거부한다. 정확한 번역과 alias가 없으면
`ko-KR→ko`, `en-US/en-GB→en`의 명시 fallback 뒤 기본 언어를 선택한다.
그 밖의 script/region subtag를 임의로 제거하지 않는다.

UI는 `consent_prompt_format(result, requirement_index, field, &text)`를 호출한다.
`field`는 `"title"` 또는 `"body"`다. 성공 시 `malloc`으로 할당한 UTF-8 문자열의
소유권을 받아 `free()`로 해제한다. 오류 시 출력은 NULL이다. 각 확장 필드는
8192바이트 이하이며 formatter는 두 템플릿 필드와 전체 변수 집합을 먼저 검증한다.
이 함수는 로컬 처리이며 IPC나 승인을 수행하지 않는다. UI는 결과를 markup이나
format string으로 해석하지 않고 실제 scope·목적·수신자·등급·허용 방식과 함께
평문으로 표시한다.

Typed `respond`는 최상위 요청 locale과 최신 `prompt_token`을 돌려보내야 한다.
언어 변경 시 prompt를 다시 가져오며 이전 token을 교체한다. Token은 요청,
표시한 정책/문구 revision, UI 프로세스 instance에도 결합된다. 같은 definition ID의
`text_revision`은 재설치나 policy 증가와 관계없이 감소할 수 없다.
`default_locale`, `message.*`, `locale_fallback.*` 중 실제 내용이 바뀌면 더 높은
text revision이 필요하며 pending 표시를 무효화한다. 동일한 문구/선택 맵은 같은
revision을 유지할 수 있다. 변수 의미/source 변경은 별도로 policy revision을
올려야 한다. 포맷 또는 locale 협상 오류는 grant를 생성하거나
작업을 허가하지 않는다.

지원 capability 누락·잘못된 정의/원본값은 `-EINVAL`, locale 불일치·이전 token은
`-EACCES`, prompt/확장 출력 예산 초과는 `-E2BIG`다. C formatter는 잘못된
필드/index/schema에 `CONSENT_ERROR_INVALID_PARAMETER`, 할당 실패에
`CONSENT_ERROR_OUT_OF_MEMORY`를 반환하며 두 경우 모두 출력은 NULL이다.

## Callback과 자원 계약

handle마다 bounded I/O thread 하나를 사용합니다. process당 최대 16 handle,
handle당 최대 64 outstanding operation이며 dispatch 대기 callback도 포함합니다.
송신 queue는 최대 256 KiB, 수신 중인 부분 프레임의 완료 제한은 5초입니다.

client 생성은 생성 thread와 GLib thread-default context 또는
`consent_client_create_with_context()`의 context를 기록합니다. 같은 thread가
async API 호출, context 반복, client 파괴를 담당합니다. async API 실행 도중
nested main loop를 돌리지 않습니다. callback은 idle source로 등록되어 inline
실행되지 않고, 실행 중 라이브러리 mutex를 잡지 않습니다.

async 성공은 로컬 접수를 뜻합니다. 원격 처리 오류는 callback으로 전달됩니다.
입력과 callback bookkeeping은 접수 전에 할당합니다. 접수 전 할당 실패는 callback,
I/O operation, user_data 참조를 남기지 않습니다. 결과는 callback 동안 빌려 쓰며
보관하려면 `consent_result_clone()`을 호출합니다. `consent_async_detach()`는
로컬 callback을 억제하며 원격 요청을 취소하지 않습니다. 원격 취소는
`consent_cancel_request()`를 사용합니다.

SYNC는 callback dispatch와 별도 condition으로 대기합니다. callback context를
소유한 상태에서 호출하면 CONSENT_ERROR_WOULD_DEADLOCK입니다. 로컬 timeout은
거부나 원격 취소를 의미하지 않습니다. 송신 후 응답을 확인하지 못하면
CONSENT_ERROR_OUTCOME_UNKNOWN일 수 있으므로 같은 안정된 ID로 재시도/조회합니다.
async 로컬 상한은 5분이며 daemon이 원격 request deadline을 별도 검사합니다.

Destroy는 대기 callback 억제, SYNC waiter 깨우기, I/O thread join을 수행합니다.
callback 안에서 destroy를 지원합니다. caller가 다른 thread의 raw handle 사용과
파괴를 조정해야 합니다. fork로 상속한 handle은 상속 mutex 접근 전에 거부하며,
child에서는 부모 callback도 억제합니다. child는 새 admission count로 새 handle을
만듭니다. 신원이 바뀌어도 기존 handle을 폐기하고 새로 생성합니다.

## 검증 범위

`src/tests/client-test.cc`는 분할 wire, Parcel golden bytes, UTF-8, 중복 key,
truncation/trailing bytes, bounded 문자열/배열, 숫자 overflow, 즉시 ALLOWED/DENIED,
PENDING 조회, 캐시/무효화, 로컬 timeout/detach, queue 상한, callback 내 destroy,
할당 실패 주입, 부모 16 handle 상태의 fork 격리를 검증합니다.
mock server는 transport fixture이며 플랫폼 role 검사나 DB 복구, 실제 승인 UI를
검증한 근거가 아닙니다. production과 격리 C 실행 프로그램은 별도 target으로
link합니다. 환경변수로 production endpoint를 바꾸거나 인증을 끄지 않습니다.

Host sanitizer는 fork와 LeakSanitizer를 나눠 실행합니다.
`ASAN_OPTIONS=detect_leaks=0 client-test`는 fork16 포함 전체 ASAN/UBSAN,
`ASAN_OPTIONS=detect_leaks=1 client-test --skip-fork`는 leak 검사를 수행합니다.
LeakSanitizer와 multithreaded fork를 함께 켜면 host child의 새 client 생성에서
hang이 관측됐습니다. 일반 GBS 시험은 fork를 포함하며 --skip-fork는 시험 프로그램
옵션일 뿐 라이브러리 설정이 아닙니다.

## 기능 선택 승인 version 1

hello 응답은 `approval_version=1` 지원을 알립니다. 새 client는 이를 알리지
않은 daemon에 opt-in 호출을 송신하지 않습니다. approval_version 필드가 있으면
잘못된 값이어도 request cache 조회/저장을 하지 않으며 daemon도 cacheable=0을
반환합니다. 기존 요청의 cache 계약은 유지합니다.

요청/check는 approval_version=1, request_kind=PREAPPROVAL|TASK,
selection_id/selection_revision/selection_digest, 배치 공통
 grant_mode=ONCE|SESSION|TIMED를 전달합니다. duration_ms는 TIMED일 때만
100–3,600,000입니다. 각 rN에는 feature_id/feature_revision과 literal 정의에서도
명시 policy_version이 필요합니다. 식별자는 ASCII 영숫자/밑줄/하이픈/점 1–128자,
revision은 선행0 없는 양의 int64 십진수입니다. SESSION에는 활성 session/generation이
필수이며 v1은 행별로 다른 기간을 선택하지 않습니다. 기능 metadata는 wildcard
권한 key가 아닙니다.

PREAPPROVAL은 선택 기간의 충족을 검사합니다. 유효한 grant는 ONCE를,
PERSISTENT는 모든 기간을 충족합니다. SESSION은 같은 session의 승인이어야 하며
TIMED는 최초 admission 시각+duration 이상의 만료시각이 필요합니다. 이 목표는
중복 처리 후 한 번 저장하는 서버 private 값이고 caller fingerprint와 분리합니다.
새 TIMED 승인은 단일 응답 시각부터 duration 뒤 만료됩니다. TASK는 현재 유효한
정확 일치 grant를 먼저 재사용하고 부족한 조건만 선택 기간으로 발급합니다.
범위 부분집합 추론이나 provider 대체는 하지 않습니다.

selection_digest는 approval_version/request_kind/selection_id/selection_revision/
grant_mode, 존재하는 경우 duration_ms, subject/profile/session/generation/count와
각 rN.definition/policy_version/scope/operation/purpose/recipient/holder/feature_id/
feature_revision 문자열 map의 SHA-256입니다. 없는 session/generation 및 행 값은
빈 문자열로 넣습니다. selection_digest와 transport/operation/step/request/deadline/
private 필드는 제외합니다. key를 ASCII byte 순으로 정렬하고
`consent-selection-v1\n` UTF-8 bytes(LF로 끝남) 뒤에 각 항목을
`key의UTF8byte길이:key문자열value의UTF8byte길이:value문자열` 형식으로 이어 붙입니다.
길이는 canonical 십진수이며 항목 사이 구분자는 없습니다. 결과는 소문자 64자리 hex입니다.
operation fingerprint는 별도로 안정 ID와 원래 요청 문맥을 결합하므로 같은 ID의
선택 변경은 CONFLICT입니다.

예: version1/PREAPPROVAL/settings-1/revision1/TIMED/1800000/owner/default,
빈 session/generation, count1, 위 행 순서의 calendar.read/1/30/read/answer/local/
holder/calendar/1은 `112eebdb4fcd0b7f0a3bebeb488d95be216259cfcaf36d0cd4da26382b9c9bdf`입니다.

UI는 typed template 지원과 함께 approval_version=1을 명시합니다. 저장된 전체 AND는
유지하면서 prompt는 부족 행만 compact count, 전체 total_count, rN.original_index로
보냅니다. provider package/app은 검증된 정의에서 가져옵니다. token은 표시된 원래
index/선택 문맥/기간/locale에 결합되고 respond는 동일 문맥을 돌려줘야 합니다.
숨겨진 기존 조건을 재발급하거나 다른 요청이 이미 승인한 ONCE를 중복 발급하지
않습니다. 최종 AND도 동일 coverage로 평가합니다. 다른 요청이 표시 대상을 모두
충족하면 get_prompt는 token 없는 정상 terminal ALLOWED/INVALIDATED를 반환하며
UI는 DENIED를 보내지 않고 닫습니다. 공개 request result에서 `_` prefix private
필드는 제거합니다. 구 UI capability, malformed 입력 또는 render 예산 실패는 기존
token을 바꾸지 않습니다.

16개는 논리적 조건 상한이며 모든 16행 prompt가 표시된다는 보장이 아닙니다.
전체 prompt에는 240필드/64KiB와 개별 render 예산을 더 엄격하게 적용합니다.
짧은 literal v1은 11개까지 표시되고 부족 조건 12/16개는 필드 예산을 초과합니다.
명시 E2BIG를 token 갱신 전에 반환하며 부분 승인·묵시 분할은 하지 않습니다.
PoC catalog는 선택 전체가 예산 안에 들어오도록 제한합니다.
