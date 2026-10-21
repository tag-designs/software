---
type: readme
status: current
summary: Table of host libraries and their dependency direction.
---

# Host Libraries

Reusable host code lives here. Libraries should have clear ownership boundaries
so applications can share behavior without inheriting unrelated dependencies.

| Directory | Target | Purpose |
| --- | --- | --- |
| `tagcore/` | `tagcore` | Qt-free tag communication, tag monitoring, log writing/reading, and low-level host logging. |
| `qcustomplot/` | `qcustomplot` | Vendored/static QCustomPlot wrapper used by visualization apps. |
| `sensoranalysis/` | `sensoranalysis` | UI-free sensor-domain algorithms such as compass calibration and orientation derivation. |
| `sensordata/` | `sensordata` | Qt-free decimating access to SQLite sensor logs: columnar loading, min/max pyramids, M4 view queries and exact read-back. Depends only on sqlite3. |
| `sensorui/` | `sensorui` | Reusable Qt widgets, QML resources, and dialogs for sensor visualization tools. |

Dependency direction:

```text
sensorui -> sensoranalysis
sensordata -> sqlite3
applications -> sensorui, sensoranalysis, sensordata, qcustomplot, tagcore
commandline -> tagcore (dataprocessing -> sensoranalysis_core, the Qt-free core)
```

Avoid adding Qt dependencies to `tagcore` or `sensordata`. If code needs Qt
widgets, QML, or dialogs, it belongs in `sensorui` or an application directory.
