# TagCore Library

`tagcore` contains the low-level host interface. The CMake target is still named
`tag` to avoid a noisy target rename.

Responsibilities:

- USB/tag communication: `tagclass.*`, `tagmonitor.*`, `linkadapt.*`
- Log writing and storage interfaces: `taglogwriter.*`, `txtlogs.*`,
  `sqlitelog.*`
- Host logging helpers: `log.*`
- Shared protocol-facing definitions used by CLI tools and Qt apps

This library should remain Qt-free. Qt applications can link it, but reusable
Qt UI code belongs in `../sensorui` or `../../common`.
