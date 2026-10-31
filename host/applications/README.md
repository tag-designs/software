---
type: readme
status: current
summary: Table of the host applications - the Qt ones and their Rust/Tauri ports - with the executable and product name of each, and the rule for where application behaviour lives.
---

# Host Applications

Each application owns its menus, workflows and application-specific glue.
There are two lines, and both ship until the Qt one is retired: the Qt
applications, and their Rust ports, which draw a web page in a Tauri window
over a toolkit-free Rust engine. The pattern the Rust applications share is
described in the [guide](../design/guide/10-application-pattern.md).

## Qt (C++)

| Directory | Target | Product name | Purpose |
| --- | --- | --- | --- |
| `qtmon/` | `qtmonitor` | Tag Monitor | Monitor, configure, start and download from an attached tag. Its Qt-free `core/` is what `tagmonitor-rs` ports. |
| `qtprogram/` | `qtprogram` | Tag Programmer | Program and test a batch of tags, through STM32CubeProgrammer. |
| `qtcalibrate/` | `qtcalibrate` | Tag Calibrator | Magnetometer and accelerometer calibration, with a live compass view. |
| `sensorviz/` | `sensorviz` | SensorViz | View SQLite sensor logs of every tag family. Its Qt-free `core/` is what `tagview` runs. |
| `btdataviz/` | `btviz` | Bit Tag Visualizer | Legacy BitTag visualization (deprecated). |

## Rust / Tauri

Each is a Cargo workspace member twice over: `core/` (the engine, a library
crate with the application's rules and state, tested without hardware) and
`app/` (the Tauri shell, the executable). The page is in `ui/`, on the shared
[UI kit](../ui-kit/README.md).

| Directory | Core crate | Executable | Product name | Purpose |
| --- | --- | --- | --- | --- |
| `tagprogram-rs/` | `tagprogram-core` | `tagprogram` | Tag Programmer | qtprogram's station, programming through probe-rs instead of STM32CubeProgrammer. |
| `tagmonitor-rs/` | `tagmonitor-core` | `tagmonitor` | Tag Monitor | qtmonitor's core, module for module; downloads through the C++ writer (`tagcore_c`). |
| `tagcalibrate-rs/` | `tagcalibrate-core` | `tagcalibrate` | Tag Calibrator | qtcalibrate's window logic over the C++ calibration session (`tagcore_c`). |
| `sensorviz-rs/` | `sensorviz-core` | `tagview` | Tag View | sensorViz's window logic over sensorViz's C++ core (`tagcore_c`), plotting with uPlot. |

The two implementations of an application are held to the same behaviour by
shared test oracles (qtmonitor's probe goldens, qtcalibrate's replay log,
sensorViz's state dump); each Rust application's README says which.

## Where behaviour lives

- Shared Qt helpers come from the `host_common` target (`../common`);
  reusable plotting and sensor widgets from `../libraries`, never copied
  between applications.
- In a Rust application every rule and all state are in `core/`. The page in
  `ui/` draws the view state it is sent and holds no rules; `app/` only
  forwards messages. The contract between them is each application's
  `design/ui-engine-contract.md`.
- Logic both implementations need lives in C++ once, reached from Rust through
  `tagcore_c` (the SQLite writer, the calibration session, sensorViz's core).

The retired `compviz` CompassTag viewer has been removed; `sensorviz` covers
CompassTag SQLite logs, and the old source is in git history.
