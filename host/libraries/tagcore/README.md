---
type: readme
status: current
summary: tagcore responsibilities - USB and monitor communication, log writers, configuration shapes, SWD capture and recovery code, offline checks - and links to its design documents.
---

# TagCore Library

`tagcore` contains the low-level host interface. The CMake target is also named
`tagcore`, so the library name matches the directory and its role.

Responsibilities:

- USB/tag communication: `tagclass.*`, `tagmonitor.*`, `linkadapt.*`
  (a Rust port of these three layers is in
  [`../tagcore-rs`](../tagcore-rs/README.md))
- Log writing and storage interfaces: `taglogwriter.*`, `txtlogs.*`,
  `sqlitelog.*`
- Host logging helpers: `log.*`
- Which configuration fields each tag type supports, and merging a
  configuration file into a tag's configuration: `configshape.*`. The shapes
  come from the tags' `embedded/proto-c/*-proto-c/default-config.json`, which
  this directory's `CMakeLists.txt` embeds as text in a generated
  `defaultconfigs.cc`.
- Shared protocol-facing definitions used by CLI tools and Qt apps

SQLite log writing is split between the public `sqlitelog.*` wrapper and the
private `sqlitelog/` implementation directory. `sqlitelog/schema.cc` owns the
table and stream metadata, while the other files in that directory decode
individual tag log protobufs into rows. The tables and columns a log contains
are documented for users in
[SQLite Log Format](../../docs/src/reference/sqlite-logs.md), which is the
authoritative schema; [`sqlitelog/README.md`](sqlitelog/README.md) covers the
writer internals and decoder rules behind them.

`recovery/` holds what is left in C++ of the SWD capture and recovery code.
The capture itself (the session, the loaders, `tag-capture`, `tag-xflash`,
`tag-sramcall`) is Rust, in tagcore-rs's `recovery` module; see
[SWD Capture and Recovery Library](../tagcore-rs/design/swd-recovery.md). What
stays:

- `capturesource`: rebuilds a download from a capture, behind the Rust
  `tag-rebuild`, which reaches it through `rbd_rebuild()` in `tagcore_c`;
- `identityrecord`: parses the tag identity record, for `capturesource` and
  the legacy qtprogram;
- `targetimage`: reads an ELF's loadable segments, for the legacy qtprogram;
- `sha256`: hashes, for `capturesource`.

`test/` holds standalone offline checks, of the decoders and of `ConfigShape`;
see
[`test/README.md`](test/README.md).

Design documents:

- [Tag Configuration Model](../../../docs/shared/design/configuration-model.md):
  which configuration fields each tag supports, `ConfigShape`, the merge used
  by Restore and `tag-start --merge`, and how it is checked.
- [SWD Capture and Recovery Library](../tagcore-rs/design/swd-recovery.md),
  now in tagcore-rs: capture without booting the firmware, external flash
  through the loaders, tag identification, and the layered API. The C++
  original's bring-up log stays here, in
  [`design/investigations/2026-09-swd-capture-library-bring-up.md`](design/investigations/2026-09-swd-capture-library-bring-up.md);
  open work, `CaptureSource`'s included, is in the
  [tagcore-rs TODO](../../../TODO.md#tagcore-rs).
- [`design/proposals/python-interface.md`](design/proposals/python-interface.md):
  proposed Python binding API, native/protobuf boundary, shared download
  service, packaging, and testing plan. No binding exists yet.
- [`design/proposals/tag-server.md`](design/proposals/tag-server.md):
  proposed in-process simulated tag for testing the applications and tools,
  and a standalone gRPC tag server, with a memory read/write side channel, for
  Python tests. Its cost measurements are in
  [`design/investigations/2026-10-grpc-cost.md`](design/investigations/2026-10-grpc-cost.md).

This library should remain Qt-free. Qt applications can link it, but reusable
Qt UI code belongs in `../sensorui` or `../../common`.
