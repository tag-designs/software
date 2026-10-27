---
type: readme
status: current
summary: tagcore_c, an experimental C interface to the C++ SQLite log writer for Rust and other languages - the tlw_* functions, the shared and static builds, symbol hiding, and how it was checked.
---

# tagcore_c (C interface)

`tagcore_c` lets a program in another language write tag SQLite logs with
the C++ `SqliteTagLogWriter`, so there is one writer and one schema. Every
message crosses as its protobuf wire encoding, and the caller interprets no
log format. The first caller is the Rust `tag-dwnld`, through
[`taglogwriter-rs`](../taglogwriter-rs/README.md). The interface and its
contracts are in [`taglogwriter_c.h`](taglogwriter_c.h); how the layers fit,
how the libraries are built and what to keep in step are in
[design/architecture.md](design/architecture.md). Why it exists, and
what else could go behind it, is in the
[proposal](../tagcore/design/proposals/rust-over-cpp-libraries.md).

**Experimental:** it is built only with `-DBUILD_TAGCORE_C=ON`, and it is not
installed or packaged.

## Builds

| Target | Output in `<build>/<Config>/lib` | Platforms |
| --- | --- | --- |
| `tagcore_c_static` | `libtagcore_c.a`: **the link on macOS**, where each application is its own bundle | macOS |
| `tagcore_c` | `libtagcore_c.so`, or a DLL and its import library: **the link on Linux and Windows**, where one copy in the install directory serves every tool (also builds as a `.dylib`) | all (checked on macOS) |

Both contain the shim, `tagcore`, the generated protobuf code, libprotobuf
with abseil, and SQLite, and both expose only the `tlw_*` functions:

- **Shared:** an export list (`exports.macos`, `exports.linux`) hides
  everything else; a Windows DLL exports only what `TLW_API` marks.
- **Static:** [`static/CMakeLists.txt`](static/CMakeLists.txt) pre-links the
  same inputs with `c++ -r` and the export list, so the archive holds one
  object in which everything but `tlw_*` is local. A caller links it plus
  the C++ runtime and the system libraries it calls (`c++`, `objc`,
  CoreFoundation, IOKit and Security on macOS).

Hiding matters because a caller may have its own SQLite or protobuf; with
the symbols hidden, the copies cannot collide or silently share state.

Two caveats, both caused by linking `tagcore` whole. The writer's
`writeHeader(Tag &)` references `Tag`, which brings in the link layer and
libusb. That code is never called, but it adds size. In the static build,
five libusb common symbols (`usbi_default_context` and its neighbours) also
stay global, because Apple's linker no longer has `-d` to turn them into
definitions. Moving `writeHeader(Tag &)` and `readTagLogHeader()` out of the
writer's files would remove libusb from both builds.

## Checking

The `log-import` example in `taglogwriter-rs` repeats `tag-log-import`
through this interface. On a tagsim IMUTag download (25 pages, a resync
segment and NaN gaps), the database it writes is byte-identical to
`tag-log-import`'s, with both the shared and the static build.

On hardware, a one-minute IMUTag run was downloaded with the C++
`tag-dwnld` and with the Rust one, linked both ways. All three databases were
byte-identical (43 pages; see the proposal).
