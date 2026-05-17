# AGENTS.md

This file is for agents working under `host/`. It summarizes host-side
ownership, dependency direction, and common checks.

## Directory Ownership

- `libraries/tagcore/`: low-level tag access, protocol-facing helpers, download
  support, and text/SQLite log writing. Keep this library Qt-free.
- `libraries/sensoranalysis/`: UI-free sensor math and processing helpers.
- `libraries/sensorui/`: reusable Qt/QML widgets and dialogs shared by host
  applications.
- `libraries/qcustomplot/`: vendored plotting support.
- `common/`: small Qt helper headers shared by applications.
- `applications/`: Qt applications.
- `commandline/`: CLI tools built mostly on `libraries/tagcore`.
- `docs/`: MkDocs user-guide sources and requirements.

## Dependency Direction

Keep dependencies flowing from applications toward libraries:

```text
applications/*       -> libraries/* and common/
commandline          -> libraries/tagcore
libraries/sensorui   -> libraries/sensoranalysis
libraries/tagcore    -> proto and tag_monitor_interface
```

Do not add Qt dependencies to `libraries/tagcore`. Put Qt-specific shared code
in `common/` or `libraries/sensorui`.

## Application Notes

- `applications/qtmon`: tag monitoring, configuration, and download UI.
- `applications/qtcalibrate`: calibration workflow and live orientation display.
- `applications/sensorviz`: general SQLite sensor-log viewer.
- `applications/compviz`: still built as a specialized/reference compass tool,
  but not packaged now that sensorViz handles CompassTag logs.
- `applications/qtprogram`, `btdataviz`, and related apps should keep one-off
  behavior local until a second app genuinely needs it.

## sensorViz Conventions

- SQLite loading belongs in `applications/sensorviz/sqlite_loader.*`.
- Normalized loaded data lives in `sensorstream.h`.
- Viewer display defaults and per-tag JSON preference overrides live in
  `sensor_preferences.*`.
- Stream visibility, colors, axis sides, and ranges are view concerns, not
  SQLite-log schema concerns.
- Menu-bar structure and plot context-menu structure should mirror each other
  when practical.
- Tag-specific controls should be hidden unless the loaded log contains the
  stream/record set/calibration that makes them useful.

## Build And Verification

Use a focused target when possible:

```sh
cmake --build <build-dir> --target sensorviz
cmake --build <build-dir> --target qtmonitor
cmake --build <build-dir> --target qtcalibrate
cmake --build <build-dir> --target docs
```

Run `git diff --check` before finishing. If a change touches packaging or
install rules, also verify the relevant package/install target when practical.

## Documentation

- Update app-level READMEs when changing structure or user-visible workflows.
- Update `host/docs/src/apps/*.md` for user-facing guide changes.
- MkDocs dependencies live in `host/docs/requirements.txt`.

