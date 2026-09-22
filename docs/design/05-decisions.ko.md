# 설계 05: 구현 결정 기록

날짜: 2026-09-22. 이 문서는 PO·아키텍트의 구현 지침이며 코드 완성이나 검증
완료를 뜻하지 않는다. `01-consent-framework.md`는 설계 제안으로 유지한다.
실제 구현 상태와 검증 근거는 한·영 개발 및 아키텍처 가이드에 기록한다.

## 협업과 산출물

- Herdr `w1:pA` 패널은 제품 범위, 설계 결정, 검토, 사용자 소통을 담당한다.
  기존 Codex `w1:pJ` 패널은 구현·통합을 담당한다. 이 패널 ID는 현재 개발
  세션에만 해당한다.
- 구현 담당은 결정 사항의 근거·선택지·추천을 PO 패널로 전달하고, 답변을
  기다리는 동안 독립 작업을 계속한다. 일상 기술 결정은 사용자 재승인 없이
  처리한다.
- CEP와 `AGENTS.md`를 보존한다. 사용자의 후속 지시로 완료·검증한 단위마다
  commit/push한다. PO 패널만 Git index·commit·push를 변경하고 구현 에이전트는
  준비된 파일 목록을 보고한다. 참고 appfw 저장소는 계속 읽기 전용이다.
- 모든 소스에 기존 appfw 형식의 Samsung copyright와 Apache-2.0 전체 고지를
  표기한다. 헤더·테스트·도구와 빌드·스크립트 파일의 언어에 맞는 고지도 포함하며
  이 작업에서는 SPDX만 있는 임시 표기로 대체하지 않는다.
- 기반, 승인·세션 흐름, 캐시, 데이터 수명을 검토 가능한 단위로 통합한다.
  일부 단계 완료를 전체 프레임워크 완성으로 보고하지 않는다.
- 운영 코드, 테스트 어댑터, 호스트 검사, 실제 플랫폼 통합 검증을 구별하고
  제품 연동의 미완료 사항을 명시한다.

## D-01: 역할 인증과 패키지 소유권

보호된 설정의 기본 거부 정책을 채택한다. 커널 peer credential, 소켓 보안
라벨, 검증된 실행파일 신원을 함께 사용한다. 공유 UID, root UID, 실행파일
이름, 호출자가 주장한 역할만으로는 충분하지 않다. 명시적으로 전달받은
패키지명과 app ID의 관계는 `pkgmgr-info`로 검증한다.

설정·실행파일·상위 디렉터리의 소유권과 권한으로 비인가 변경을 막는다.
실행파일 신원이 불명확하거나 삭제된 경우, 필수 라벨 부재, 설정 오류는
거부한다. 프로세스 신원은 접수한 peer의 수명과 연결해야 하며, 재사용된 PID
조회는 인증이 아니다. 대상의 pidfd 또는 동등한 수명 확인 수단을 조사한다.

실제 argo, Capability Manager, Context Engine, 승인 UI, Installer, 세션 제어
주체, holder의 신원은 플랫폼 근거로 확인한다. 경로·라벨·기존 privilege를
만들어 가정하지 않는다. 미설정 역할은 거부 상태를 유지한다. 역할 검사는
Subject/profile 위임, 정의 소유권, enforcement owner, prompt/session 결합
검증을 대신하지 않는다.

AMD의 Cynara 소켓 credential 어댑터를 참고하되 대상 의존성과 정책을 확인한
뒤 채택한다. 테스트 신원 어댑터는 별도 빌드 target과 격리된 설정·상태에서
사용하며 운영 실행파일에 인증 우회 옵션을 제공하지 않는다.

로컬 `libcynara-commons`에서는 주의가 필요하다. 2025-07-01의 `3ca3518f` commit이
`cynara_session_from_pid()`의 프로세스 시작 시각을 제거했다. 확인한 구현은 헤더
설명과 달리 PID 문자열만 반환하므로 peer 수명을 증명하지 못한다. 소켓 credential
helper도 thread-safe하지 않다고 선언한다. 두 가정을 그대로 가져오지 않는다.

## D-02: 직렬 SQLite 내구성과 복구

SQLite를 소유하는 DB executor 하나로 시작한다. `journal_mode=DELETE`,
`synchronous=EXTRA`, foreign key 활성화, 제한된 busy 대기를 사용하고 대상에서
설정값을 다시 조회해 확인한다. EXTRA는 rollback journal 제거 후 디렉터리
동기화를 포함한다. 실제 저장장치와 재부팅 동작은 별도 검증이 필요하다.
[SQLite 동기화 문서](https://www.sqlite.org/pragma.html#pragma_synchronous)를 참고한다.

일반 DB 작업과 복구가 같은 직렬화 경계를 사용한다. 시작뿐 아니라 실행 중
파일 소실·교체·사용 불가 상태를 감지한다. 같은 경로를 재생성하기 전에 이전
DB handle을 종료하고 DB generation을 변경하며 요청·캐시·세션을 무효화한다.
불확실한 상태에서 권한을 허용하지 않는다. 권한 오류, 저장공간 부족, 임의 I/O
오류를 DB 삭제 사유로 취급하지 않는다. 정의로 사용자 승인을 추론하지 않는다.

## D-03: 정의 전용 복구 원본

동적 등록을 복구하려면 `consent.db`와 독립된 원본이 필요하다. 크기가 제한되고
보호된 consentd 전용 정의 registry를 정의의 원하는 상태에 대한 원본으로 삼고
DB에 트랜잭션으로 반영한다. 기존 패키지 consent manifest 규격이 존재한다고
가정하지 않는다.

registry에는 schema/revision, 검증한 package/app/install 신원, 등록 인스턴스,
정책·번역 전체, 제거 tombstone, fingerprint에 결합한 작업 중복 방지 정보를
저장한다. 사용자 결정·grant·사용 기록·session은 저장하지 않는다. 같은 작업의
재시도는 등록 인스턴스를 유지하되, 제거·재설치 후 같은 정의 내용에 과거 grant가
다시 연결되지 않게 한다.

하나의 DB executor에서 다음 순서로 처리한다.

1. 인증된 Installer 권한, package/app 소유권, 요청 fingerprint, 현재 설치
   신원을 검증한다.
2. 같은 디렉터리의 임시 파일에 새 registry를 쓰고 파일 동기화, 원자적 rename,
   디렉터리 동기화를 수행한다.
3. 한 DB 트랜잭션에서 정의, 관련 무효화, 반영한 registry revision을 갱신한다.
   commit 후 성공과 이벤트를 전달한다.

두 저장 단계는 하나의 원자적 트랜잭션이 아니다. registry 확정 후 DB 반영이
실패하면 권한 허용을 차단하고 적절한 저장소/결과 불명 오류를 반환한다. 같은
작업 ID의 재시도·재적용으로 수렴시킨다. rename/sync 실패 후 이전 snapshot을
확정 원본인 것처럼 취급하지 않는다. DB가 정상이어도 READY 또는 권한 허용 전에
미반영 registry를 재조정한다.

DB 소실 시 검증된 registry에서 정의만 재생성하고 새 승인을 요구한다. 설치
소유권과 설치 신원을 다시 검증하며 불확실한 정의는 비활성 상태로 둔다.
registry도 소실·손상되면 재등록이 필요하며 복구했다고 가장하지 않는다.
tombstone은 패키지가 설치된 상태여도 제거한 정의가 복원되지 않게 한다.
전체 상태 소실만으로 holder 정리 완료를 증명할 수 없으며 생존 holder의
무효화와 재조정이 필요하다.

인증된 Installer가 제공하는 보호된 설치 generation 원본을 사용한다. 새 설치마다
generation을 회전시키고 uninstall은 tombstone을 남긴다. 형식·검증·expected
generation 결합과 준비·시험 도구는 이 저장소에 구현한다. 실제 플랫폼 Installer의
수명 hook 배포는 별도 통합 의존성이다. 근거가 없거나 불일치하면 등록·활성화를
명시 오류로 거부한다. 초 단위 설치 시각·version·root inode/ctime만으로 동일
설치 인스턴스를 증명하지 않는다.
[Installer 인계 문서](../guides/06-installer-integration.ko.md)에 조사한 callback의 한계와
아직 필요한 외부 transaction/recovery 계약을 기록한다.

DB 작업 전과 commit 후 결과 게시 전에 pathname 신원을 확인한다. 중지 중 정상
형식의 과거 DB로 바꾼 경우도 감지하도록 예상 DB 신원·인스턴스를 별도의 보호된
제어 메타데이터에 저장한다. 무결성 검사나 DB와 함께 복사된 UUID는 최신성을
증명하지 못한다. 특권 주체의 임의 in-place rollback까지 inode 검사로 감지한다고
주장하지 않는다. 살아 있는 SQLite 파일을 별도로 open/read/close해 검증하면 POSIX
잠금에 영향을 줄 수 있으므로 피한다.
[SQLite 손상 지침](https://www.sqlite.org/howtocorrupt.html)을 참고한다.

## D-04: 실행 모델과 공개 API 계약

CEP의 GLib/GIO 모델을 유지한다. main loop는 수명, 제한된 I/O context는 연결,
worker는 짧은 작업, DB executor는 직렬 DB 작업을 담당한다. 승인 대기 중
worker·mutex·DB 트랜잭션을 점유하지 않는다. 소유권과 종료 순서를 명확히 한다.

`consent_register()`에 패키지명과 app ID를 별도 인자로 제공하고 패키지 단위로
제거한다. async request/check 접수와 최종 결정을 구별한다. 접수된 결과는
소유 dispatcher에서 API 반환 이후 최대 한 번, 라이브러리 잠금 없이 전달한다.
명시적 source와 nested loop 금지 계약을 사용한다. GLib `invoke()`는 직접 실행될
수 있다. [GLib 계약](https://docs.gtk.org/glib/method.MainContext.invoke.html)을 참고한다.

QUERY는 승인 소비나 UI 생성을 하지 않는다. AUTHORIZE는 모든 조건 평가와
일회성 사용 기록을 원자적으로 처리한다. 캐시는 실제 실행의 최종 확인을
대신하지 않는다. 재시작 시 세션 무효화와 holder cleanup ACK 의미를 유지한다.

## D-05: 초기 대상과 wire 선택

구현 패널은 x86_64, kernel 4.4.35, SMACK을 지원하는 systemd 244, GLib 2.80.5,
SQLite 3.50.2를 보고했다. 검증 기록에는 실제 명령을 남겨야 한다. 이 커널에서는
제안한 pidfd 경로를 사용할 수 없다. 엄격한 소켓 라벨·credential·실행파일 검증과
신원 조회 전후 및 요청 시 프로세스 시작 시각 확인을 사용한다. connect부터 최초
조회까지의 PID 재사용 한계는 명시하며 완전히 제거했다고 주장하지 않는다.

초기 root daemon 및 `0660 root:system_share` socket을 대상 group·참여자 DAC·SMACK
검증 조건으로 채택한다. root도 역할 검사를 우회하지 않는다. 역할 설정은 신원이
검증될 때까지 비어 있는 기본 거부 상태로 시작한다.
후속 비-root 배포 요구는 D-12에 명세한다. 이 문단은 앞선 검증 build가 사용한
계정의 기록이다.

사용자의 후속 요구에 따라 초기 GVariant 선택을 `platform/core/base/bundle`의
실제 `parcel` library로 대체한다. 4바이트 big-endian 길이 frame 안에 생성한 C++
`tizen_base::Parcelable` 메시지를 넣는다. 양측에서 `SetByteOrder(true)`로 고정 폭
정수와 길이의 byte order를 지정한다. 공개 C ABI는 wire 표현과 분리한다. 이전
GVariant 빌드 결과는 중간 검증이며 새 프로토콜의 검증 근거가 아니다.

## D-06: 작은 IDL compiler와 제한된 Parcel 읽기

사용자가 허용한 consent 전용 IDL과 Python 표준 라이브러리 기반 compiler를 채택한다.
소스·schema는 `src/`에 두고 빌드 디렉터리에 결정적인 C++ Parcelable 코드를 생성해
client와 daemon이 공유한다. Python은 빌드·시험 의존성이다. 제한된 문자열·배열,
record, 고정 폭 정수를 지원하고 중복 선언, 알 수 없는 타입, 잘못된 상한, 지원하지
않는 재귀 구조를 거부한다. 생성물에도 전체 라이선스 고지를 넣는다. 범용 RPC
runtime으로 확장하거나 기존 GLib/UDS 실행 모델을 교체하지 않는다.

모든 primitive read 결과, 할당 전 길이, 남은 바이트, UTF-8, 문자열 종료, 중복,
correlation, version, trailing bytes를 검사한다. 조사한 Parcel `ReadString()`은
wire 길이만큼 먼저 할당하며 문자열의 안전한 종료를 보장하지 않는다. 비신뢰
문자열에는 Parcel 정수·raw-byte API에 기반한 제한된 helper를 사용한다.
`ReadParcelable()`은 void virtual reader 호출 후 항상 성공을 반환하므로 생성
객체가 decode 오류 상태를 별도로 보존·노출해야 한다.

native struct·native endian·float/double을 피한다. 부분 송신이 끝날 때까지 Parcel
또는 소유한 바이트를 유지하며 할당 예외를 C ABI·GLib 경계에서 처리한다. 결정적
생성, 잘못된 IDL, wire 시험 벡터, 잘림·초과·malformed parcel, 실제 emulator 통신을
검증한다. 독립적인 DB/registry canonical 형식과 wire versioning은 명시적으로
구별한다. CEP v0.4는 이 요구를 기록하며 구현 완료를 주장하지 않는다.

## D-07: 대상 경로와 client의 activation endpoint 검증

선택한 emulator의 `/var`는 `/opt/var`로 연결된다. 상태 디렉터리는 정규 경로인
`/opt/var/lib/consentd`와 `/opt/var/lib/consent-test`를 사용한다. 보호 상태 경로의
symlink 거부를 완화하지 않는다. 필수 endpoint `/run/.consentd.sock`은 유지한다.

대상의 `/run`은 `root:system_share`, mode 0775다. 정확히 이 디렉터리와 확인된
소유자·그룹에 한해 group-write를 허용한다. world-write, symlink, 다른 쓰기 가능한
상위 경로는 계속 거부한다. 연결 전후 socket의 root 소유 및 device/inode 동일성도
확인한다. 이 pathname 검사만으로 일시 교체 후 복원 공격을 방어한다고 주장하지 않는다.

protocol 송신 전에 연결된 커널 `SO_PEERCRED`의 UID 0/PID 1을 확인하고,
`getpeername()`의 AF_UNIX family·길이·NUL 종료를 검증해 주소가 정확히
`/run/.consentd.sock`인지 확인한다. peer name은 원래 bind 주소여서 다른 systemd
socket을 rename해도 바뀌지 않는다. PID 1만 신뢰하지 않고 system manager가 만든
listener를 consent endpoint에 결합한다. 직접 bind한 위장 서버는 listener credential
검사에서 거부한다. 근거는 [Linux 4.4 AF_UNIX 구현](https://github.com/torvalds/linux/blob/v4.4/net/unix/af_unix.c)과
[Linux peer credential 문서](https://man7.org/linux/man-pages/man7/unix.7.html)다.

`SO_PEERSEC`는 대상 관측과 socket unit 정책으로 확정한 값을 요구한다. service
프로세스 라벨이나 socket 파일 라벨로 listener의 outgoing label을 추정하지 않는다.
실제 credential/name/label과 다른 서비스 socket rename·직접 listener 거부를
대상에서 검증한다. 구현 패널의 실제 probe 결과는 UID/GID 0, PID 1, credential
길이 12, peer label `System::Privileged`(NUL 포함 19바이트), peer address
길이 22(`/run/.consentd.sock`과 NUL 포함)다. 이 대상에는 관측한 label을 사용하고
daemon의 `System` 프로세스 라벨로 대체하지 않는다.
환경 변수 우회는 추가하지 않고 시험 endpoint는 별도로 빌드한다.
이는 system manager·커널·privileged unit 정책을 신뢰하는 조건이며 쓰기 가능한
상위 경로를 통한 서비스 거부까지 방지하지는 못한다. 관측·실행 결과는 이 결정과
구분해서 기록한다.

## D-08: Holder 재시작과 cleanup 권한

데이터 사용 권한은 holder 프로세스 instance에 결합한다. 대체 프로세스가 이전
취득 receipt·활성 artifact·grant·session을 상속하면 안 된다. 정리를 위한 별도
재조정 경로에서는 인증된 동일 stable holder identity가 subject/profile과 저장된
소유 관계를 검증한 뒤 자신의 미완료 cleanup만 조회하고 ACK할 수 있게 한다.
정리를 끝내기 위해 이미 종료한 instance가 계속 살아 있어야 하는 구조를 피한다.

이 권한은 삭제 재조정에만 사용하며 데이터 재활성화나 만료 연장을 허용하지 않는다.
명시적이고 검증된 holder ACK 전까지 pending/failed 상태를 유지한다. 재연결,
프로세스 부재, DB 재구성만으로 물리 삭제를 증명하지 않는다. 재시도는 완료된 삭제를
보존하고 다른 holder나 권한 없는 context를 거부한다. ACK 실패·holder/daemon
재시작·재시도·TTL·파생 데이터 무효화를 기본 session-close 경로와 별도로 시험한다.

새 holder는 artifact ID를 잃었을 수 있으므로 공개 C API로 상한 있는 미완료 정리
조회를 제공한다. 동일한 인증된 정리 전용 repository 경로로 연결하고 ACK는 기존
data-release API로 전달할 수 있다. 새 영속 ACK 테이블은 schema 2로 버전 관리하고
schema 1에서 transaction으로 이행한다. 유효한 persistent grant·definition을
보존하며 임시 상태에는 기존 restart 무효화 정책을 적용한다. 알 수 없는 미래
schema version은 명시적 오류로 차단한다.

## D-09: 데이터 출처와 최종 승인 경계

데이터 사용·파생 연산은 저장된 artifact 출처를 기준으로 검증한다. 여러 부모와
다단계 파생의 모든 원본을 포함하며, 호출자가 보낸 조건 필드로 저장된
grant/definition 연결을 대신하지 않는다. 파생 metadata 생성과 사용 가능한 데이터
metadata 반환 전에 holder instance, subject/profile, session generation과 유효 수명,
artifact 보관 기한, 철회, definition version, 신뢰 설치 generation을 확인한다.
보호된 설치 authority는 SQLite transaction 밖에서 바뀔 수 있으므로 commit 후
성공 게시 직전에도 해당 검증을 반복한다. 다음 주기적 Tick까지 기다려야만 이전
설치를 거부하는 구조는 허용하지 않는다.

접근 소비와 보관은 구분한다. ONCE 사용 횟수를 소비했다고 이미 획득한 artifact가
무효가 되지는 않으며, 접근 grant의 만료를 artifact 보관 정책으로 대신하지 않는다.
파생 artifact의 합성 receipt는 원본 획득 receipt가 아니다. 재검증으로 보관 기한을
늘리거나 새 holder instance에 사용 권한을 이전하지 않는다. 게시 전 검증 실패로
이미 commit된 artifact가 남더라도 동일한 출처 검증에 의해 사용할 수 없어야 한다.

처음 저장소 상태 확인에 성공한 시점부터 결과 게시까지 연산의 DB epoch를 고정한다.
마지막 metadata snapshot 자체가 DB 소실을 발견해 복구할 수 있으므로, 성공 결과에
붙이기 전에 snapshot epoch와 연산 epoch를 비교한다. 불일치는 이전 decision,
receipt, permit을 포함하지 않는 명시적 오류로 반환한다. 과거 성공에 복구된 epoch를
붙여 서버의 후속 epoch 비교를 무력화하면 안 된다. 최종 검증 중 실제 DB unlink,
새 epoch 복구, 같은 repository의 다음 연산에서 새 승인 필요 상태를 시험한다.

UI 응답 transaction에서 새로 승인한 grant를 기록한 뒤 현재 요청의 모든 조건을
재평가한다. 소비하지 않는 QUERY 의미를 사용하고 전체·조건별 최종 결과를 함께
산출한다. prompt 대기 중 기존 허용 조건이 철회·만료·소비될 수 있으므로 현재
판단을 ALLOWED로 덮어쓰지 않는다. 자동으로 팝업을 다시 열지 않고 최종 결과를
한 번 반환한다. 실제 실행 직전 AUTHORIZE의 원자적 AND 평가와 소비는 유지한다.

재평가에서 부족한 조건이 발견되면 요청을 INVALIDATED와 명시적 사유로 완료하고
실제 조건별 결과를 보존한다. 사용자가 새로 명시 승인한 grant는 유지하지만 이
복합 요청은 진행하지 못한다. UI 거부 시 기존 조건은 현재 평가값을 보존하고
이번에 거부한 조건을 DENIED로 기록한다. 재평가 예외는 transaction을 rollback하고
오류를 반환하며 가상의 결정을 만들지 않는다. 이미 완료된 요청의 과거 결과는
이후 철회 때문에 소급 변경하지 않는다.

회귀 시험은 다음 Tick 전 generation 회전, commit과 게시 사이 회전, 혼합·중첩
파생의 일부 원본 무효화, 다른 package artifact 보존, ONCE 소비 후 유효 보관을
포함한다. UI 시험에서는 다른 조건 승인 대기 중 기존 허용 조건을 변경하고
QUERY가 grant를 소비하지 않음을 확인한다. 이 결정은 수정 계약이며 build10
checkpoint에서는 후속 구현·검증 전까지 해당 경로를 미완료로 명시한다.

## D-10: 승인 필드에 결합한 타입 있는 문구

`template_version=1`을 상한이 있는 이름 기반 변수 계약으로 구현한다. 정의마다
최대 8개 `parameter.<name>` schema에 source·type·해당 범위를 명시한다. source는
요구 조건의 `scope`, `purpose`, `recipient`, `operation` 또는 정의의
`retention_ms`로 제한한다. scope는 길이가 제한된 UTF-8 string 또는 정규 십진
integer, 다른 요청 source는 string, retention은 integer다. 독립적인 호출자
표시 인자나 임의 source 표현식은 받지 않는다.
요청에서 가져오는 source 필드는 반드시 명시되어야 한다. 문자열 누락은 오류이며
명시한 빈 문자열만 schema 범위 안에서 허용한다.

데몬은 저장된 요청과 등록된 정의에서 값을 구한다. 이 source는 기존 grant 범위,
policy version, retry/cache key, receipt/artifact 검증에 이미 결합되어 있다.
schema·source·type·범위 변경은 승인 의미 변경이므로 `policy_version`을 올린다.
typed 요청은 해당 조건의 일치하는 policy version을 명시해야 한다. “최근 30일”은
실제 조회 scope를 사용하며 결과 보관 시간을 대신 표시하지 않는다. v1은 단위를
자동 변환하지 않고 retention 값을 ms로 유지한다.

템플릿은 `{name}` 치환만 허용한다. 잘못되거나 중첩된 문법을 거부하고 모든 locale의
title/body 변수 집합이 선언 schema와 일치해야 한다. template·값·field 수·wire·확장
출력의 상한을 할당과 게시 전에 확인한다. 치환 값의 중괄호·퍼센트·markup은 재해석하지
않는 평문이다. 공개 C formatter는 해제 API가 문서화된 소유 문자열을 반환하고 실패 시
출력 포인터를 null로 유지한다. 정수는 우선 정규 십진 표기이며 언어별 복수형·날짜·숫자
서식은 별도 플랫폼 통합 과제로 남긴다.

기존 literal 정의는 호환된다. typed prompt 조회에는 UI가 template version 1 지원을
명시해야 하며 구 UI에는 미확장 문구 대신 미지원 오류를 반환한다. typed 응답은 표시한
locale과 최신 token에 결합하고 재표시마다 token을 갱신한다. 등록된 직접 locale
fallback mapping을 기존 명시적 fallback과 기본 언어보다 먼저 적용한다. mapping
연쇄·순환·미등록 대상은 거부한다. title/body 번역 쌍이 등록된 locale을 alias 출발점으로 중복해
정확한 번역을 가리는 mapping도 거부한다. 번역·fallback mapping 변경은 text revision을
올리고 표시 중 요청을 무효화한다. 의미 변경은 여전히 policy revision이 필요하다.
같은 definition ID의 text revision은 제거·재설치 후에도 감소하지 않는다.
기본 locale·번역·fallback mapping은 policy version과 독립적으로 비교한다.
문구 map이 같으면 revision 유지가 가능하며, 변경되면 policy version 증가 여부와
관계없이 더 큰 text revision이 필요하다.

한영 값, 미지정·누락 변수, 타입·범위 오류, 미지원 version, locale/token 경쟁, 출력
상한, 치환 값의 평문 처리를 검증한다. 30일 승인으로 90일 요청을 허용하지 못하며
cache·retry·receipt·artifact가 같은 범위 결합을 유지함을 확인한다. native unit,
GBS, 실제 C API/emulator 결과는 이 결정과 구분해 기록한다.

## D-11: 공개 C 헤더와 Tizen 오류 값

공개 C API는 `src/consent/inc/consent.h`에 두고 이 디렉터리에 private C++ 헤더를
넣지 않는다. 설치된 `<consent.h>`와 pkg-config 계약은 유지한다. 플랫폼 `<tizen.h>`를
포함하며 `capi-base-common`을 공개 pkg-config 및 build/devel 의존성으로 선언한다.
참조는 `core/api/common` commit `0e569d4`의 `include/tizen.h`, `tizen_error.h`다.

NONE, INVALID_PARAMETER, OUT_OF_MEMORY, PERMISSION_DENIED, BUSY, NOT_FOUND,
TIMEOUT, DISCONNECTED는 대응하는 Tizen 상수를 사용한다. BUSY는 RESOURCE_BUSY,
NOT_FOUND는 NO_SUCH_FILE, TIMEOUT은 CONNECTION_TIME_OUT, DISCONNECTED는
ENDPOINT_NOT_CONNECTED로 기존 errno 값을 유지한다. WOULD_DEADLOCK은
WOULD_CAUSE_DEADLOCK을 사용한다. stale 상태와 인자/frame 상한 등 실제 API가
반환하는 추가 표준 오류에도 공개 이름을 부여한다.

플랫폼에 배정된 `TIZEN_ERROR_CONSENT` base는 없다. 문서화된 모듈 오류 규칙에 따라
PROTOCOL, OUTCOME_UNKNOWN, SESSION_INACTIVE, SESSION_CLOSED, CONFLICT, STORAGE를
이 순서의 `TIZEN_ERROR_MIN_MODULE_ERROR + 0..5`로 정의한다. consent 지역 오류이며
플랫폼 전체의 고유 base 배정을 주장하지 않는다. 복구 의미를 서로 구분하고 다른
Storage API의 `TIZEN_ERROR_STORAGE`를 가져오지 않는다. daemon·시험은 공개 enum을
공유하며 `consent_error_string()`은 지역 오류 설명을 제공한다.

출시 전 v0.1 숫자 계약을 정비하는 변경이다. 소비자·library·daemon을 함께 재빌드하고
갱신하며 기존 `-200x` 소비자와의 혼합 호환을 주장하지 않는다. 과거 검증 출력은 당시
숫자를 보존한다. Parcelable framing은 바꾸지 않는다. C/C++ 소비자·export ABI,
module 범위·표준값 assert, GBS와 실제 IPC 오류 반환을 검증한다.

사용자 지시에 따라 RPM spec의 라이선스 주석은 없애고 패키지 metadata인
`License: Apache-2.0`은 유지한다. CMake 설정 파일의 라이선스 주석도 제외한다.
소스 코드의 라이선스 표기는 계속 필수다. 후속 헤더 구성 요구에 따라 공개 선언을
`inc` 아래에서 기능별로 나누고 `consent.h`는 통합 헤더로 유지한다. private 구현
공통 도구는 공개 디렉터리 밖에 둔다.

## D-12: 비-root service와 설치 authority 분리

사용자는 security 계정의 service와 AMD 방식으로 RPM install 단계에서 만드는
`basic.target.wants/consentd.service` 상대 링크를 요구했다. 선택한 emulator에는
문자 그대로의 `security`는 없고 플랫폼 보안 계정인 UID/GID 402의 `security_fw`가
있다. 사용자에게 명시한 해석에 따라 이 기존 계정을 사용하고 별도 계정은 만들지 않는다.
실제 사용하는 이름을 문서와 검증에 명시한다.

`Requires/After/Sockets=consentd.socket`과 기존 기본 의존성을 유지한다.
`WantedBy=basic.target`을 추가하며 `Before=basic.target`은 넣지 않는다.
부팅 시 service를 시작하고 systemd listener를 상속한다. socket activation과
공존하지만 요청 때만 기동한다는 뜻은 아니다. socket은 root:system_share 0660이며
client는 daemon UID와 독립적으로 PID 1/root 생성자, label, 원래 bind 주소를 검증한다.

daemon 쓰기 상태는 `/opt/var/lib/consentd`에 유지한다. 선택 계정 소유 디렉터리
0700, DB와 definitions registry 0600이다. root 소유 ancestor와 daemon 소유 leaf를
별도로 검증하며 역할·실행파일의 root 보호 검사를 유지한다. 외부 설치 authority는
`/opt/var/lib/consent-authority`의 root:선택 primary group 0750, 파일은
`installations.conf` root:같은 group 0640으로 분리하고 daemon에는 읽기만 허용한다.
writer lock·임시 파일·rename·directory sync도 이 보호 디렉터리에서 수행한다.

root 전용 준비 helper는 기존 상태의 경로·소유자·파일 타입·link count를 검증한 뒤
이전하며 DB inode/incarnation과 durable 결정을 보존하고 state 디렉터리 소유권을
마지막에 바꾼다. daemon·authority writer는 lifecycle lock SH, migration은 EX를
보유한다. legacy daemon은 이전 전에 정지한다. 중단 후 재실행이 가능해야 하며,
불확실한 상태·잘못된 링크는 시작을 막는다. systemd StateDirectory나 RPM의 소유권
처리가 이 검증 전에 재귀 chown을 수행해서는 안 된다. 준비 helper의 권한과 장시간
실행하는 daemon의 제한된 계정·권한을 구분한다.
특권 준비 process는 다른 SMACK label로 시작할 수 있다. 검증된 관리 디렉터리·파일
FD에만 `security.SMACK64=System`을 설정하고 동기화한다. 운영 label 실패는 시작을
막으며, 명시적으로 컴파일한 비-SMACK fixture만 label 처리를 생략할 수 있다.
재시도에서도 parent 디렉터리 sync를 다시 수행하고 적절한 lock을 얻기 전에
lifecycle lock의 metadata를 바꾸지 않는다.

target의 읽기 전용 probe에서 `security_fw`, SMACK System, NoNewPrivileges 조건에
CAP_SYS_PTRACE 하나로 교차 UID 실행파일 조회가 가능했고, 없으면 `/proc/1/exe`가
거절됐다. 추가 근거가 없다면 daemon ambient/bounding capability는 이것으로
제한한다. AMD의 광범위한 capability나 DAC override를 복사하지 않는다.
관측한 보조 group은 역할 신뢰 근거가 아니다. 실행파일/starttime/SMACK 검증,
default-deny와 이전 커널의 PID 경합 한계는 유지한다.

실제 선택 계정, 이전 보존·거절, authority 게시·읽기 권한, 교차 UID 실제 API,
복구와 정상 부팅을 검증한다. 앞선 root daemon 결과로 이 배포 검증을 대신하지 않는다.

## D-13: 이미지 설치 중 offline 등록

이 절은 구현 계약이며 완료 여부는 검증 문서로 구분한다. 설치 담당 system service는
이미지 생성 중 consentd와 socket이 없어도 `consent_register()`를 호출할 수 있어야
한다. RPM 후처리만 조정하거나 정의 등록용 CLI만 제공해서는 요구를 충족하지 못한다.

`consent_client_create_offline_registration(image_root, &client)`를 추가한다.
명시적인 등록 전용 handle에서 `consent_register()`는 socket 연결 없이 동작한다.
성공은 정의가 **STAGED** 상태로 영속 저장됐음을 뜻하며 활성 등록이나 승인이 아니다.
update를 포함한 다른 handle API는 INVALID_OPERATION으로 거부한다. 프로세스·스레드
소유권과 C ABI 검증을 유지한다. 일반 client 생성은 online 계약을 유지하고 권한 오류,
잘못된 peer, timeout, 송신 후 불확실한 결과를 offline 쓰기로 자동 전환하지 않는다.

초기 writer는 root 전용이다. 실제 system service 신원이 아직 없으므로 실행 파일·UID·
SMACK 역할을 추정하지 않는다. 명시적 image root를 보호된 directory FD와 no-follow
탐색으로 처리하고 쓰기는 해당 이미지의 canonical `opt/var` 안으로 제한한다. 환경변수
경로 우회와 `consent.db` 접근은 금지한다. 이미지 내부 caller는 `/`를 선택할 수 있지만
live daemon을 배제하는 lifecycle lock을 그대로 적용한다. 보호 metadata 변경 전에
필요한 잠금을 얻는다.

generation authority 도구에 `--image-root`를 추가한다. 신뢰된 이미지 설치 담당자가
stable ID로 begin/attach하고 실제 설치의 영속 완료를 확인한 뒤 commit한다. 그
generation을 공개 등록 API에 전달한다. 기존 authority에는 expected-generation과
operation receipt 계약을 유지한다. authority 부재만으로 과거 generation을 복원하거나
설치 성공을 추정하지 않는다. fresh image부터 실행 가능하게 하면서 정의 등록은 C API로
수행한다.

root 보호 record에 version, package/app, operation ID, expected generation, 전체
검증된 정의와 canonical fingerprint를 저장한다. record당 64 KiB, 최대 128개, 전체
4 MiB로 제한한다. 같은 ID·같은 내용 재시도는 성공하고 다른 내용은 충돌이다. 파일
fsync·rename·디렉터리 fsync 후 성공하며 불확실한 결과는 같은 ID로 재시도한다.
host 파일은 root:root 0700/0600으로 보호하고 target NSS나 host SMACK를 요구하지
않는다. target 준비 단계에서 검증 후 실제 읽기 group과 System label을 설정한다.
production label 오류를 무시하지 않는다.

직렬 DB executor는 online 인증 caller와 검증된 offline source를 구별하면서 공통 등록
검증·mutation을 재사용한다. 임의 Installer Peer로 우회하지 않는다. 실제 pkgmgr
app/package 관계와 보호된 active generation을 최종 게시 경계까지 재검증한다.
부재·pending·stale·미설치 generation은 비활성으로 두고 관련 없는 유효 package는
계속 처리한다. 형식이나 보호 조건이 잘못된 원본은 오류다. deduplication은 영속
definitions registry에 보존하여 DB 삭제나 반복 부팅으로 과거 정책이 재적용되거나
제거된 정의가 부활하지 않게 한다. 파일 열거 순서가 결과를 결정하지 않도록 호환되는
revision을 결정적으로 처리하고, 상충하는 payload를 모두 stale로 간주해 무시하지 않는다.
소유자·generation이 같고 기존 두 revision이 각각 입력 이상이며 하나 이상 더 높으면
obsolete seed로 처리한다. 같은 revision 축은 의미도 같아야 한다. 동일 generation의
비활성 정의를 seed로 재활성화하지 않는다. obsolete 결과와 fingerprint를 보존하여
같은 재시도는 STALE을 유지하고 내용 변경은 충돌로 거부한다. 재조정은 시작 시 READY
전에 실행하며, 보류된 record의 설치 authority를 수정한 뒤에는 service를 재시작한다.
image record에 승인·실행 receipt·session·artifact·앱 데이터를 저장하지 않는다.

daemon 없는 실제 C API, 재시도·충돌, 경로 격리, 중단된 쓰기, 첫 기동 반영, 여러
app/package, 오래된 generation 거부 및 승인 부활 없는 DB 복구를 검증한다. 이 결과가
외부 production Installer transaction adapter 구현 완료를 뜻하지는 않는다.

## D-14: 개발 문서와 격리된 .NET 승인 UI

다음 증분의 구현 계약이다. 실제 target 근거는 가이드 07에 따로 기록하며 아래 요구를
구현·검증 완료 주장으로 해석하지 않는다.

번호를 부여한 설계 문서는 `docs/design/`, 한영 실무 가이드는 `docs/guides/`로
구분한다. 원 제안과 실제 구현을 구별하고 영문 프로젝트 README를 진입점으로 둔다.
공개 C 헤더는 로컬 Tizen device API 형식에 따라 설명·인자·오류·소유권·callback
context를 명시하고 실행 가능한 C 예제를 제공한다. 배정되지 않은 Tizen platform
API 버전이나 privilege를 임의로 선언하지 않는다.

단일 앱 `org.tizen.consentui`의 .NET 팝업 TPK를 GBS 빌드 안에서 컴파일하고 PoC
구성 요소와 함께 패키징한다. host에서 미리 빌드한 DLL은 GBS .NET 빌드 근거가 아니다.
앱 구조는 `tizen-action-examples`를 참고하며 참고 저장소는 수정하지 않는다.
서명 credential은 소스와 빌드 로그에 넣지 않는다. emulator 서명·설치 성공과 제품
배포 준비 완료를 구별한다.

대화형 PoC와 별도 argo/Capability Manager/Context Engine/holder mock은 전용
library·daemon·socket·state·설치 authority·role 설정을 사용한다. endpoint는
컴파일 시 정한다. production의 기본 거부 설정과 신뢰 검증은 유지한다. PoC는 실제
공개 C API·Parcel 통신·daemon을 거치며 mock 서비스 연동과 실제 제품 연동을 구별한다.

공통 .NET launcher 실행파일만으로 UI 신원을 인정하지 않는다. 설치된 단일 앱의 실제
socket peer SMACK label을 관측한 후 기존 UID·실행파일 신원·프로세스 수명 검사와
결합해 등록한다. package/app 관계와 앱 UID가 설치된 managed code를 교체할 수 없음을
확인한다. UID·launcher가 같은 다른 .NET 패키지의 UI 역할 거부를 검증한다.
preload 프로세스의 공통 label을 등록하지 않는다.

전용 worker 하나가 native C API handle을 소유하고 표시 변경은 UI thread로 전달한다.
`get_prompt`를 제한된 주기로 조회해 종료·무효 상태를 감지하며 argo 전용 결과 조회
권한을 UI에 부여하지 않는다. 종료·오류 시 닫고 로컬 표시 수명을 최대 60초로 제한한다.
언어를 바꾸면 새로 받은 표시 token과 해당 문구를 함께 사용한다.

첫 팝업은 명시적인 이번 한 번 허용과 거부를 제공한다. 모든 요청 조건이 ONCE를
지원하고 모든 페이지를 확인했을 때만 허용 버튼을 활성화한다. 등록 문구와 결합된
값은 생략하거나 markup으로 해석하지 않고 전부 표시한다. 페이지의 본문은 실제
글꼴과 가용 content 폭에서 배치한 크기로 검사하며, 폭 제한 없는 문단의 natural
width를 잘림 검사로 사용하지 않는다. 전체를 표시할 수 없는 내용은 거부한다.
Back·닫기·실패·시간 초과를 승인으로 간주하지
않는다. holder의 정리 증거가 없으면 삭제 완료로 표시하지 않는다. 비대화형 mock 시험과
별도로 실제 팝업 클릭을 검증한다.

## D-15: 재시도 증거를 보존하는 저장소 정리

기존 직렬 DB executor에서 낮은 빈도로 metadata compaction을 수행한다. 한
transaction에서 최대 128개 대상 record를 변경하며 유형별 우선순위를 순환한다.
이는 batch당 변경량 상한이고 query scan 시간이나 보존 행 전체 수의 상한은 아니다.

authorization ID·실행 key·fingerprint·무효 receipt 행은 남긴다. 무효 receipt의
사용 불가능한 payload와 grant 연결은 줄일 수 있지만, 같은 재시도는 STALE,
내용이 달라진 재시도는 CONFLICT를 유지한다. ONCE를 다시 채우지 않는다.
authorization과 artifact provenance 양쪽 모두 참조하지 않는 revoked grant만
제거한다. artifact 행·재귀 provenance·cleanup ACK와 모든 등록/request 중복 방지
기록은 보존한다.

종료된 request의 표시 token·private UI context, CLOSED session의 resume hash만
지운다. 공개 결과나 policy revision은 바뀌지 않아야 한다. 원자적으로 commit하고
기존 storage failure 차단을 적용하며 I/O·full·busy 오류로 파괴적인 reset을 하지
않는다. 요청 경로에서 VACUUM하지 않으며 이 정리로 디스크 사용량이 영구 제한된다고
주장하지 않는다. 동작과 한계는 가이드 09에 기록한다.

신뢰하는 definitions registry가 유실되면 계속 시작 오류로 처리한다. 제거 tombstone을
잃은 뒤 과거 offline seed를 자동 재적용하면 제거한 정의가 부활할 수 있다.
향후 명시적 복구 도구는 기존 DB·registry·spool을 격리하고 갱신된 설치 generation에
결합한 현재의 신뢰 가능한 정의 집합을 받아야 하며 cleanup-unknown 경계를 유지해야
한다. maintenance batch에 그러한 reset을 묵시적으로 허용하지 않는다. 모든 영속
재시도 기록을 잃으면 알 수 없는 과거 operation ID를 알아볼 수도 없으므로 새 승인을
요구하고 결과가 불확실한 외부 실행은 별도로 재조정해야 한다.

## D-16: 기능 선택, 부족한 승인과 대화 내 재사용

TV의 기본 흐름은 기능을 선택하여 사전 승인하고, 현재 작업에 부족한 권한만 모아
확인한 뒤 같은 대화에서 승인된 접근이나 허용된 결과를 재사용하는 것이다. 다음
증분의 구현 계약이며 실행 근거는 가이드 07에 기록한다. 가이드 10은 연동 책임을
설명한다.

기능은 정확한 요구조건 집합을 사용자가 이해하는 단위로 묶은 것이며 포괄적인 새
grant가 아니다. argo의 보호된 버전별 catalog가 선택한 기능 ID를 정의·명시적 정책
버전·제공 앱·작업·범위·목적·수신자·holder로 확장한다. 불변 selection ID·revision과
canonical digest를 요청에 결합한다. 이후 catalog에 추가한 항목은 이전 선택에
합치지 않는다. 같은 요구조건은 중복 제거한 뒤 기존 16조건 상한을 적용한다.
기능 이름은 제공 앱의 신원 증거가 아니다. 등록 정의의 검증된 package/app·설치
세대를 사용하며 사람에게 보여줄 이름은 보호된 catalog에서 가져온다.

기존의 정확한 GrantKey를 유지한다. 기능 ID·catalog revision·선택 metadata·요청
허용 기간이 실제 권한 tuple을 대체해서는 안 된다. A·B를 가진 기능에 C를 추가해도
변경 없는 A·B의 정확한 승인은 재사용할 수 있다. 제공 앱·작업·목적·수신자·holder·
정책·범위가 달라지면 별도의 유효 승인이 필요하다. scope는 계속 정확히 비교하며
임의 문자열 사이의 포함 관계를 추론하지 않는다. 이미 허용된 더 좁은 대안은 명시된
실행 계획과 정확히 일치해야 한다.

추가 계약은 `approval_version=1`, `request_kind=PREAPPROVAL|TASK`, `selection_id`,
`selection_revision`, `selection_digest`, 공통 `grant_mode`와 TIMED일 때만 사용하는
`duration_ms`이다. 각 요구조건은 기능 metadata와 명시적 policy version을 가진다.
admission·canonical request fingerprint·저장 payload·prompt token·재시도까지
검증하여 보존한다. 초기 UI는 ONCE·SESSION·TIMED를 지원하며 새 영구 승인
바로가기는 제공하지 않는다. 한 배치 안에서 다른 기간을 혼합하지 않고 공통 기간을
명확히 표시한다. 설정에는 SESSION과 30분 TIMED preset을 제공하며 ONCE는
명시적인 작업 한정 선택에만 사용한다. 기존 재사용 grant 때문에 한 번의 선택이
지속적인 기능 활성화로 변하지 않게 한다.

PREAPPROVAL은 기존 승인이 선택한 기간을 충족하는지 검사한다. ONCE 하나가
SESSION 선택을 충족하지 않는다. TIMED 보장 시점은 최초 admission 시각에 기간을
더해 고정하며 refresh나 retry마다 뒤로 밀지 않는다. TASK는 정확한 권한이 현재
유효한지 검사하고 부족한 항목에만 선택 기간의 승인을 발급한다. 따라서 기존 TIMED
승인으로 작업이 가능하면 SESSION 추가 승인 때문에 다시 묻지 않는다. 응답의
mode·기간은 표시한 요청과 일치해야 하며 새 TIMED 만료는 한 응답 시각을 기준으로
계산한다. coverage 목표는 retry 중복 확인 뒤 서버 내부 값으로 저장하며 재계산한
시각을 caller fingerprint에 넣지 않는다. admission·표시 projection·응답·최종 AND에
모두 같은 kind별 coverage 판정을 적용한다. TIMED의 기존 범위 100~3,600,000ms를 유지한다.

SESSION에는 실제 살아 있는 대화와 일치하는 generation이 필요하다. session 생성
없이 SESSION 문구를 표시하는 것만으로 권한이 생기지 않는다. 기존 daemon
heartbeat를 인증된 session controller용 C API로 공개한다. 기존 idle·최대 수명
안에서 lease를 갱신하며 닫힌 session은 다시 열지 않는다. 팝업에는 session 역할을
부여하지 않는다. TIMED 승인은 현재 대화 밖에서도 유효할 수 있으므로 대화 동안만
허용한다고 표시하지 않는다. 접근 승인 기간은 등록된 결과 보유 기간 및
artifact·session·holder 검사와 별개이다.

원래의 전체 AND 요청을 보존한다. 새 표시 계약을 지원하는 UI에는 현재 부족한
조건만 압축하여 제공하고 token에 결합된 snapshot에 원본 index를 보존한다.
typed argument도 같은 행에 맞게 재배치한다. 그 token으로 실제 표시한 조건에만
승인을 발급하고 다른 요청으로 이미 충족된 조건을 다시 채우지 않는다. 대기 중
숨겨진 조건이 만료·철회·소비되어도 묵시적으로 승인하지 않는다. 최종 결정 전에
원래 전체 집합을 다시 검사한다. A가 더 이상 충족되지 않으면 명시 승인한 B는
남을 수 있지만 전체 작업 요청은 INVALIDATED가 되며, 기존의 비소비 최종 판정을
유지한다. 부족한 조건이 없으면 승인 팝업 없이 끝낸다. 새 표시 capability가 없는
이전 UI는 이 계약에 응답할 수 없다.

새 계약의 요청은 항상 daemon에 도달한다. `approval_version`이 있는 요청은
client cache 조회·저장에서 제외하고 `cacheable=0`을 반환한다. 잘못된 version도
cache hit에 가려지면 안 된다. 보조 cache 결과만으로 새 선택의 기간·admission
snapshot·재시도 신원을 확정할 수 없다. 기존 legacy cache 동작은 구분한다.
송신 전에 server hello의 approval-v1 지원을 확인하여 알 수 없는 필드를 무시하는
구 daemon으로 조용히 downgrade되지 않게 한다. 알 수 없는 version 및 opt-in version
없이 승인 전용 필드를 넣은 요청은 거부한다. 실제 보호 작업에는 계속 authoritative
AUTHORIZE가 필요하다.

설정은 선택을 전달하는 client이며 승인 요청 역할이 아니다. 격리 PoC에서는 전용
유계 bridge가 기능 ID·catalog revision·허용된 공통 기간 preset을 별도 인증된
argo mock endpoint에 전달한다. argo만 보호 catalog를 읽고 selection revision을
소유하며 공개 consent C API로 요청하여 request ID를 반환한다. 별도 PID1 systemd
listener `consent-feature-poc.socket`과 `consent-feature-poc.service`를 사용하며
경로는 `/opt/var/lib/consent-feature-runtime/argo.sock`이다. UI는 kernel 생성자
UID·SMACK label·원래 bind address와 보호
path/inode를 확인하고, server는 상속 listener와 실제 UI process/package 신원을
검증한다. 실제 probe에서 앱 UID로 root peer의 proc executable·stat·status를 읽을
수 없었으므로 UI capability를 추가하거나 양방향 실행파일 검증을 주장하지 않는다.
caller가 주장한 role과 임의 requirement payload는 받지 않는다. 운영 consent
endpoint와 통신을 분리하며 운영 role은 등록하지 않는다. 선택 명령에는 안정된 command ID와 예상 selection
revision을 넣어 compare-and-swap으로 적용한다. 늦게 도착한 저장 명령이 해제한
기능을 다시 켜면 안 된다. 변경 명령은 coordinator incarnation에도 결합하여 재시작으로
메모리 명령 기록·revision counter가 초기화된 뒤 과거 명령이 새 작업으로 재실행되지
않게 한다. 결과가 불확실한 submission과 ID를 보존해 같은 명령으로 재시도하며 답을
잃었다고 자동으로 새 operation을 만들지 않는다. 작업 한정 선택은 task 신원과 제한된 수명에 결합한다.
한 TPK의 두 화면은 같은 앱 신원이므로 app-control 인자와 재실행을 사용자 선택·
승인으로 간주하지 않는다. PID1 listener credential은 보호 endpoint를 인증하며
현재 argo process 자체가 PID1이라는 뜻이 아니다.

비공개 argo-worker 연결은 대상 kernel의 socketpair probe가 System에서도 빈
SO_PEERSEC label을 반환한 제약을 반영한다. 정확한 label 검사를 유지하기 위해
보호된 feature runtime 디렉터리에 root 소유 0600 임시 Unix stream listener를
만들고, 연결 양단의 신원을 생성한 부모 PID·UID/GID 0·System으로 검증한다. 이후
pathname과 listener를 제거하고 연결된 FD만 child에 전달한다. 연결 준비를 유계로
처리하고 실패 시 정리하며 worker의 부모 실행파일·starttime·생존 검사를 유지한다.
UI bridge나 운영 endpoint의 인증 정책은 바꾸지 않는다.

정확한 재사용 grant가 있더라도 선택하지 않은 기능은 비활성 상태를 유지한다.
선택 해제·기간 만료·catalog 변경은 과거 선택으로 대기 중인 작업을 무효화한다.
각 bridge 호출의 실제 UI process는 인증하되 확정한 선택은 안정된 앱·subject 신원에
소유시켜 설정 process 종료 뒤에도 유지한다. PoC coordinator 자체를 재시작하면
선택을 초기화할 수 있으며 그 재시작을 넘는 영속 설정 보존을 약속하지 않는다.
각 provider 동작 전에 argo가 현재 불변 선택과 실행 계획을 확인하고 enforcement
service는 실제 대상·효과에 AUTHORIZE를 수행한다. 선택 변경·catalog 교체·작업
시작을 argo actor에서 직렬화한다. 비동기 AUTHORIZE 완료는 그 actor로 돌려보내고
같은 실행 구간에서 현재 snapshot을 재확인한 직후 작업을 시작한다. 검사 후 잠금을
풀고 다른 queue로 넘기기만 하는 방식은 충분하지 않다. 선택 해제로 이미 시작한
작업을 되돌린다고 약속하지 않는다. 작업에 필요한 비활성 기능은
그 작업에 한해 명시적으로 선택할 수 있으며 설정에서 영구 활성화하지 않는다.
기능 선택 해제는 해당 기능의 실행을 막는 것이고, 다른 선택 기능과 공유하는 exact
grant 전체를 revoke한다는 뜻은 아니다. 허용된 대안은 현재 선택과 정확한 승인에
명시적으로 포함되어야 하며 거부된 원래 작업은 실행하지 않는다.

GBS로 빌드한 TPK·실제 공개 C API·격리 mock service로 전체 흐름을 검증한다.
필수 항목은 부족분만 표시, 불변 선택 뒤 catalog 항목 추가, 제공 앱·목적·수신자·
범위 변경, 선택 해제, 기간 변조, 대기 중 철회·만료·ONCE 소비, ONCE 재충전 금지,
재시도 충돌, 같은 대화 재사용과 종료, bridge 신원 거부, 거부된 원래 작업을 수행하지
않는 허용 대안 실행이다. 운영 제품 연동과 emulator PoC 근거를 구분한다.

## 완료 전에 확인할 초기 검토 사항

- 이전 handle을 닫은 뒤 관련 DB와 journal 파일들을 함께 격리·정리해야 한다.
  DB는 없고 rollback journal만 남은 시작 상황도 포함한다. 새 generation에 이전
  DB의 journal을 재적용하지 않는다.
- 데몬 중지 중 유효한 과거 SQLite DB로 교체하는 시험이 필요하다. 일반 무결성
  검사만으로 결정의 최신성을 확인할 수 없다.
- registry 디렉터리 sync 실패 후에는 내구성이 다시 확보될 때까지 권한 차단을
  유지한다. rename한 snapshot을 읽었다는 사실이 내구성을 증명하지는 않는다.
- 손상 분류는 감지 시점부터 보존한다. 나중의 DB 오류 상태는 rollback/finalize로
  바뀔 수 있고, 무결성 검사는 SQL 실행 오류 없이 손상 설명을 반환할 수 있다.
- 패키지 제거에는 app ID 없이 안정된 Installer 작업 ID와 예상 설치 generation을
  결합해야 한다. 같은 이름으로 재설치한 후 도착한 과거 제거 재시도가 새 설치를
  지우면 안 된다. 패키지명 일치를 재시도 신원으로 가정하지 않고 신규 API에 반영한다.
- tizen-watcher metadata parser plugin은 Installer의 설치·업데이트·제거·rollback
  hook 사례다. 참고 저장소를 수정하지 않는 consent 전용 plugin이 통합 경로가 될 수
  있지만 callback 자체가 영속 설치 generation을 제공하지는 않는다.

## 근거와 미확정 제품 설정

- AMD `29ed218b`: `src/modules/component-manager/src/worker.{h,cc}`의 작업 소유권과
  `Quit()`/`Join()`, `src/modules/cynara-core/cynara_manager.cc`의 소켓 신원·플랫폼
  권한 검사 어댑터를 참고한다.
- AUL `ac581e7`: `src/aul/launch_with_result.cc`는 mutex 안에서 콜백 항목의 소유권을
  이전하고 잠금 해제 후 콜백을 호출한다.
- AUL `ac581e7`: `src/aul/socket/packet.hh`와 `socket/client.cc`는 기존 Parcelable
  작성·송신 패턴을 보여준다. Bundle `5ef6073`(2026-01-14)에 제한된 codec에 필요한
  정수·byte order·reader 위치 API가 있다. 새로운 capacity 생성자·UInt8 메서드는
  대상 확인 없이 사용하지 않는다.
- 확인 가능한 tizen-watcher 이력은 2026-02-02의 `a1de9f6`부터다. 요청한 레이아웃과
  현재 패키징의 근거로 사용하고 1월까지의 수작업 스타일은 AMD/AUL을 참고한다.
- 대상 architecture·의존성 버전, 역할 신원, 설치 세대 확인 수단, 자원·시간 상한,
  서비스 계정·라벨, 외부 holder/UI 연동은 실제 구현 근거로 확정해야 한다.
- GBS RPM 생성, emulator 설치·API 실행, 장애 주입, 재부팅은 각각 다른 검증이다.
  process kill이나 정상 재부팅으로 강제 전원 차단 내구성을 증명하지 않는다.
  CEP 수용 기준을 코드 검토만으로 통과 처리하지 않는다.
