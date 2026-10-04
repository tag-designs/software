---
type: design
status: current
summary: Conventions for the packed C log structs in include/ that host and firmware both compile: naming, packing, size and endianness checks, nanopb buffer coherence, and copy-not-cast host decoding.
---

# Shared Binary Log Formats

A tag that writes raw binary log records defines each record once, as a packed
C struct in a header under the top-level [`include/`](../../include) directory,
and both the firmware and the host decoders compile that header. The headers in
use are `imutag_log_format.h`, `prestag_log_format.h` and
`uiuctag_log_format.h`; each carries Doxygen for its own fields. Why the formats
are shared this way, and why in `include/`, is
[decision 0001](../decisions/0001-shared-binary-log-structs-in-include.md).

## Where the pieces live

```text
include/<family>_log_format.h          shared structs and constants
embedded/tags/.../inc/datalog.h        firmware log implementation; aliases the shared types
host/libraries/tagcore/sqlitelog/      host decoders (imutag.cc, pressure.cc, uiuctag.cc)
```

## Naming

Host code may include several format headers in one translation unit, so names
must not collide:

1. **File:** `<tag_family>_log_format.h`, lowercase (`imutag_log_format.h`).
2. **Structs:** prefixed with the family: `t_<TagFamily>DataLog`,
   `t_<TagFamily>DataHeader`, and so on (`t_ImuTagDataLog`, `t_PresTagRawBlock`,
   `t_UIUCTagSample`).
3. **Firmware alias:** the tag's private `datalog.h` aliases the shared type to
   the generic name the common code uses:

   ```c
   #include "imutag_log_format.h"
   typedef t_ImuTagDataLog t_DataLog;
   typedef t_ImuTagDataHeader t_DataHeader;
   ```

## Portability rules for a format header

- **Fixed-width integers only** (`int16_t`, `uint32_t`, ...). Never `int`,
  `long` or `short`.
- **Explicit packing.** Wrap the structs in `#pragma pack(push, 1)` /
  `#pragma pack(pop)` so neither compiler inserts padding.
- **Little-endian only.** Records are written from MCU memory in native byte
  order and read directly by the host, so each header refuses a big-endian
  compile:

  ```c
  #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    #if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
      #error "This binary logging layout only supports little-endian architectures."
    #endif
  #elif defined(__BIG_ENDIAN__)
    #error "This binary logging layout only supports little-endian architectures."
  #endif
  ```

- **C and C++ compatible.** Wrap any function declarations in
  `extern "C"` under `#ifdef __cplusplus`.
- **Assert sizes at compile time**, on the firmware side at least, against the
  page or block size the layout is designed for, for example
  `static_assert(sizeof(t_DataLog) == IMUTAG_PAGE_SIZE, ...)` in the IMUTag
  `datalog.h`. Layouts the offline rebuild decodes are also asserted next to
  the type with a message naming `capturesource.cc`; see the
  [shared contracts](README.md).

## Coherence with nanopb options

Raw records travel to the host inside protobuf messages, as a bytes field
(`IMUTagRawLog.samples`, `PresTagRawLog.samples`, `UIUCTagLog.samples`) whose
size nanopb fixes from `embedded/proto-c/default-options/tagdata.options`. If
that limit drifts from the C layout, the result is a buffer overflow, truncated
logs or wasted RAM. The firmware therefore asserts the relationship where it
encodes the message:

```c
static_assert(sizeof(((IMUTagRawLog*)0)->samples.bytes) == DATALOG_SAMPLES * sizeof(t_DataLog),
              "...");
```

(`families/IMUTag/src/datalog.c`; the PresTag family has the equivalent `>=`
check against `t_PresTagRawBlock`, compiled only when `PRESTAG_RAW_LOG` is set,
as it is for PresTagRaw.) A tag may send fewer records than the buffer holds:
UIUCTag trims trailing never-written sample slots from a block before sending it
(`UIUCTag/src/datalog.c`).

## Host decoding

- **Count records from the payload length**, `payload.size() / sizeof(T)`; the
  message carries no separate count. Reject a payload that is not a whole
  multiple of the record size.
- **Copy, do not cast.** Reinterpreting a `std::string` or `char*` buffer as
  `T*` violates C++ strict aliasing, and unaligned access still costs on hosts
  that permit it. `std::memcpy` each record (or the whole payload into a
  `std::vector<T>`) into naturally aligned storage; compilers lower it to
  register moves. `sqlitelog/pressure.cc` and `sqlitelog/uiuctag.cc` decode
  this way.
