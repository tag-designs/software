---
type: readme
status: current
summary: Table of the Qt host applications and their targets.
---

# Host Applications

Qt applications live here. Each application owns its menus, workflows, and
application-specific SQLite/UI glue.

| Directory | Target | Purpose |
| --- | --- | --- |
| `qtmon/` | `qtmonitor` | Monitor, configure, and download from attached tags. |
| `qtprogram/` | `qtprogram` | Program tags. |
| `qtcalibrate/` | `qtcalibrate` | Calibration tool. |
| `btdataviz/` | `btviz` | Legacy BitTag visualization. |
| `sensorviz/` | `sensorviz` | General sensor SQLite log visualization, including CompassTag logs. |

The retired `compviz` CompassTag viewer has been removed; `sensorviz` covers
CompassTag SQLite logs, and the old source is in git history.

Shared Qt helpers are exposed through the `host_common` CMake target from
`../common`. Reusable plotting and sensor widgets should come from
`../libraries` rather than being copied between applications.
