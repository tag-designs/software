---
type: decision
status: accepted
summary: Raw binary log records shared by host and firmware are defined once, as packed C structs in the top-level include/ directory, rather than decoded on the host by hand-written offsets.
---

# 0001. Shared: binary log records are packed C structs in include/

Date: 2026-06-12

High-bandwidth tags write raw binary log records rather than protobuf
messages, and both sides now compile the same packed C struct from the
top-level `include/` directory. The conventions that follow from it are in
[Shared Binary Log Formats](../shared/binary-datalogs.md). Extracted verbatim
from that document.

## Context

Historically, host applications (`host/applications/` and `host/commandline/`) and embedded tags (`embedded/tags/`) communicated through protocol buffer messages defined in [**`proto/`**](../../proto/README.md). However, for high-bandwidth telemetry tags (such as `IMUTag`), serialization overhead and protocol buffer sizing make raw binary log formats necessary. 

Without a shared definition, an "air gap" is created between the host decoding code and the tag firmware:
* The tag firmware structures memory based on a private C `struct`.
* The host tool manually decodes the binary block using magic offsets and manual byte parsing (e.g. `readLeI16(block + offset)`).

To prevent this air gap, binary formats should be defined by a single, shared C/C++ data structure.

## Decision: why `include/`

* **Decoupled Dependencies:** It prevents the host code from depending on paths inside `embedded/`, and the firmware code from depending on paths inside `host/`.
* **Universal Compiler Access:** The top-level `include/` path is already configured in the compiler search paths for both the ARM GCC embedded toolchain and the host application compilers.
