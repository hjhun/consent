# 가이드 13: 도구 실행과 데이터 조회 예제

도구 실행과 데이터 조회 전에 사용자 승인을 검사하는 예제를 설명합니다.
CM 예제는 JSON-RPC 도구를 실행하고 CE 예제는 테스트 파일을 읽습니다.
두 예제 모두 consentd의 실행 허가와 receipt를 확인합니다.
[가이드 12](12-developer-smoke.ko.md)의 도구에 `--tools`를 지정해 실행합니다.

검사자(checker 또는 enforcer)는 실제 동작 직전에 권한을 확인하는 서비스입니다.
신뢰된 metadata는 도구나 데이터를 consent 정의에 연결합니다. 요구 조건 묶음
(tuple)은 작업, 범위, 목적, 수신자와 정책 버전을 모두 포함하며 어느 값이든
바뀌면 새 승인이 필요할 수 있습니다. Receipt와 실행 기록은 안전 검사 절에서,
제품 연동 상태는 별도 절에서 설명합니다.

## 파일 및 빌드

`tests/smoke/tool.c`는 테스트 JSON-RPC 제공자입니다. `tool-check.c`에서 서로
다른 신원의 `consent-smoke-tool-cm`과 `consent-smoke-tool-ce`를 만듭니다.
`tool-process.c`는 자식 실행, 출력 수집과 회수를 맡습니다. `tool-json.c`는
JSON-GLib로 테스트 프로토콜을 해석합니다.

`CONSENT_BUILD_SMOKE_TOOLS=ON`에는 `CONSENT_BUILD_SMOKE=ON`과
`pkg-config json-glib-1.0`이 필요합니다. 요청한 의존성이 없으면 빌드 설정이
실패합니다. RPM은 이 기능을 기본으로 켜며
`--define '_without_smoke_tools 1'`로 끌 수 있습니다. JSON-GLib는 테스트
실행 파일에만 연결하고 제품 라이브러리나 데몬에는 추가하지 않습니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

가이드 12처럼 에뮬레이터와 아키텍처를 확인한 뒤 같은 빌드의 runtime/devel/tests
RPM을 설치하세요. 제품 데몬 RPM과 서비스는 변경하지 않습니다. `--tools`에
필요한 실행 파일이 없으면 설정 전에 실패합니다.

```sh
sdb -s DEVICE shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --tools --seed 20261002'
```

결과를 확인한 뒤 같은 도구의 `--cleanup`으로 정리합니다. 정리는 가이드 12의
소유권, unit과 경로 검사를 그대로 사용합니다. 검증 당시의 기록 위치는 마지막
절에 있으며 새 실행 결과는 별도 디렉터리에 보관하세요.

## 도구 설명과 승인 조건

설치된 `tool-package/cli.json`은 실제 CM descriptor schema를 사용합니다:

```json
{
  "version": 1,
  "key": "smoke-tool",
  "name": "Fixture summary tool",
  "desc": "Synthetic JSON-RPC provider; not a product adapter",
  "executable": "bin/consent-smoke-tool",
  "inputSchema": {
    "type": "object",
    "properties": {
      "record": {
        "type": "string",
        "enum": [
          "summary",
          "fail",
          "timeout",
          "malformed",
          "stderr",
          "nonzero",
          "conflict",
          "nul"
        ]
      }
    },
    "required": [
      "record"
    ],
    "additionalProperties": false
  },
  "outputSchema": {
    "type": "object",
    "properties": {
      "summary": {
        "type": "string",
        "maxLength": 1024
      }
    },
    "required": [
      "summary"
    ],
    "additionalProperties": false
  }
}
```

Manifest에는 `http://tizen.org/metadata/capability/cli`, 소유 패키지
`smoke.package`와 설치한 tool-package의 루트를 지정합니다. 실제
`capmgr-package-tool --offline ABS_DB stage MANIFEST`와
`finalize tool-install success`로 `cli:smoke-tool`을 게시합니다.
Finalize 재시도는 revision1을 유지합니다. CM 예제는 catalog의 소유자와
실행 경로를 읽고 root 소유 상위 디렉터리와 inode를 검증한 뒤 실행 FD를
고정합니다. Shell 없이 그 FD로 `posix_spawn`합니다. 이 parser가 consent
metadata를 가져오지는 않습니다.

별도 Installer가 패키지, 앱, 설치 세대와 정의를 등록합니다. Argo와 두 검사자는
같은 `smoke_requirement()` 연결 정보를 사용합니다:

| 도구 record | Definition | Enforcer/level | Requirement operation 및 scope |
| --- | --- | --- | --- |
| CM summary | `smoke.cm.tool.summary` | cm/1 | execute, `cli:smoke-tool/summary` |
| CM 실패/stream probe | `smoke.cm.tool.RECORD` | cm/1 | execute, `cli:smoke-tool/RECORD` |
| CE level0–3 | `smoke.ce.tool.levelN` | ce/N | read, `context.fixture/levelN` |

모든 조건 묶음은 purpose `developer-tool-smoke`, recipient `fixture-provider`,
policy_version1을 사용합니다. 제공자 인자는 승인 대상 record만 포함하고 검사자가
도구 이름과 경로를 고정합니다. 범위, 작업, 목적 또는 수신자가 달라지면 동작을
실행하지 않고 새 승인을 요구합니다. CE 등급은 root 소유의 보호된
`tool-metadata.json`에서 읽습니다. 호출자는 알려진 record만 고를 수 있고 등급을
낮추거나 다른 경로를 지정할 수 없습니다. level0도 승인이 필요하고 알려지지 않은
등급은 거부하며 level3은 ONCE만 허용합니다. 등급은 예제 정책입니다.

## 승인 metadata와 API 입력

아래 JSON은 현재 C API에 전달하는 값을 읽기 쉽게 표현한 것입니다.
새 JSON API를 제공하는 것은 아닙니다. 기록된 검증은 Release21 실행에 해당합니다.

### Action 확인 요구와 별도 consent binding

실제 Tizen Action은 다음 boolean fragment를 지정할 수 있습니다:

```json
{
  "requiresConfirmation": true
}
```

필드는 선택 사항이며 기본값은 false입니다. Action API
`action_is_confirmation_required(action, &required)`가 값을 읽고 CM Action
catalog에도 `requiresConfirmation`이 보존됩니다. Action과 consent 정의의 연결,
최종 Action 실행 권한 검사는 아직 구현하지 않았습니다. Action의 확인 요구 metadata는 CLI consent 등록 필드가
아닙니다. Boolean은 확인 요구 여부를 나타낼 뿐 승인 기간을
선택하지 않습니다. false로 설정해도 다른 데이터 접근 권한을 우회할 수 없습니다.

보호된 `tool-metadata.json`의 정확한 두 entry는 다음과 같습니다:

```json
{
  "smoke.cm.tool.summary": {
    "definition": "smoke.cm.tool.summary",
    "enforcer": "cm",
    "level": 1,
    "record": "summary"
  },
  "smoke.ce.tool.level1": {
    "definition": "smoke.ce.tool.level1",
    "enforcer": "ce",
    "level": 1,
    "record": "level1"
  }
}
```

실행 도구가 나머지 record와 보호된 연결 정보를 만듭니다. CM 제공자는 테스트
요약을 반환하고 CE는 `tool-context/level1.txt`를 읽습니다. 본문은
`synthetic context summary level1` 뒤 줄바꿈입니다. 본문은 consent 등록과
저장소에 넣지 않습니다. Metadata는 접근 검사 대상만 식별합니다. level0도
승인이 필요하고 level3은 ONCE만 등록하며 호출자가 등급을 바꿀 수 없습니다.

### Installer 등록 입력

패키지 이름 `smoke.package`와 app ID `smoke.app`은 `consent_register()`의
별도 인자입니다. 데몬은 신뢰할 신원과 설치 세대를 대조하며 문자열만으로
등록자를 인증하지 않습니다. 아래는 CM 정의에 전달할 전체 인자입니다.
숫자도 문자열로 전달하고 지역화 메시지는 `message.en.title`과
`message.en.body`라는 평탄화된 key를 사용합니다. Generation 자리표시자는
신뢰 Installer가 발급한 실제 설치 세대로 바꿔야 합니다.

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

JSON은 `consent_params_set()`의 입력 설명이며 JSON 등록 endpoint나 읽기
함수가 아닙니다. 아래 `fields`는 위 값을 담은 호출자 소유 key/value 배열이고
`field_count`는 길이, `client`는 인증된 Installer handle입니다. 오류가 나면
등록을 중단하고 인자 객체를 해제합니다:

```c
/* Run only as the enrolled Installer; generation is trusted provisioning. */
consent_params_t* params = NULL;
int status = consent_params_create(&params);
if (status == 0) {
  for (size_t i = 0; i < field_count && status == 0; ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_register(client, "smoke.package", "smoke.app", params);
}
consent_params_free(params);
/* Propagate status; client and the borrowed fields remain caller-owned. */
```

CE level1은 definition을 `smoke.ce.tool.level1`, enforcer를 `ce`로 바꾸고
등록 operation ID에는 별도 값을 씁니다. level과 modes는 같습니다. level3은
level을 `3`, modes를 `ONCE`로 설정합니다. `60000`은 밀리초 단위 데이터 사용
한도이며 접근 승인 기간과 별개입니다.
이 한도는 인증된 holder가 `consent_data_register()`로 등록한 receipt/session에
결합된 MEMORY_ONLY artifact에 적용됩니다. CE fixture `.txt` 파일을 자동으로
삭제하지 않으며 이 예제는 접근 gate를 보여 줍니다.

### 요청자와 검사자 입력

Argo는 `consent_request_async()`에 다음 평탄화된 인자 객체를 전달합니다:

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

CM 검사자는 같은 요구 조건 묶음으로 `consent_check()`를 호출합니다.
AUTHORIZE 인자는 다음과 같습니다:

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

CE level1에서는 요청자 Argo와 검사자 CE 모두 `r0.definition`을 `smoke.ce.tool.level1`,
`r0.operation`을 `read`, `r0.scope`를 `context.fixture/level1`로 바꿉니다.
목적, 수신자와 정책 버전은 같습니다. 연결 정보는 제공자 인자에서 추측하지 않고
별도로 공유합니다. 버전을 지정하지 않은 이 예에는 `approval_version`이나
`grant_mode`가 없습니다.

Argo가 승인을 시작하고 request ID를 인증된 UI에 전달합니다. UI는 안내를 받고
응답하며 CM/CE check는 UI를 열지 않습니다. QUERY는 `mode: "QUERY"`를 사용하고
보호 동작을 수행하지 않습니다. 실행이나 읽기 전에는 API 상태 0, ALLOWED와
receipt가 모두 필요합니다. 아래 실행 기록도 확인합니다. 정의나 승인 결과만으로
실행할 수 없으며 재시도와 불명확한 실행 결과에도 권한 검사를 적용합니다.

## 프로토콜과 입력 예

Fixture는 `--json` 뒤 하나의 완전한 JSON-RPC2 argv를 받습니다. ID는 비어 있지
않은 최대95 byte 문자열이며 method는 `tools/call`, name은 `cli:smoke-tool`,
arguments는 알려진 `record` 하나입니다. 임의 command/path/caller level은 거부합니다.
직접 provider 호출은 프로토콜 예시이며 fixture enforcer를 통하지 않습니다.
제품 보안 경계로 주장하지 않습니다:

```sh
/usr/libexec/consent/smoke/tool-package/bin/consent-smoke-tool --json \
  '{"jsonrpc":"2.0","id":"demo","method":"tools/call","params":{"name":"cli:smoke-tool","arguments":{"record":"summary"}}}'
```

응답은 `{"jsonrpc":"2.0","id":"demo","result":{"summary":"synthetic capability summary"}}`입니다.
`fail`은 같은 id의 native error code-32001, unknown name/arguments는-32602입니다.
`stderr`는 stderr에만 유효 응답, `nonzero`는 유효 result 뒤 exit7입니다.
`timeout`, `malformed`, `conflict`, `nul`은 failed/unknown 실행을 검증하는
synthetic record입니다.

아래 명령은 초기 설정을 마친 테스트 환경이 실행 중인 해당 단계에서 사용합니다.
전체 runner가 끝나면 registry 손실 상태이므로 그 뒤 그대로 실행하지 마세요.
초기 설정, metadata와 역할 등록은 runner가 담당합니다. 각 터미널은 등록된
root/System 역할이어야 합니다. 먼저 라이브러리 경로를 지정합니다:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'query demo-query CONSENT_REQUIRED\nauthorize demo-before CONSENT_REQUIRED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-cm smoke.cm.tool.summary
```

Terminal A에서 argo를 시작하고 대기 중 출력한 REQUEST_ID를 복사합니다:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-argo smoke.cm.tool.summary demo-approval
```

동시에 terminal B에서 복사한 ID로 응답합니다:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
/usr/libexec/consent/smoke/consent-smoke-ui --auto-approve-smoke REQUEST_ID ONCE
```

Argo의 완료 콜백 뒤 CM 검사자가 권한을 확인하고 실행합니다. CE 실행 파일도
`smoke.ce.tool.level0`에 별도로 승인받은 뒤 같은 순서로 조회합니다:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'authorize demo-authorized ALLOWED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-cm smoke.cm.tool.summary
# After a separate argo/UI approval for smoke.ce.tool.level0:
printf 'authorize context-demo-authorized ALLOWED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-ce smoke.ce.tool.level0
```

## 안전 검사와 복구

QUERY는 실행하지 않습니다. 승인 전, 철회 후나 권한/API 오류에서는 실행
횟수가 늘지 않습니다. 제공자 자체의 오류는 실행 허가 뒤에 발생하므로 count1과
별도 `native_error` 상태를 남깁니다.

실행 기록(ledger)은 자식 실행이나 파일 읽기 전에 receipt를 저장합니다.
성공, 제공자 오류와 불명확한 실행을 구별하며 만료나 잘못된 출력, 프로세스 실패
뒤 같은 receipt로 자동 재실행하지 않습니다. 기록이 없는 이전 재시도도 차단합니다.
Handle을 다시 만들어도 같은 프로세스의 기록은 유지되지만 프로세스 종료를 넘어서
유지되지는 않습니다. 제품 검사자는 필요에 따라 영속적인 중복 방지를 구현해야
합니다.
데몬은 검사자의 신원, operation_id와 step_id를 불변 요구 조건에 연결합니다.
다른 record나 조건 묶음은 다른 operation ID가 필요하며 정확히 같은 재시도만
ID를 재사용합니다. CE runner는 첫 호출과 재시도에 `context-levelN-once`,
다음 작업에 `context-levelN-next`를 사용합니다.

stdout과 stderr는 각각 16KiB, 1.5초 제한으로 독립 수집합니다. 한쪽의 유효한
응답과 다른 쪽의 공백, 또는 양쪽의 같은 응답을 허용합니다. 충돌, 로그가 섞인
출력, 잘못된 JSON과 NUL은 거부합니다. 응답과 프로세스 종료/신호는 별도로
기록하므로 0이 아닌 종료 코드의 유효 응답도 보존합니다. 제공자는 완료하거나
종료시킨 뒤 반드시 회수합니다. Metadata와 CE 파일도 전체를 제한된 크기로 읽고
NUL을 거부합니다. JSON-GLib 예제는 제품 CM 수집기 전체나 중복 JSON key 검증을
대체하지 않습니다.

```mermaid
sequenceDiagram
  participant I as Installer
  participant A as argo and opt-in UI
  participant D as isolated consentd
  participant E as CM or CE tool actor
  participant P as Synthetic provider or data
  I->>D: register explicit tool binding
  A->>D: request and approve same tuple
  E->>D: AUTHORIZE operation/step
  D-->>E: ALLOWED receipt
  E->>E: record admission before side effect
  E->>P: pinned spawn or protected lookup
  P-->>E: native result/error or unknown execution
  E->>E: retain receipt state; no duplicate admission
```

Seed로 순서를 정한 시나리오는 실행 중 DB 삭제, 중지 중 삭제와 손상을
시험합니다. 새 승인 전에 기존 승인 소실, 새 epoch, schema2, 정상 integrity,
정의12개, grants0과 cleanup_unknown1을 확인합니다. 그 다음 새 승인으로 제공자
실행과 CE 조회를 수행합니다. 정상 재시작은 지속 승인을 보존합니다.
끊긴 handle은 DISCONNECTED로 거부하고 프로세스와 실행 기록을 유지한 채 새로
만듭니다. Registry까지 잃으면 실행을 차단합니다. 이 검사는 채워진 QUERY
캐시의 무효화 검증을 포함하지 않습니다.

## 제품 연동 현황

CLI descriptor 등록은 실제 CM parser를 사용하지만 parser에는 consent 필드가
없습니다. 공개 CM create는 권한 거부를 반환하며 제품 실행 통신과 consent
어댑터는 없습니다. 조사한 `tizen-context-cli`는 gRPC context/screenshot/key-event
호출을 제공할 뿐 consent나 데이터 등급 등록 API가 아닙니다. 현재 CE 서버의
신원과 등급 체계에는 플랫폼 계약이 필요합니다. 예제는 개인 context RPC를 호출하지
않고 `capmgr_client_execute()`나 `app_fw` 실행기를 구현하지 않습니다.

## 검증한 단계

Release21 r4 GBS에서 23개가 통과하고 root 전용 4개를 건너뛰었습니다. 설치된
tools/default는 0, strict는 모든 시나리오 완료 뒤 예상 코드 1, 정리는 0으로
종료했습니다. 설치 파일 hash는 RPM과 일치했습니다. 실제 parser 등록은 통과했지만
공개 CM API 사전 검사는 BLOCKED였습니다. CM/CE 도구는 세 DB 손실 모두에서
새 승인 뒤에만 실행했습니다. 운영 daemon18과 PoC18은 변경하지 않았습니다.
증거: `/var/tmp/consent-artifacts/consent-integration-02/`.

[스냅샷 상세와 실패 이력](../history/07-verification-history.ko.md#guide-13-checkpoint)
