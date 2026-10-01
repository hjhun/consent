# API 05: Manage sessions and retained data

[한국어](05-sessions-and-data.ko.md)

Open a conversation session, authorize acquisition for its holder, register only
retained-data metadata, then close and acknowledge actual deletion. Consent
never stores the data body or deletes the holder's buffer for it.

## Before you start

Use separate session-controller and holder identities. The definition must allow
positive retention. The AUTHORIZE requirement must bind `r0.holder` to the
actual holder and include the current session/generation. Do not use the simple
sessionless tool recipe as proof of a data-use permit.

## 1. Open a session

These are string inputs, illustrated as JSON rather than a JSON loader:

```json
{
  "subject": "configured.subject",
  "profile": "configured.profile.A",
  "lifecycle": "RESUMABLE_CONVERSATION",
  "lease_ms": "30000"
}
```

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `subject`, `profile` | string | Yes | Explicit delegated context |
| `lifecycle` | string | No | Default CONNECTION_BOUND; resumable here |
| `lease_ms` | decimal string | No | Initial lease; 30000 here |

This complete helper runs as the session controller:

```c
#include <consent.h>

/* client has the session role; caller owns *result on success. */
int open_conversation(consent_client_h client, consent_result_t **result) {
  consent_params_t *params = NULL;
  *result = NULL;
  int status = consent_params_create(&params);
  if (status == 0)
    status = consent_params_set(params, "subject", "configured.subject");
  if (status == 0)
    status = consent_params_set(params, "profile", "configured.profile.A");
  if (status == 0)
    status = consent_params_set(params, "lifecycle", "RESUMABLE_CONVERSATION");
  if (status == 0)
    status = consent_params_set_int64(params, "lease_ms", 30000);
  if (status == 0)
    status = consent_session_open(client, params, result);
  consent_params_free(params);
  return status;
}
```
On status 0, copy `session`, `generation` and `resume_token` from the owned
result before freeing it. The pointers borrowed from that result die with it.
Keep the copied values with the same owner process. Renew before lease expiry;
heartbeat does not extend idle or maximum lifetime.

## 2. Authorize acquisition and register retained metadata

The holder's current AUTHORIZE must return status 0, ALLOWED and receipt before
acquisition. Its exact scope/purpose/recipient must match this registration:

```json
{
  "receipt": "<AUTHORIZE receipt>",
  "subject": "configured.subject",
  "profile": "configured.profile.A",
  "session": "<returned session>",
  "generation": "<current generation>",
  "scope": "today",
  "purpose": "answer-calendar",
  "recipient": "conversation",
  "storage_class": "MEMORY_ONLY"
}
```

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `receipt` | string | Yes | Holder-bound acquisition receipt |
| `subject`, `profile` | string | Yes | Same delegated context |
| `session`, `generation` | string | Yes | Active current session |
| `scope`, `purpose`, `recipient` | string | Yes | Exact acquired-data use tuple |
| `storage_class` | string | MEMORY_ONLY | Other classes are denied |
| `requirement` | decimal string | No | Receipt requirement index, default 0 |

```c
#include <consent.h>
#include <stddef.h>

/* holder owns this receipt and buffer; session fields are current. */
int register_buffer(consent_client_h holder, const char *receipt,
                    const char *session, const char *generation,
                    consent_result_t **result) {
  const struct { const char *key; const char *value; } fields[] = {
    {"receipt", receipt}, {"subject", "configured.subject"},
    {"profile", "configured.profile.A"}, {"session", session},
    {"generation", generation}, {"scope", "today"},
    {"purpose", "answer-calendar"}, {"recipient", "conversation"},
    {"storage_class", "MEMORY_ONLY"}
  };
  consent_params_t *params = NULL;
  *result = NULL;
  int status = consent_params_create(&params);
  for (size_t i = 0; status == 0 && i < sizeof(fields) / sizeof(fields[0]); ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_data_register(holder, params, result);
  consent_params_free(params);
  return status;
}
```
Status 0 returns an artifact control record; copy its `artifact` and any needed
fields before freeing the result. The body stays with the holder. Expiry is
acquisition time plus definition retention_ms; retry does not extend it.

## 3. Reuse only current artifacts

For reuse, check `operation=reuse-data` with artifact, subject/profile,
session/generation and the same scope/purpose/recipient. Do not acquire again
or use an old receipt as a general data permission. Derived registration records
parent provenance; it does not perform the transformation.

## 4. Close, delete and acknowledge

The session controller calls `consent_session_close()` with its current context.
Further data use is blocked. The holder calls `consent_cleanup_get_pending()`,
deletes each body, then calls `consent_data_release()` with artifact and
`success=1` only after real deletion (`success=0` after failure). Each
successful
API result is owned and must be freed. Continue every cleanup page and retry
failed entries. CLOSING becomes CLOSED only after recorded ACKs; API success
alone is not physical deletion proof.

## Troubleshooting

A closed session never revives after reconnect or daemon restart. Stale
generation
requires state reconciliation. A restarted holder can use delegated context and
`reconcile=1` for cleanup only; this does not restore data-use authority.

## Reference: periods, bounds, provenance and cleanup pages

## Grant modes, sessions and data lifetime

| Mode | Access grant lifetime |
|---|---|
| ONCE | Consumed atomically by the first successful AUTHORIZE execution, not by QUERY or UI completion |
| SESSION | Bound to the logical session; suspended/closed/expired or stale-generation use is rejected |
| TIMED | UI duration_ms 100–3600000, default 300000; absolute expiry is not extended by checks |
| PERSISTENT | Durable until invalidation/revocation; does not imply unlimited data retention |

Eligible PERSISTENT/SESSION request results can have an advisory per-handle
cache
lease of at most 500 ms, conservatively anchored to local admission and session
deadlines. Policy/revocation/generation changes and lost synchronization
invalidate
it. ONCE, TIMED and level 3 are not cacheable. Every protected access still uses
authoritative check; `source=CACHE` is never an execution permit.

Session open requires subject/profile. Lifecycle defaults to CONNECTION_BOUND;
RESUMABLE_CONVERSATION permits suspend/resume within its bounds.
Defaults/ranges:

| Field | Default ms | Inclusive range ms |
|---|---:|---:|
| `idle_timeout_ms` | 600000 | 100–86400000 |
| `max_lifetime_ms` | 3600000 | 100–86400000 |
| `lease_ms` | 30000 | 100–60000 |
| `reconnect_grace_ms` | 30000 | 100–300000 |

Keep returned session/generation/resume_token. Transitions require
subject/profile,
session and current generation; resume also needs the token and the same owner
process instance. Suspend of CONNECTION_BOUND closes it; resumable suspend
increments generation and blocks use. Resume rotates generation/token and starts
a 30000 ms lease without extending the original idle/maximum deadlines. The
session owner can call `consent_session_heartbeat` with
subject/profile/session/generation
before lease expiry. It sets a 30,000 ms lease without extending idle or
absolute
lifetime. The synchronous wait is bounded to 5,000 ms, the caller owns inputs,
and the result is freed with `consent_result_free`. State queries and ordinary
checks do not renew the lease. Wrong owner, stale generation, inactive/closed
session or an offline handle is rejected. Daemon restart does not reactivate
old sessions.
Returned deadline/expiry values use daemon monotonic milliseconds, not UTC
dates.

| Data operation | Required schema and behavior |
|---|---|
| `consent_data_register` | `receipt`, subject/profile, active session/generation, exact scope/purpose/recipient; optional `requirement` index, default 0. AUTHORIZE must have bound rN.holder to this holder. Only MEMORY_ONLY is accepted. Definition retention_ms must be positive. |
| `consent_data_register_derived` | Subject/profile, active session/generation, scope/purpose/recipient, count 1–16 and distinct parent0…parentN. Same holder instance/context; earliest parent expiry and combined provenance are inherited. |
| `consent_check` data reuse | Set `operation=reuse-data`, artifact, subject/profile, active session/generation and exact scope/purpose/recipient. This validates current retained-data provenance without acquiring data again. |
| `consent_data_release` | `artifact`, explicit `success=1` only after deletion, or `success=0` after failure. Previous-holder-instance cleanup also needs `reconcile=1` and matching subject/profile. |
| `consent_cleanup_get_pending` | Subject/profile and optional `reconcile=1`; returns up to 48 aN.artifact/session/state/error entries. Acknowledge deletion, then query again. |
| Session/cleanup state | Subject/profile/session; state and cleanup_pending are observations, not lease renewal or independent proof of physical deletion. |

Original data expires at acquisition time plus definition retention_ms; retries
cannot extend it. Access grant lifetime and already acquired data retention are
separate: consuming ONCE or expiry of a TIMED access grant does not by itself
erase an independently valid data-use permit. Revocation, session closure,
policy/installation invalidation and data expiry still block use and require
cleanup. CLOSING becomes CLOSED only after recorded holder deletion ACKs.
Consent stores control metadata, never conversation bodies. Product holders
must enforce real memory/storage deletion and remote-recipient obligations.


### Cleanup pages and retry sweeps

Keep count/aN consumption and follow more/next_cursor after each page, even if
an individual deletion/ACK failed. Copy the next position before freeing the
result. After more="0", cursor must be empty for a new sweep; it retries
failures
and includes additions. The executable isolated `consent-scenario cleanup-pages`
exercises 97 real registrations and failed first48 ACKs; feature holder cleanup
uses the same continuation contract. Example traversal (deletion/ACK belongs to
the actual holder and must be evidence-based):

```c
int more = 0;
int status = consent_params_set(params, "cursor", "");
do {
  consent_result_t *page = NULL;
  if (status)
    break;
  status = consent_cleanup_get_pending(client, params, &page);
  if (status)
    break;
  /* Process each aN entry; failed deletion must not stop page traversal. */
  more = strcmp(consent_result_get(page, "more"), "1") == 0;
  status = consent_params_set(params, "cursor",
      consent_result_get(page, "next_cursor"));
  consent_result_free(page);
} while (more);
/* Before the next retry sweep, explicitly clear cursor. */
consent_params_set(params, "cursor", "");
```

The position is bound to durable DB incarnation and authenticated
holder/process/subject/profile/reconcile scope. It survives a normal daemon
restart for the same holder process. A new process starts a fresh sweep, even
with reconcile=1. Cache epoch and ACK policy revision are separate values.
DB reset returns STALE; a changed caller scope returns PERMISSION_DENIED.
Malformed tokens return INVALID_PARAMETER. After these errors, the caller
abandons its old traversal and starts a new sweep; an internal CleanupProgress
owner
explicitly resets its continuation first. This reset does not attest to physical
data deletion. A page/API failure is not a successful deletion result.

---

[Previous](04-results-and-callbacks.en.md) · [API overview](../02-c-api.en.md)
