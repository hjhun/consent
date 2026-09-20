# consent 개발 가이드

이 문서는 저장소의 실제 구현을 설명합니다. 원본
[CEP](CEP_Consent_Framework.md)는 설계 제안으로 보존합니다. 구현과 검증은
통합 중이며, API나 테스트 소스가 존재한다는 사실만으로 모든 CEP 수용 기준을
통과했다고 판단하지 않습니다.

## 빌드 환경

CMake 3.12 이상, Python 3, C11, C++17과 pkg-config 패키지 `glib-2.0`, `gio-2.0`,
`gio-unix-2.0`, `sqlite3`, `libsystemd`, `pkgmgr-info`, native Tizen `parcel`이
필요합니다.
2026-09-20 확인한 개발 emulator는 x86_64, GLib 2.80.5, SQLite 3.50.2,
systemd 244를 사용합니다. 호스트 빌드는 GBS 검증을 보완합니다.

현재 장치를 탐색하고 사용할 serial을 명시합니다.

```sh
sdb devices
CONSENT_DEVICE=<선택한-개발-emulator>
sdb -s "$CONSENT_DEVICE" shell 'uname -m; systemctl --version'
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root
```

위 프로필은 초기 개발 환경에서 확인한 값이며 설치된 SDK에 맞게 조정합니다.
`--include-all`은 미커밋 소스를 포함하므로 commit이 필요하지 않습니다.
GBS chroot 초기화에는 로컬 권한이 필요할 수 있습니다. 저장소 자격증명이나
GBS 설정 전체를 로그에 노출하지 않습니다.

## 패키지와 설치 파일

| 패키지 | 내용 |
|---|---|
| `consent` | 버전이 있는 공개 공유 라이브러리 |
| `consent-devel` | 공개 C 헤더, 링커 symlink, `consent.pc`, 한영 가이드 |
| `consentd` | `/usr/bin/consentd`, `/usr/sbin/consent-installation-authority`, systemd unit, 빈 신원 정책 |
| `consent-tests` | 설정된 libexec 경로의 C 실행 프로그램과 격리 테스트 |

구현과 테스트 소스는 `src/` 아래에 있습니다. 빌드 설정은 구성 요소별로
나눕니다. 데몬만 mode 0700의 `/opt/var/lib/consentd`와 `consent.db`를 소유합니다.
Tizen의 `/var`는 `/opt/var`로 연결되므로 실제 경로를 지정하여 symlink를
허용하지 않는 state 경로 검증을 유지합니다. 이 환경에서 systemd의
`StateDirectory=consentd`는 같은 위치를 만듭니다.
systemd가 `/run/.consentd.sock`을 소유하고 데몬은 상속받은 listener를
사용합니다. IPC는 크기를 제한한 4바이트 길이 헤더와 native Tizen Parcel을
사용합니다. 부팅 시에는 socket만 활성화합니다.

## Native Parcel IDL

`src/protocol/consent.idl.json`이 wire record를 정의합니다. 빌드는 표준
라이브러리만 사용하는 `src/tools/parcel_codegen.py` 컴파일러로 빌드 디렉토리의
`generated/consent_wire.hh`를 생성합니다. `Field`와 `Envelope`는
`WriteToParcel`, `ReadFromParcel`, `Valid` 메서드가 있는 native
`tizen_base::Parcelable` 하위 클래스입니다. 생성 코드는
`src/common/parcel_codec.hh`의 크기를 제한하는 native Parcel helper를
사용하며 Python은 런타임 의존성이 아닙니다.

지원 IDL 타입은 `u32`, `i32`, `u64`, `max_bytes`가 있는 UTF-8 `string`,
앞서 선언된 record, `max_count`가 있는 앞서 선언된 record의 `array`입니다.
문자열에는 `min_bytes`, 정수에는 범위 검증되는 `default`를 둘 수 있습니다.
필드 선언 순서가 wire 순서입니다. 재귀·전방 참조, 중복 이름·JSON key,
모르는 제약, 예약 식별자 및 상한 없는 필드는 생성에 실패합니다. license
metadata에는 Apache-2.0 전문이 필요합니다. 컴파일러는 중첩 깊이와 확장된
record 구조·wire 크기·할당량을 제한하며, 배열 읽기는 남은 wire 바이트를
검사한 다음 저장 공간을 늘립니다.
잘못된 입력에서는 기존 생성 파일을 보존하며 출력이 같으면 파일을 다시 쓰지
않습니다. 호환되지 않는 필드 구조 변경 전 wire 버전을 확장해야 합니다.

```sh
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json --check
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json /tmp/consent_wire.hh
python3 src/tests/idl_codegen_test.py
```

## 신원 정책과 배포

초기 서비스는 다른 UID의 프로세스 신원 확인과 복구 registry 보호를 위해
root로 실행합니다. socket은 mode 0660, root:`system_share`입니다.
이 그룹은 전송 경로의 접근만 허용합니다. 데몬은 kernel credential과
root 소유 `/etc/consent/roles.conf`로 역할을 인증합니다. 출하 설정에는
허용 신원이 없습니다. 플랫폼 통합 담당자가 실행 파일, kernel security
label, 역할 및 위임 문맥을 명시적으로 등록해야 합니다.

운영 클라이언트는 고정된 `/run/.consentd.sock` 경로와 root 소유권을 검사하며
연결 전후 socket의 device/inode가 같은지 확인합니다. kernel peer credential이
PID 1/UID 0과 설정된 `System::Privileged` security label을 가리킬 때만 systemd
상속 endpoint를 인정합니다. 이 환경에서는 `/run`의 root:`system_share`
group 쓰기 권한만 허용하며, 다른 쓰기 가능한 상위 경로는 거부합니다.
일반 프로세스가 소유한 socket은 서비스 신원으로 인정하지 않습니다.

각 `[identity NAME]` keyfile 절에는 `uid`, `executable`, `label`과
세미콜론으로 구분한 `roles`, `subjects`, `profiles`, `enforcers`, `packages`를
적습니다. `enforcers`는 위임된 집행 주체 신원이며 `packages`는 Installer가
관리할 패키지를 제한합니다. Subject와 profile의 wildcard는 거부합니다.
테스트 신원을 운영 정책으로 복사하지 않습니다. 실제 신원 검증과 역할명은
`src/consentd/identity.cc`가 정의합니다.

선택한 장치의 architecture에 맞게 빌드한 RPM만 설치합니다. 설치 후 다음을
확인합니다.

```sh
CONSENT_RPM_DIR=/var/tmp/consent-gbs-root/local/repos/tizen_10_1_emulator/x86_64/RPMS
sdb -s "$CONSENT_DEVICE" root on
for package in consent consentd consent-devel consent-tests; do
  sdb -s "$CONSENT_DEVICE" push "$CONSENT_RPM_DIR/$package-0.1.0-1.x86_64.rpm" /tmp/
done
sdb -s "$CONSENT_DEVICE" shell 'rpm -Uvh --replacepkgs --replacefiles /tmp/consent-0.1.0-1.x86_64.rpm /tmp/consentd-0.1.0-1.x86_64.rpm /tmp/consent-devel-0.1.0-1.x86_64.rpm /tmp/consent-tests-0.1.0-1.x86_64.rpm'
sdb -s "$CONSENT_DEVICE" shell 'systemctl status consentd.socket'
sdb -s "$CONSENT_DEVICE" shell 'journalctl -u consentd.service -n 80 --no-pager'
```

GBS가 출력한 실제 release와 RPM 경로를 사용합니다. 검증한 개발 재설치는
버전 번호를 바꾸지 않고 위 consent RPM에 `--replacepkgs --replacefiles`를
적용했습니다.

`consentd.service`만 중지하면 다시 활성화될 수 있습니다. 유지보수 시에는
socket과 service를 함께 중지합니다. systemd endpoint를 직접 지우지 않습니다.

## API 연동 계약

설치된 메타데이터로 C 프로그램을 빌드합니다.

```sh
cc consumer.c -o consumer $(pkg-config --cflags --libs consent)
```

`consent_register()`는 패키지명과 app ID를 각각 전달받고,
`consent_unregister()`는 패키지에 속한 모든 정의를 제거합니다.
호출자가 보낸 문자열만으로 소유권을 인정하지 않습니다. request는 인증된
argo만 사용할 수 있고 check는 UI를 호출하지 않습니다. 보호 작업 실행에는
AUTHORIZE 검사를 사용하며 일회성 소비와 재시도 식별자를 원자적으로 처리합니다.

비동기 함수 성공은 접수를 뜻합니다. 즉시 결정되는 결과를 포함하여 최종
성공·실패는 등록한 dispatcher callback으로 전달합니다. 입력은 반환 전에
복사합니다. callback 결과는 callback 실행 중 빌린 객체이므로 보관하려면
공개 clone/free 계약을 따릅니다. 비동기 함수는 dispatcher 소유 스레드에서
호출하며 중첩 loop iteration을 하지 않습니다. request cache를 보호 작업의
최종 실행 권한으로 사용하지 않습니다.

세션은 전송 연결과 독립된 대화를 뜻합니다. artifact와 data-use permit은
제어 메타데이터만 저장하며 대화 본문을 저장하지 않습니다. 실제 데이터 수명은
holder가 집행하고 정리를 ACK합니다. close 응답은 이후 사용 차단을 뜻하며
물리적 삭제 완료를 증명하지 않습니다.
holder가 재시작하면 명시적인 subject/profile과 `reconcile=1`로
`consent_cleanup_get_pending()`을 호출하여 이전 프로세스 인스턴스의 미완료
정리를 조회합니다. 실제 삭제 후 같은 문맥으로 `consent_data_release()`를
호출하여 완료를 알립니다. 이 대조는 정리 권한만 부여합니다. DB 전체 소실과
holder 대조의 한계는 [저장 가이드](storage-design.ko.md)를 참조합니다.

## 파라미터 필드

공개 ABI는 opaque `consent_params_t` builder와 문자열 필드를 사용합니다.
`consent_params_set()`은 UTF-8 입력을 복사하며 정수 setter는 십진수로
변환합니다. 프로토콜 신원 필드와 밑줄로 시작하는 내부 필드는 예약되어 있습니다.

| 작업 | 필수 필드와 의미 |
|---|---|
| register/update | `operation_id`, `expected_generation`, `definition`, `enforcer`, 양수 `policy_version`·`text_revision`, 0–3의 `level`, 쉼표 구분 `modes`, `default_locale`, `message.<locale>.title`·`.body`; package/app은 별도 API 인자 |
| unregister | 패키지명은 별도 인자이며 params에 재시도 시 같은 `operation_id`와 현재 `expected_generation` 전달; app ID 불필요 |
| request | `subject`, `profile`, 안정된 `client_request_id`, `operation_id`, requirements; 선택적으로 `session`과 해당 `generation`; `deadline_ms`는 로컬 대기 timeout과 별개 |
| check | `subject`, `profile`, requirements; mode는 QUERY 또는 AUTHORIZE; AUTHORIZE에는 `operation_id`·`step_id`도 필요 |
| requirement | `consent_params_add_requirement()`로 definition·operation·정확 scope·purpose·recipient 추가; 최대 16개 |
| session open | `subject`, `profile`; lifecycle은 CONNECTION_BOUND 또는 RESUMABLE_CONVERSATION; 시간 상한은 데몬이 검증 |
| session transition | `subject`, `profile`, `session`, 현재 `generation`; resume에는 회전되는 `resume_token`도 필요 |

현재 버전은 scope의 정확 일치를 비교합니다. UI는 등록 문구와 실제
scope·purpose·recipient를 함께 표시해야 합니다. 타입 있는 포맷 스키마가
준비될 때까지 문구 placeholder는 거부합니다. Level 3은 ONCE만 허용합니다.
정책 의미가 변경되면 `policy_version`, 번역이 수정되면 `text_revision`을
증가시킵니다.

승인 응답은 ONCE 승인을 소비하지 않고 현재 요구 조건 전체의 AND를 다시
판정합니다. 예를 들어 A는 이미 허용된 상태에서 B에 대한 선택을 기다렸는데,
사용자가 B를 승인하기 전에 A가 만료·철회되거나 다른 작업에서 소비되면 요청은
`INVALIDATED`로 종료합니다. 조건별 결과는 현재 상태(A `CONSENT_REQUIRED`,
B `ALLOWED`)를 나타내며, 명시적으로 선택했고 여전히 유효한 B 승인은 소비하지
않고 유지합니다. 결합된 작업의 실행을 허용한 결과는 아닙니다. 기존 요청은
종료 상태이며 자동으로 승인 화면을 다시 열지 않습니다. 추가 승인이 필요하면
인증된 요청자가 새 request/operation ID로 요청을 시작해야 합니다. 실제 보호
작업은 여전히 전체 조건에 대한 `AUTHORIZE`가 필요합니다.

아래는 인증된 집행 서비스가 C API로 사전 조회하는 예입니다. 0이 아닌
status에서는 실행을 차단합니다.

```c
#include <consent.h>

int query_calendar(consent_client_h client) {
  consent_params_t *params = NULL;
  consent_result_t *result = NULL;
  int status = consent_params_create(&params);
  if (status != 0)
    return status;
  status = consent_params_set(params, "subject", "org.example.agent");
  if (status == 0)
    status = consent_params_set(params, "profile", "owner");
  if (status == 0)
    status = consent_params_add_requirement(params, "calendar.read", "read",
        "today", "answer-calendar", "");
  if (status == 0)
    status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
  if (status == 0)
    status = consent_check(client, params, 2000, &result);
  /* status == 0일 때만 consent_result_get_decision(result)를 확인합니다.
   * QUERY는 사전 조회이며 보호 데이터 접근 전 AUTHORIZE가 필요합니다. */
  consent_result_free(result);
  consent_params_free(params);
  return status;
}
```

## 복구와 검증

root 전용 installation authority는 `begin`, `attach`, `commit`, `remove`로
`installations.conf`를 관리합니다. begin은 세대를 회전하고 과거 앱 목록의
사용을 차단하며 attach는 패키지의 각 앱을 기록하고 commit은 완성된 목록을
활성화합니다. 한 번도 기록되지 않은 패키지에만 `absent`를 사용합니다.
명령마다 고유 operation ID와 기대 세대가 필요하며 결과가 불확실하면 같은
operation으로 재시도합니다. 실제 패키지 설치와 authority commit이 성공한
후에만 등록합니다. 삭제할 때는 authority를 먼저 removed로 바꾼 뒤 같은
기대 세대로 consent 정의를 unregister합니다. tombstone은 패키지 전체 정리가
진행되는 동안 새 권한 부여를 막습니다. 재설치는 새 세대를 사용합니다. 이 도구는
Installer 연동 지점이며 플랫폼 hook 설치 완료를 뜻하지 않습니다.

C API 실행 프로그램은 `METHOD [PACKAGE [APP]] key=value ...`, `--async`,
`--timeout-ms=N`, `--repeat=N`, `--expect-status=N`,
`--expect-decision=VALUE`를 받습니다. 결과가 다르면 0이 아닌 종료값을
반환합니다. 일치하는 위임 문맥으로 신뢰 checker를 설정한 후 다음과 같이
실행할 수 있습니다.

```sh
/usr/libexec/consent/tests/consent-api-test check \
  subject=org.example.agent profile=owner count=1 \
  r0.definition=calendar.read r0.operation=read r0.scope=today \
  r0.purpose=answer-calendar --async --expect-decision=CONSENT_REQUIRED
```

`-isolated` 도구는 별도 정적 테스트 클라이언트를 연결합니다. `consentd-test`는
같은 역할 검사와 `/tmp/consent-test`의 socket/config 경로,
`/opt/var/lib/consent-test`의 영속 state 경로를 사용하며 설치 인벤토리 adapter만
대체합니다. 운영 바이너리에는 해당 adapter를
활성화하는 런타임 switch가 없습니다. client unit test는 모의 전송 peer를
사용하므로 클라이언트 계약을 검증하며 데몬 정책이나 플랫폼 신원 검증을
대신하지 않습니다.

`scripts/emulator-scenario.sh`는 격리된 에뮬레이터 단계별 테스트를 제공합니다.
`basic`은 새 테스트 설치 상태가 필요하며 이전 결과를 임의로 삭제하지 않습니다.
후속 단계는 지속성, 실행·정지 중 DB 삭제, 손상 DB 복구, 과거 DB 교체 및
동일 UID의 역할 거부를 확인합니다. 선택한 개발 에뮬레이터에서 root와
`System` security label로만 실행합니다. `endpoint-fixture`는 실제
`/run/.consentd.sock`을 사용하는 별도 수동 endpoint 인증 도구이므로 CTest에서
제외합니다. 실행 전 [검증 증거](verification.ko.md)의 준비 절차를 따릅니다.

미완성 IPC 입력, UI 승인을 기다리는 요청, 실제 대기 중인 DB 작업의 종료
증거는 구분합니다. 수동 `wire-scenario --shutdown-wait`는 일부 header만 받은
연결의 종료를 확인하며 supervisor가 서비스 정상 종료와 `database-drained`
로그도 확인합니다. 이것만으로 대기 중이던 DB 작업의 완료를 입증하지는 않습니다.
UI를 기다리는 요청은 DB transaction이나 worker를 점유하지 않으므로 그 요청의
연결 단절·재시작 동작은 별도 시나리오입니다. 실제로 확인한 경계는 검증 기록을
참조합니다.

프로세스 강제 종료, 정상 재부팅, emulator 전원 강제 중단을 별도 시나리오로
기록합니다. 강제 DB 삭제는 격리된 테스트 상태 또는 선택한 개발 emulator의
consent 상태만 대상으로 합니다. 다른 플랫폼 DB를 삭제하지 않습니다.
승인 정보가 소실되면 새 승인이 필요하며 등록 정의 복원에는 별도로 신뢰할 수
있는 원본이 필요합니다. 권한·용량 부족·I/O 오류를 DB 삭제로 숨기지 않습니다.

초기 저장 구현은 직렬화한 SQLite 연결 하나와 `journal_mode=DELETE`,
`synchronous=EXTRA`, foreign key 및 100 ms busy timeout을 사용합니다.
rollback journal은 SQLite가 관리하므로 복구 중 따로 지우지 않습니다.
보호된 `definitions.registry`는 DB와 독립적으로 정의와 패키지 삭제 기록을
보존합니다. 별도 Installer 세대 registry와 플랫폼 `pkgmgr-info`로 정의를
현재 설치 인스턴스에 결합합니다. 복원 대상은 정의이며 사용자 승인은 복원하지
않습니다. 실행 중 DB가 없거나 교체되면 기존 handle을 닫고 새 epoch를 만듭니다.
정상 재시작에서는 pending 요청과 세션을 무효화하며 조건을 만족하는 지속 승인을
보존합니다. 삭제 증거가 수신될 때까지 holder 정리는 미완료로 남습니다.

GBS는 GCC 14.2와 `-Werror`로 native Parcel 클라이언트·데몬·C 실행
프로그램·installation authority를 빌드합니다. 패키지 검사는 클라이언트 계약,
저장 정책·복구, 저장 장애 주입, 프로세스 강제 종료 경계 및 IDL 생성을 다룹니다.
build-root 테스트와 에뮬레이터 통합 검증은 구분합니다. [검증 증거](verification.ko.md)에
검사한 snapshot, 실제 결과와 남은 수용 조건을 기록합니다. mock 신원 테스트가
제품 신원 연동을 입증하지는 않으며 실제 Installer·argo·UI 정책 연동이 필요합니다.

데몬은 조건을 만족하는 PERSISTENT/SESSION 승인을 최대 500 ms 동안 cache
가능으로 표시하며 timer 처리에서 설치 세대를 대조합니다. 각 클라이언트
handle은 request cache를 최대 64개 유지합니다. SESSION 항목은 서버가 확인한
session과 generation이 일치해야 하며 전달된 TTL과 session 만료를 넘지
않습니다. handle 사이에 cache를 공유하지 않습니다. 이벤트·epoch 변경·연결
단절은 항목을 무효화하며 QUERY와 AUTHORIZE는 항상 데몬을 확인합니다.
실제 승인 UI·Installer 수명주기 hook·제품 정책 연동이 필요하며 프로토콜
실행 프로그램은 운영 UI를 대체하지 않습니다.

## 문제 해결

호스트의 pkg-config 의존성 부재를 SDK 문제로 단정하지 말고 설정된 GBS
프로필로 확인합니다. socket 접근 오류는 DAC·SMACK·역할 정책을 각각
확인합니다. credential과 역할 로그에 raw scope·대화 내용·자격증명을 추가하지
않습니다. 유효한 activation listener가 정확히 하나 없으면 데몬은 시작에
실패해야 하며 직접 bind로 우회하지 않습니다.
