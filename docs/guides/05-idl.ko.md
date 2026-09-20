# 가이드 05: 상한이 있는 Parcel IDL과 compiler

단일 원본은 `src/protocol/consent.idl.json`입니다. CMake가 Python 표준 라이브러리만
사용하는 `src/tools/parcel_codegen.py`를 실행해 빌드 디렉터리에
`consent_wire.hh`를 생성합니다. 양쪽 endpoint가 동일한 native Tizen Parcelable
클래스를 포함합니다. 생성물을 직접 수정하거나 public C ABI로 설치하지 않습니다.
Python은 빌드/테스트 의존성입니다.

```sh
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json /tmp/consent_wire.hh
python3 src/tests/idl_codegen_test.py
```

최상위 JSON에는 license/namespace/records만 있습니다. license는 지원하는 전체
Apache 고지를 유효한 JSON metadata로 저장합니다. record는 name/fields를 가지며
배열 순서가 wire 필드 순서입니다. 앞서 정의된 record만 참조하므로 순환과 전방
참조를 거부합니다. 중복 JSON key, 알 수 없는 제약, C++/helper 예약명, 범위 밖
기본값은 오류입니다. schema에 있는 코드/import/template을 실행하지 않습니다.

| 필드 타입 | name/type 외 속성 | 인코딩 |
|---|---|---|
| u32 | 선택 default, 0..2^32-1 | 4바이트 BE unsigned |
| i32 | 선택 default, -2^31..2^31-1 | 4바이트 BE signed |
| u64 | 선택 default, 0..2^64-1 | 8바이트 BE unsigned |
| string | 필수 max_bytes, 선택 min_bytes | 마지막 NUL 포함 BE u32 길이, UTF-8, NUL |
| 앞서 정의한 record 이름 | 없음 | 선언 순서로 inline 필드 |
| array | 앞선 record인 element, 필수 max_count | BE u32 count 뒤 record 원소 |

float/union/service RPC 생성/import/임의 코드/무제한 타입은 없습니다.
namespace/identifier를 검증하며 record 이름은 대문자, 필드 이름은 소문자로
시작합니다. record 최대64개, record별 필드64개, 중첩16단계, array count4096입니다.
문자열의 설정 상한은65536바이트 이하입니다. 전이 최소 wire 크기와 고정 layout
추정은65536바이트, 전이 최대 wire와 heap 예산은16MiB 이하입니다. 이 compiler
예산과 별도로 runtime 전체 frame은65536바이트로 제한합니다. 고정 layout은
보수적 예산 추정이며 모든 C++ compiler ABI의 실제 크기라고 주장하지 않습니다.

생성 reader는 primitive 가용 바이트와 문자열 길이를 할당 전에 검사하고 마지막
NUL/중간 NUL/UTF-8을 검증합니다. array는 count<=max_count와
count×원소 최소 encoded size를 담을 남은 바이트가 모두 있어야 할당합니다.
Decode 오류는 명시적인 validity 상태로 남깁니다. native ReadParcelable이 파생
reader 실패를 전파하지 않으므로 caller가 이 상태를 확인합니다. frame 전체 소비와
envelope 의미 검증도 필요합니다.

현재 schema는 Field(key,value)와 Envelope(version,kind,correlation,method,status,
fields)입니다. endian/test vector/correlation/frame 상한과 도메인 parameter
표현은 [프로토콜 가이드](../design/03-protocol.ko.md)에 정의합니다. 생성기는 필드명으로 role이나
정책 의미를 추론하지 않으며 server/repository가 별도 검증합니다.

출력은 전체 license를 포함한 결정적 UTF-8이며 timestamp/source path를 넣지
않습니다. compiler는 임시 파일과 atomic rename으로 씁니다. 회귀시험은 정상 생성,
동일 입력의 byte-identical 출력, 잘못된 schema, helper/type 충돌, 지수적인 layout
증대를 검증합니다. native Parcel roundtrip/truncation/oversize는 client-test와
수동 daemon wire fixture에서 검증합니다. 저장소 registry는 별도로 버전 관리하는
내부 codec이며 IPC envelope나 대체 wire가 아닙니다.
