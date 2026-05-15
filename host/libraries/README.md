# Host Libraries

Reusable host code lives here. Libraries should have clear ownership boundaries
so applications can share behavior without inheriting unrelated dependencies.

| Directory | Target | Purpose |
| --- | --- | --- |
| `tagcore/` | `tag` | Qt-free tag communication, tag monitoring, log writing/reading, and low-level host logging. |
| `qcustomplot/` | `qcustomplot` | Vendored/static QCustomPlot wrapper used by visualization apps. |
| `sensoranalysis/` | `sensoranalysis` | UI-free sensor-domain algorithms such as compass calibration and orientation derivation. |
| `sensorui/` | `sensorui` | Reusable Qt widgets, QML resources, and dialogs for sensor visualization tools. |

Dependency direction:

```text
sensorui -> sensoranalysis
applications -> sensorui, sensoranalysis, qcustomplot, tagcore
commandline -> tagcore
```

Avoid adding Qt dependencies to `tagcore`. If code needs Qt widgets, QML, or
dialogs, it belongs in `sensorui` or an application directory.
