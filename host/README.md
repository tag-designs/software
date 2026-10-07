---
type: readme
status: current
summary: Host tree layout by role, dependency direction, CMake entry points, packaging notes and maintenance guidelines.
---

# Host Software

This directory contains the host-side libraries, command-line tools, Qt
applications, and packaged user documentation for Ultralight Tags.

The layout is organized by role:

| Directory | Role |
| --- | --- |
| `libraries/` | Reusable code: low-level tag access, plotting, sensor analysis, and shared sensor UI widgets. |
| `applications/` | Qt applications such as `qtmonitor`, `qtprogram`, `qtcalibrate`, `btviz`, and `sensorviz`. |
| `commandline/` | CLI tools built on the low-level tag library. |
| `common/` | Small Qt helpers shared by applications, but not part of the low-level tag library. |
| `docs/` | MkDocs user-guide sources and the fixtures its screenshots are generated from; see [docs/README.md](docs/README.md). Build with the CMake `docs` target; installed with packages when `BUILD_HOST_DOCS=ON`. |

## Dependency Direction

Keep dependencies flowing from applications toward libraries:

```text
applications/*       -> libraries/* and common/
commandline          -> libraries/tagcore (dataprocessing: libraries/sensoranalysis)
libraries/sensorui   -> libraries/sensoranalysis
libraries/tagcore    -> proto and tag_monitor_interface
```

`libraries/tagcore` intentionally stays Qt-free so the CLI tools and low-level
host code do not need Qt. Qt-only helpers belong in `common/` or
`libraries/sensorui`.

## CMake Entry Points

`host/CMakeLists.txt` wires the layout together:

- `libraries/tagcore` and `commandline` are added whenever host tools are
  built.
- Qt support libraries and applications are added when `BUILD_QT_APPS=ON`.
- A small `host_common` interface target exposes `common/` headers to Qt apps.
- The `docs` target builds the MkDocs user guide for local preview. The
  `BUILD_HOST_DOCS` option adds that target to the default build and package
  install.

The CMake target names are intentionally stable:
`tagcore`, `qcustomplot`, `sensoranalysis`, `sensorui`, `qtmonitor`,
`qtprogram`, `qtcalibrate`, `btviz`, and `sensorviz`.

## Packaging Notes

Install/package target selection still lives in `host/CMakeLists.txt`.
`host_qt_install_targets` controls which Qt apps are included in the package.
Applications that use QML set `QT_DEPLOY_QML_DIRS` so deployment tooling can
collect the required QML imports and resources. For each packaged app with
QML directories, the default build also runs `qml_import_check_<app>`
(`cmake/CheckQmlImports.cmake`). It fails when the Qt install lacks a module
those directories import, or a Qt library that one of those modules' plugins
links. Deployment would otherwise skip the missing module silently and ship an
app that fails when it loads its QML. A new QML import of a Qt add-on
therefore also needs that add-on under `modules:` in
`.github/workflows/release.yml`.
On macOS, macdeployqt copies every module installed under each QML directory
an app imports. The install then prunes the bundle back to the QML modules
the app needs, and the Qt frameworks those modules link to
(`cmake/PruneMacosQml.cmake`, option `MACOS_PRUNE_UNUSED_QML`), before
signing. With a full Qt this keeps GPL-only modules the app does not use,
such as Qt Virtual Keyboard, out of the package.
`compviz` source remains in the tree for reference, but it is retired from the
normal build and packaging paths now that `sensorviz` handles CompassTag logs.

## Maintenance Guidelines

- Put tag communication, log writer/reader code, and protocol-facing helpers in
  `libraries/tagcore`.
- Put UI-free sensor algorithms in `libraries/sensoranalysis`.
- Put reusable Qt widgets/resources for sensor tools in `libraries/sensorui`.
- Put one-off application behavior inside the relevant `applications/*`
  directory until a second app genuinely needs it.
- Prefer native SQLite APIs over QtSql in shared host code.
