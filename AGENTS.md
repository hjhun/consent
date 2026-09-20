# consent / consentd Agent Guide

## Purpose and source of truth

Develop the Tizen consent framework: `consent`, a public C API library with a
C++ implementation, and `consentd`, a C++ daemon. Keep this guide in English.

Read `docs/CEP_Consent_Framework.md` before implementation and revisit the
relevant sections for each change. It is a design proposal, not a statement
that its APIs, schema, or examples have already been implemented. Distinguish
explicit requirements from proposals and open decisions. Direct user
instructions take precedence over the design draft and this guide.

The user requires:

- Source code under `src/`.
- Coding style and packaging consistent with the user's Tizen appfw packages.
- Development builds using `gbs build` and validation on a connected emulator.
- Executable API test programs and actual verification of behavior.
- Resilience to interrupted DB writes, reboot, corruption, and forced DB deletion.
- Final development guides in both Korean and English under `docs/`, including
  architecture explanations with Mermaid diagrams.
- Both package name and app ID as explicit inputs to `consent_register()`;
  removal by package name.

## Local references and coding style

Inspect the relevant reference implementation before introducing a convention:

- `~/tizen/platform/core/appfw/tizen-watcher/`: target layout, C API wrappers,
  daemon classes, common helpers, CMake, RPM spec, manifest, service/socket units.
- `~/tizen/platform/core/appfw/amd/`: daemon lifecycle, workers, IPC, databases,
  platform integration, logging, and packaging.
- `~/tizen/platform/core/appfw/aul-1/`: public C ABI over C++, client IPC,
  callback ownership, test tools, and recovery utilities.

The user identifies code through January 2026 as the handwritten style
reference. Consult history through `2026-01-31` where available; do not assume
recent generated code represents that style. Initial reference points include
AMD `29ed218b` (2026-01-28), particularly
`src/modules/component-manager/src/worker.{h,cc}`, and AUL `ac581e7`
(2026-01-23), particularly `src/aul/launch_with_result.cc`. Use current package
files to check integration details. Reference repositories are read-only inputs
unless the user asks for changes there.

For new C++ code, follow the observed conventions:

- `.cc` implementations; `.h` for public C headers; use `.hh` consistently for
  new private C++ headers, as in tizen-watcher.
- Keep public C API headers under `src/consent/inc/`. Do not put private C++
  declarations in this directory. Split headers by function and preserve
  `consent.h` as the umbrella header. Separate implementation by function where
  this clarifies ownership without duplicating common ABI checks.
  Define public errors using the platform's
  `tizen.h`/`tizen_error.h` constants and documented module-error rules, rather
  than standalone numeric literals or an invented platform module allocation.
- Two-space indentation, opening braces on the declaration/control line,
  `PascalCase` classes and methods, `snake_case` variables, and trailing `_`
  for member fields. Public C functions use the `consent_` prefix.
- Header guards, component namespaces, anonymous namespaces for file-local
  helpers, and the corresponding header first in implementation files.
- Small classes with clear ownership and explicit lifecycle; RAII for resource
  cleanup, `std::unique_ptr` for sole ownership, and shared ownership only when
  required. Use `nullptr`, `const`, and `override` appropriately.
- Validate parameters at API boundaries, return consistent errors, and keep
  implementation symbols hidden. Never let C++ exceptions cross the C ABI.
- Preserve license notices and follow the repository's Apache-2.0 license.
  The RPM spec needs its `License:` metadata tag, but no license comment header.
  CMake configuration files also omit license comment headers, as requested.
  Every new source file, including generated code and tests, must contain the
  full copyright and Apache-2.0 notice used by the reference appfw code.
- Use the platform logging conventions with useful error context; do not log
  raw conversation data, sensitive scope values, or credentials.

Match style without copying unrelated dependencies or known unsafe behavior.
The CEP's GLib/GIO execution model and Unix socket transport take precedence
over tizen-watcher's use of tizen-core or RPC-port. Select the C++ standard from
actual target toolchain support; reference packages currently use different
standards.

## Intended repository layout

Use this starting layout, adapting only where implementation needs justify it:

```text
CMakeLists.txt
cmake/Modules/           # Shared CMake helpers, when needed
src/
  CMakeLists.txt
  consent/              # Client implementation and consent.pc.in
    inc/                # Public C API headers only
  consentd/             # Daemon and private headers
  common/               # Shared protocol and narrowly reusable utilities
  tools/                # API exerciser and diagnostic programs
  tests/                # Unit and integration test source code
packaging/              # RPM spec, SMACK manifest, systemd units
scripts/                # Build/deployment/test orchestration, when needed
docs/                   # Design, bilingual guides, architecture, test evidence
```

Keep implementation, private headers, and test program source under `src/`.
Use per-target CMake files and pkg-config dependencies. Package the versioned
shared library, daemon, development headers, linker symlink, and `consent.pc`
with the appropriate runtime/devel split. Keep test-only dependencies out of
the production runtime. Follow the reference RPM conventions for installation,
library cache updates, upgrades, systemd units, ownership, and SMACK labels;
verify the target settings instead of copying service identities blindly.
Use the verified platform security account (`security_fw` on the selected
emulator) for the daemon service and install
its symlink in `basic.target.wants`, following AMD packaging. Preserve socket
activation and verify storage/configuration access and cross-UID authentication
under the selected account; never substitute a guessed UID or weaken role checks.

## API and architecture requirements

- `consent_register()` must receive package name and app ID separately and
  explicitly, alongside registration data. Preserve both through IPC and DB
  storage. Validate their relationship against trusted package information;
  neither caller-provided field is proof of identity.
- `consent_unregister()` removes registrations by package name without requiring
  an app ID. Support multiple apps in one package. Atomically deactivate all
  affected definitions, invalidate related grants and pending requests, and
  publish cache invalidation after commit. Do not affect other packages.
  Retain cleanup/audit metadata as required by the retention policy.
- Make registration, updates, and removal retry-safe. Reinstallation with the
  same package name or app ID must not silently restore old approvals.
- Support daemonless image installation through `consent_register()` on an
  explicit offline registration handle used by the installing system service.
  Persist only protected definition records, not the consent DB or approvals.
  Generation provisioning must work against the selected image root; the daemon
  validates installed package identity and active generation before activation.
  Keep ordinary online authentication and error handling unchanged.
- Provide both synchronous and asynchronous request/check APIs. Synchronous
  names have no `_sync` suffix; asynchronous names end in `_async`.
- Async acceptance is separate from the final decision. Deliver accepted
  operations' results through callbacks, including immediate allowed/denied
  and cache results. Follow the CEP dispatcher contract so callbacks occur
  after API return, at most once per live registration, without holding locks.
  Document input/output ownership, cancellation, timeout, and shutdown.
- Only authenticated argo may initiate approval requests. Capability Manager
  and Context Engine check authorization; `check` must not open approval UI.
  Authenticate Installer, approval UI, session controller, and holder roles.
- Use `AF_UNIX` / `SOCK_STREAM` at `/run/.consentd.sock` and systemd socket
  activation through `consentd.socket` and `consentd.service`. Validate inherited
  FDs and ownership; do not independently bind or unlink the systemd endpoint.
- Use the actual parcel library from `~/tizen/platform/core/base/bundle` for
  client-daemon Parcelable messages. Keep a small shared IDL and deterministic
  compiler under `src/`, with generated C++ code shared by both endpoints.
  Specify byte order and validate primitive reads, bounded strings/arrays,
  message version, and full payload consumption. Do not trust `ReadParcelable()`
  alone to report decoding failure or apply unbounded `ReadString()` to input.
- Obtain and log kernel `SO_PEERCRED` PID/UID/GID. A shared UID alone does not
  establish an application role; integrate platform identity/authorization.
- Keep listener/lifecycle work on a GLib main loop, connection I/O on a bounded
  set of I/O threads with their own contexts, short independent work in a
  bounded pool, and SQLite work on one serialized DB executor initially.
  Never hold a DB transaction or worker while waiting for user approval.
- Define thread ownership and lock ordering. Use `GMutex` for shared state and
  `GRecMutex` only for justified reentrancy. Bound frames, queues, jobs, clients,
  and waits; handle partial I/O, disconnects, stale completions, and shutdown.
- Keep QUERY separate from AUTHORIZE. Evaluate all required conditions and
  consume one-time grants atomically, with operation retry deduplication.
- Support localized registered messages with deterministic fallback and bind
  UI responses to the displayed request, policy, and session generation.
- Invalidate caches on policy changes, revocation, package removal, lost
  synchronization, and daemon/DB generation changes. Actual protected actions
  require authoritative checking; a local cache is not final authorization.
- Keep logical sessions separate from transport connections and data-use
  permits separate from access grants. Store control metadata, not conversation
  bodies. Enforce provenance, expiry, revocation, and holder cleanup ACKs.
  Do not automatically reactivate old sessions after daemon restart or reboot.

## Database integrity and recovery

Only `consentd` accesses `consent.db`. Implement recovery as part of the storage
lifecycle, not as a manual workaround in tests.

- Version the schema and use atomic transactions for registration, policy
  changes, authorization consumption, revocation, and cleanup state. Enable
  foreign keys and bound busy retries. Publish success/events after commit.
- Choose and document journal and synchronization settings against reboot and
  interrupted-write requirements. WAL is not mandatory; if used, include its
  sidecars and checkpoint behavior in recovery design and tests.
- Detect a missing, replaced, or unusable DB both at startup and during daemon
  operation. Consider open handles to deleted files and stale in-memory state;
  merely opening the path again does not establish recovery correctness.
- Serialize recovery, stop authorization based on uncertain state, retire old
  DB handles, invalidate affected caches/requests/sessions, and establish a new
  DB generation. Do not let recovery race normal DB jobs.
- Recreate a valid schema and reconcile definitions from trusted installed
  package metadata or another explicitly designed recovery source. Implement
  and test that source; do not claim lost registrations can be reconstructed
  from a deleted DB alone.
- Never reconstruct user approvals from package definitions. Lost or uncertain
  decisions require fresh approval. Any backup recovery must prevent revival
  of revoked or consumed grants. Preserve valid durable decisions on normal
  restart according to the documented policy.
- Distinguish recoverable missing/corrupt data from permission, storage-full,
  and I/O failures. Return explicit errors and block protected execution when
  safe recovery is unavailable. Do not hide arbitrary failures by wiping data.
- Preserve enough cleanup state for retries where possible. After total DB
  loss, define holder reconciliation/invalidation; never report physical data
  deletion complete without evidence.

## Build and verification workflow

Use a short plan for nontrivial work, inspect local context, implement in
reviewable increments, run relevant checks, and update documentation. Preserve
user edits and untracked design files. Commit or push only when requested.

1. Inspect the current GBS profile, package conventions, and available SDK.
   Discover the emulator with `sdb devices` and query its architecture; do not
   assume host architecture, hardcode a device serial, or expose credentials.
2. Build through `gbs build` using the verified profile and architecture. Use
   `--include-all` when needed to include uncommitted work. Record the exact
   command, result, and produced RPM paths. A host-only build is supplementary.
3. Provide a C API test program under `src/tools/` or `src/tests/` that links
   against the installed library and supports repeatable, noninteractive
   scenarios with useful output and failing exit status. Include C-header/ABI
   validation and targeted unit tests for policy, protocol, and DB behavior.
4. Install the built RPMs on the selected emulator and exercise the real
   library, IPC, daemon, socket activation, and persistent storage. Use explicit
   device selection when needed. Keep mock-based results distinct from real
   platform integration; test authorization adapters must not weaken production.
5. Test normal and failure paths, including the relevant CEP section 19
   acceptance criteria. At minimum cover:
   - Registration with package/app identity, multiple apps per package,
     mismatched identity rejection, update, package-wide removal, and reinstall.
   - Request/check SYNC and ASYNC, callback ordering/lifetime, role rejection,
     localization, cancellation, timeout, and concurrent one-time authorization.
   - Revocation, cache invalidation, daemon restart, session lifecycle, data
     expiry, and incomplete holder cleanup.
   - Fragmented/malformed IPC, disconnects, queue limits, and clean shutdown.
   - DB write interruption before/during commit and after commit before reply;
     daemon termination and emulator reboot; integrity and policy after restart.
   - Forced DB deletion while stopped and while running, including active
     writes; recreation/reconciliation; stale-cache rejection; successful API
     use after recovery; isolation from unrelated packages. Cover journal/WAL
     artifacts according to the selected storage mode.
6. Run destructive recovery scenarios only against the selected development
   emulator or isolated test state, with explicit paths and repeatable setup
   and teardown. Never delete unrelated platform databases. Distinguish process
   kill, orderly reboot, and abrupt emulator termination in reported evidence.
7. Capture commands, relevant logs, expected/actual results, and remaining gaps.
   Do not claim emulator validation, reboot resilience, or API coverage from
   code inspection alone. Report environmental blockers precisely.

## Documentation and completion

Keep `docs/CEP_Consent_Framework.md` as the design reference. Record resolved
design choices and deviations with rationale; avoid silently rewriting the
original proposal to imply implementation or verification.

Deliver paired guides such as `docs/developer-guide.ko.md` and
`docs/developer-guide.en.md`, plus `docs/architecture.ko.md` and
`docs/architecture.en.md`. Keep each pair synchronized. Cover build/package
commands, deployment, API examples, identity rules, callbacks, tests, DB
recovery, troubleshooting, and known limitations. Include Mermaid component,
request/response sequence, and lifecycle/recovery diagrams matching the code.

A feature is complete only when implementation and packaging are integrated,
relevant tests pass, available emulator checks have been performed, and the
Korean/English guides describe the actual behavior. Final handoff identifies
the artifacts, verification evidence, and any remaining limitations.
