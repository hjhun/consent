# 가이드 01: Consent 빌드와 배포

<a id="가이드-01-consent-개발-가이드"></a>

[English](01-development.en.md)

선택한 개발 에뮬레이터용 패키지를 빌드하고 그 빌드의 정확한 RPM을 설치한 뒤
서비스 시작 상태를 확인합니다. 승인 API 사용은
[API 01](api/01-start.ko.md)로 이어집니다. 원래 설치를 복원하는 임시 네이티브 UI
시험은 [가이드 17](17-native-ui-smoke.ko.md)을 사용하세요.

## 1. SDK 준비하고 에뮬레이터 선택하기

CMake 3.12 이상, Python 3, C11, C++17과 pkg-config 패키지 `glib-2.0`, `gio-2.0`,
`gio-unix-2.0`, `sqlite3`, `libsystemd`, `pkgmgr-info`, `capi-base-common`,
native Tizen `parcel`이
필요합니다.
2026-09-20 확인한 개발 emulator는 x86_64, GLib 2.80.5, SQLite 3.50.2,
systemd 244를 사용합니다. 호스트 빌드는 GBS 검증을 보완합니다.

현재 장치를 탐색하고 사용할 serial을 명시합니다.

```sh
sdb devices
CONSENT_DEVICE='SELECTED_EMULATOR'
sdb -s "$CONSENT_DEVICE" shell 'uname -m; systemctl --version'
gbs build -A x86_64 -P tizen_10_1_emulator --include-all \
  -B /var/tmp/consent-gbs-root
```

위 프로필은 초기 개발 환경에서 확인한 값이며 설치된 SDK에 맞게 조정합니다.
`--include-all`은 미커밋 소스를 포함하므로 commit이 필요하지 않습니다.
GBS chroot 초기화에는 로컬 권한이 필요할 수 있습니다. 저장소 자격증명이나
GBS 설정 전체를 로그에 노출하지 않습니다.

기본 RPM 빌드는 격리된 .NET PoC도 활성화합니다. 추가 BuildRequires는
`dotnet-build-tools` 8.0.421, `csapi-tizenfx-nuget` 14.0.0.19364,
`capi-appfw-app-control`입니다. GBS 자체 SDK와 offline NuGet 입력으로 TPK를
컴파일하고 개발 서명합니다. 별도 패키지와 설정 계약은
[가이드 08](08-consent-ui-poc.ko.md)을 참고하세요.

## 2. 같은 빌드의 패키지 선택하고 설치하기

성공한 GBS 빌드가 출력한 정확한 RPM 경로를 사용하세요. 설치 전에 패키지 이름,
버전, 아키텍처와 의존성을 확인합니다. 아래 명령은 환경에 맞춰 바꾸는 예제이며
새 장치 실행 결과를 뜻하지 않습니다.

```sh
CONSENT_RUNTIME_RPM=/path/to/consent-VERSION-RELEASE.x86_64.rpm
CONSENT_DAEMON_RPM=/path/to/consentd-VERSION-RELEASE.x86_64.rpm
CONSENT_TESTS_RPM=/path/to/consent-tests-VERSION-RELEASE.x86_64.rpm
rpm -qp --qf '%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n' \
  "$CONSENT_RUNTIME_RPM" "$CONSENT_DAEMON_RPM" "$CONSENT_TESTS_RPM"
sdb -s "$CONSENT_DEVICE" root on
for rpm_file in "$CONSENT_RUNTIME_RPM" "$CONSENT_DAEMON_RPM" \
    "$CONSENT_TESTS_RPM"; do
  sdb -s "$CONSENT_DEVICE" push "$rpm_file" /tmp/
done
runtime_name=$(basename "$CONSENT_RUNTIME_RPM")
daemon_name=$(basename "$CONSENT_DAEMON_RPM")
tests_name=$(basename "$CONSENT_TESTS_RPM")
sdb -s "$CONSENT_DEVICE" shell \
  "systemd-run --quiet --wait --pipe \
  -p SmackProcessLabel=System::Privileged /bin/sh -c \
  'rpm -Uvh /tmp/$runtime_name /tmp/$daemon_name /tmp/$tests_name; \
  install_status=\$?; echo CONSENT_INSTALL_EXIT=\$install_status; \
  exit \$install_status'"
sdb -s "$CONSENT_DEVICE" shell 'systemctl status consentd.socket'
sdb -s "$CONSENT_DEVICE" shell \
  'journalctl -u consentd.service -n 80 --no-pager'
```

호스트에서 SDB root를 선택한 뒤 실행합니다. root 임시 unit은 확인한
System::Privileged 설치 문맥을 사용합니다. CONSENT_INSTALL_EXIT=0과 원격 unit의
성공 종료를 확인하세요. 호스트 SDB의 0만으로는 부족합니다. 타깃 정책이 해당
문맥을 허용하지 않으면 의존성이나 라벨을 우회하지 말고 원인을 확인합니다.

RPM 설치가 성공하고 소켓이 대기하며 저장소·listener·신원 설정 오류 없이
서비스가 시작해야 합니다. 기본 역할 정책은 어떤 앱에도 권한을 주지 않습니다.
서비스 시작 성공이 첫 API 호출의 권한을 뜻하지는 않습니다.

소비자 프로그램을 빌드하려면 같은 빌드의 `consent-devel`도 설치하세요.
의존성은 설정한 SDK와 저장소로 해결합니다. `--nodeps`를 쓰거나 무관한 플랫폼
라이브러리를 강제로 올리지 마세요. 같은 버전 교체는 소유 파일을 별도로 확인해야
합니다. 위 명령은 선택한 패키지만 지정하는 일반 업그레이드입니다.

## 3. 첫 API 소비자 빌드하기

헤더와 라이브러리가 설치된 타깃 또는 호환 SDK에서
[API 01](api/01-start.ko.md)의 완전한 조회 프로그램을 `query.c`로 저장하세요.

```sh
cc -std=c11 -Wall -Wextra query.c -o consent-query \
  $(pkg-config --cflags --libs consent)
```

프로그램에는 별도로 등록한 checker 신원이 필요합니다. QUERY는 현재 조건 충족
여부만 조회하며 UI를 열거나 데이터를 읽지 않습니다. 실제 접근은
[승인 요청과 실행 허가](api/03-request-and-check.ko.md)로 이어집니다.

## 문제 해결

호스트의 pkg-config 의존성 부재를 SDK 문제로 단정하지 말고 설정된 GBS
프로필로 확인합니다. socket 접근 오류는 DAC·SMACK·역할 정책을 각각
확인합니다. credential과 역할 로그에 raw scope·대화 내용·자격증명을 추가하지
않습니다. 유효한 activation listener가 정확히 하나 없으면 데몬은 시작에
실패해야 하며 직접 bind로 우회하지 않습니다.


호환되는 `consent.h` umbrella는 독립 기능 헤더 `consent_common.h`,
`consent_client.h`, `consent_params.h`, `consent_result.h`,
`consent_registration.h`, `consent_request.h`, `consent_prompt.h`,
`consent_session.h`, `consent_data.h`를 포함합니다. 각 헤더는 C/C++에서 독립적으로
컴파일됩니다. C wrapper도 기능별로 분리했으며 비공개 Guard/Call/Submit만 공유하고
transport/cache 소유권은 기존 `client.cc`에 유지합니다.
명시적 등록 전용 offline 생성자는 [offline 등록](04-offline-registration.ko.md)을
참고하세요. 일반 client가 연결 실패 후 이 모드로 자동 전환하지 않습니다.
## 패키지와 설치 파일

| 패키지 | 내용 |
|---|---|
| `consent` | 버전이 있는 공개 공유 라이브러리 |
| `consent-devel` | 공개 C 헤더, 링커 symlink, `consent.pc`, 한영 가이드 |
| `consentd` | `/usr/bin/consentd`, `/usr/sbin/consent-installation-authority`, `/usr/sbin/consent-storage-prepare`, systemd unit, 빈 신원 정책 |
| `consent-tests` | libexec 아래 C 실행 프로그램, C 연동 예제 4개와 격리 테스트 |
| `consent-poc` | 별도 .NET UI/음성 시험 TPK, 엄격한 client, daemon, 역할별 mock 5개 |

구현 소스는 `src/`, 테스트 소스는 프로젝트 루트의 `tests/`에 있습니다. 빌드 설정은 구성 요소별로
나눕니다. 데몬만 mode 0700의 `/opt/var/lib/consentd`와 `consent.db`를 소유합니다.
Tizen의 `/var`는 `/opt/var`로 연결되므로 실제 경로를 지정하여 symlink를
허용하지 않는 state 경로 검증을 유지합니다. root 준비 도구가 디렉터리 생성과
이행을 맡으며, systemd StateDirectory나 RPM 디렉터리 속성으로 검증 전에
재귀 소유권 변경을 수행하지 않습니다.
systemd가 `/run/.consentd.sock`을 소유하고 데몬은 상속받은 listener를
사용합니다. IPC는 크기를 제한한 4바이트 길이 헤더와 native Tizen Parcel을
사용합니다. RPM은 sockets.target.wants/consentd.socket과 AMD 방식의
basic.target.wants/consentd.service symlink를 설치합니다. 부팅 기동과
socket activation 모두 같은 상속 listener를 사용합니다.

## 신원 정책과 배포

서비스는 기존 플랫폼 계정 `security_fw`(관측 UID/GID402)를 사용하며,
bounded/ambient capability는 `CAP_SYS_PTRACE` 하나이고 NoNewPrivileges를
유지합니다. 이름이 정확히 `security`인 계정은 없었으며 새 계정은 만들지 않습니다.
플랫폼에서 계정 이름을 조회하고 숫자 UID를 추정하여 대체하지 않습니다.
root 전용 ExecStartPre 준비 도구는 별도 프로세스입니다. socket은 기존처럼
mode0660, root:`system_share`입니다.
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

선택한 장치의 architecture에 맞게 유지보수할 때는 consentd.socket과 consentd.service를 모두 중지하세요.
서비스만 중지하면 소켓으로 다시 시작할 수 있습니다. systemd endpoint를 직접
삭제하지 마세요.
## 참조: 생성 프로토콜

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
python3 tests/idl_codegen_test.py
```

<a id="빌드-환경"></a>

[빌드 환경](api/01-start.ko.md)

<a id="native-parcel-idl"></a>

[Native Parcel IDL](05-idl.ko.md)


<a id="api-연동-계약"></a>
<a id="공개-오류-값과-업그레이드"></a>

[공개 오류 값과 업그레이드](api/04-results-and-callbacks.ko.md)

<a id="파라미터-필드"></a>

[파라미터 필드](api/03-request-and-check.ko.md)


<a id="다국어-승인-문구"></a>
<a id="복구와-검증"></a>

[복구와 검증](09-storage-maintenance.ko.md)

---

[이어 읽기](api/01-start.ko.md) · [역할별 문서](../README.md)
