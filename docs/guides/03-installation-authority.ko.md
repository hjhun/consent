# 가이드 03: Installer generation authority

운영 adapter는 두 가지 사실을 각각 검증합니다. pkgmgr-info에서 앱과 패키지
관계가 확인되어야 하고, 보호된 installation authority에서 앱과 패키지가 예상
설치 generation으로 active여야 합니다. 패키지명, 버전, 초 단위 설치 시각, 서명
신원이나 inode만으로 서로 다른 설치 인스턴스가 같다고 판단하지 않습니다.

파일은 `/opt/var/lib/consent-authority/installations.conf`입니다. 디렉터리는
root 소유0750, 파일은 root 소유0640이며 데몬의 primary group에는 읽기만
허용합니다. 데몬은 root가 보호하는 상위 경로에서 authority를 교체할 수 없습니다.
확인한 Tizen emulator는 `/var -> opt/var`이므로 symlink를 거부하는
보호 경로 열기에 맞춰 canonical `/opt/var` 경로를 사용합니다.
`consent-installation-authority`는 root 전용 provisioning 도구이며 클라이언트
역할 인증을 우회하는 API가 아닙니다.

`consent-storage-prepare`가 데몬 시작 전에 이 디렉터리를 준비합니다. 업그레이드에서는
기존 authority를 데몬 상태 디렉터리 밖으로 옮긴 뒤 그 상태 디렉터리의 소유권을
바꾸며 generation 내용을 보존합니다. 읽을 수 없거나 충돌하는 authority를 빈
자료로 대체하지 않습니다. 평상시 데몬과 Installer는 `lifecycle.lock`을 공유
잠금으로 잡고 준비 도구는 독점 잠금으로 잡습니다. Installer는 준비된 lifecycle
파일을 읽고 잠그기만 하며 소유권이나 mode를 바꾸지 않습니다. 이행 전 socket/service를
중지하고 기존 Installer 작업을 종료해야 합니다.

## Provisioning 실행 문맥

writer는 root로, 명시적인 `System::Privileged` SMACK 프로세스 문맥에서 실행합니다.
root UID만으로는 충분하지 않습니다. emulator의 `System` 문맥 root shell에서는
필요한 파일 label을 설정할 수 없었습니다. 운영 준비 도구는 service의 root
`ExecStartPre=+` 실행으로 성공했습니다. 격리 writer의 `begin`도 다음 명령으로
실행했을 때 성공하고 generation UUID를 반환했습니다.

```sh
systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/libexec/consent/tests/consent-installation-authority-isolated \
  begin context.probe context-probe-16 absent
```

운영 provisioning에는 같은 명시적 문맥에서 운영 실행파일을 사용합니다.
아래 수명 예제는 root가 호출하는 다음 shell 함수를 사용합니다.

```sh
consent_authority() {
  systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
    /usr/sbin/consent-installation-authority "$@"
}
```

이는 명시적인 Installer 특권 작업입니다. 클라이언트 역할을 부여하거나 데몬의
패키지·generation 검증을 완화하지 않습니다. 격리 명령의 성공은 실행 문맥을
검증한 것이며 운영 Installer hook 연동까지 검증한 것은 아닙니다.

## 수명 계약

Installer가 안정된 transaction ID를 관리하고 패키지별 수명 변경을 직렬화합니다.
새 설치 인스턴스마다 새 generation을 발급합니다. 각 하위 작업에는 서로 다른
안정된 ID를 사용하고, 결과가 불확실하면 같은 ID와 입력으로 재시도합니다.
다음 명령은 해당 설치 패키지에 대해서만 실행합니다.

```sh
# 최초 설치에는 absent, 교체 설치에는 이전 generation을 전달합니다.
generation=$(consent_authority begin org.example.package install-01 absent)
consent_authority attach org.example.package org.example.app attach-01 "$generation"
consent_authority attach org.example.package org.example.app2 attach-02 "$generation"
# 실제 플랫폼 설치 transaction이 내구성 있게 완료된 다음 실행합니다.
consent_authority commit org.example.package commit-01 "$generation"
```

`begin`은 패키지를 pending으로 전환하고 과거 앱 항목을 차단합니다. `attach`는
그 generation에 앱을 추가하고, `commit`은 일치하는 앱만 활성화합니다. pending
설치로 승인 정의를 활성화할 수 없습니다. 플랫폼과 authority commit 이후 인증된
Installer가 `consent_register(client, package, app, params)`에
`expected_generation`, 안정된 `operation_id`, 정책과 다국어 문구를 전달합니다.

삭제 시에는 authority tombstone으로 해당 설치를 먼저 사용할 수 없게 합니다.

```sh
consent_authority remove org.example.package remove-01 "$generation"
```

인증된 Installer는 같은 예상 generation과 안정된 API 작업 ID로
`consent_unregister(client, package, params)`도 호출합니다. app ID는 필요하지
않습니다. 데몬은 tombstone 상태에서도 현재 패키지 generation을 검증하고 영향을
받는 등록 전체를 확인합니다. 늦게 도착한 과거 삭제가 새 설치를 지우지 못합니다.
등록·삭제 재시도 fingerprint에 요청 내용을 결합하므로 ID를 유지한 입력 변경은
충돌로 반환합니다.

rollback 시 실패 generation은 pending 또는 removed로 남깁니다. 패키지를
복구하려면 신뢰할 수 있는 설치 검증 후 새 begin/attach/commit과 정의 재등록을
수행합니다. 과거 사용자 승인은 복원하지 않습니다. 도구가 플랫폼 transaction의
성공 여부를 자체 추정하지는 않습니다.

## 내구성 있는 파일 형식

```ini
[authority]
schema=1

[package org.example.package]
generation=SERVER-GENERATED-UUID
state=active

[org.example.app]
package=org.example.package
generation=SERVER-GENERATED-UUID
state=active

[operation install-01]
fingerprint=begin|org.example.package||absent
generation=SERVER-GENERATED-UUID
```

식별자는 길이가 제한되며 ASCII 영문자·숫자·점·밑줄·하이픈만 허용하므로
fingerprint 구분자가 모호하지 않습니다. 작업 결과와 tombstone은 상한 안에서
보관하며 자동 정리는 아직 제공하지 않습니다. 상한에 도달하면 재시도 보호를
지우는 대신 오류를 반환합니다. nonblocking advisory lock을 잡고 같은 디렉터리의
배타적 임시 파일에 작성한 뒤 file fsync, rename, directory fsync 순으로 저장합니다.
게시한 파일은 root 소유, 데몬 primary group, mode0640과 `System` SMACK label을
유지합니다. 권한이나 label 설정이 실패하면 게시하지 않습니다.
실패는 미완료 또는 불확실한 결과입니다. 설치 차단을 유지하고 같은 작업 ID로
재시도·재조정한 뒤 상위 Installer에 성공을 보고해야 합니다.

이 파일은 정의 원본 `definitions.registry`, 정책 projection·사용자 승인·임시
메타데이터를 담는 `consent.db`와 별개입니다. 세 저장소가 하나의 원자적 분산
transaction은 아닙니다. 데몬은 재반영·접근 검사 및 주기적인 확인으로 authority
변경의 영향을 무효화합니다. authority에는 grant·사용자 결정·세션·사용 기록을
넣지 않습니다.

## 현재 연동 경계

이 저장소는 adapter, provisioning 실행파일, 격리 시험 authority를 제공합니다.
운영 pkgmgr-info 검증은 유지합니다. 실제 Installer hook이나 운영 역할이 이미
설정됐다고 가정하지 않습니다. tizen-watcher metadata plugin은 install/upgrade/
uninstall/rollback 확장점 후보이지만 callback만으로 내구성 있는 설치 generation이나
callback 사이의 안정된 transaction ID가 제공되는 것은 아닙니다. 실제 연동에는
Installer commit/rollback 순서 검증과 해당 실행 프로세스 신원 등록이 필요합니다.

별도 `consent-installation-authority-isolated` binary는
`/opt/var/lib/consent-test-authority/installations.conf`를 사용하며, 격리 데몬의
상태 디렉터리는 `/opt/var/lib/consent-test`입니다. 시험 데몬은 pkgmgr-info 대신 명시적인
시험 inventory를 조회합니다. 운영 신원 정책은 바뀌지 않으며 시험 generation은
운영 adapter의 신뢰 원본이 아닙니다.
