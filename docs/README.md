# consent documentation

This index separates the framework's design from practical development and
integration guides. Numbers indicate reading order within each directory.
English and Korean guides describe the same behavior; the original CEP is
preserved in Korean.

## Where to start

1. Read the [project overview](../README.md), then **Guide 01** to build and deploy.
2. Use **Guide 02** for C API integration and **Design 02** for component ownership
   and trust boundaries.
3. Follow **Guides 03–06** for installation generations, offline registration,
   protocol generation, and the external Installer integration contract.
4. Check **Guide 07** before making verification claims. Use **Guide 08** for the
   interactive Consent UI proof of concept and **Guide 09** for storage maintenance.
5. For framework changes, read **Design 01** and **Design 05**, then the relevant
   protocol or storage design. The proposal, adopted decisions, and executable
   evidence have distinct scopes.

## Development guides

| No. | Read this for | English | 한국어 |
| --- | --- | --- | --- |
| 01 | SDK setup, packages, deployment, and development workflow | [Development](guides/01-development.en.md) | [개발 가이드](guides/01-development.ko.md) |
| 02 | Public C API examples, ownership, and asynchronous callbacks | [C API](guides/02-c-api.en.md) | [C API](guides/02-c-api.ko.md) |
| 03 | Protected installation generations and provisioning tool | [Installation authority](guides/03-installation-authority.en.md) | [설치 authority](guides/03-installation-authority.ko.md) |
| 04 | Root image registration, staged definitions, and startup reconciliation | [Offline registration](guides/04-offline-registration.en.md) | [Offline 등록](guides/04-offline-registration.ko.md) |
| 05 | Bounded Parcel IDL and deterministic compiler | [IDL](guides/05-idl.en.md) | [IDL](guides/05-idl.ko.md) |
| 06 | Installer hook findings and the required external transaction contract | [Installer integration](guides/06-installer-integration.en.md) | [Installer 통합](guides/06-installer-integration.ko.md) |
| 07 | Exact build/emulator evidence, tested snapshots, and remaining limits | [Verification](guides/07-verification.en.md) | [검증 기록](guides/07-verification.ko.md) |
| 08 | Interactive Consent UI, isolated participants and TPK workflow | [Consent UI PoC](guides/08-consent-ui-poc.en.md) | [Consent UI PoC](guides/08-consent-ui-poc.ko.md) |
| 09 | Receipt-preserving compaction and registry-loss recovery boundaries | [Storage maintenance](guides/09-storage-maintenance.en.md) | [저장소 관리](guides/09-storage-maintenance.ko.md) |

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

Product role identities, the Installer transaction hook, and a production
approval UI remain integration work. The planned UI PoC is intended to exercise
the public API with an isolated test identity; it does not enroll a product UI or
replace platform approval policy. Offline registration stages definitions only;
startup import still validates the installed package/app and protected generation.

Keep English/Korean pairs synchronized when behavior changes. Update this index
and relative links when moving or adding documents; preserve historical evidence
and distinguish source inspection from executed verification.
