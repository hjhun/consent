# Consent documentation

Choose your role, then follow one task at a time. Every task guide has an
English
and Korean version. Input objects illustrate C parameter fields unless a guide
explicitly names the test-only JSON-RPC service. There is no public JSON loader.

## Application developer: request or check access

| Task | English | 한국어 |
| --- | --- | --- |
| Include, link and run a first client | [API 01](guides/api/01-start.en.md) | [클라이언트 시작](guides/api/01-start.ko.md) |
| Ask for approval and authorize an action | [API 03](guides/api/03-request-and-check.en.md) | [요청과 실행 허가](guides/api/03-request-and-check.ko.md) |
| Own results, wait for callbacks and shut down | [API 04](guides/api/04-results-and-callbacks.en.md) | [결과와 콜백](guides/api/04-results-and-callbacks.ko.md) |
| Manage sessions and retained-data cleanup | [API 05](guides/api/05-sessions-and-data.en.md) | [세션과 데이터](guides/api/05-sessions-and-data.ko.md) |
| Find any public function or role | [Guide 02](guides/02-c-api.en.md) | [API 목록](guides/02-c-api.ko.md) |
| Follow a complete tool/data example | [Guide 13](guides/13-tool-examples.en.md) | [도구 실행·데이터 조회](guides/13-tool-examples.ko.md) |

Argo requests approval; a separate UI responds; CM/CE check immediately before
the protected action. A shared UID or parameter does not confer those roles.

Argo가 승인을 요청하고 별도 UI가 응답합니다. CM/CE는 보호 동작 직전에 검사합니다.
같은 UID나 입력 문자열만으로 역할을 얻을 수 없습니다.

## Plugin developer: publish definitions

| Task | English | 한국어 |
| --- | --- | --- |
| Register policy and displayed messages | [API 02](guides/api/02-registration.en.md) | [정의 등록](guides/api/02-registration.ko.md) |
| Provision installation generations | [Guide 03](guides/03-installation-authority.en.md) | [설치 세대](guides/03-installation-authority.ko.md) |
| Stage definitions in an offline image | [Guide 04](guides/04-offline-registration.en.md) | [이미지 등록](guides/04-offline-registration.ko.md) |
| Implement install/update/remove hooks | [Guide 06](guides/06-installer-integration.en.md) | [플러그인 연동](guides/06-installer-integration.ko.md) |

## UI developer: show and bind the user's choice

| Task | English | 한국어 |
| --- | --- | --- |
| Build and understand the NUI example | [Guide 08](guides/08-consent-ui-poc.en.md) | [승인 화면](guides/08-consent-ui-poc.ko.md) |
| Select features and request task permissions | [Guide 10](guides/10-feature-approval.en.md) | [기능과 작업 승인](guides/10-feature-approval.ko.md) |
| Run actual native UI checks in one command | [Guide 17](guides/17-native-ui-smoke.en.md) | [UI 자동 검증](guides/17-native-ui-smoke.ko.md) |

## Test developer: run isolated examples

| Task | English | 한국어 |
| --- | --- | --- |
| Verify approval-gated resource reads | [Guide 12](guides/12-developer-smoke.en.md) | [리소스 접근 검사](guides/12-developer-smoke.ko.md) |
| Run private mock CM/CE services | [Guide 14](guides/14-mock-services.en.md) | [Mock 서비스](guides/14-mock-services.ko.md) |
| Test explicit profiles and provision authority | [Guide 15](guides/15-profile-authority.en.md) | [프로필](guides/15-profile-authority.ko.md) |
| Read verified snapshots and limits | [Guide 07](guides/07-verification.en.md) | [검증 현황](guides/07-verification.ko.md) |

## Framework maintainer: build, change and integrate

| Task | English | 한국어 |
| --- | --- | --- |
| Build packages and deploy to an emulator | [Guide 01](guides/01-development.en.md) | [빌드와 배포](guides/01-development.ko.md) |
| Generate the bounded Parcel protocol | [Guide 05](guides/05-idl.en.md) | [IDL 생성](guides/05-idl.ko.md) |
| Maintain metadata and understand registry loss | [Guide 09](guides/09-storage-maintenance.en.md) | [저장소 관리](guides/09-storage-maintenance.ko.md) |
| Check CEP acceptance evidence | [Guide 11](guides/11-acceptance.en.md) | [수용 기준](guides/11-acceptance.ko.md) |
| Review ownership and remaining integration work | [Guide 16](guides/16-maintenance-and-integration.en.md) | [유지보수와 연동](guides/16-maintenance-and-integration.ko.md) |

## Design documents

| No. | Read this for | English | 한국어 |
| --- | --- | --- | --- |
| 01 | Original CEP, requirements, proposals, and acceptance criteria | Original document is Korean | [Consent framework proposal](design/01-consent-framework.md) |
| 02 | Components, thread ownership, trust boundaries, and lifecycle | [Architecture](design/02-architecture.en.md) | [구조](design/02-architecture.ko.md) |
| 03 | Implemented wire format, messages, validation, and transport limits | [Protocol](design/03-protocol.en.md) | [프로토콜](design/03-protocol.ko.md) |
| 04 | Policy transactions, storage formats, recovery, and retained metadata | [Storage design](design/04-storage-design.en.md) | [저장소 설계](design/04-storage-design.ko.md) |
| 05 | Adopted implementation decisions and their rationale | [Decision log](design/05-decisions.en.md) | [결정 기록](design/05-decisions.ko.md) |

## Reading implementation status

The CEP is a design proposal. Architecture, protocol, and storage documents
describe the current implementation and its limits. A described API or test
source alone does not establish successful target execution: the verification
guides record the evidence for each snapshot and preserve earlier results.

Product role identities and approval UI deployment remain integration work.
Each plugin implements Installer hooks under the authenticated publication and
installation-generation contract. The UI PoC exercises the public API with
an isolated test identity; it does not enroll a product UI or
replace platform approval policy. Offline registration stages definitions only;
startup import still validates the installed package/app and protected
generation.

## Historical evidence

[English record](history/07-verification-history.en.md) ·
[한국어 기록](history/07-verification-history.ko.md)

Build-by-build commands and failures are preserved there. Portable examples in
edited documents are not byte-for-byte copies of archived host commands. Raw
execution records remain in the external artifact directories named by each
checkpoint; document edits do not imply new tests.

[Prior-layout popup HTML archive](previews/consent-popup.html) · 이전 배치 팝업 보관본

[Compact One UI-style proposal](previews/consent-popup-oneui.html) · 작은 팝업 제안
