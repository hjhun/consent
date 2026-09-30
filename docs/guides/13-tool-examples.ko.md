# 가이드 13: Capability 및 context fixture 도구

[가이드 12](12-developer-smoke.ko.md)의 단일 `emulator-smoke.py`에 opt-in
`--tools` 모드를 추가합니다. 기본 smoke01 동작과 경로는 유지합니다.
실제 CM offline parser로 CLI descriptor를 게시하고, 별도로 인증된 C CM/CE
예제가 설치된 consent 라이브러리와 격리 daemon을 사용합니다. ALLOWED와
receipt를 받은 뒤에만 synthetic provider 실행 또는 context fixture 조회를 합니다.
`capmgr_client_execute()` 제품 연동이나 `app_fw` launcher를 구현한 것은 아닙니다.

현재 CM public create gate는 permission denied이며 제품 실행 transport와
내부 consent adapter가 없습니다. 발견한 `tizen-context-cli` gRPC client는
context/screenshot/key-event 명령을 제공하지만 consent/data-level 등록 계약이
없고 JSON-RPC adapter가 필요합니다. 최신 CE server source, 인증 신원, 등급
taxonomy는 외부 gate입니다. 개인 context RPC나 옛 contextd를 사용하지 않습니다.

## 파일 및 빌드

`tests/smoke/tool.c`는 synthetic JSON-RPC provider입니다. `tool-check.c`는
서로 다른 실행 파일 신원의 `consent-smoke-tool-cm`과 `consent-smoke-tool-ce`를
만듭니다. `tool-process.c`는 spawn, 독립 pipe 수집, 최종 wait를 소유하고
`tool-json.c`는 제한된 fixture 프로토콜에 JSON-GLib를 사용합니다.
공통 helper는 JSON과 자원 수명 처리를 담당하는 C 예제입니다.

`CONSENT_BUILD_SMOKE_TOOLS=ON`은 `CONSENT_BUILD_SMOKE=ON`과
`pkg-config json-glib-1.0`이 필요합니다. 명시적으로 요청하면 의존성 누락 시
configure가 실패합니다. RPM은 기본 활성화하고
`--define '_without_smoke_tools 1'`로 끕니다. JSON-GLib는 새 test executable에만
링크되어 tests RPM의 자동 ELF 의존성이 되며 제품 library/daemon에는 추가되지
않습니다. 최종 Release21으로 기존 Release19/r6 보관본 및 초기 Release20 시도와
snapshot을 구분합니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator --include-all \
  --define '_without_poc 1'
```

가이드12처럼 emulator와 architecture를 확인하고 matching runtime/devel/tests
RPM을 설치합니다. 제품 daemon RPM/service는 변경하지 않습니다. `--tools`에서
binary가 없으면 setup 전에 명확히 실패합니다.

```sh
sdb -s DEVICE shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --tools --seed 20261002'
```

증거 확인 후 같은 runner의 `--cleanup`을 사용합니다. 가이드12의 소유권/unit/path
검증을 유지합니다. 새 증거는
`/var/tmp/consent-artifacts/consent-integration-02/`에 보존하며 smoke01/r6는
덮어쓰지 않습니다.

## Descriptor, 도구 및 consent tuple

설치된 `tool-package/cli.json`은 실제 CM descriptor schema를 사용합니다:

```json
{"version":1,"key":"smoke-tool","name":"Fixture summary tool",
 "desc":"Synthetic JSON-RPC provider; not a product adapter",
 "executable":"bin/consent-smoke-tool",
 "inputSchema":{"type":"object","properties":{"record":{"type":"string",
 "enum":["summary","fail","timeout","malformed","stderr","nonzero",
 "conflict","nul"]}},"required":["record"],"additionalProperties":false},
 "outputSchema":{"type":"object","properties":{"summary":{"type":"string",
 "maxLength":1024}},"required":["summary"],"additionalProperties":false}}
```

manifest는 `http://tizen.org/metadata/capability/cli`, owner `smoke.package`,
설치된 tool-package root를 지정합니다. 실제
`capmgr-package-tool --offline ABS_DB stage MANIFEST`와
`finalize tool-install success`로 canonical `cli:smoke-tool`을 게시합니다.
finalize 재시도는 revision1을 유지합니다. C CM 예제는 실제 catalog owner와
executable을 읽고 고정 설치 경로, root 소유 ancestor/inode를 검증해 실행 FD를
고정합니다. shell 없이 해당 FD로 `posix_spawn`합니다. parser가 consent metadata를
가져오는 것은 아닙니다.

별도 Installer가 package/app/generation과 definition을 등록합니다. Argo와 두
check actor는 같은 `smoke_requirement()` mapping을 사용합니다:

| 도구 record | Definition | Enforcer/level | Requirement operation 및 scope |
| --- | --- | --- | --- |
| CM summary | `smoke.cm.tool.summary` | cm/1 | execute, `cli:smoke-tool/summary` |
| CM 실패/stream probe | `smoke.cm.tool.RECORD` | cm/1 | execute, `cli:smoke-tool/RECORD` |
| CE level0–3 | `smoke.ce.tool.levelN` | ce/N | read, `context.fixture/levelN` |

모든 tuple은 purpose `developer-tool-smoke`, recipient `fixture-provider`,
policy_version1을 사용합니다. Provider 인자는 승인 대상 record만 포함하며 enforcer가
도구 name/path를 고정합니다. scope/operation/purpose/recipient mismatch는 추가
admission 없이 새 승인을 요구합니다. CE 등급은 root 소유 보호된
`tool-metadata.json`에서 읽습니다. Caller는 알려진 record를 고를 수 있지만 낮은
등급이나 다른 path를 지정할 수 없습니다. level0도 승인이 필요하며 unknown level은
거부하고 level3은 ONCE만 허용합니다. 검증된 제품 CE taxonomy가 아닌 예제 정책입니다.

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

아래는 실행 중인 freshly provisioned fixture 단계에 맞춰 사용하는 입력 예입니다.
완료된 runner는 registry-loss 상태이므로 완료 후 붙여 넣는 명령이 아닙니다.
Provisioning/metadata/enrollment는 runner가 담당합니다. 각 terminal은 미리 등록된
root/System actor context여야 합니다. Library path를 지정합니다:

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

Argo callback 후 CM actor가 실행을 승인합니다. CE executable도
`smoke.ce.tool.level0`과 별도 CE 승인을 사용해 같은 순서로 조회합니다:

```sh
export LD_LIBRARY_PATH=/usr/libexec/consent/smoke
printf 'authorize demo-authorized ALLOWED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-cm smoke.cm.tool.summary
# After a separate argo/UI approval for smoke.ce.tool.level0:
printf 'authorize context-demo-authorized ALLOWED\n' |
  /usr/libexec/consent/smoke/consent-smoke-tool-ce smoke.ce.tool.level0
```

## Admission 및 복구 경계

QUERY는 실행하지 않습니다. 승인 전, 철회 후 또는 권한 검사/API 오류에서는
admission counter가 증가하지 않습니다. Native provider 오류는 admission 후 발생하며
count1과 별도 `native_error` 상태를 유지합니다. Ledger는 spawn/context read 전에 receipt를 기록합니다. 성공, native error,
unknown execution을 구분하며 timeout/invalid stream/프로세스 실패 후 같은 receipt로
자동 재실행하지 않습니다. Ledger 없는 다른 actor의 prior retry도 unknown으로
차단합니다. 같은 actor에서 명시적 handle 재생성 후에도 ledger는 유지하지만 process
local입니다. 제품 enforcer는 필요한 경우 durable side-effect deduplication이 필요합니다.
Daemon은 enforcer identity + operation_id + step_id를 immutable requirement
fingerprint에 연결합니다. 다른 record/tuple은 다른 operation ID가 필요하며 동일한
retry만 같은 ID를 사용합니다. CE runner는 first/retry에 `context-levelN-once`,
다음 작업에 `context-levelN-next`를 사용합니다.

stdout/stderr는 각각16KiB,1.5초 deadline으로 독립 수집합니다. 한쪽 유효 응답과
다른 쪽 whitespace, 또는 동일한 양쪽 응답을 허용하며 충돌/logging/malformed/NUL은
거부합니다. Native 응답과 exit/signal metadata는 별도로 기록해 nonzero exit의 유효
응답도 보존합니다. Provider는 완료/kill 후 항상 wait합니다. Metadata와 CE record는
bounded complete read와 NUL 거부를 사용합니다. 제한된 JSON-GLib helper이며 제품
CM collector 전체나 duplicate-member 검증의 대체가 아닙니다.

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

Seeded 시나리오는 running DB 삭제, stopped 삭제, corruption 각각에서 새 도구
실행 gate를 검증합니다. 두 tool approval 소실, 새 epoch,
schema2/integrity/active definitions12/grants0/cleanup_unknown1을 fresh 승인 전에
확인하고, 새 승인 후 실제 provider 실행과 CE 조회를 수행합니다. 정상 restart는
persistent grant를 보존합니다. Old handle은 DISCONNECTED이며 actor PID/ledger를
유지하고 명시적으로 새 handle을 만듭니다. Total registry loss는 fail-closed입니다.
채워진 QUERY cache invalidation의 증거로 주장하지 않습니다.

## 검증된 Release21 snapshot (2026-09-30)

CONSENT-INTEGRATION-02 구현 r4는 baseline `a569363`와 개발자 예제 변경입니다.
`source-r4.json`의 native/build/runner21개 파일은 실제 실행·설치한 Release21
snapshot과 일치합니다. 게시 시 runner의 trailing space 세 곳만 제거했으며 Python
AST는 동일하지만 게시된 byte hash는 설치 r4와 다릅니다. 원래 로그/RPM을 유지하고
외부 `publication-whitespace.json`에 old/new hash를 기록했습니다. 이 mechanical
게시 수정으로 package를 다시 빌드하지 않았습니다. 검증 후 문서에 증거를 추가합니다.
증거는 `/var/tmp/consent-artifacts/consent-integration-02/`에 보존합니다.

| 증거 | 실제 결과 |
| --- | --- |
| `gbs-r4.log`, `gbs-r4.exit` | 위 exact build 명령; exit0, CTest27개 중23 PASS +4 root-only SKIP |
| `rpms-r4/`, `rpm-r4-sha256.json` | Release21 RPM 보관 및 matching source manifest |
| `install-r4.log`, `install-r4.exit` | 정상 upgrade exit0, runtime/devel/tests만 설치 |
| `installed-r4-hash.log` | 설치 smoke payload17개 hash가 보관 RPM과 일치; runner도 실행한 source-r4와 일치 |
| `installed-tools-seed20261002-r4.log` | SMOKE_EXIT0, TOOLS_OUTER_EXIT0; 모든 도구 시나리오 및3개 복구 PASS |
| `installed-strict-seed20261003-r4.log` | 도구 시나리오 전부 PASS 후 명시적 product gate; SMOKE_EXIT1, STRICT_OUTER_EXIT1 |
| `installed-default-seed20261004-r4.log` | 기본 smoke01 회귀 SMOKE_EXIT0, DEFAULT_OUTER_EXIT0 |
| `final-cleanup-r4.log`, `final-services-r4.log` | 최종 cleanup0; smoke unit2개 not-found, fixture directory4개 없음 |
| `device-before-r2.log`, `final-services-r4.log` | 제품 PID31569 active/success; PoC PID0 inactive/success; 전체 상태 metadata fingerprint 동일 |

`results-r4.json`은 exit와 recovery case를 요약하고 `commands.txt`는 모든 exact
host/device 명령을 기록합니다. SDB host는 strict 원격 실패에서도0이므로 원격
SMOKE_EXIT와 OUTER marker로 판정합니다. 발견한 emulator는 `emulator-26101`,
architecture x86_64, profile `tizen_10_1_emulator`입니다. 설치된
`consent`, `consent-devel`, `consent-tests`는0.1.0-21이고 제품 `consentd` 및
`consent-poc`는0.1.0-18을 유지합니다. GBS가 만든 제품 daemon RPM은 보관만 하고
설치하지 않았습니다. CM은0.1.0-15, JSON-GLib는1.8.0입니다.
패키지 의존성 audit에서 JSON-GLib는 tests에만 존재합니다.

선택한 emulator에서 사용한 최종 설치/실행 명령:

```sh
sdb -s emulator-26101 shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System::Privileged rpm -Uvh \
  /tmp/consent-integration-02-r4-rpms/consent.rpm \
  /tmp/consent-integration-02-r4-rpms/consent-devel.rpm \
  /tmp/consent-integration-02-r4-rpms/consent-tests.rpm'
# For each replay, use this same invocation prefix with the options below:
sdb -s emulator-26101 shell 'systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System /usr/bin/python3 \
  /usr/libexec/consent/smoke/emulator-smoke.py --tools --seed 20261002'
```

실행 순서/옵션은 `--tools --seed 20261002` → `--cleanup` →
`--tools --require-product --seed 20261003` → `--cleanup` →
`--seed 20261004` → `--cleanup`입니다. 보존된 명령에는 device shell 종료 전
원격 outer exit 출력 및 assertion도 있습니다.

실제 parser가 `cli:smoke-tool`을 게시하고 owner/executable을 검증했습니다.
Public CM preflight는 `product_public_api/BLOCKED create_status=-2
handle=null`, PREFLIGHT_EXIT3입니다. stderr-only 및 nonzero exit7 native reply는
성공하며 유효 native error는 `native_error`, timeout/malformed/conflict/NUL은
`unknown`입니다. 각 retry는 count1로 deduplicate됩니다. CE level1–3 각각 first는
정확히 `state=succeeded count=1`, retry는
`deduplicated state=succeeded count=1`입니다. 두 CM/CE tool actor는 stopped-delete,
corrupt, running-delete에서 fresh 승인 후 실제 실행/조회를 수행했고, 세 번의
정의/grant/epoch readback을 기록했습니다. Strict도 이를 모두 통과한 후 product
integration 부재만을 이유로 기대한 실패를 반환했습니다.

실패/수정도 보존합니다. `installed-tools-seed20261002-r2.log`는 CElevel2에서
다른 level과 `context-once` immutable tuple을 재사용해 원격1로 실패했고,
`cleanup-failed-r2.log`는0입니다. R3에서 level별 operation namespace를 구분했으며
daemon policy를 바꾸지 않았습니다. `gbs-r1/r2/r3.log`는 모두 성공했습니다.
`install-r3.log`의 원격3은 Tizen MSM이 같은 Release20의 변경된 runner를
`--replacepkgs`에서도 거부한 결과입니다. Release21 정상 upgrade로 force-file
replacement 없이 해결했습니다. 너무 긴 SDB hash 명령의 service-name-too-long은
`installed-r4-hash-command-size-failure.log`에 보존하고, 짧은 read-only 수집으로
17개 파일 hash를 검증했습니다. 설치 ldconfig permission warning도 install log에
남겨 두고 transaction exit와 설치 hash를 확인했습니다.
기존 smoke01/r6 증거를 유지했으며 commit/push는 하지 않았습니다.
