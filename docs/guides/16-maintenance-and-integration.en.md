# Guide 16: Maintenance and product integration

[한국어](16-maintenance-and-integration.ko.md)

Use this checklist when changing framework code or preparing a product
integration. It summarizes implemented ownership safeguards and work that
still needs a platform contract or target evidence.

## 1. Review resource and callback ownership

Callbacks must keep their owner alive, contain exceptions and close uncertain
authority state. Profile callbacks use `noexcept` adapters; timers retain their
callback data even if the callback removes its own source. Lifecycle and
SQLite owners cannot be copied. SQLite error buffers use `sqlite3_free` and
client synchronization uses the shared `MutexLock`.

Profile configuration is decoded before publication. Missing configuration
keeps legacy behavior; a present malformed file prevents startup. Required
strings check GLib errors: explicit `subsession=` is valid, but
`subsession=\x` is not. Protected opening, complete bounded reads and rejection
of NUL, duplicate and unknown keys prevent partial or ambiguous configuration.

A spawned provider belongs to the process helper until it is reaped, including
setup failures. The async profile example owns one pending result, clears it
after successful client destruction and transfers it only once. Keep ABI,
protocol, roles, generation fences and storage semantics unchanged when
refactoring. The version map is a link dependency, so symbol changes relink.

## 2. Choose a product integration checkpoint

| Work | Contract or evidence required |
| --- | --- |
| CM/CE adapters | Trusted service identities; capability/record mapping; final AUTHORIZE gate; current CE source and taxonomy |
| Installer hooks | Each plugin's install/update/remove transaction; authenticated publisher/offline handle; protected package/app generation |
| Approval UI | Product build/endpoint packaging; trusted UI role; argo launch mapping; fixed native library contract |
| Profiles | Protected sessiond account/profile mapping, bus privileges and live provider coordination |
| Holder cleanup | Actual data deletion and authenticated retryable cleanup ACKs |
| Registry recovery | Trusted definition producer after total registry loss; no approval reconstruction |
| Durability and load | Interrupted storage/power and representative sustained-load tests |

The existing NUI code is reusable, but its fixed library cannot be replaced by
an arbitrary launch argument. Profile discovery found installed sessiond
inactive, bus names unowned and mapping absent; those observations alone did
not establish the cause. Synthetic fixtures do not close these product tasks.

## 3. Compare with retained verification

Release26 r2 GBS returned 0: 27 tests passed and four root-only tests were
skipped. Configuration decoding, callback exception containment and spawned
child cleanup regressions passed. Installed profiles/mock/tools/default modes
returned 0; strict mode returned the expected product-gate 1; all five cleanups
returned 0. Installed payloads matched 162 RPM file digests, and native config
and process checks returned 0. Production daemon18 and PoC18 were unchanged.

Evidence: `/var/tmp/consent-artifacts/consent-style-06/`. See the
[detailed checkpoint
record](../history/07-verification-history.en.md#guide-16-checkpoint).


<a id="code-maintenance"></a>

<a id="product-integration-checklist"></a>

<a id="verified-maintenance-checkpoint"></a>
---

[Related task](07-verification.en.md) · [Continue](17-native-ui-smoke.en.md) ·
[Reading paths](../README.md)
