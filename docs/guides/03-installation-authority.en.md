# Guide 03: Installer generation authority

The production adapter requires two independent facts: pkgmgr-info must confirm
the app belongs to the package, and the protected installation authority must
mark that app and package active at the expected generation. Package name,
version, second-resolution install time, signature identity or inode alone is
not a unique installation incarnation.

The authority is `/opt/var/lib/consent-authority/installations.conf`. Its directory
is root-owned mode0750 and the file is root-owned mode0640, with the daemon's
primary group permitted to read. The daemon cannot replace the authority in its
root-protected directory chain. The canonical `/opt/var` path is
intentional: the inspected Tizen emulator has `/var -> opt/var`, while protected
file opens reject symlink components. `consent-installation-authority` is a
root-only provisioning utility, not a client authorization bypass.

`consent-storage-prepare` prepares this directory before the daemon starts. During
an upgrade it moves the legacy authority from the daemon state directory before
changing that directory's ownership; it preserves generation contents. It does
not create an empty replacement for an unreadable or conflicting authority.
Normal daemon and Installer operations hold a shared `lifecycle.lock`; preparation
takes that lock exclusively. The Installer only reads and locks the prepared
lifecycle file and never changes its ownership or mode. Stop the socket/service
and quiesce legacy Installer operations before migration.

## Provisioning execution context

Run the writer as root in an explicit `System::Privileged` SMACK process context.
Root UID alone is insufficient: the emulator's root shell in the `System`
context could not set the required file label. The production preparation helper
succeeded through the service's root `ExecStartPre=+` invocation. An isolated
writer `begin` command also succeeded and returned a generation UUID when run as:

```sh
systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
  /usr/libexec/consent/tests/consent-installation-authority-isolated \
  begin context.probe context-probe-16 absent
```

For production provisioning, use the same explicit context with the production
executable. The lifecycle examples below use this shell function, invoked by root:

```sh
consent_authority() {
  systemd-run --quiet --wait --pipe -p SmackProcessLabel=System::Privileged \
    /usr/sbin/consent-installation-authority "$@"
}
```

This is an explicit privileged Installer operation. It does not grant a client
role or relax the daemon's package and generation checks. The successful isolated
command verifies the execution context; it does not establish production Installer
hook integration.

## Lifecycle contract

Installer owns stable transaction IDs and serializes lifecycle operations for a
package. Every newly installed incarnation begins with a fresh generation.
Each sub-operation has a different stable ID, reused unchanged after an uncertain
result. Run the following only for the package being installed:

```sh
# First installation: use absent. For replacement, supply the previous generation.
generation=$(consent_authority begin org.example.package install-01 absent)
consent_authority attach org.example.package org.example.app attach-01 "$generation"
consent_authority attach org.example.package org.example.app2 attach-02 "$generation"
# Only after the actual platform package transaction is durably committed:
consent_authority commit org.example.package commit-01 "$generation"
```

`begin` makes the package pending and fences prior app entries. `attach` adds an
app to that pending generation. `commit` activates only matching attached apps.
A pending generation does not activate consent definitions. After platform and
authority commit, the authenticated Installer calls
`consent_register(client, package, app, params)` with `expected_generation`, a
stable `operation_id`, policy and localized messages.

Removal first makes the generation unavailable through an authority tombstone:

```sh
consent_authority remove org.example.package remove-01 "$generation"
```

The authenticated Installer also calls
`consent_unregister(client, package, params)` with that same expected generation
and a stable API operation ID. No app ID is required. The daemon verifies the
current package generation even for a tombstone and checks every affected
registration. A delayed removal cannot delete a newer generation. Registration
and removal retry fingerprints include their payloads; changing a payload while
reusing an ID is a conflict.

On rollback, leave the failed generation pending/removed. Restoring a package
requires a new begin/attach/commit cycle after trusted package verification and
new definition registration. This conservative policy does not restore old
approvals. The tool does not infer a platform transaction's success by itself.

## Durable file format

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

The tool only accepts bounded identifiers containing ASCII letters, digits,
period, underscore or hyphen, so the fingerprint delimiter is unambiguous. It
keeps bounded operation receipts and tombstones; automatic pruning is not
implemented. A full authority returns an error rather than discarding replay
protection. Writes hold a nonblocking advisory lock and use an exclusive
same-directory temporary file, file fsync, atomic rename, then directory fsync.
Published files retain root ownership, the daemon's primary group, mode0640 and
the `System` SMACK label. Permission or label-setting failures prevent publication.
Any failure is an unsuccessful or uncertain outcome: keep the install fenced,
retry the same transaction ID and reconcile before returning success upstream.

The authority is separate from `definitions.registry` (the daemon's definitions
source) and `consent.db` (the policy projection, approvals and transient metadata).
They do not form one atomic distributed transaction. The daemon validates the
authority during replay and access checks and periodically invalidates affected
state. The authority never contains grants, user decisions, sessions or usage.

## Current integration boundary

The repository provides the adapter, provisioning executable and isolated test
authority. Production pkgmgr-info validation remains enabled. No actual platform
Installer hook or production role is assumed configured. The tizen-watcher
metadata plugin provides a candidate install/upgrade/uninstall/rollback extension
point, but its callbacks do not by themselves provide a durable install generation
or stable cross-callback transaction ID. Wiring that hook requires validating the
Installer commit/rollback order and registering its real process identity.

The separate `consent-installation-authority-isolated` binary targets
`/opt/var/lib/consent-test-authority/installations.conf`; the isolated daemon's
state remains in `/opt/var/lib/consent-test`. The test daemon accepts this explicit inventory
instead of querying pkgmgr-info. Neither binary changes production identity
policy, and test generation values are not accepted by the production adapter.
