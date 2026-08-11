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
individual tag log protobufs into rows. See
[`sqlitelog/README.md`](sqlitelog/README.md) for the IMUTag downloader schema
and timing-field meanings.

Design documents:

- [`design/python-interface.md`](design/python-interface.md): Proposed Python
  binding API, native/protobuf boundary, shared download service, packaging,
  and testing plan.

This library should remain Qt-free. Qt applications can link it, but reusable
Qt UI code belongs in `../sensorui` or `../../common`.
