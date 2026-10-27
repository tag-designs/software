---
type: proposal
status: proposed
summary: How Rust (or other non-Qt) host applications should use the C++ libraries they depend on - what is already Qt-free, one C interface library over them (prototyped for the SQLite writer with a Rust tag-dwnld), shared against static linking measured, and where leaving Qt actually costs.
---

# Rust host applications over the C++ libraries

This proposal measures what moving the host applications off Qt would cost.
Tag Programmer has already moved, as tagprogram-rs. The proposal looks at
the functions every application depends on, and how a Rust (or other non-Qt)
application should reach the C++ that implements them.

**Recommendation: keep the C++ libraries, and give them one C interface
library, `tagcore_c`.** Rust links it statically on macOS and as a shared
library on Linux and Windows. The libraries
applications depend on are already Qt-free C++. Wrapping them keeps one
implementation of each format and algorithm, and costs about 150 lines of
C++ and a Rust wrapper per library. The cost of leaving Qt is in the
applications' own UI code, plus one Qt-typed layer of sensoranalysis.

**Rust host applications may depend on the C++ libraries and tools.** A
C++-free Rust stack is not a goal (owner's decision, 2026-10-27), so
porting these libraries to Rust is not pursued.

The first piece is prototyped: `tagcore_c` over the SQLite log writer, and a
Rust `tag-dwnld` built on it (see [Prototype](#prototype)).

## What applications depend on

| Function | Today | Qt-free? | From Rust |
| --- | --- | --- | --- |
| Tag access | tagcore-rs | yes, native Rust | done (Tag Programmer, `commandline-rs`) |
| Writing logs | `tagcore/sqlitelog*`, about 2,600 lines | yes | `tagcore_c` (prototyped) |
| Reading logged streams | `sensordata`, about 1,470 lines | yes: needs only sqlite3, and builds without Qt by design | `tagcore_c`, next |
| Sensor maths | `sensoranalysis`: a 654-line Qt-free core under a 2,300-line layer on `QVector3D` and `QQuaternion` | the core only | the core through `tagcore_c` if needed; the layer must first leave Qt |

sensorviz's `core/` (view model, plot spec, stream metadata, 1,527 lines) is
also Qt-free already.

## Where leaving Qt costs

| Application | Lines (excluding tests and design docs) | Note |
| --- | --- | --- |
| sensorviz | 7,166 | its `core/` is Qt-free |
| qtmon | 6,599 | |
| qtcalibrate | 3,587 | |
| qtprogram | 4,953 | already replaced: tagprogram-rs is 2,311 lines plus the ui-kit |
| btdataviz | 2,905 | deprecated |

The libraries add one more item: sensoranalysis's outer layer would move
onto the core's own vector types (`core/vecmath.h`). That reverses an
earlier decision, that the layer's API stays Qt and the core stays
bit-identical to Qt's results. `compass_core_parity_check` exists to measure
exactly this move.

## One C interface library

`tagcore_c` (in `host/libraries/tagcore-c`) exposes Qt-free C++ through C
functions. Its rules:

- **Protobuf messages cross as wire bytes.** Rust encodes them with prost and
  the shim parses them with `ParseFromArray`, so neither side converts a
  float or interprets the other's data, and `proto/` does not change.
- **Plain data crosses as C types**: arrays into caller-provided buffers,
  and callbacks as a function pointer and a context pointer.
- **No C++ exception crosses the boundary.** Every entry point catches them
  and returns an error, with the text available from a `*_last_error()`
  call.
- **Only the C functions are visible.** libprotobuf, abseil and SQLite are
  linked inside with every other symbol hidden, so they cannot collide with a
  caller's own copies.
- **An interface version is checked first,** so a caller and a library built
  from different headers refuse each other.

### The writer (prototyped)

`taglogwriter_c.h` wraps `SqliteTagLogWriter`. One detail of the download
decides how the two languages must be connected:

```c++
len = writer->writeLog(ack);   // records this page consumed, 0 at end, <0 on error
total += len;                  // the next page requested is GetDataLog(total)
```

**The writer decides how far each page advances the download, and where it
ends.** Both depend on the tag type and are decided by the decoders. So the
writer must answer each page before the next request. A function call does
that naturally.

Two other ways were considered and rejected for this:

- **A separate writer process over a pipe.** It works, but adds a framing
  protocol, error mapping across processes, and a child process to look
  after.
- **A separate writer process over grpcxx, as `tag-server` uses.** grpcxx
  has no streaming, so this would be one unary call per page, about 65 µs
  over a Unix socket. tag-server is deliberately kept out of the release
  build: it needs `libprotoc`, CMake 3.28 and a network fetch at configure
  time. It also adds a server lifecycle, and tokio and tonic on the Rust
  side. It would earn its place only if Python test programs should write
  logs, and then as a call on `tag-server` itself.

A one-way stream of the same records is still useful as an input format for
`tag-log-import`: an exact, replayable record of a download.

### The reader (next)

`sensordata` passes easily through the same rules:

| Call | Crosses as |
| --- | --- |
| `open` | path in, handle out |
| `streams()`, `info()` | stream metadata copied into C structs |
| `view(stream, t0, t1, width)` | the decimated envelope, at most four points per pixel column, into a caller buffer |
| `valueAt`, `range` | plain values |
| `forEachRow`, `addComputed` | a C callback and a context pointer |

The data crossing per frame stays small, because the decimation happens on
the C++ side. Reading natively in Rust would be lower risk than writing,
since stored columns need no decoding. But the decimation pyramid and the
exact read-back rules are measured design work, and wrapping keeps one copy
of them.

## Shared or static

Both are built and both work; they produce byte-identical databases. The
numbers are from macOS arm64, release build:

| | Shared (`tagcore_c`) | Static (`tagcore_c_static`) |
| --- | --- | --- |
| What Rust links | `libtagcore_c.dylib` | `libtagcore_c.a`, plus `c++`, `objc`, CoreFoundation, IOKit, Security |
| `tag-dwnld` | 2.8 MB, plus the 3.5 MB library | 6.3 MB, self-contained |
| Run-time dependencies outside the OS | the library, found by rpath | none |
| Packaging | install the library, set the rpath, sign it on macOS, put the DLL beside the `.exe` on Windows | nothing beyond the executable |
| Symbol hiding | export list (macOS, Linux); `dllexport` (Windows) | pre-link with `c++ -r` and the export list (macOS); `ld -r` and `objcopy` would do it on Linux; Windows has no equivalent |
| CMake | an ordinary `SHARED` target | a pre-link rule set in its own directory, plus `libtool` |
| Several executables | share one copy | each carries its own |

**The link follows each platform's packaging** (owner's decision, 2026-10-27):

- **macOS: static.** Each application is its own `.app` bundle, so a shared
  library would be copied into every bundle and save nothing. A static
  executable also needs no rpath, no extra signed library and nothing at run
  time, as Tag Programmer ships today.
- **Linux and Windows: shared.** The tools share one install directory
  (`tag_tools/bin`, with `lib/` beside it on Linux), so one
  `libtagcore_c.so` or DLL serves every tool. Each executable stays small. A
  shared library also hides the C++ dependencies' symbols on Windows, which
  a static archive cannot.

## Prototype

Built and checked on macOS:

- `host/libraries/tagcore-c`: the `tlw_*` interface, both builds, behind
  `BUILD_TAGCORE_C` (off by default, not packaged).
- `host/libraries/taglogwriter-rs`: `LogWriter`, a safe wrapper;
  `TAGCORE_C_LINK` chooses the shared or static library.
- `host/commandline-rs` `tag-dwnld`: a port of `dwnld.cc`, behind the
  `sqlitelog` feature, so the other tools still build with cargo alone.
- **Checked offline:** `log-import`, the wrapper's example, repeats
  `tag-log-import`. On a tagsim IMUTag download (25 pages, a resync segment,
  NaN gaps), it writes a database byte-identical to `tag-log-import`'s, with
  both builds.
- **Checked on hardware (2026-10-27):** an IMUTagNandBmp581 (UUID
  `00303143433650090059002E`, fw `56e5e6a0`) ran for about a minute at
  100 Hz and was stopped. It was then downloaded three times: with the C++
  `tag-dwnld`, and with the Rust `tag-dwnld` linked shared and linked static.
  All three databases were byte-identical: 733,184 bytes, 43 pages, 6,450
  accelerometer and gyro samples, 645 pressure and magnetometer rows. Each
  took one to two seconds. A UIUCTag (breakout, UUID
  `2036354B3032500800520028`, 3.32 V) ran for six minutes the same day. Its
  three downloads were also byte-identical: 45,056 bytes, 2 records, with
  pressure, temperature, activity and voltage rows.

What the prototype leaves for a production version:

- **Split the writer from the tag.** `sqlitelog.cc` includes `tagclass.h`
  for `writeHeader(Tag&)`, so linking `tagcore` brings in the link layer and
  libusb. It is never called. In the static build it also leaves five libusb
  common symbols global, since Apple's linker no longer has `-d` to localise
  them. Move `writeHeader(Tag&)` and `readTagLogHeader()` into their own
  file, and build the writer sources as a library `tagcore` links.
- **The shared library on Linux and Windows**: checked so far only as a
  `.dylib`. Linux needs its vcpkg static libraries to be
  position-independent to go into a `.so` (to be checked on `x64-linux`),
  and both need install rules: `lib/` with an `$ORIGIN/../lib` rpath on
  Linux, the DLL beside the tools on Windows.
- **CMake drives cargo** for `tag-dwnld`, as it does for Tag Programmer, so
  a build sets `TAGCORE_C_LIB_DIR` itself.

## Plan

1. Repeat the hardware comparison on the remaining tag types the writer
   supports (done for IMUTag and UIUCTag).
2. Split the writer from `Tag`, and build and install the shared library on
   Linux and Windows.
3. Add the reader (`sensordata`) to `tagcore_c`, with a Rust wrapper and a
   check that a Rust view matches the C++ one.
4. Decide on sensoranalysis's Qt layer before any analysis code is needed
   from Rust.

## Questions for the owner

- Should the Rust `tag-dwnld` replace the C++ one, or ship beside it until
  both have produced matching databases on every tag type?
- Should sensoranalysis's Qt-typed layer move onto the core's types? It is
  the only library work that leaving Qt requires.
