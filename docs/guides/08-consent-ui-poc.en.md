# Guide 08: Consent UI example

[한국어](08-consent-ui-poc.ko.md)

The .NET NUI app displays a compact approval popup and reviews every page
before allowing. An explicit approval-v2 request can offer an initially
unchecked always-allow choice; the complete registered policy must permit it.
See the [native layout and period
contract](#ui09-compact-native-layout-and-explicit-period-choice).

Use [Guide 17](17-native-ui-smoke.en.md) for the current one-command emulator
runner. The manual procedures below identify their older build snapshots.
The example uses the public C API and isolated mock participants. Its native
library is fixed to `libconsent-poc.so.0`. Product UI role, endpoint/package
provisioning and argo launch mapping still need integration. Each plugin owns
Installer hooks under authenticated publication and package/app generations.

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

## 1. Run the current native popup example

For a repeatable run with automatic restoration, follow the complete host
command in [Guide 17](17-native-ui-smoke.en.md#prepare-and-run). It uses
matching
RPM/TPK artifacts, an installed but stopped Aurum bootstrap and the selected
emulator. On screen, review all pages, keep the checkbox initially off, and
observe the actual CE/CM effect only after authoritative authorization.

Expected: a compact 580×600 maximum popup, separate access/retention disclosure,
and an optional always-allow choice only for an explicitly eligible request.
Deny, close or expiry never approves. Guide 17 checks worker receipts and
counts;
visual appearance alone is not an authorization result.

## 2. Build the application and inspect its package


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

## UI09: compact native layout and explicit period choice

The new native card uses a 580×600 logical layout, 26px title, 16px body,
rounded white/grey surfaces and a blue primary action. It never scales above 1;
24px margins bound short and landscape windows. Feature settings retain their
separate existing geometry. Complete bound metadata remains paginated without
ellipsis, and every page must be reviewed before approval.

An initially unchecked native checkbox keeps the requester's original ONCE,
SESSION or TIMED period. PERSISTENT is available only for explicit approval-v2
requests whose entire registered batch permits it. The separate opt-in fixture
commands are `feature-register-choice` and `feature-serve-choice`; default
commands and approval-v1 remain unchanged. The checkbox cannot alter identity,
profile, capability, scope, operation, purpose, recipient or retention. Choice,
language or changed content resets full review; unchanged token refresh retains
only the same reviewed binding. Deny has initial focus; timeout/close never
approves. The original worker snapshot reference is checked before submission.

The fixed `libconsent-poc.so.0` and feature library are bundled from matching
native build targets into the signed main TPK, with SONAME/payload hash
evidence.
No library/socket is accepted from app launch data. Development verification
uses an owned isolated daemon and protected fixture identities. This is a PoC;
production UI role, endpoint/package deployment and argo launch provisioning
remain external requirements. The actual target evidence below is distinct
from host managed checks. The default NUI checkbox rendered orange; this is
not a claim that a Samsung runtime theme was installed.

### Verified native UI checkpoint

Release27 r7 GBS returned 0 with 29 PASS and four root-only SKIP, plus managed
UI checks. Actual emulator runs proved unchecked ONCE, checked PERSISTENT,
fresh-operation reuse, revocation and ONCE-only CM policy. Follow-up evidence
proved restart preservation, DB-loss fresh approval and helper generation
retirement followed by a fresh actual UI-approved CE effect. Helper generation
change was not a TPK reinstall test.

No global RPM was installed: test helpers were extracted and signed TPK bytes
were used for UI and namespaced worker libraries. Original TPK, services and
protected production/PoC state were restored or unchanged. Evidence:
`/var/tmp/consent-artifacts/consent-ui-native-09/`. Use Guide 17 for the current
repository-owned runner rather than the archived manual controllers.

[Detailed native UI
evidence](../history/07-verification-history.en.md#guide-8-checkpoint)

## Feature Settings

Settings chooses features and a base period: this conversation or 30 minutes.
The task-only option uses ONCE without changing saved selections. Argo owns
the catalog and submits approval requests; Settings has no argo role. Users
review a task even when existing grants avoid another consent popup. Workers
still perform AUTHORIZE before protected effects.

See [Guide 10](10-feature-approval.en.md) for feature bindings and conversation
reuse. The [build26–29
walkthrough](../history/07-verification-history.en.md#guide-8-settings-history)
retains earlier Settings commands and visual failures; use Guide 17 for new
automated runs.

## Explicit identity setup

The endpoint is `/opt/var/lib/consent-poc-runtime/consent.sock`, listened on by
PID1 through `consentd-poc.socket`. Its parent is protected root-owned 0755;
the socket is root:users 0660. Explicit stopped-service preparation validates
that leaf and sets only its SMACK label to `_`, then reads it back. The
ancestor labels and production policy are unchanged. The strict client
verifies PID1/UID0, the exact
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
`User::Pkg::org.tizen.consentui` label, with subject `owner` and profile
`default`.
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
must be reviewed before approval is enabled. Legacy requests use Allow once
and require ONCE support. Approval-v1 requests display the bound common
SESSION/TIMED/ONCE period and issue only the displayed missing conditions.
Prompt refresh is bounded to 500 ms, preserving page review only when the
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

## Historical manual procedure (build26)

Run these commands only against the selected development emulator, from the
repository version matching the frozen RPM artifacts. Both host and target
need Python 3: the host driver also runs small target-side Python checks.
`consent-tests` requires target `python3-base`; `consent-poc` alone does not.
The UI/registration service itself does not require Python. Resolve missing
runtime or test dependencies with matching Tizen repository RPMs in the normal
RPM transaction; dependency-check bypasses are not part of this procedure.

For new automated runs, use Guide 17. The following preserved host example
selects verified build26. Confirm the matching
snapshot and artifact hashes in [Guide 07](07-verification.en.md) before
installation. Build24 exposed a text-fit failure and build25 exposed clipped
popup placement;
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

## Browser previews

A self-contained [prior-layout HTML archive](../previews/consent-popup.html)
reproduces the earlier NUI layout and page-review flow without APIs or real
approvals.
The existing NUI visual/interaction code can be reused for a product UI; its
current NativeApi library is fixed to `libconsent-poc.so.0`. A production build,
endpoint/package setup, trusted UI role and argo launch mapping remain required;
this is not runtime substitution of an arbitrary library. Installer hooks belong
to each plugin, using authenticated Installer publication and protected
package/app generations.

A separate [compact One UI-style proposal](../previews/consent-popup-oneui.html)
adds an initially unchecked “Always allow” checkbox. It illustrates a proposed
PERSISTENT policy for the current profile and the same capability, scope,
purpose and recipient; each acquisition still has a maximum 30-minute retention.
Policy changes, reinstall or a profile-authority gap may require fresh approval.
The repository supports PERSISTENT for eligible legacy definitions, but the
default feature definitions omit it and approval-v1 selected modes are limited
to ONCE, SESSION and TIMED. This proposal changes no native policy or API and
creates no grants. See the [implemented UI09 opt-in
fixture](#ui09-compact-native-layout-and-explicit-period-choice)
for the actual native flow. The prior-layout preview above remains unchanged.

The proposal follows Samsung's
[dialog](https://developer.samsung.com/one-ui/comp/dialog.html)
and [grid](https://developer.samsung.com/one-ui/layout/grid.html) guidance with
local fonts, a 580px desktop cap and 24px mobile margins. Browser typography
can differ from NUI. Deny has initial focus; close, Escape and the preview's
60-second deadline never approve. A scrollable disclosure must be reviewed
before allowing; changing the checkbox refreshes that disclosure.

---

[Related task](17-native-ui-smoke.en.md) ·
[Continue](10-feature-approval.en.md) · [Reading paths](../README.md)
