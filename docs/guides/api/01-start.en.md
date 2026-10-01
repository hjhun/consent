# API 01: Start a C API client

[한국어](01-start.ko.md)

Build a small checker that reads the current approval decision. It performs
QUERY only: it neither opens an approval popup nor reads protected data.

## Before you start

Install matching development headers and the library. The process must have an
authenticated checker identity and delegated subject/profile/enforcer. A root
shell alone is insufficient. This program uses the fixed production endpoint.
An integrator must enroll its
exact executable; Guide 12 enrolls only its own fixed smoke actors and private
library, not this new consumer.

## 1. Include and link the library

Use `<consent.h>` and `pkg-config consent`. Save the following complete program
as `query.c`. Its seven arguments select an already registered definition and
its exact access context. The program creates a client, issues one query and
releases every owned object.

### Minimal C query program

This sessionless example fixes the operation to read. For SESSION checks, use
[check.c](../../../src/examples/check.c) with its session/generation arguments.
Exit 0 is advisory ALLOWED only; the program obtains no execution receipt.

```c
#include <consent.h>
#include <stdio.h>

int main(int argc, char** argv) {
  consent_client_h client = NULL;
  consent_params_t* params = NULL;
  consent_result_t* result = NULL;
  int exit_status = 1;
  int status;

  if (argc != 8) {
    fprintf(stderr, "Usage: %s SUBJECT PROFILE DEFINITION POLICY_VERSION "
        "SCOPE PURPOSE RECIPIENT\n", argv[0]);
    return 2;
  }
  status = consent_params_create(&params);
  if (!status)
    status = consent_params_set(params, "subject", argv[1]);
  if (!status)
    status = consent_params_set(params, "profile", argv[2]);
  if (!status)
    status = consent_params_add_requirement(params, argv[3], "read",
        argv[5], argv[6], argv[7]);
  if (!status)
    status = consent_params_set(params, "r0.policy_version", argv[4]);
  if (!status)
    status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
  if (!status)
    status = consent_client_create(&client);
  if (!status)
    status = consent_check(client, params, 5000, &result);
  if (!status) {
    consent_decision_e decision = consent_result_get_decision(result);
    const char* name = consent_result_get(result, "decision");
    printf("QUERY: %s (advisory only)\n", name ? name : "UNKNOWN");
    exit_status = decision == CONSENT_DECISION_ALLOWED ? 0 : 3;
  } else {
    fprintf(stderr, "query: %s (%d)\n", consent_error_string(status), status);
  }
  consent_result_free(result);
  consent_params_free(params);
  if (client) {
    status = consent_client_destroy(client);
    if (status) {
      fprintf(stderr, "destroy: %s (%d)\n", consent_error_string(status), status);
      exit_status = 1;
    }
  }
  return exit_status;
}
```

```sh
cc -std=c11 -Wall -Wextra query.c -o consent-query \
  $(pkg-config --cflags --libs consent)
./consent-query SUBJECT PROFILE DEFINITION POLICY_VERSION SCOPE PURPOSE RECIPIENT
```

Replace placeholders with actual values; use `""` for an empty recipient. Exit
code 1 means API failure, 2 means wrong argument count, and 3 means a decision
other than ALLOWED. When QUERY reports CONSENT_REQUIRED, delegate approval to
Argo; follow [API 03](03-request-and-check.en.md) before actual access.

## Headers and ownership

| Header | Functions or declarations |
|---|---|
| `consent_common.h` | Handles, enums, callback type, `consent_error_string` |
| `consent_client.h` | Online constructors, offline constructor, destroy |
| `consent_params.h` | Create/free/set/set_int64/set_check_mode/add_requirement |
| `consent_request.h` | Request/check SYNC and ASYNC, detach, result lookup, cancel |
| `consent_registration.h` | Register/update/unregister/revoke |
| `consent_prompt.h` | Get prompt, respond, plain-text formatter |
| `consent_session.h` | Open/heartbeat/suspend/resume/close/get_state |
| `consent_data.h` | Register/derive/release data, cleanup state/list |
| `consent_result.h` | Free/clone/decision/get/size/get_at |
| `consent.h` | Umbrella over all feature headers |

Each feature header works alone in C and C++. Private C++ headers are not
installed. Run compiler commands on the target or inside a compatible Tizen SDK
with matching development packages, not on an unconfigured ordinary host:

```sh
cc -std=c11 consumer.c -o consumer $(pkg-config --cflags --libs consent)
cc -std=c11 async-consumer.c -o async-consumer \
  $(pkg-config --cflags --libs consent glib-2.0)
```

| Object | Ownership and lifetime |
|---|---|
| `consent_client_h` | One owner; destroy on the creating thread. Never race destroy with a call using the raw handle. |
| `consent_params_t` | Caller-owned, single-threaded builder. Every operation copies fields before return; free or reuse afterward. |
| Synchronous result | Caller-owned on success; output is NULL on error. Free with `consent_result_free`. |
| Callback result | Borrowed only until callback return; NULL for a nonzero callback status. Clone to retain; never free the borrowed result. |
| Result field pointers | Borrowed until the owning result dies; an existing empty field is different from a missing field. |
| Formatted prompt | Caller-owned UTF-8 allocation; release with standard `free()`. |
| Error string | Static borrowed string; never free. |

Initialize outputs to NULL or zero. `consent_result_get_at()` leaves its output
pointers unchanged on invalid input. A NULL result has size zero and decision
UNKNOWN. Check the operation status before interpreting a result. Destroyed or
freed objects must never be reused.


## Build executable examples

The sources under [`src/examples`](../../../src/examples/) provide four
executables:

| Source / executable | Purpose |
|---|---|
| [`check.c`](../../../src/examples/check.c) / `consent-example-check` | Advisory QUERY followed by authoritative AUTHORIZE with stable execution IDs |
| [`request.c`](../../../src/examples/request.c) / `consent-example-request` | ASYNC approval, explicit GLib dispatcher, result clone, watchdog, detach/cancel and shutdown |
| [`register.c`](../../../src/examples/register.c) / `consent-example-register` | Complete online definition with separate package/app arguments |
| [`offline-register.c`](../../../src/examples/offline-register.c) / `consent-example-offline-register` | Explicit root-only image staging through the same register API |

The component CMake builds them against `consent`; only the request example adds
GLib. Its install destination is `${libexecdir}/consent/examples`; source files
are installed under `${docdir}/src/examples`. Guides are installed under
`${docdir}/docs/guides`, preserving the relative source links used here. The
actual prefix, docdir and libexecdir follow the package's CMake configuration.
Installed
sources can also be compiled directly; for example, from that directory:

```sh
cc -std=c11 -Wall -Wextra check.c example_common.c -o consent-example-check \
  $(pkg-config --cflags --libs consent)
cc -std=c11 -Wall -Wextra request.c example_common.c -o consent-example-request \
  $(pkg-config --cflags --libs consent glib-2.0)
```

The registration sources use the same first command with their filename.
No-argument invocation prints usage without connecting or writing state. Exit
codes are 0 for the example's successful outcome, 1 for API/setup failure,
2 for invalid argument count, and 3 for a non-ALLOWED request/check decision.
Request exit 0 remains advisory. The check example actually performs AUTHORIZE
and can consume ONCE, but deliberately performs no protected resource action.
Compilation alone is not evidence of an authorized target run.

## Integration contract

See [the API overview](../02-c-api.en.md#api-integration-contract) for the shared
identity, session, cleanup and compatibility contract. This consumer still
requires its exact production executable to be enrolled.

---

[Next](02-registration.en.md) · [API overview](../02-c-api.en.md)
