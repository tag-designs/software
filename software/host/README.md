# Host Software Architecture

This directory contains the host-side library, command-line tools, and Qt
applications for working with Ultralight Tags. The host code is organized around
one shared hardware/protocol library, a set of small CLI tools, and several Qt
applications for programming, monitoring, calibration, and visualization.

The top-level `software/host/CMakeLists.txt` always adds `lib` and `cli`. Qt
applications are added when `BUILD_QT_APPS=ON`.

## Build Layout

| Directory | Target(s) | Role |
| --- | --- | --- |
| `lib` | `tag` | Shared host library for USB/tag communication, tag logs, SQLite log IO, and host logging. |
| `cli` | `tag-dwnld`, `tag-test`, `tag-test-example`, `tag-info`, `tag-reset`, `tag-start`, `tag-cal`, `tag-monitor-test` | Command-line utilities built on the shared `tag` library. |
| `qcustomplot` | `qcustomplot` | Shared static Qt plotting library used by visualization applications. |
| `qtmon` | `qtmonitor` | Qt monitor/configuration application. |
| `qtprogram` | `qtprogram` | Qt tag programming application. |
| `qtcalibrate` | `qtcalibrate` | Qt calibration application. |
| `btdataviz` | `btviz` | Qt visualization application using QCustomPlot and FastFIR filtering. |
| `compviz` | `compviz` | Qt visualization application for compass/tag SQLite data. |
| `sensorviz` | `sensorviz` | Qt visualization application for scalar sensor SQLite data and derived streams. |
| `tag-orientation` | `tag-orientation` | Qt orientation experiment/tool. Built with Qt apps, but not included in the deployed package. |

## Shared Architecture

The active dependency direction is:

```text
host/lib/tag
  -> cli tools
  -> Qt apps

qcustomplot
  -> btviz
  -> compviz
  -> sensorviz
```

`btviz`, `compviz`, and `sensorviz` consume `qcustomplot`; they do not depend on
each other.

Qt applications should keep tag/device access in `lib` where possible. This
keeps command-line tools usable without linking Qt and lets shared routines use
the `log_trace`, `log_debug`, `log_info`, and related logging APIs from
`lib/log.h`. In Qt applications, that logging path is integrated with the Qt
logging setup, so library diagnostics naturally show up in the host app logs.

SQLite support lives in two places:

| Area | Purpose |
| --- | --- |
| `lib/sqlitelog.*` | Qt-free SQLite log read/write helpers shared by host tools. |
| app-specific SQLite files | UI/application glue such as `compviz/sqliteload.cpp`. Download writing is shared through `lib/taglogwriter.*`. |

The codebase intentionally uses the native SQLite C API instead of QtSql.

## Dependencies By Directory

### `lib`

The `tag` static library is the core host interface.

Internal responsibilities:

- USB/tag communication: `tagclass.*`, `tagmonitor.*`, `linkadapt.*`
- Log decoding and data structures: `txtlogs.*`
- SQLite log storage: `sqlitelog.*`
- Host logging: `log.*`
- Shared command definitions: `commands.h`

Build dependencies:

| Dependency | Linkage |
| --- | --- |
| `proto` | Public |
| `tag_monitor_interface` | Private |
| `SQLite::SQLite3` | Private |
| `libusb-1.0` | Private, through pkg-config or vcpkg |

Public include directory: `software/host/lib`.

### `cli`

The CLI tools are thin executables over the `tag` library. They share option
parsing code in `options.cc` and use `cxxopts.hpp` from `lib`.

Targets:

| Target | Sources | App Function |
| --- | --- | --- |
| `tag-dwnld` | `dwnld.cc`, `options.cc` | Downloads tag log data using the default supported writer for the tag type, with optional `--format` and `--output`. |
| `tag-test` | `test.cc`, `options.cc` | TODO: describe the test workflow. |
| `tag-test-example` | `test-example.cc`, `options.cc` | TODO: describe how this differs from `tag-test`. |
| `tag-info` | `tag-info.cc`, `options.cc` | TODO: describe reported tag/base information. |
| `tag-reset` | `tag-reset.cc`, `options.cc` | TODO: describe reset behavior and safety notes. |
| `tag-start` | `tag-start.cc`, `options.cc` | TODO: describe start behavior and required arguments. |
| `tag-cal` | `tag-calibrate.cc`, `options.cc` | TODO: describe calibration input/output. |
| `tag-monitor-test` | `tag-monitor-test.cc`, `options.cc` | TODO: describe monitor test purpose. |

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `tag` | Shared host library. |
| `tag_monitor_interface` | Linked by CLI helper function. |
| `pthread` | Non-Windows builds only. |

### `qcustomplot`

This directory contains the shared QCustomPlot static library used by host
visualization apps.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `Qt6::Widgets` | Public dependency. |
| `Qt6::PrintSupport` | Public dependency. |

Consumers:

- `btdataviz`
- `compviz`
- `sensorviz`

### `qtmon`

`qtmon` builds the `qtmonitor` application. It contains the monitor UI,
configuration tabs, schedule/hibernate widgets, log display, and download code.

Notable components:

- `mainwindow.*`, `mainwindow.ui`: main application window.
- `configtab.*`, `configtab.ui`: tag configuration UI.
- `schedule.*`, `hibernate.*`: configuration panels.
- `abstractdownload.*`: data download workflow backed by the shared host-log writer interface.
- `bittaglog.*`: Qt-side tag log UI support.
- `logwindow.*`: Qt log display.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `tag` | Shared host library. |
| `Qt6::Widgets` | Main Qt dependency. |

App function template:

```text
TODO: Describe qtmonitor from a user's point of view.
- Primary users:
- Typical workflow:
- Inputs:
- Outputs/files:
- Hardware assumptions:
- Notes/caveats:
```

### `qtprogram`

`qtprogram` builds the Qt tag programming application.

Notable components:

- `mainwindow.*`, `mainwindow.ui`: main programming UI.
- `custommessagebox.*`: app-specific message box behavior.
- `logwindow.*`: log display.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `tag` | Shared host library. |
| `Qt6::Widgets` | Main Qt dependency. |

App function template:

```text
TODO: Describe qtprogram from a user's point of view.
- Primary users:
- Typical workflow:
- Inputs:
- Outputs/files:
- Hardware assumptions:
- Notes/caveats:
```

### `qtcalibrate`

`qtcalibrate` builds the Qt calibration application. It combines calibration
math, plotting, and QML/SVG orientation resources.

Notable components:

- `mainwindow.*`, `mainwindow.ui`: main calibration UI.
- `compassdata.*`: compass data handling.
- `magcal.c`, `matrix.c`, `quality.c`: calibration/math routines.
- `magplot.*`, `sinbin.*`, `ema.h`: plotting/filtering helpers.
- `qfi.qrc`, `orientation_frame/`: QML/SVG resources deployed with the app.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `tag` | Shared host library. |
| `Qt6::Core` | Qt core. |
| `Qt6::Widgets` | Widget UI. |
| `Qt6::Svg`, `Qt6::SvgWidgets` | SVG resources/widgets. |
| `Qt6::Qml`, `Qt6::Quick`, `Qt6::QuickWidgets` | QML orientation UI. |

App function template:

```text
TODO: Describe qtcalibrate from a user's point of view.
- Primary users:
- Typical workflow:
- Inputs:
- Outputs/files:
- Hardware assumptions:
- Notes/caveats:
```

### `btdataviz`

`btdataviz` builds the `btviz` visualization application.

Notable components:

- `mainwindow.*`, `mainwindow.ui`: main visualization UI.
- `actogram.*`, `actogram.ui`: actogram view.
- `tickerdatetimeoffset.*`: date/time ticker support.
- `solpos.c`, `solpos00.h`: solar position support.
- `FastFIR/`: active FIR filtering support. `qjfastfir.*` is built directly;
  the KISS FFT C sources are included by `qjfastfir.cpp`.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `qcustomplot` | Shared plotting library. |
| `Qt6::Widgets` | Widget UI. |
| `Qt6::PrintSupport` | Plot printing/export support. |

App function template:

```text
TODO: Describe btviz from a user's point of view.
- Primary users:
- Typical workflow:
- Inputs:
- Outputs/files:
- Hardware assumptions:
- Notes/caveats:
```

### `compviz`

`compviz` builds the compass visualization application. It uses SQLite log
loading, QCustomPlot charts, and QML/SVG orientation resources.

Notable components:

- `mainwindow.*`, `mainwindow.ui`: main visualization UI.
- `sqliteload.cpp`: SQLite input path.
- `compassdata.cpp`: compass data processing.
- `controls.cpp`: UI control behavior.
- `constants_dialog.*`, `constants_dialog.ui`: configurable constants dialog.
- `tickerdatetimeoffset.*`: date/time ticker support.
- `qfi.qrc`, `orientation_frame/`: QML/SVG resources deployed with the app.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `tag` | Shared host library and tag data types. |
| `SQLite::SQLite3` | Native SQLite C API. |
| `qcustomplot` | Shared plotting library. |
| `Qt6::Widgets` | Widget UI. |
| `Qt6::Svg`, `Qt6::SvgWidgets` | SVG resources/widgets. |
| `Qt6::Qml`, `Qt6::Quick`, `Qt6::QuickWidgets` | QML orientation UI. |

App function template:

```text
TODO: Describe compviz from a user's point of view.
- Primary users:
- Typical workflow:
- Inputs:
- Outputs/files:
- Hardware assumptions:
- Notes/caveats:
```

### `sensorviz`

`sensorviz` builds a scalar sensor visualization application for SQLite logs.
It loads available streams such as pressure, activity, temperature, and voltage,
then plots selected raw or derived streams with QCustomPlot.

Notable components:

- `mainwindow.*`: programmatic Qt UI, stream selection, transforms, and plotting.
- `sqlite_loader.*`: native SQLite input path for sensor log tables.
- `sensorstream.h`: shared in-memory stream/log data structures.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `SQLite::SQLite3` | Native SQLite C API. |
| `qcustomplot` | Shared plotting library. |
| `Qt6::Widgets` | Widget UI. |
| `Qt6::PrintSupport` | QCustomPlot dependency. |

### `tag-orientation`

`tag-orientation` builds a Qt orientation tool. It is currently built when
`BUILD_QT_APPS=ON`, but it is not listed in `host_qt_install_targets`, so it is
not part of the deployed package.

Notable components:

- `MainClass.*`: primary orientation class.
- `main.cpp`: application entry point.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| `tag` | Shared host library. |
| `Qt6::Core` | Qt core. |
| `Qt6::Gui` | Qt GUI support. |
| `../qtcalibrate` include path | Reuses calibration/orientation declarations. |

App function template:

```text
TODO: Describe tag-orientation from a developer/user point of view.
- Primary users:
- Typical workflow:
- Inputs:
- Outputs/files:
- Hardware assumptions:
- Current status:
- Why it is not deployed:
```

## Shared Qt Helpers

The host directory also contains top-level Qt helper headers:

| File | Purpose |
| --- | --- |
| `qtfiledialog.h` | Shared file dialog helper used by Qt apps. |
| `qthoststyle.h` | Shared host Qt styling helper. |

Keep these Qt-only helpers outside `lib` so the shared host library remains
usable by CLI tools without linking Qt.

## Packaging Notes

Install/package selection is controlled in `software/host/CMakeLists.txt`.

| Platform | Installed host targets |
| --- | --- |
| Windows | CLI tools plus Qt apps. |
| macOS and other non-Windows builds | Qt apps listed in `host_qt_install_targets`. |

Current deployed Qt apps:

- `btviz`
- `compviz`
- `sensorviz`
- `qtmonitor`
- `qtprogram`
- `qtcalibrate`

Qt apps that use QML resources set `QT_DEPLOY_QML_DIRS` so deployment tooling
can collect the required QML imports/resources.

## Maintenance Guidelines

- Keep reusable tag communication, log parsing, and SQLite routines in `lib`.
- Keep `lib` free of Qt dependencies.
- Prefer native SQLite APIs over QtSql; this keeps CLI tools and library code
  independent of Qt.
- Keep plotting code shared through `qcustomplot` when both visualization apps
  need it.
- Treat app-local UI glue as app-local unless another host app needs it.
- If a Qt app needs shared behavior from `lib`, use `log_trace`, `log_debug`,
  `log_info`, `log_warn`, or `log_error` for diagnostics so messages flow into
  each host application's logging setup.
