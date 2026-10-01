# 가이드 07: 검증 상태

이 가이드는 실제로 실행한 검사와 결과의 한계를 요약합니다. API 예제나 테스트
소스만으로 기기 검증을 완료했다고 판단하지 않습니다.
[과거 기록](../history/07-verification-history.ko.md)에 실제 명령, 실패한 시도와
이전 스냅샷을 보존했습니다. 이번 문서 수정으로 빌드나 기기 검증을 추가하지
않았습니다.

## 최근 검증한 실행 흐름

저장소의 네이티브 UI 실행 도구를 Release28 빌드와 고정된 `source-r14.json`으로
x86_64 개발 에뮬레이터에서 검증했습니다. GBS는 종료 코드 0으로 완료했으며
CTest 30개가 통과하고 root 전용 4개는 건너뛰었습니다. 관리 코드 UI 검사와
호스트 안전성 테스트 58개도 통과했습니다.

한 명령으로 실행한 전체 검증은 실제 UI 선택과 작업 실행, 정상 재시작, 중지한
DB 삭제, 설치 세대 변경을 포함합니다. 별도의 세대 변경 실행과 기능 반복
실행도 통과했습니다. 총 여섯 트랜잭션에서 정리가 성공했고, 원래 운영/PoC의
패키지 파일, 보호된 상태와 서비스 정보는 계약에 따라 유지하거나 복원했습니다.
임시 TPK 교체와 허용된 시각 변경은 따로 기록했습니다. 합성 CM/CE 참여자가
실제 격리 consent IPC를 사용한 결과이며 제품 어댑터 검증은 아닙니다.

준비 사항, 명령과 결과 파일은 [가이드 17](17-native-ui-smoke.ko.md)에 있습니다.
원본 증거는 `/var/tmp/consent-artifacts/consent-ui-smoke-10/`에 보관합니다.

## 주요 검증 단계

| 스냅샷 | 검사 대상 | 기록된 결과 |
| --- | --- | --- |
| Release19, smoke01 r6 | CM parser, 역할별 검사, DB 복구 | 22 PASS + 4 SKIP; smoke 0, 제품 strict 1 |
| Release21, tool r4 | JSON-RPC 제공자, CE 데이터, 재시도와 복구 | 23 PASS + 4 SKIP; tools/default 0, strict 1 |
| Release23, mock r5 | 지속형 CM/CE 서비스와 재시도 권한 검사 | 23 PASS + 4 SKIP; mock/tools/default 0 |
| Release25, profile r8 | 프로필 차단, 전용 bus 테스트, 정리 | 25 PASS + 4 SKIP; profiles/mock/tools/default 0, strict 1 |
| Release26, maintenance r2 | 설정 오류, 콜백 예외, 자식 회수 | 27 PASS + 4 SKIP; 설치 모드 0, strict 1 |
| Release27, native UI r7 | 실제 기간 선택, receipt와 수명 검증 | 29 PASS + 4 SKIP; 완료한 기능/세대 실행 0 |
| Release28, UI runner r14 | UI 자동 조작과 네 수명 단계 | 30 PASS + 4 SKIP; 전체 실행과 반복 0 |

각 행은 다른 스냅샷이며 테스트 수를 합산한 결과가 아닙니다. 제품 strict의
예상 종료 코드 1은 격리 시나리오가 통과했지만 필요한 제품 연동이 없다는
뜻입니다. 과거 기록과 각 가이드에 실제 명령, 실패 이력과 생략한 검사가 있습니다.

## 알려진 한계

UI runner r12에서는 OFF 확인 뒤 거절 완료를 관측하지 못했고 원인은 아직
모릅니다. r13의 세대 변경 실행에서는 Next 입력 후 첫 관측에서 페이지가
바뀌지 않아 실패했습니다. 이후 진단과 제한된 페이지 전환 대기로 관측을
개선했지만 네이티브 입력/갱신 경합을 고쳤다고 증명하지는 않았습니다. 같은
빌드의 전체 실행과 기능 반복은 통과했으나 간헐적 실패는 재현성의 한계로
남습니다. 이 경우 runner는 여전히 실패로 종료합니다.

제품 CM/CE 어댑터, 신뢰 UI/argo 배포, sessiond 계정 연결과 권한, 실제 holder의
삭제는 아직 검증하지 않았습니다. Installer hook은 각 plugin이 인증된 등록과
설치 세대 계약에 따라 구현합니다. Registry 손실 뒤에는 신뢰할 정의 생산자가
필요하며 정의로 잃어버린 승인을 복원할 수 없습니다. 정상 재부팅과 주입한
실패만으로 갑작스러운 전원 차단이나 실제 지속 부하를 검증했다고 할 수 없습니다.

과거 CEP 수용 기준은 [가이드 11](11-acceptance.ko.md), 남은 작업은
[가이드 16](16-maintenance-and-integration.ko.md)을 참고하세요.
