# Consent documentation

Choose a reading path by the work you need to do. Each guide has English and
Korean versions; the original CEP is in Korean.

## Reading paths

- **Build or deploy:** [Guide 01](guides/01-development.en.md), then
  [Guide 07](guides/07-verification.en.md) for tested snapshots and limits.
- **Use the API:** [Guide 02](guides/02-c-api.en.md) for ownership and callbacks;
  [Guide 13](guides/13-tool-examples.en.md) for registration and approval inputs.
- **Integrate services:** [Guide 06](guides/06-installer-integration.en.md) for
  plugin-owned Installer hooks, [Guide 14](guides/14-mock-services.en.md) for
  mock CM/CE services, and [Guide 15](guides/15-profile-authority.en.md) for
  trusted profiles.
- **Run the approval UI:** [Guide 08](guides/08-consent-ui-poc.en.md) for the
  example app; [Guide 17](guides/17-native-ui-smoke.en.md) for automated native
  UI checks; [Guide 10](guides/10-feature-approval.en.md) for feature selection.
- **Change the framework:** [Design 02](design/02-architecture.en.md) for
  ownership, [Design 05](design/05-decisions.en.md) for adopted contracts,
  and [Guide 16](guides/16-maintenance-and-integration.en.md) for remaining work.

한국어 독자는 아래 표에서 같은 번호의 한국어 문서를 선택하세요. 빌드/배포는
01→07, API 입력은 02→13, 서비스 연동은 06→14→15, UI 검증은 08→17,
프레임워크 변경은 설계 02→05와 가이드 16 순서로 읽으면 됩니다.

## Development guides

| No. | Read this for | English | 한국어 |
| --- | --- | --- | --- |
| 01 | SDK setup, packages, deployment, and development workflow | [Development](guides/01-development.en.md) | [개발 가이드](guides/01-development.ko.md) |
| 02 | All 42 public C functions, terminology, role flow, C examples, ownership and callbacks | [C API](guides/02-c-api.en.md) | [C API 설명·사용법](guides/02-c-api.ko.md) |
| 03 | Protected installation generations and provisioning tool | [Installation authority](guides/03-installation-authority.en.md) | [설치 authority](guides/03-installation-authority.ko.md) |
| 04 | Root image registration, staged definitions, and startup reconciliation | [Offline registration](guides/04-offline-registration.en.md) | [Offline 등록](guides/04-offline-registration.ko.md) |
| 05 | Bounded Parcel IDL and deterministic compiler | [IDL](guides/05-idl.en.md) | [IDL](guides/05-idl.ko.md) |
| 06 | Installer hook findings and the required external transaction contract | [Installer integration](guides/06-installer-integration.en.md) | [Installer 통합](guides/06-installer-integration.ko.md) |
| 07 | Verification summary and historical evidence | [Verification](guides/07-verification.en.md) | [검증 기록](guides/07-verification.ko.md) |
| 08 | Interactive Consent UI, isolated participants and TPK workflow | [Consent UI PoC](guides/08-consent-ui-poc.en.md) | [Consent UI PoC](guides/08-consent-ui-poc.ko.md) |
| 09 | Receipt-preserving compaction and registry-loss recovery boundaries | [Storage maintenance](guides/09-storage-maintenance.en.md) | [저장소 관리](guides/09-storage-maintenance.ko.md) |
| 10 | Feature selection, missing-only approval and conversation reuse | [Feature approval](guides/10-feature-approval.en.md) | [기능 승인](guides/10-feature-approval.ko.md) |
| 11 | CEP 19.2 evidence and open integration gates | [Acceptance audit](guides/11-acceptance.en.md) | [수용 기준 감사](guides/11-acceptance.ko.md) |
| 12 | Isolated real CM parser and CM/CE consent developer examples | [Developer smoke](guides/12-developer-smoke.en.md) | [개발자 smoke](guides/12-developer-smoke.ko.md) |
| 13 | Registered JSON-RPC fixture tool and context lookup examples | [Tool examples](guides/13-tool-examples.en.md) | [도구 예제](guides/13-tool-examples.ko.md) |
| 14 | Persistent CM/CE mock service integration | [Mock services](guides/14-mock-services.en.md) | [Mock 서비스](guides/14-mock-services.ko.md) |
| 15 | Explicit requester profiles and sessiond authority | [Profile authority](guides/15-profile-authority.en.md) | [프로필 authority](guides/15-profile-authority.ko.md) |
| 16 | Code maintenance and product integration | [Guide 16](guides/16-maintenance-and-integration.en.md) | [가이드 16](guides/16-maintenance-and-integration.ko.md) |
| 17 | Repeatable native UI smoke | [Guide 17](guides/17-native-ui-smoke.en.md) | [가이드 17](guides/17-native-ui-smoke.ko.md) |

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
startup import still validates the installed package/app and protected generation.

## Historical evidence

[English record](history/07-verification-history.en.md) ·
[한국어 기록](history/07-verification-history.ko.md)

Build-by-build commands and failures are preserved there. Portable examples in
edited documents are not byte-for-byte copies of archived host commands. Raw
execution records remain in the external artifact directories named by each
checkpoint; document edits do not imply new tests.

[Prior-layout popup HTML archive](previews/consent-popup.html) · 이전 배치 팝업 보관본

[Compact One UI-style proposal](previews/consent-popup-oneui.html) · 작은 팝업 제안
