---
type: readme
status: current
summary: Table of the host libraries, C++ and Rust, the C interface that joins them, and their dependency direction.
---

# Host Libraries

Reusable host code lives here, in C++ and in Rust. Libraries have clear
ownership boundaries so applications can share behaviour without inheriting
unrelated dependencies. How they fit together is explained in the
[Host Software Guide](../design/guide/README.md).

## C++

| Directory | Target | Purpose |
| --- | --- | --- |
| `tagcore/` | `tagcore` | Qt-free tag communication (`Tag`, `TagMonitor`, `LinkAdapt`), `ConfigShape`, the SQLite and text log writers, host logging, and the SWD capture and recovery library (`recovery/`). |
| `tagcore-c/` | `tagcore_c`, `tagcore_c_static` | A C interface over the C++ the Rust programs reuse: the SQLite writer (`tlw_*`), the calibration session (`tcs_*`) and sensorViz's core (`svz_*`). Shared library on Linux and Windows, static archive on macOS. |
| `sensoranalysis/` | `sensoranalysis`, `sensoranalysis_core` | Sensor-domain algorithms: compass calibration and orientation, the calibration session. The arithmetic is in the Qt-free `core/`; the Qt-typed layer adapts it for the Qt applications. |
| `sensordata/` | `sensordata` | Qt-free decimating access to SQLite sensor logs: columnar loading, min/max pyramids, M4 view queries and exact read-back. Depends only on sqlite3. |
| `sensorui/` | `sensorui` | Reusable Qt widgets, QML resources and dialogs for the Qt sensor tools. |
| `qcustomplot/` | `qcustomplot` | Vendored QCustomPlot, used by `sensorviz` and `btviz`. |

## Rust

Every Rust crate is a member of the workspace at [`host/Cargo.toml`](../Cargo.toml).

| Directory | Crate | Purpose |
| --- | --- | --- |
| `tagcore-rs/` | `tagcore` | The tag interface over an ST-LINK base, ported from `tagcore`'s tag classes: `Tag`, `TagMonitor`, `LinkAdapt`, the protobuf types generated from `proto/`, `ConfigShape`. |
| `tagprobe-rs/` | `tagprobe` | Programming tags through probe-rs (with a field-safe ST-LINK driver for bases on old firmware) and bases over USB DFU. |
| `tagcore-c-sys/` | `tagcore-c-sys` | Links `tagcore_c` and declares its functions; the one crate that does, so the wrappers below can be used together. |
| `taglogwriter-rs/` | `taglogwriter` | Safe wrapper over `tlw_*`: writes tag SQLite logs with the C++ writer. |
| `calibsession-rs/` | `calibsession` | Safe wrapper over `tcs_*`: the live calibration session and compass display angles. |
| `sensorviz-session-rs/` | `sensorviz-session` | Safe wrapper over `svz_*`: opens and views a log as the Qt sensorViz does. |

## Dependency direction

```text
C++:   sensorui -> sensoranalysis -> sensoranalysis_core
       sensordata -> sqlite3
       tagcore-c -> tagcore (sqlitelog), sensoranalysis_core, applications/sensorviz/core (-> sensordata)
       applications -> sensorui, sensoranalysis, sensordata, qcustomplot, tagcore
       commandline -> tagcore (dataprocessing -> sensoranalysis_core)

Rust:  tagprobe -> tagcore
       taglogwriter, calibsession, sensorviz-session -> tagcore-c-sys -> libtagcore_c (C++)
       taglogwriter -> tagcore (the protobuf types it encodes)
```

Rules:

- Avoid adding Qt dependencies to `tagcore`, `tagcore-c` or `sensordata`. Code
  that needs Qt widgets, QML or dialogs belongs in `sensorui` or an application
  directory. No Rust crate uses Qt.
- The C++ is reached from Rust only through `tagcore-c`. A new interface gets a
  header there, a module in `tagcore-c-sys` and its own wrapper crate, with an
  interface version both sides check ([design](tagcore-c/design/architecture.md)).
- `tagcore` and `tagcore-rs` must agree on the wire; the C++ is the reference
  ([tagcore-rs design](tagcore-rs/design/architecture.md)).
