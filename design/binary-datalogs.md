# Shared Binary Log Formats Design

This document details the recommended design and naming conventions for sharing binary log formats between host applications and embedded tag firmware.

## Background & Problem Statement

Historically, host applications (`host/applications/` and `host/commandline/`) and embedded tags (`embedded/tags/`) communicated through protocol buffer messages defined in [**`proto/`**](file:///proto). However, for high-bandwidth telemetry tags (such as `IMUTag`), serialization overhead and protocol buffer sizing make raw binary log formats necessary. 

Without a shared definition, an "air gap" is created between the host decoding code and the tag firmware:
* The tag firmware structures memory based on a private C `struct`.
* The host tool manually decodes the binary block using magic offsets and manual byte parsing (e.g. `readLeI16(block + offset)`).

To prevent this air gap, binary formats should be defined by a single, shared C/C++ data structure.

---

## 1. Storage Location for Shared C Types

All shared binary log formats must be stored in the top-level [**`include/`**](file:///include) directory at the repository root.

```text
repository-root/
├── include/
│   ├── imutag_log_format.h      <-- Shared formats (structs, constants)
│   ├── btdataviz_log_format.h
│   └── ...
├── embedded/
│   └── tags/
│       └── IMUTagBreakout/
│           └── inc/datalog.h    <-- Embedded-only log implementation
└── host/
    └── libraries/
        └── tagcore/             <-- Host-side log decoding
```

### Why this location?
* **Decoupled Dependencies:** It prevents the host code from depending on paths inside `embedded/`, and the firmware code from depending on paths inside `host/`.
* **Universal Compiler Access:** The top-level `include/` path is already configured in the compiler search paths for both the ARM GCC embedded toolchain and the host application compilers.

---

## 2. Naming Conventions

To avoid name collisions on the host side (which may include multiple tag format headers in a single compilation unit), follow these naming rules:

1. **File Names:** Name the file `<tag_family>_log_format.h` in lowercase (e.g., `imutag_log_format.h`).
2. **Struct Names:** Prefix all structures with the tag family name to guarantee uniqueness:
   * Data log format: `t_<TagFamily>DataLog` (e.g., `t_ImuTagDataLog`)
   * Header format: `t_<TagFamily>DataHeader` (e.g., `t_ImuTagDataHeader`)
3. **Firmware Local Aliasing:** Inside the tag's private `datalog.h` (e.g., `embedded/tags/<Tag>/inc/datalog.h`), alias the shared structure to `t_DataLog` and `t_DataHeader`. This allows general firmware code to remain generic:
   ```c
   #include "imutag_log_format.h"
   typedef t_ImuTagDataLog t_DataLog;
   typedef t_ImuTagDataHeader t_DataHeader;
   ```

---

## 3. Structure Portability & Serialization Rules

To guarantee that the host and embedded compilers align and size the structures identically, apply these rules to all shared binary format headers:

### A. Use Fixed-Width Integers
Exclusively use standard types from `<stdint.h>` (e.g., `int16_t`, `int32_t`, `uint32_t`). Never use `int`, `long`, or `short`.

### B. Force Explicit Packing
Prevent compilers from inserting platform-dependent padding bytes by wrapping the structures in packing pragmas:

```c
#pragma pack(push, 1)

typedef struct {
    int32_t mx, my, mz;
    int32_t pressure;
    int16_t pressure_temperature;
    uint8_t reserved[12];
    RawSensorData raw_data[8];
    uint16_t flags;
} t_ImuTagDataLog;

#pragma pack(pop)
```

### C. Assert Structure Size
Verify that structural alignment matches your design targets on both compilers at compile-time:
* **Embedded (C):** `static_assert(sizeof(t_ImuTagDataLog) == 128, "IMUTag log block size must be exactly 128 bytes!");`
* **Host (C++):** `static_assert(sizeof(t_ImuTagDataLog) == 128, "IMUTag log block size must be exactly 128 bytes!");`

### D. Verify Endianness at Compile-Time
Since raw binary logs are written directly from memory in the MCU's native byte-order (little-endian) and read directly by the host, we must guard against compiling for any big-endian architectures. 

Add the following preprocessor assertions to the top of all shared header files to enforce little-endian compilation:

```c
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
    #error "This binary logging layout only supports little-endian architectures."
  #endif
#elif defined(__BIG_ENDIAN__)
  #error "This binary logging layout only supports little-endian architectures."
#endif
```

### E. Use C-Compatible Syntax
Ensure that shared files compile successfully in both C (embedded) and C++ (host) contexts. Wrap any function declarations or C++ symbols in `#ifdef __cplusplus` guards:

```c
#ifdef __cplusplus
extern "C" {
#endif

// Shared C declarations here...

#ifdef __cplusplus
}
#endif
```

---

## 4. Coherence with Nanopb Options

When raw binary logs are downloaded or transmitted, they are wrapped in Protobuf messages (e.g., `IMUTagRawLog` carrying the raw bytes from one page). In embedded firmware, nanopb constrains these byte fields to fixed sizes via `.options` files (e.g., `IMUTagRawLog.samples max_size:2048` in `embedded/proto-c/default-options/tagdata.options`). The tag may transmit fewer than `DATALOG_SAMPLES` blocks when the footer flags show that the tail of the external page is still erased.

To ensure these option limits do not fall out of sync with the actual C layout sizes (which would lead to buffer overflows, truncated logs, or wasted RAM):

### Assert buffer size equality in the code
Add a compile-time check in the tag's firmware source files (where the logs are written or encoded) to verify that the generated nanopb struct array size matches the expected binary page size:

```c
#include "tagdata.pb.h"         // nanopb generated header
#include "imutag_log_format.h"  // Shared C types

// Assert that the nanopb buffer is exactly sized for our layout:
static_assert(sizeof(((IMUTagRawLog*)0)->samples) == sizeof(t_ImuTagDataLog) * DATALOG_SAMPLES,
              "Nanopb IMUTagRawLog.samples limit in tagdata.options is out of sync with the C struct size!");
```

This acts as a compile-time bridge, preventing a build from completing if someone modifies the options file or structure layout without updating the other.

## 5. Recommended Host Side Decoding

### A. Communicating the Number of Raw Buffers

Since the binary structures are now shared, the host application knows the
exact compile-time size of a single buffer ( sizeof(t_ImuTagDataLog) ).

Therefore, you do not need a separate field in the Protobuf message to
communicate the buffer count. The host can dynamically and safely compute
the count from the length of the received bytes/string payload:

```c
  // On the host side:                                                     
  const std::string& payload = log.samples();                              
                                                                            
  // Calculate the number of blocks sent in this message                   
  size_t num_blocks = payload.size() / sizeof(t_ImuTagDataLog);  
```          

This is robust against future changes to the page size or sample counts
since it automatically adjusts to the payload length.

### B. Casting vs. Copying (Alignment & Strict Aliasing)

While the shared struct is packed ( #pragma pack(push, 1) ), which
technically reduces its alignment requirement to 1 byte, direct casting is 
highly discouraged on the host application side for two reasons:

1. Strict Aliasing Violations: Reinterpreting a  std::string  or  char* 
buffer directly as a  t_ImuTagDataLog*  violates C++ strict aliasing rules.
Compilers (like GCC, Clang, or MSVC) can optimize code under the assumption
that these pointers do not alias, leading to silent, hard-to-debug runtime
bugs.
2. Unaligned Access Overhead: Even though modern host CPUs (x86_64, arm64,
and Apple Silicon) handle unaligned loads/stores in hardware, unaligned
access can still cross cache lines, resulting in a performance penalty.

### C. The Best Way to Handle Alignment

The most standard-compliant, safe, and performant way to decode the raw
bytes is to copy them into a type-safe, naturally aligned container using  
std::memcpy .

Modern C++ compilers recognize  std::memcpy  as a compiler intrinsic. They
will optimize the copy into direct CPU register moves, completely eliding
the overhead of an actual library call.

### D. Recommended Host C++ Pattern

Use a  
```c
std::vector<t_ImuTagDataLog>```
```
The vector's underlying buffer is
guaranteed by the C++ allocator to be properly aligned for the type:

```c

// 1. Calculate count   

size_t num_blocks = payload.size() / sizeof(t_ImuTagDataLog);            
                                                                          
// 2. Allocate vector (guarantees memory is aligned for t_ImuTagDataLog) 

std::vector<t_ImuTagDataLog> blocks(num_blocks);                         
                                                                          
// 3. Copy payload (highly optimized by the compiler, avoids aliasing issues)    
                                                              
std::memcpy(blocks.data(), payload.data(), payload.size());              
                                                                          
// 4. Access safely and cleanly                                          
for (size_t i = 0; i < num_blocks; ++i) {
    int32_t p = blocks[i].pressure;
    int32_t mx = blocks[i].mx;
    // ...
}
```
