# 구현 결정 기록

날짜: 2026-09-20. 이 문서는 PO·아키텍트의 구현 지침이며 코드 완성이나 검증
완료를 뜻하지 않는다. `CEP_Consent_Framework.md`는 설계 제안으로 유지한다.
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
[Installer 인계 문서](installer-integration.ko.md)에 조사한 callback의 한계와
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
