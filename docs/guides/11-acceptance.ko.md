# 11. CEP 19.2 수용 기준 감사

이 표는 [Design 01의 63개 수용 기준](../design/01-consent-framework.md)을
Build43 Release11과 번호가 다른 Build45–49 추가 근거에 대응합니다.
`Target`은 지정한 framework 동작을 emulator의 격리·패키지 fixture에서
실행했거나, build 전용 기준을 해당 행의 GBS에서 실행했다는 뜻입니다.
제품 통합을 뜻하지 않습니다. `Partial`은 범위가 좁거나 실패 조건 시험이
남았고, `Product`는 실제 제품 participant 또는 신뢰할 producer가 없다는
뜻입니다. 번호별
로그는 `/var/tmp/consent-artifacts/gbs-build-{43,45,46,48,49}`에 있습니다.
추가 근거가 있는 행은 Build 번호를 명시합니다. 이전 시험을 Build49
재실행으로 소급하지 않습니다. 이 과거 감사의 집계는 Target23, Partial33,
Product7입니다.

| ID | 범위 | 근거 또는 남은 경계 |
| --- | --- | --- |
| A-01 | Product | Mock/거부 역할 시험만 존재; 실제 CM·CE 신원 없음. |
| A-02 | Target | 격리 basic 및 public API SYNC/ASYNC. |
| A-03 | Target | Client owner/cache callback 순서 fixture. |
| A-04 | Target | Check는 UI 없이 CONSENT_REQUIRED 반환. |
| A-05 | Target | Repository feature/만료 fixture. |
| A-06 | Target | Cache scenario 및 owner 무원격 cache 회귀. |
| A-07 | Partial | Revoke/restart cache 시험; 제품 profile authority 없음. |
| A-08 | Partial | Mock provider/subject 분리; 제품 provider 없음. |
| A-09 | Partial | 거부 판단 시험; 실제 보호 action 없음. |
| A-10 | Target | 동시 일회성 authorization fixture. |
| A-11 | Target | Repository 다중 조건 transaction fixture. |
| A-12 | Target | Basic/race의 operation ID 중복·충돌. |
| A-13 | Target | 승인/취소/만료 경쟁 및 callback fixture. |
| A-14 | Partial | Repository crash 창 fixture; 실제 daemon 정확한 창 미시험. |
| A-15 | Partial | Localization·UI PoC; production UI 없음. |
| A-16 | Partial | PoC의 stale policy 응답; production UI 없음. |
| A-17 | Product | Installer lifecycle hook·완전한 desired ledger 없음. |
| A-18 | Partial | Kill/주입 storage·정상 reboot; 갑작스러운 전원 종료 없음. |
| A-19 | Target | Build49 설치 격리 C API: 로컬 TIMEOUT/NULL과 새 ID 원격 PENDING 조회, 범위 오류 조회, 취소·최종 CANCELLED; target 4회. |
| A-20 | Target | Callback 재진입/Close owner fixture. |
| A-21 | Partial | Framework session/permit; 제품 holder 없음. |
| A-22 | Partial | Framework ONCE/SESSION; 제품 holder 없음. |
| A-23 | Partial | Framework PERSISTENT/session; holder cleanup 없음. |
| A-24 | Partial | 격리 연결/resume; 제품 session 없음. |
| A-25 | Partial | Framework suspend fence; 제품 data holder 없음. |
| A-26 | Partial | Framework 재연결 신원; 제품 controller 없음. |
| A-27 | Partial | Framework stale-generation fence; 제품 continuation 없음. |
| A-28 | Partial | Framework idle/절대 TTL; 제품 holder 없음. |
| A-29 | Partial | Framework 만료/provenance; model summary 없음. |
| A-30 | Partial | Provenance revoke fixture; 실제 파생 data 없음. |
| A-31 | Product | 실제 inference 경계·model 문맥 cleanup 없음. |
| A-32 | Partial | Holder ACK 재시도 metadata; 물리적 삭제 미증명. |
| A-33 | Target | Build46 packaged fixture: 서버 ID가 입력 ID를 대체; 같은 holder/context도 타 세션 artifact check·derived 거부. 첫 세션 허용 유지, 둘째 artifact 없음. 제품 holder는 별개. |
| A-34 | Partial | MEMORY_ONLY 규칙; 메모리 압박/spill 미증명. |
| A-35 | Product | History/embedding writer 및 제한 상속 없음. |
| A-36 | Product | 외부 model recipient 통합 없음. |
| A-37 | Partial | 격리 정상 reboot grant 유지; argo 재시작/session fence 미증명. |
| A-38 | Partial | Framework 재조회; 실제 eviction holder data 없음. |
| A-39 | Partial | Cleanup 실패 상태 표시; 물리적 holder cleanup 없음. |
| A-40 | Product | 다중 부모 제품 summary/copy 연결 없음. |
| A-41 | Product | 제품 지연 result/model/history participant 없음. |
| A-42 | Target | Framework 비활성/종료 session 거부. |
| A-43 | Target | 패키지 daemon I/O 및 main context fixture. |
| A-44 | Partial | Bounded thread 설계; 장기 구독 부하 미증명. |
| A-45 | Partial | Async queue 설계; 장기 승인 대기 부하 미증명. |
| A-46 | Target | 설치된 wire 분할/EOF/크기 fixture. |
| A-47 | Partial | Build46 checker의 PID/UID/GID 위조 session 요청 거부; DLOG는 실제 kernel peer 사용. 안팎 role guard 구분 및 다른 역할은 미증명. |
| A-48 | Partial | 역할 거부 확인; SO_PEERCRED 실패 주입 부족. |
| A-49 | Partial | Bounds/fault fixture; 지속 과부하 수치 부족. |
| A-50 | Target | Matching service의 socket activation·READY. |
| A-51 | Partial | Build45 target 네 거부 통과; child FD 명시 close는 미추적. |
| A-52 | Target | Socket unit과 Build43 service 재시작 100/100. |
| A-53 | Partial | 재진입/cache 일부 interleaving 통과; 전체 lock/no-wait 불변식 미증명. |
| A-54 | Target | Cache-only caller context·반환 후 전달 시험. |
| A-55 | Partial | 같은 PID/UID 기본 거부; 실제 shared-UID 역할 없음. |
| A-56 | Target | 패키지 shutdown/drain·ownership fixture. |
| A-57 | Target | IO/owner late completion·FD reuse fixture. |
| A-58 | Partial | Slow-reader bound; UNSYNCED 재동기화 결과 부족. |
| A-59 | Target | Target log의 kernel PID/UID/GID·종료 이유. |
| A-60 | Partial | Retry metadata; ACK 기한초과·제품 holder 없음. |
| A-61 | Target | 설치 API/wire에서 생성 parcel 교환. |
| A-62 | Partial | Build45 target version/크기/개수/trailing/EOF; early allocation·target NUL 미증명. |
| A-63 | Target | Build48 GBS 격리 IDL 수정: 결정성·invalid schema 시험과 header 재생성, client/daemon compile·link, license·정확 복원. 설치 runtime은 원래 IDL 사용. |

번호 밖 release gate도 열려 있습니다. Root 인증된 완전한 최신
desired-definition producer가 없어 production `--begin`은 저장소 변경 없이
실패합니다. Build43 receipt나 격리 DB-only recovery로 registry 전손 import,
`import_complete` fence, 물리적 `cleanup_unknown` 해소를 주장하지 않습니다.
