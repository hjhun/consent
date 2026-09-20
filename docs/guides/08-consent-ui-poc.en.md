# 08. Consent UI and participant PoC

This development-only integration uses a .NET NUI application and five native
mock participants against the public C API and an isolated consent daemon.
It does not provision production roles, replace the platform approval UI, or
integrate a production Installer lifecycle hook.

```mermaid
flowchart LR
  I[Installer mock] --> L[libconsent-poc.so.0]
  A[Argo mock] --> L
  C[CM / CE mocks] --> L
  H[Holder mock] --> L
  U[ConsentUI .NET popup] --> W[Dedicated interop worker]
  W --> L
  L --> S[PID1-owned PoC socket]
  S --> D[consentd-poc]
  D --> R[PoC SQLite / definitions registry]
  D --> P[Actual pkgmgr + protected generation authority]
```

## Build and installation

The RPM build enables `CONSENT_BUILD_POC`. The separate `consent-poc` package
contains the fixed-endpoint `libconsent-poc.so.0`, daemon, preparation and
installation-authority tools, native participant mocks, and two signed TPKs.
The PoC library retains production socket authentication; it does **not** use
`CONSENT_TESTING` or a runtime endpoint override.

Inside GBS, `dotnet-build-tools` 8.0.421 and the offline
`csapi-tizenfx-nuget` 14.0.0.19364 packages compile the application sources for
`net8.0-tizen10.1`. The SDK development signer creates the emulator TPK.
No host-precompiled application DLL is an input. The positive build also runs
the managed lifecycle/model regressions with that SDK. Signing is for this
emulator PoC, not distribution or production signing.

`consent-poc-register.service` registers only `org.tizen.consentui` globally.
An image/chroot RPM install skips live service actions; the installed boot
symlink runs registration when package-manager is available. This does not
create consent definitions, approval grants, roles or installation generations.
The negative package `org.tizen.consentui.negative` is installed explicitly for
identity testing. RPM removal leaves registered TPKs and their data in place;
after stopping the PoC, an operator may explicitly remove them with:

```sh
pkgcmd -u -n org.tizen.consentui --global
pkgcmd -u -n org.tizen.consentui.negative --global
```

## Explicit identity setup

The endpoint is `/opt/var/lib/consent-poc-runtime/consent.sock`, listened on by
PID1 through `consentd-poc.socket`. Its parent is protected root-owned 0755;
the socket is root:users 0660. Explicit stopped-service preparation validates
that leaf and sets only its SMACK label to `_`, then reads it back. The
ancestor labels and production policy are unchanged. The strict client verifies PID1/UID0, the exact
kernel bind address and the listener's `System::Privileged` security label.
This listener label is distinct from the daemon's `System` label and the UI
application's package label.

The package ships a deny-all role configuration. On the selected development
emulator, `scripts/emulator-poc-setup.sh probe-start` temporarily replaces only
the PoC service command with a bounded activated-socket peer diagnostic. Launch
the UI as `owner:users` in an explicit privileged test-launch context
(`SmackProcessLabel=System::Privileged`) using
`consent-poc-launch org.tizen.consentui REQUEST_ID en-US`; the
probe records kernel SO_PEERCRED/SO_PEERSEC and sends no consent response.
`probe-stop` restores the daemon unit and saves observations. A client failure
against this diagnostic is expected and is not an authorization test.

Only after observing the actual UI socket label does `configure` provision
its explicit PoC rule: the observed owner UID, protected exact
`/usr/bin/dotnet-hydra-loader`, and exact
`User::Pkg::org.tizen.consentui` label, with subject `owner` and profile `default`.
The installed single-app package relation and its DLL/ancestors must remain
unwritable by that app UID. The `tizenglobalapp` package-manager account is a
trusted installation owner; it is not the UI caller. A common loader/UID or
preload label alone never grants UI authority.

Configuration prepares independent state at `/opt/var/lib/consent-poc-state`
and root-owned authority at `/opt/var/lib/consent-poc-authority`, then performs
an explicit stable installation transaction. Definitions are subsequently
registered through `consent-mock-installer` using the actual installed package.
No production state or role file is changed. Reinstalling the application is a
new installation and requires an explicit new generation transaction using
`configure INSTALL_OPERATION EXPECTED_GENERATION`. Keep those two inputs stable
for retries; the default transaction is for initial setup only; repeating
the same setup transaction is not evidence of a new installation.

## Popup and participant contract

The popup accepts only the initial VIEW request ID and requested language.
Displayed title/body and all authorization bindings come from `get_prompt`.
An active popup ignores subsequent app-control payloads, including unrelated
launches. Only its language button changes language, fetching a fresh complete
snapshot/token before display. Technical IDs/revisions remain internal.

A dedicated worker owns the C client and its private GLib context. Every page
must be reviewed before Allow once is enabled; every condition must support
ONCE. Prompt refresh is bounded to 500 ms, preserving page review only when the
bound content is unchanged. Token/policy/session changes cannot be replaced by
launch data. Deny, Back, close and the 60-second local timeout never approve.
Errors dismiss the popup; a failed cleanup response is not reported as success.
There is no automatic approval path in the UI.

The separate argo, CM, CE, holder and installer executables have distinct
executable identities. `serve` mode keeps async/session/holder ownership alive.
Bounded INI examples are installed under `/usr/share/consent/poc/fixtures`.
Argo emits the daemon request ID for UI launch, CM checks without consumption,
CE consumes ONCE with AUTHORIZE, and the holder registers derived metadata and
ACKs cleanup only after clearing its owned mock buffer. No real user data is
used. The negative .NET app records its status only; actual authorization
rejection also requires the matching PID in the daemon role-rejection log.

## Repeatable emulator procedure

Run these commands only against the selected development emulator, from the
repository version matching the frozen RPM artifacts. Both host and target
need Python 3: the host driver also runs small target-side Python checks.
`consent-tests` requires target `python3-base`; `consent-poc` alone does not.
The UI/registration service itself does not require Python. Resolve missing
runtime or test dependencies with matching Tizen repository RPMs in the normal
RPM transaction; dependency-check bypasses are not part of this procedure.

The following host example selects verified build26. Confirm the matching
snapshot and artifact hashes in [Guide 07](07-verification.en.md) before installation. Build24 exposed a text-fit failure and build25 exposed clipped popup placement;
neither is a successful complete visual baseline.

```sh
# Host: select the serial shown by sdb, then select the reviewed build.
sdb devices
CONSENT_SERIAL='<selected-emulator-serial>'
CONSENT_BUILD=26
CONSENT_RPMS="/var/tmp/consent-artifacts/gbs-build-$CONSENT_BUILD"
CONSENT_TARGET_RPMS="/tmp/consent-rpm$CONSENT_BUILD"
python3 --version
sdb -s "$CONSENT_SERIAL" root on
sdb -s "$CONSENT_SERIAL" shell "mkdir -p '$CONSENT_TARGET_RPMS'"
for package in consent consentd consent-tests consent-poc; do
  sdb -s "$CONSENT_SERIAL" push \
    "$CONSENT_RPMS/$package-0.1.0-1.x86_64.rpm" "$CONSENT_TARGET_RPMS/"
done
sdb -s "$CONSENT_SERIAL" push scripts/emulator-poc-setup.sh /tmp/emulator-poc-setup.sh
sdb -s "$CONSENT_SERIAL" shell
```

In that target root shell, first restrict and verify the pushed script.
SDB push may change its mode; the source mode is not proof of target mode.
Require a regular root-owned file with one link and no symlink before running
it in the privileged context. Then install the four matching packages together.
Add any missing matching dependency RPMs to the same staging directory first.
The target directory below must match `CONSENT_TARGET_RPMS` above.

```sh
# Target root shell; initial install or an ordinary version upgrade.
set -eu
[ -f /tmp/emulator-poc-setup.sh ] && [ ! -L /tmp/emulator-poc-setup.sh ]
[ "$(stat -c '%u:%g:%h' /tmp/emulator-poc-setup.sh)" = 0:0:1 ]
chmod 0600 /tmp/emulator-poc-setup.sh
[ "$(stat -c '%u:%g:%a:%h' /tmp/emulator-poc-setup.sh)" = 0:0:600:1 ]
systemd-run --quiet --wait --pipe \
  -p User=root -p Group=root -p SmackProcessLabel=System::Privileged \
  /bin/sh -c 'exec rpm -Uvh /tmp/consent-rpm26/*.rpm'

systemctl start consent-poc-register.service
systemctl show consent-poc-register.service \
  -p Result -p ExecMainCode -p ExecMainStatus
journalctl -u consent-poc-register.service -n 40 --no-pager
pkginfo --app org.tizen.consentui
pkginfo --pkg org.tizen.consentui
pkginfo --list org.tizen.consentui
python3 --version
```

Expect the registration command to succeed, `Result=success` and
`ExecMainStatus=0` with an exited process. An inactive successful oneshot is
normal. Check the exact package/app relation, version `0.1.0`, single app, and
`Exec: /opt/usr/globalapps/org.tizen.consentui/bin/ConsentUI.dll`.
For a deliberate development rebuild with the **same NEVRA**, use
`rpm -Uvh --replacepkgs --replacefiles /tmp/consent-rpm26/*.rpm` in the same
privileged `systemd-run` context instead of the initial-install RPM command.
These replacement flags are for that reviewed development rebuild only;
dependency verification remains enabled.

Use `/bin/sh` explicitly for the pushed script because target `/tmp` may be
mounted `noexec`. Define this function in the target root shell and run the
observation/configuration sequence:

```sh
poc_setup() {
  systemd-run --quiet --wait --pipe \
    -p User=root -p Group=root -p SmackProcessLabel=System::Privileged \
    /bin/sh /tmp/emulator-poc-setup.sh "$@"
}

poc_setup probe-start
systemd-run --quiet --wait --pipe \
  -p User=owner -p Group=users -p SmackProcessLabel=System::Privileged \
  /usr/libexec/consent/poc/consent-poc-launch \
  org.tizen.consentui dummy-probe en-US
poc_setup probe-stop
cat /opt/var/lib/consent-poc-control/peer-observation.log

# Fresh installation only: stable transaction with no previous generation.
poc_setup configure poc-ui-install-1 absent
cat /opt/var/lib/consent-poc-control/generation
```

The diagnostic deliberately cannot serve a prompt; the UI may fail/close.
Do not interpret that as a consent denial. After a real reinstall, use a new
installation operation ID and the expected current generation, preserving
both values for retries. For example, read the current generation before the
new transaction, record it with the chosen operation ID, then run:

```sh
CONSENT_EXPECTED_GENERATION=$(cat /opt/var/lib/consent-poc-control/generation)
poc_setup configure poc-ui-install-2 "$CONSENT_EXPECTED_GENERATION"
```

Return to a **host** terminal in the matching repository. Start the approval
case with a fresh artifact directory; do not change its serial or directory
between commands:

```sh
CONSENT_ALLOW_DIR=$(mktemp -d /var/tmp/consent-poc-allow.XXXXXX)
python3 scripts/emulator-poc-mocks.py \
  --serial "$CONSENT_SERIAL" --artifact-dir "$CONSENT_ALLOW_DIR" \
  start-request --expect ALLOWED --locale ko-KR
```

On the actual emulator UI, review every page and select **Allow once** within
the 60-second prompt bound. `start-request` only launches the UI; it never
sends an approval. After the UI response, continue on the host:

```sh
python3 scripts/emulator-poc-mocks.py \
  --serial "$CONSENT_SERIAL" --artifact-dir "$CONSENT_ALLOW_DIR" finish-allow
python3 scripts/emulator-poc-mocks.py \
  --serial "$CONSENT_SERIAL" --artifact-dir "$CONSENT_ALLOW_DIR" stop
```

`finish-allow` checks ALLOWED, non-consuming QUERY, ONCE acquisition/retry/
exhaustion, derived holder metadata and cleanup. A failed or timed-out UI must
not be substituted with an automatic response. Preserve failure evidence,
stop the actors, and use a fresh artifact directory for a new attempt.

For denial, use a different fresh directory and select **Deny** in the real
UI within the same bound:

```sh
CONSENT_DENY_DIR=$(mktemp -d /var/tmp/consent-poc-deny.XXXXXX)
python3 scripts/emulator-poc-mocks.py \
  --serial "$CONSENT_SERIAL" --artifact-dir "$CONSENT_DENY_DIR" \
  start-request --expect DENIED --locale en-US
# Select Deny in the emulator before running the next command.
python3 scripts/emulator-poc-mocks.py \
  --serial "$CONSENT_SERIAL" --artifact-dir "$CONSENT_DENY_DIR" stop
```

There is no `finish-allow` step for denial: `stop` verifies the DENIED callback
and the subsequent QUERY before shutting down the actors. Driver `stop`
preserves private logs and stops only its argo/holder processes. Finally stop
the PoC daemon/socket from the target root shell:

```sh
poc_setup stop
```

This retains PoC state, roles and installed UI packages for inspection. It does
not reset a production database or remove package data. Use the explicit TPK
removal commands above only when that separate cleanup is intended.

## Verification status

Frozen26 passed GBS compilation/managed tests, actual TPK installation and
paired socket identity checks. Actual EN/KO page review, language reset, Allow
and Deny, long unbroken text, cancellation, timeout and Back were exercised.
The Allow flow completed QUERY/ONCE authorization/retry and holder cleanup.
See [Guide 07](07-verification.en.md) for hashes, screenshots, exact evidence
and the separate24/25 failures. After verification the PoC daemon/socket and
actors are stopped; explicit PoC state/roles and installed TPKs remain for review.
This is a Common Emulator PoC, not product role/UI/Installer or physical TV acceptance.
