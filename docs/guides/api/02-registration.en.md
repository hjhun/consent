# API 02: Register a definition

[한국어](02-registration.ko.md)

An Installer registers what needs approval and what the UI will show. It does
not grant approval. The example below registers the synthetic summary tool used
in Guide 13; product tools must supply their own trusted policy and identity.

## Before you start

Use an authenticated Installer client. Its package delegation must include
`smoke.package`, and `smoke.app` must belong to that package in the configured
fixture. Obtain the generation from the committed installation authority, not
from a caller-created UUID. [Guide 03](../03-installation-authority.en.md)
explains
that lifecycle. The runner in Guide 13 provisions these fixture identities.

## 1. Prepare the definition

Package `smoke.package` and app `smoke.app` are separate `consent_register()`
arguments. The remaining inputs are shown here. Numbers are string fields.
Replace only the generation placeholder with its trusted value.

```json
{
  "subject": "smoke.subject",
  "profile": "smoke.profile",
  "operation_id": "register-summary",
  "expected_generation": "<trusted Installer generation>",
  "definition": "smoke.cm.tool.summary",
  "enforcer": "cm",
  "policy_version": "1",
  "text_revision": "1",
  "level": "1",
  "modes": "ONCE,PERSISTENT",
  "retention_ms": "60000",
  "default_locale": "en",
  "message.en.title": "Developer smoke approval",
  "message.en.body": "Allow this isolated fixture tool operation?"
}
```

| Field | Type in params | Required here | Meaning |
| --- | --- | --- | --- |
| `subject`, `profile` | string | Yes | Delegated approval context |
| `operation_id` | string | Yes | Stable registration retry ID |
| `expected_generation` | string | Yes | Committed Installer generation |
| `definition`, `enforcer` | string | Yes | Policy name and delegated checker |
| `policy_version` | decimal string | Yes | Positive semantic policy version |
| `text_revision` | decimal string | Yes | Positive message revision |
| `level` | decimal string | Yes | Trusted sensitivity, 0–3 |
| `modes` | comma-separated string | Yes | Permitted access grant lifetimes |
| `retention_ms` | decimal string | Set here | Retained-data limit, not grant time |
| `default_locale` | string | Yes | Complete fallback translation |
| `message.en.title`, `message.en.body` | string | Yes | Displayed approval text |

The JSON is a readable illustration of `consent_params_set()` calls. There is
no public JSON registration endpoint or automatic JSON loader.

## 2. Build parameters and call register

This complete helper receives a live authenticated client and the trusted
generation. It checks every setter, stops on error and frees the builder. The
caller still owns the client and generation string.

```c
#include <consent.h>
#include <stddef.h>

/* client belongs to the authenticated Installer; generation is provisioned. */
int register_summary(consent_client_h client, const char *generation) {
  const struct { const char *key; const char *value; } fields[] = {
    {"subject", "smoke.subject"},
    {"profile", "smoke.profile"},
    {"operation_id", "register-summary"},
    {"expected_generation", generation},
    {"definition", "smoke.cm.tool.summary"},
    {"enforcer", "cm"},
    {"policy_version", "1"},
    {"text_revision", "1"},
    {"level", "1"},
    {"modes", "ONCE,PERSISTENT"},
    {"retention_ms", "60000"},
    {"default_locale", "en"},
    {"message.en.title", "Developer smoke approval"},
    {"message.en.body", "Allow this isolated fixture tool operation?"}
  };
  consent_params_t *params = NULL;
  int status = consent_params_create(&params);
  for (size_t i = 0; status == 0 && i < sizeof(fields) / sizeof(fields[0]); ++i)
    status = consent_params_set(params, fields[i].key, fields[i].value);
  if (status == 0)
    status = consent_register(client, "smoke.package", "smoke.app", params);
  consent_params_free(params);
  return status;
}
```
## 3. Interpret success and release the client

Status 0 means online registration committed. It creates no grant; a check
still needs user approval. After the caller finishes its Installer work, call
`consent_client_destroy()` on the creating thread and check that status too.
The executable [register.c](../../../src/examples/register.c) shows create,
register and destroy in one `main` with full error handling.

For an image build, use the explicit offline constructor instead. Success means
STAGED only, not active; follow [Guide 04](../04-offline-registration.en.md).
Never fall back to offline registration after an online permission error.

## 4. Update or remove a definition

An update supplies complete replacement content. Increase policy version for
semantic changes and text revision for changed messages. Unregister takes the
package name, expected generation and stable operation ID, without an app ID;
it retires every affected app definition. Plugin hooks own this work under the
[Installer contract](../06-installer-integration.en.md).

## Troubleshooting

- Permission denied: verify enrolled executable, label and delegation.
- Stale generation: reconcile the committed installation; do not invent a value.
- Conflict: retry the original ID with identical content, not a changed policy.
- Level 3: only ONCE is supported; other modes are rejected.

## Reference: policy, image publication and localized prompts

## Registration online and during image construction

```sh
consent-example-register PACKAGE APP DEFINITION ENFORCER GENERATION \
  OPERATION_ID POLICY_VERSION TEXT_REVISION
consent-example-offline-register IMAGE_ROOT PACKAGE APP DEFINITION ENFORCER \
  GENERATION OPERATION_ID POLICY_VERSION TEXT_REVISION
```

Both build a complete bilingual typed definition: level 1,
`ONCE,SESSION,TIMED,PERSISTENT`, retention 60000 ms, default locale `en`, and
English
and Korean title/body pairs. `template_version=1` binds string variables
`scope`,
`purpose` and `recipient` to those exact requirement fields, with UTF-8 byte
limits 512, 256 and 512 respectively. Both localized bodies contain all three
placeholders, so the formatted prompt shows the actual requested values. The
UI must additionally show the available grant-mode choice and its lifetime;
the wording does not assume ONCE or PERSISTENT before that choice. The Guide
08 PoC supports explicit approval-v2 period choice when the
request and every registered condition permit it. Product UI deployment remains
separate integration work.
They pass package and app separately to
`consent_register()`. The examples do not infer the package or enforcer,
generate
installation authority, or grant consent. Replace the example text and allowed
modes with the product policy before real use.

Policy/text revisions are positive integers through INT32_MAX. Change policy
version for semantic changes, and increase text revision when messages, default
locale or fallback aliases change. The default translation must have a nonempty
title/body; at most 32 message fields, each at most 4,096 UTF-8 bytes, are
allowed.
Level 3 allows ONCE only. Update supplies the complete definition, not a patch.
Unregister is package-wide. Read the public headers for their errors and
ownership.

Online success means the daemon committed the registration. Offline creation
requires real/effective root, a protected absolute image path, an exclusive
lifecycle lock and stable Installer-provisioned generation. It never connects or
implicitly falls back after online failure. Offline success is **STAGED** only:
the protected record is durable, but no active registration, DB or user approval
is created. Startup import validates actual installed package/app membership and
active generation. All other handle operations except register/destroy return
INVALID_OPERATION. Guide 04 covers path restrictions, authority provisioning,
bounded records and retry after interrupted publication.


## Approval UI and localized templates

The UI calls `consent_get_prompt()` with request_id, requested locale and
`template_version=1` when supporting typed prompts. It receives a rotating
prompt_token and each requirement's actual scope/purpose/recipient, definition
revisions, sensitivity, allowed modes and selected translation. Display all of
that context. Use `consent_prompt_format(result, index, "title"/"body", &text)`
and release successful output with `free()`. Render plain text; never interpret
it as markup, a printf format or a second template.

Typed registration binds named values to authorization fields:

| Field | Contract |
|---|---|
| `template_version` | Exactly `1` |
| `parameter.<name>.type` | `integer` or `string` |
| `.source` | Integer: `scope` or definition `retention_ms`; string: `scope`, `purpose`, `recipient` or `operation` |
| `.min`, `.max` | Required inclusive signed-64-bit limits for integer |
| `.max_bytes` | Required 1–512 UTF-8 byte limit for string |
| `locale_fallback.<requested>` | Direct complete registered translation; no chain/cycle or shadow of a complete translation |

At most eight names of 1–32 ASCII identifier characters are allowed. Every typed
translation has title/body and their combined `{name}` placeholders exactly
match the declared variables. Values come from validated `rN` fields and current
policy, not independent display strings. Include `rN.policy_version`; a missing
or stale version fails. Integer scope is canonical decimal (`30`, not `030` or
`+30`). A missing string source fails; explicitly empty input can be valid.
`retention_ms` defaults to zero and must meet the declared integer bounds.
Each rendered field is bounded to 8,192 bytes. ICU plural/date/localized numeric
formatting is not implemented.

Locale selection tries a complete exact translation, an explicit direct alias,
the built-in `ko-KR` to `ko` or `en-US`/`en-GB` to `en` fallback, then the
registered default. Other locale mappings require explicit aliases. Reply with
the most recent
prompt_token, decision=ALLOWED/DENIED, and the chosen allowed grant_mode; typed
responses also echo the requested locale. The same UI process instance must
respond. The daemon re-evaluates the full AND without consuming new ONCE grants.
If a previously satisfied condition changed during UI wait, an ALLOWED response
can produce INVALIDATED. A still-valid grant explicitly approved for another
condition remains unused. Retrieve the resulting decision and use AUTHORIZE
before execution; the historical request result is not current authorization.

## Localized approval text

The A-15 increment adds a bounded, typed `{name}` template contract while
preserving existing literal messages. Frozen build15 passed GBS and the isolated
emulator C API scenarios; the [verification record](../07-verification.en.md)
identifies
the tested snapshot and limits. This formatter supports plain text and
canonical decimal
integers. It does not implement ICU syntax, plural rules, dates, or localized
number formatting.

Registration defines each variable's type, limits and trusted source. A
variable is derived from its validated requirement or retention metadata, not
from a caller-supplied display string. For example, an integer scope `30` can
represent 30 days of past query coverage; `retention_ms` is a distinct interval
for retaining acquired results, measured in milliseconds. Formatting performs
no unit conversion. An omitted `retention_ms` uses the policy's actual default
of zero, which must satisfy the declared bounds. A missing string source is
invalid; an explicitly supplied empty string is allowed by the string schema.
The UI displays scope, purpose, recipient, sensitivity and
allowed modes alongside the complete registered sentence.

Use the existing params builder to register the following additional fields:

| Field | Contract |
|---|---|
| `template_version` | `1` for typed messages |
| `parameter.<name>.type` | `integer` or `string` |
| `parameter.<name>.source` | Integer: `scope` or `retention_ms`; string: `scope`, `purpose`, `recipient` or `operation` |
| `parameter.<name>.min`, `.max` | Required inclusive signed 64-bit bounds for an integer |
| `parameter.<name>.max_bytes` | Required UTF-8 byte limit from 1 to 512 for a string |
| `locale_fallback.<requested>` | Direct target locale with both title and body registered |

The following is a whole alternative definition for a configured package/app,
not an extra variable to append to the scope/purpose/recipient template above.
Its only placeholder is days, and its only declared variable is days. A matching
requirement uses definition calendar.days, operation read, scope "30", purpose
answer-calendar, recipient conversation and policy_version "1". Its subject and
profile must match the delegated context shown here.

```json
{
  "subject": "configured.subject",
  "profile": "configured.profile.A",
  "operation_id": "register-calendar-days",
  "expected_generation": "<trusted Installer generation>",
  "definition": "calendar.days",
  "enforcer": "ce",
  "policy_version": "1",
  "text_revision": "1",
  "level": "1",
  "modes": "ONCE,SESSION,TIMED,PERSISTENT",
  "retention_ms": "60000",
  "default_locale": "en",
  "message.en.title": "Read calendar history",
  "message.en.body": "Read the last {days} days?",
  "template_version": "1",
  "parameter.days.type": "integer",
  "parameter.days.source": "scope",
  "parameter.days.min": "1",
  "parameter.days.max": "365"
}
```
Pass the actual package/app separately; enroll the Installer and CE for those
identities. This JSON illustrates C params, not a loader or provisioned product.

Typed requests and checks reject a missing or stale policy version. Do not
send `display_args` or
`r0.arg*` fields; the daemon rejects caller-supplied display arguments.
Changing parameter types, bounds or sources requires a new policy version.

At most eight variables are allowed, each named by a 1–32 character ASCII
identifier. Every typed locale needs title and body; the union of their
placeholders must exactly match the declared variable names. Each template
is limited to 4,096 UTF-8 bytes and each rendered field to 8,192 bytes.
Integers must use canonical decimal form: `30` is valid, `030`, `+30`,
`30.0`, overflow and out-of-range values are rejected. Invalid registration,
missing translations or argument errors never imply approval.

The UI explicitly sets `template_version=1` and its requested `locale` when
fetching typed prompts. The response's top-level `locale` is the requested
locale; each `rN.locale` identifies the selected registered translation.
Use `consent_prompt_format(result, index, "title", &text)` or the same call
with `"body"` for a returned requirement. On success, `text` is an allocated
UTF-8 string owned by the caller; release it with `free()`. On failure, the
output is NULL and the UI must not proceed as if formatting succeeded. The
formatter processes the template in one pass and never interprets substituted
text as another template or markup. Render the result as plain text.
Invalid prompt fields, indices or arguments return
`CONSENT_ERROR_INVALID_PARAMETER`; allocation failure returns
`CONSENT_ERROR_OUT_OF_MEMORY`.

```c
#include <consent.h>
#include <stdio.h>
#include <stdlib.h>

int print_prompt_body(const consent_result_t *prompt, unsigned int index) {
  char *text = NULL;
  int status = consent_prompt_format(prompt, index, "body", &text);
  if (status != 0)
    return status;
  puts(text);
  free(text);
  return 0;
}
```

Locale selection first uses an exact registered translation, then an explicit
alias directly to a registered locale. The supported `ko-KR` to `ko` and
`en-US`/`en-GB` to `en` fallbacks follow, then the registered default locale.
Aliases cannot chain or form cycles. Script and region subtags are not
generically stripped. Registration rejects an alias whose source already has
a complete registered translation. A typed approval response must echo the
top-level
requested `locale` and current `prompt_token`. Refetch after a locale change;
an old
token cannot authorize the newly displayed prompt. Changes to aliases require
a new text revision and invalidate pending prompts.

---

[Previous](01-start.en.md) · [Next](03-request-and-check.en.md) · [API
overview](../02-c-api.en.md)
