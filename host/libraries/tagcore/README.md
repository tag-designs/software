---
type: readme
status: current
summary: tagcore responsibilities - USB and monitor communication, log writers, SWD capture and recovery code, offline decoder checks - and links to its design documents.
---

# TagCore Library

`tagcore` contains the low-level host interface. The CMake target is also named
`tagcore`, so the library name matches the directory and its role.

Responsibilities:

- USB/tag communication: `tagclass.*`, `tagmonitor.*`, `linkadapt.*`
- Log writing and storage interfaces: `taglogwriter.*`, `txtlogs.*`,
  `sqlitelog.*`
- Host logging helpers: `log.*`
- Shared protocol-facing definitions used by CLI tools and Qt apps

SQLite log writing is split between the public `sqlitelog.*` wrapper and the
private `sqlitelog/` implementation directory. `sqlitelog/schema.cc` owns the
table and stream metadata, while the other files in that directory decode
individual tag log protobufs into rows. The tables and columns a log contains
are documented for users in
[SQLite Log Format](../../docs/src/reference/sqlite-logs.md), which is the
authoritative schema; [`sqlitelog/README.md`](sqlitelog/README.md) covers the
writer internals and decoder rules behind them.

`recovery/` holds the SWD capture and recovery code, separate from the monitor
path:

- `swdsession`: an exclusive session that halts the tag at its reset vector
  before the firmware runs;
- `swdmcu`: per-MCU memory and register maps;
- `statecapture`: registers, internal flash and SRAM capture, behind
  `tag-capture`;
- `identityrecord`: parses the tag identity record from captured internal
  flash;
- `targetimage` and `sramcall`: load an ELF image into SRAM and call its
  functions, behind `tag-sramcall`;
- `externalflash` and `externalcapture`: read (and in rescue erase) external
  flash through an `embedded/loaders` image, behind `tag-xflash` and the
  external stage of `tag-capture`;
- `capturesource`: rebuilds a download from a capture, behind `tag-rebuild`;
- `sha256`: hashes captured regions for the manifest.

`test/` holds standalone offline decoder checks; see
[`test/README.md`](test/README.md).

Design documents:

- [`design/swd-recovery.md`](design/swd-recovery.md): design of the SWD capture
  and recovery library -- capture without booting the firmware, external flash
  through the loaders, tag identification, and the layered API. Its bring-up
  log is
  [`design/investigations/2026-09-swd-capture-library-bring-up.md`](design/investigations/2026-09-swd-capture-library-bring-up.md)
  and its open work is in [`recovery/TODO.md`](recovery/TODO.md).
- [`design/proposals/python-interface.md`](design/proposals/python-interface.md):
  proposed Python binding API, native/protobuf boundary, shared download
  service, packaging, and testing plan. No binding exists yet.

This library should remain Qt-free. Qt applications can link it, but reusable
Qt UI code belongs in `../sensorui` or `../../common`.
