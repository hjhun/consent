# 16. 작성 이력 기반 스타일 점검과 남은 개발

작성자 `h.jhun@samsung.com`, 작성일 2020-01-01부터 2026-01-31까지,
로컬 Git 전체 refs를 기준으로 점검했습니다. 기존 2020–2025 스킬 조사
(142개 저장소, 작성 이력 53개 저장소, 대표 패치 61개)를 재사용합니다.
이 과거 집계를 현재 전체 집계로 바꾸지 않습니다. 9개 저장소의 대표 작성
변경 10개를 직접 확인했으며, 모든 과거·현재 코드 줄을 읽었다는 뜻은 아닙니다.

| 저장소 | 작성 패치 | 확인 근거 |
| --- | --- | --- |
| base/bundle | 0337ede3538b (2020) | 올바른 해제자와 래퍼 |
| api/app-control | f3e351be4cc5 (2021) | 공개 API와 내부 소유권 분리 |
| api/preference | a2f8c65d2a7b (2021) | backend 책임 분리 |
| appfw/amd | 13a4bd592dc5 (2023) | 공통 queue 소유권 재사용 |
| appfw/aul-1 | 0a4f77d53ff8 (2023), 7f0a555a0e63 (2025) | 생성 이후 공개와 콜백 수명 |
| appfw/launchpad | f85766b27ea8 (2023) | 작은 worker/job 책임 |
| appfw/rpc-port | 50bfbe4a86e3 (2024) | static 전송 콜백 어댑터 |
| api/app-common | b3879488c97a (2025) | 명시적 multiuser 입력 |
| api/app-event | 44361a3d58fc (2025) | 콜백 실행 중 소유권 유지 |

AMD `29ed218b`와 AUL `ac581e7`는 다른 작성자의 릴리스 스냅샷이며,
파일 전체가 사용자 작성 변경이라는 근거가 아닙니다. Watcher는 요청한 레이아웃
참조로 유지하지만 기준일 이전 사용자 작성 패치는 확인하지 못했습니다.
기존 `.h` 이름과 record 이름 예외를 유지합니다. 명시적 80열 기준을 과거 코드
전체에 일률적으로 적용됐던 관례라고 주장하지 않습니다.

## 한정된 변경

프로필 네이티브 콜백은 명명된 `noexcept` 어댑터에서 예외를 차단하고 Stop으로
닫습니다. 비동기 envelope가 소유자를 유지하며, 타이머는 할당 가능한 함수 복사
없이 envelope를 고정하므로 실행 중 소스 제거도 안전합니다. 수명 facade와
SQLite 소유자는 복사를 금지합니다. SQLite 오류 문자열은 `sqlite3_free`로
해제하고 클라이언트 잠금은 기존 `MutexLock`을 재사용합니다.

프로필 설정은 전용 checked loader로 검사합니다. 설정 부재는 기존 동작을
유지하고, 존재하는 잘못된 설정은 시작을 거부합니다. 모든 필수 문자열의 GLib
디코딩 오류를 검사합니다. 명시적 `subsession=`는 유효하지만 `subsession=\x`는
거부합니다. 보호된 파일 열기, 제한된 전체 읽기, NUL/중복/알 수 없는 키 거부와
검증 이후 공개를 유지합니다. 공통 `KeyValue` 동작은 변경하지 않습니다.

private provider fault 테스트는 spawn 이후 설정 실패 시 자식 kill과 reap을
확인합니다. 프로필 예제는 단일 Pending의 reset/wait/take와 API 반환 이후
단일 콜백·결과 이전을 사용합니다. 80열 초과 네 줄만 정리하고 smoke version
map을 링크 의존성에 추가합니다. ABI·wire·역할·프로필 세대·저장 정책을 유지합니다.

## 남은 개발 우선순위

1. 제품 CM의 신뢰 identity·consent 위임·최종 실행 어댑터와 최신 CE 소스,
   identity·등급·어댑터 계약.
2. 각 plugin이 install/update/remove hook을 담당하되 인증된 Installer
   publication/offline handle과 보호된 package/app generation을 사용합니다.
   consent 연동 테스트는 이 계약을 유지합니다. 기존 NUI 시각·상호작용 코드는
   재사용하며 제품 build·endpoint 패키징·신뢰 UI 역할·argo launch mapping은
   남습니다. 임의 라이브러리 runtime 대체를 뜻하지 않습니다.
3. 보호된 sessiond mapping과 권한 provisioning. Profile05 조사에서는 설치된
   sessiond의 비활성 상태, bus owner 부재와 mapping 부재를 확인했습니다.
   이 관찰만으로 서비스가 비활성인 원인을 확정하지 않습니다.
4. 실제 holder cleanup과 DB 전체 손실 이후 신뢰 registry producer.
5. 저장·전원 중단과 대표적인 지속 부하 검증.

합성 CM/CE·프로필 fixture는 격리 consent 동작을 검증하며 제품 어댑터,
실제 사용자 전환 또는 물리적 삭제 근거가 아닙니다.
[Guide15](15-profile-authority.ko.md)의 프로필 신뢰 경계를 참고하십시오.
Guide11의 과거 스타일 근거 집계는 그대로 유지합니다.

## 검증한 Release26 체크포인트

기준 `c15336c`에서 추적 중인 네이티브/CMake 파일 159개의 줄 길이를
검사했고 네 줄이 80열을 초과했습니다. 집중 의미 검토와 대표 작성 변경 확인은
모든 코드 줄을 읽었다는 주장이 아닙니다. 기존 스킬이 발견 항목을 이미 다루므로
스킬은 변경하지 않았습니다.

근거: `/var/tmp/consent-artifacts/consent-style-06/`.
순차 실행한 `gbs-r1.log/.exit`와 최종 `gbs-r2.log/.exit`는 모두 exit0,
31개 테스트 중 27 PASS·문서화된 root 전용 4 SKIP입니다. 문자열 오류,
spawn 이후 kill/reap과 실제 private bus barrier 예외 회귀가 통과했습니다.
마지막 검증은 제품 어댑터의 private 테스트 bus 경로이며 실제 sessiond
사용자 전환은 하지 않았습니다.

```sh
gbs build -A x86_64 --profile tizen_10_1_emulator \
  --include-all --define '_without_poc 1'
```

`source-r2.json`, `export-r2.json`, `inputs-r2/`는 실행한 25개 파일과
export 입력을 보존합니다. `rpms-r2/`의 생성 RPM 9개 해시는
`rpms-r2.json`에 있습니다. 설치는 `consent-0.1.0-26.x86_64.rpm`,
`consent-devel-0.1.0-26.x86_64.rpm`, `consent-tests-0.1.0-26.x86_64.rpm`
세 개만 선택했고 제품 daemon/PoC RPM은 제외했습니다.
`rpm-dependencies-r2.log`는 의존성 검사를 보존합니다. libsessiond·dbus·
JSON-GLib은 테스트 패키지 의존성이며 새 제품 의존성으로 추가하지 않았습니다.

발견한 `emulator-26101`, `x86_64`에서 `install-r2.log`는 INSTALL_EXIT0입니다.
`installed-payload-r2.log`는 설치된 일반 파일 162개를 archive RPM과
비교해 VERIFY_EXIT0을 기록합니다. 설치된 설정·프로세스 네이티브 회귀도
`native-config-r2.log`, `native-process-r2.log`에서 각각 NATIVE_EXIT0입니다.

| 설치 runner 모드 | Seed | SMOKE_EXIT / OUTER_EXIT | 근거 로그 |
| --- | --- | --- | --- |
| profiles | 20261020 | 0 / 0 | installed-profiles-seed20261020-r2.log |
| profiles --require-product | 20261021 | 1 / 1, 예상 결과 | installed-strict-seed20261021-r2.log |
| mock-services | 20261022 | 0 / 0 | installed-mock-seed20261022-r2.log |
| tools | 20261023 | 0 / 0 | installed-tools-seed20261023-r2.log |
| default | 20261024 | 0 / 0 | installed-default-seed20261024-r2.log |

정확한 명령은 `commands.jsonl`, host driver는 `target-owner.py`입니다.
runner는 `systemd-run --quiet --wait --pipe`, `User=root`,
`SmackProcessLabel=System`과
`/usr/bin/python3 /usr/libexec/consent/smoke/emulator-smoke.py`에 표의
모드·seed 인자를 전달했습니다. strict는 프로필 시나리오를 완료한 뒤 오직
`product profile provisioning/privilege unverified`로 실패했습니다.
예상한 registry 전체 손실 시 서비스 start 실패는 보호 차단이며 시나리오 실패가
아닙니다. 두 프로필 실행은 begin/reconnect/new async/new pending과 admission
불변을 확인합니다. 이전 콜백은 Pending 정리 이전에 detach됩니다.

모든 중간 cleanup과 `cleanup-final-r2.log`는 cleanup0입니다.
`device-before-r2.log`, `device-after-r2.log`의 전체 제품·PoC metadata 지문은
정확히 같습니다. 제품 daemon18은 PID31569로 active, PoC18은 PID0으로
inactive, CM15도 그대로입니다. 최종 smoke unit 두 개는 not-found이고
fixture 디렉터리 네 개는 absent입니다. 제품 활성화·정책 변경·실제 계정 전환·
외부 참조 저장소 변경은 하지 않았습니다.

실행 소스와 publication 구분: r2 export 이후 `repository.cc`의 인접 표준
`<type_traits>`/`<utility>` include 순서만 바뀌었습니다. 역교환하면 실행 해시가
정확히 재현되며 두 해시와 검증은 `publication-include-order.json`에 있습니다.
당시 나머지 24개 파일은 byte 일치했습니다. 최종 publication은 이 한·영
가이드의 나중 근거 서술도 포함합니다. 따라서 나머지 22개 파일은 byte 일치,
한 개는 승인된 include 순서 변경, 두 개는 나중 서술입니다. 이 publication
차이에 대한 RPM 재빌드나 설치본과의 해시 일치를 주장하지 않습니다.
이전 r1 소스·RPM 근거도 보존했고 대표 저장소 수의 원래 8개 표기는 r2에서
9개로 정정했습니다.
