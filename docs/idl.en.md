# Bounded Parcel IDL and compiler

The source of truth is `src/protocol/consent.idl.json`. CMake runs the Python
standard-library-only `src/tools/parcel_codegen.py` to generate
`consent_wire.hh` in the build directory. Both endpoints include the same
native Tizen `Parcelable` classes. Generated files are not edited or installed
as the public C ABI. Python is a build/test dependency only.

```sh
python3 src/tools/parcel_codegen.py src/protocol/consent.idl.json /tmp/consent_wire.hh
python3 src/tests/idl_codegen_test.py
```

The top-level JSON object contains exactly `license`, `namespace`, `records`.
`license` stores the supported complete Apache notice as valid JSON metadata.
Records have `name` and `fields`; array order is wire field order. A record may
reference only an earlier record, rejecting cycles and forward references.
Duplicate JSON keys, unknown constraints, reserved C++/helper names and
out-of-range defaults are errors; the compiler does not evaluate source code,
imports or templates from the schema.

| Field type | Properties beyond name/type | Encoding |
|---|---|---|
| u32 | optional default, 0..2^32-1 | four-byte BE unsigned |
| i32 | optional default, -2^31..2^31-1 | four-byte BE signed |
| u64 | optional default, 0..2^64-1 | eight-byte BE unsigned |
| string | required max_bytes; optional min_bytes | BE u32 length including final NUL, UTF-8 bytes, NUL |
| earlier record name | none | fields inline in declaration order |
| array | earlier record `element`, required max_count | BE u32 count followed by record elements |

This small grammar intentionally has no float, union, service/RPC generation,
import, arbitrary code or unbounded types. Namespace/identifiers are validated;
record names begin uppercase and field names lowercase. There are at most
64 records, 64 fields per record, nesting depth16 and array count4096. Strings
have configured bounds no larger than65536 bytes. Transitive minimum wire and
estimated fixed layout cannot exceed65536 bytes; transitive maximum wire and
heap budgets cannot exceed16MiB. These compile-time schema budgets supplement
the stricter runtime 65536-byte total frame limit. Fixed layout estimates are
conservative budgeting, not a claim about all C++ compiler ABIs.

Generated readers check primitive availability and bounded string length before
allocation, final NUL, embedded NUL and UTF-8. Array allocation requires both
count<=max_count and enough remaining bytes for count times the element's
minimum encoded size. Decode errors set explicit validity; callers check it
because native `ReadParcelable()` does not propagate the derived reader's
failure. Runtime frames must also consume all bytes and pass envelope semantics.

The present schema defines Field(key,value) and Envelope(version,kind,
correlation,method,status,fields). Integer endian, test vector, correlation,
frame limits and domain parameter representation are specified in
[the protocol guide](protocol.en.md). Record generation does not infer role,
policy or domain semantics from field names; those remain validated by the
server and repository.

Output is deterministic UTF-8 with a complete license and no timestamps or
source paths. The compiler writes through a temporary file and atomic rename.
Regression tests cover valid generation, repeated byte-identical output,
malformed schemas, helper/type-name collisions and exponential layout growth.
Native Parcel roundtrip/truncation/oversize tests are in client-test and the
manual daemon wire fixture. The storage registry uses a separate explicitly
versioned internal codec; it is not an IPC envelope or an alternative wire.
