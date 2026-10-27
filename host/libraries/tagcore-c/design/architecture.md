---
type: design
status: current
summary: How Rust tools write tag SQLite logs through the C++ writer - the layers (tag-dwnld, taglogwriter-rs, tagcore_c, SqliteTagLogWriter), the tlw_* contract, how the static and shared libraries are built and linked per platform, the Rust safety model, and what a maintainer must keep in step.
---

# tagcore_c and taglogwriter: architecture

**Rust tools write SQLite logs with the C++ writer, not a Rust copy of it.**
The Rust side encodes protobuf messages with prost. It hands the bytes to
`tagcore_c`, a small C interface over the C++ `SqliteTagLogWriter`. The C
side parses them and makes the same writer calls the C++ tools make. The
database is therefore the one the C++ tools write: on the IMUTag and UIUCTag
downloads checked so far it was byte-identical. There is one schema and one
set of decoders, and the Rust side interprets no log format.

The library is linked statically on macOS and as a shared library on Linux
and Windows. Why Rust uses the C++ libraries at all, and what else could go
behind this interface, is in the
[proposal](../../tagcore/design/proposals/rust-over-cpp-libraries.md).

## Layers

```
tag-dwnld (host/commandline-rs/src/bin/tag-dwnld.rs)
 ├─ tagcore-rs ── Tag: attach, status, header reads, data pages (prost messages)
 └─ taglogwriter-rs ── LogWriter: safe Rust; encodes each message (prost)
     └─ tagcore_c (C ABI: tlw_*) ── parses the bytes (C++ protobuf)
         └─ SqliteTagLogWriter (host/libraries/tagcore) ── schema, decoders, SQLite
```

| Layer | Files | Owns |
| --- | --- | --- |
| Tool | `host/commandline-rs/src/bin/tag-dwnld.rs` | Options, the tag state checks, the header reads, the page loop, progress |
| Rust wrapper | `host/libraries/taglogwriter-rs/src/lib.rs`, `build.rs` | `LogWriter`, `LogHeader`, `WriterError`; all of the `unsafe`; choosing and linking the library |
| C interface | `host/libraries/tagcore-c/taglogwriter_c.h`, `taglogwriter_c.cc` | The `tlw_*` functions and their contract; exception and error handling at the boundary |
| Library builds | `host/libraries/tagcore-c/CMakeLists.txt`, `static/CMakeLists.txt`, `exports.*` | The shared and static libraries, and which symbols they expose |
| Writer | `host/libraries/tagcore/sqlitelog*` | Everything about the log format |

The tag side never crosses the C boundary: tagcore-rs reads the tag in Rust.
Only the writer is C++.

## The contract

The header's Doxygen is the authority; this is its shape.

| Call | Does |
| --- | --- |
| `tlw_abi_version()` | The interface version the library was built with |
| `tlw_set_debug(on)` | The C++ writer's log level, process-wide, as `-d` sets it for the C++ tools |
| `tlw_open(path, config, len, replace)` | Creates the database for the config's tag type; returns a handle, closed on failure |
| `tlw_is_open(w)` | Whether `tlw_open` succeeded |
| `tlw_header_add(w, kind, msg, len)` | Adds one header part: `TLW_CONFIG`, `TLW_INFO`, `TLW_CALIBRATION` or `TLW_STATE` |
| `tlw_write_header(w)` | Writes the gathered header (`writeHeader(const TagLogHeader &)`) |
| `tlw_write_info(w, field, value)` | One more info row |
| `tlw_begin_log(w)`, `tlw_end_log(w)` | The transaction the pages are written in |
| `tlw_write_log(w, ack, len)` | Decodes one page; returns the records it consumed, 0 at the end of the log, negative on error |
| `tlw_last_error(w)` | Why the last failing call failed |
| `tlw_close(w)` | Closes the database and frees the handle |

The rules behind it:

- **Messages cross as protobuf wire bytes.** Both sides compile them from
  the same `proto/` files, so the encoding is the whole contract: no
  conversion of floats or byte fields, no JSON, no second copy of a schema.
- **The header crosses in parts.** It has no protobuf message of its own
  (`TagLogHeader` is a C++ struct), so the handle collects the parts and
  writes them together. `proto/` does not change for this interface, which
  matters because the firmware's nanopb build shares it.
- **Each page is one synchronous call, and its answer drives the
  download.** How many records a page holds, and which page ends the log,
  depend on the tag type and are decided by the decoders. The tool cannot
  request the next page until `tlw_write_log` returns. This is why the
  writer is a library call, not a process fed a stream.
- **Status codes, not exceptions.** Calls return 1 or 0 (or a count), and
  `tlw_last_error()` says why. Every entry point catches every C++ exception
  and reports it as a failure. `tlw_open` returns a handle even on failure,
  so there is somewhere to keep the error; only running out of memory gives
  NULL.
- **Caller-owned memory is borrowed for the call only.** The library copies
  what it keeps. Its own strings (`tlw_last_error`) live until the next call
  on that handle.
- **A version check comes first.** `TLW_ABI_VERSION` is raised whenever a
  function's signature or meaning changes. The Rust wrapper refuses a
  library with a different version.
- **One handle, one thread at a time.** Handles are independent of each
  other.

## How the library is built

`BUILD_TAGCORE_C=ON` adds both targets. Neither is installed or packaged yet.

**What goes in.** Both builds link the shim with `libtagcore.a`, the
generated protobuf code, libprotobuf and its abseil libraries, and SQLite:
87 archives from vcpkg on macOS. CMake works out that list as it does for any
C++ tool, so neither build lists it by hand. `tagcore` is linked whole, so
the writer's `writeHeader(Tag &)`, which reads the header from a live tag,
also brings in the link layer and libusb. That code is never called. Moving
`writeHeader(Tag &)` and `readTagLogHeader()` out of the writer's files would
remove it (see the proposal).

**What comes out.** Only the `tlw_*` functions are visible, in both builds.
A caller can have its own SQLite or protobuf without the two copies
colliding or silently sharing state.

| | `tagcore_c` (shared) | `tagcore_c_static` |
| --- | --- | --- |
| Output | `libtagcore_c.so`, a DLL, or `.dylib` | `libtagcore_c.a` (macOS) |
| Hiding | `exports.linux` (version script) or `exports.macos`; on Windows only `TLW_API` (`dllexport`) functions are exported | the shim and all 87 archives pre-linked into one object with `c++ -r` and `exports.macos`, so everything else becomes local |
| How | an ordinary `SHARED` target with `-dead_strip` / `--gc-sections` | `static/CMakeLists.txt` redefines the `MODULE` link rule as `c++ -r` for its one target, so the pre-link reuses CMake's own link line; then `libtool -static` wraps the object |
| Caller links | the library | the archive, plus `c++`, `objc`, CoreFoundation, IOKit and Security (the last four for libusb) |

The pre-link rule is set in its own directory because CMake rule variables
are per directory: a separate subdirectory confines it to that target.

**Platform choice.** On macOS each application is its own `.app` bundle and
would carry its own copy of a shared library anyway. So macOS links
statically, and the executable needs nothing at run time, as Tag Programmer
ships. On Linux and Windows the tools share one install directory, so one
shared library serves them all, and each executable stays small. A shared
library also keeps the C++ dependencies hidden on Windows, where a static
archive could not.

**Known wrinkle.** Apple's current linker has no `-d` option, so in the
static build five libusb "common" symbols (`usbi_default_context` and its
neighbours) stay global. A Rust program that also links libusb, as
tagcore-rs does, shares those variables with the copy inside the archive.
That copy never runs, so nothing touches them twice. The split above would
remove them.

## The Rust side

**Linking (`taglogwriter-rs/build.rs`).**
- `TAGCORE_C_LIB_DIR` names the directory CMake built into
  (`<build>/<Config>/lib`).
- `TAGCORE_C_LINK` overrides the platform default: `static` on macOS,
  `dylib` elsewhere.
- For the shared library, the crate's `links = "tagcore_c"` key passes the
  directory to dependents as `DEP_TAGCORE_C_LIB_DIR`. That is needed because
  only the final binary's package can give that binary an rpath;
  `commandline-rs/build.rs` does it for `tag-dwnld`.
- A static build reruns when `libtagcore_c.a` changes.

**The wrapper (`src/lib.rs`).** The `ffi` module declares the C functions
by hand from the header; with a dozen of them, `bindgen` would cost more
than it saves. `LogWriter` owns one handle:
- **Never null.** It is created by `create()`, which checks the interface
  version and `tlw_is_open`.
- **Closed exactly once,** in `Drop`. Pages since `begin_log` without an
  `end_log` are rolled back by the writer.
- **`Send` but not `Sync`,** and every writing method takes `&mut self`,
  which is the C side's one-thread-at-a-time rule.
- **Every `unsafe` block states its reason.** The crate rustdoc's "Safety
  model" section gives the three rules they rest on.

**The tool (`tag-dwnld.rs`).** A port of `dwnld.cc`. It reads the tag with
tagcore-rs and gathers the header with the same requests, in the same
order, as `readTagLogHeader()`. Then it runs the page loop on
`LogWriter::write_log`'s answer. It is built only with commandline-rs's
`sqlitelog` feature, so the other Rust tools still build with `cargo build`
alone. Its rustdoc lists where it differs from the C++.

## Keeping it in step

| When this changes | Do this |
| --- | --- |
| A `tlw_*` signature or meaning | Raise `TLW_ABI_VERSION` in the header and `ffi::TLW_ABI_VERSION` in the crate; update the `ffi` declaration and the safe method |
| A new `tlw_*` function | Mark it `TLW_API`; the export lists match `tlw_*`, so nothing else is needed for visibility. Declare it in `ffi` and wrap it |
| `tlw_header_kind` values | Keep `ffi::TLW_CONFIG` … `TLW_STATE` equal |
| `proto/` | Nothing in this interface. Rebuild both sides so prost and protoc see the same definitions |
| The writer (`sqlitelog*`) | Rebuild the library. A static build relinks into each Rust tool; a shared one only needs replacing |
| `dwnld.cc` | Make the same change in `tag-dwnld.rs` (see its "Maintaining" section) |

Hazards:

- **Don't let an exception cross the boundary.** Any new entry point goes
  through `guarded()` in `taglogwriter_c.cc`, or catches everything itself.
- **Don't keep a caller's pointer** beyond the call that received it.
- **Don't open one database file from two SQLite copies in one process.**
  The library's private SQLite and, say, a `rusqlite` in the same program do
  not see each other's POSIX locks.

## Checking

- **Offline.** `taglogwriter-rs/examples/log-import.rs` makes the same
  calls as `tag-log-import`. On the same JSONL input, given by the same path,
  the two databases are byte-identical (`cmp`).
- **On hardware.** Download one finished tag with the C++ `tag-dwnld` and the
  Rust one, and compare the files. Downloading does not change the tag. The
  results so far are recorded in the
  [proposal](../../tagcore/design/proposals/rust-over-cpp-libraries.md#prototype).
- **API documentation.** Doxygen over `host/libraries/tagcore-c`, which is
  among `docs/developer/Doxyfile.in`'s inputs, and `cargo doc --document-private-items` with
  warnings denied are both clean.
