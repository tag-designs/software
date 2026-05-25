# sensorViz Development Notes

This note captures the current `sensorviz` design state and the work that still
belongs in future passes. The shorter user/maintainer overview is in
[README.md](README.md).

## Current State

`sensorviz` is the general Qt/QCustomPlot viewer for SQLite sensor logs produced
by the host download tools. It currently supports:

- scalar streams discovered from the SQLite `streams` metadata table;
- pressure, activity, voltage, core temperature, and sensor temperature streams;
- pressure-derived altitude, using ambient sensor temperature when available;
- activity low-pass display;
- CompassTag raw record sets converted into heading, acceleration magnitude,
  pitch, roll, dip, and magnetic field strength streams;
- CompassTag calibration-constant display;
- a narrow shared QML compass/orientation panel beside the plot;
- editable graph title, print preview, UTC offset, cursors, zoom-to-cursors,
  stream visibility, colors, axis sides, and per-stream y-axis ranges;
- per-tag display preferences stored as sparse, formatted JSON overrides.

`compviz` remains buildable as a specialized/reference tool, but `sensorviz` is
now the intended general viewer for BitTag, BitPresTag, PresTag, and CompassTag
SQLite logs.

## Current Architecture

- `main.cpp`: application startup and Qt message logging.
- `mainwindow.*`: static Qt UI construction, persistent actions, and shared
  application state.
- `dataloading.cpp`: File > Load workflow, active `SensorLog` replacement,
  default graph title, stream-action creation, File Info updates, and initial
  plot refresh.
- `sqlite_loader.*`: read-only SQLite adapter. It consumes tagcore's mandatory
  `streams` metadata table, loads scalar streams, groups multi-column
  `record_column` rows into `SensorRecordSet`, and loads CompassTag calibration
  metadata.
- `sensorstream.h`: normalized loaded-data model:
  - `SensorStream`: plottable scalar time series;
  - `SensorRecordSet`: loaded multi-column data for transforms;
  - `SensorLog`: file metadata, streams, record sets, and typed calibration.
- `sensor_preferences.*`: sensorViz display policy:
  - built-in defaults from `defaultDisplayForStream()`;
  - in-memory per-tag overrides;
  - JSON load/store for sparse preference files.
- `stream_actions.cpp`: visible-stream dialog, axis-side dialog, color dialog,
  range dialog, and range coupling between related streams.
- `transforms.cpp`: scalar display transforms such as altitude and activity
  low-pass.
- `compass_transforms.cpp`: CompassTag record-set transforms, heading
  declination, battery-forward convention, and compass-panel sample updates.
- `plotting.cpp`: QCustomPlot graph rebuild, dynamic axes, labels, title
  placement, cursor placement, and range reset behavior.
- `interaction.cpp`: context menu, print preview, cursor interaction, UTC
  offset, and mouse readout.
- `controls.cpp`: general actions and small shared helpers, including graph
  title editing and calibration-constant display.

## Design Rules

- SQLite logs describe the data contract: stream ids, labels, units, table
  names, time columns, value columns, and stream kind.
- Viewer policy belongs in `sensorviz`, not in the SQLite file. Colors, default
  visibility, axis side, and fixed display ranges live in
  `defaultDisplayForStream()`.
- Per-tag preference files store only user overrides from those defaults:
  visible streams, colors, and axis sides.
- Preference files intentionally do not store y-axis ranges, sea-level
  pressure, declination, UTC offset, battery-forward, or graph title. Those are
  current-view or analysis-session state.
- Feature availability should be driven by loaded stream ids, record sets, or
  calibration metadata rather than hardcoded tag-type checks.
- The menu bar and plot context menu should use the same organization:
  `File`, `View`, `Configuration`, and `Help`, with `File > Preferences` as a
  submenu.
- Tag-specific controls should be hidden unless the loaded log supports them.

## Current UI Decisions

- At startup, File > Load and the Help menu are enabled. Other menu structure is
  visible but disabled where it is broadly applicable.
- Tag-specific controls are hidden until relevant:
  - Sea-level Pressure appears when pressure data exists.
  - Activity Filter appears when activity data exists.
  - Declination and Battery Forward appear for CompassTag logs.
  - Calibration Constants appears only when calibration metadata exists.
- View groups stream-display controls together: Visible Streams, Axis Sides,
  Colors, and Ranges.
- Configuration owns current-view/session parameters: graph title, UTC offset,
  sea-level pressure, activity filter, declination, and battery-forward.
- The graph title defaults to the loaded file name, not the full path, and can
  be edited or hidden.
- Activity defaults to visible and uses fixed range `0-100`.
- Voltage defaults off, uses the right axis, and uses fixed range `0-5 V`.
- Core temperature defaults off, uses the right axis, and uses fixed range
  `0-50 C`.
- Autoscaled y-axes receive a 5% margin.
- Reset Zoom clears custom y-axis ranges and restores the full x-axis range.
- Pressure and altitude ranges are coupled unless altitude has an explicit
  custom range.
- Activity and activity-filter ranges are coupled unless one has an explicit
  custom range.

## Known Limitations

- Transform definitions are still mostly hardcoded in `transforms.cpp` and
  `compass_transforms.cpp`.
- Transform parameter dialogs are simple action-specific dialogs, not a common
  transform configuration framework.
- Display preferences are loaded/stored manually from JSON files; there is no
  automatic recent/default preference file mechanism.
- Current-view session parameters are intentionally not persisted. That is
  correct for now, but future workflows may want optional project/session files.
- Multi-column record sets are currently used mainly for CompassTag data. More
  record-set users may reveal patterns that should be generalized.
- There are no automated GUI tests for menu organization, preference load/store,
  or plot-title behavior.

## Future Work

Near-term cleanup:

- Keep validating CompassTag derived streams against real logs and against
  `compviz` behavior where it remains the reference.
- Warn the user when a load succeeds but an expected stream or derived stream is
  skipped, for example when CompassTag data lacks calibration metadata needed
  for heading/orientation streams.
- Add new stream display defaults to `defaultDisplayForStream()` as new tag
  streams appear.
- Improve transform-specific configuration structure if more transforms are
  added.
- Consider a small shared helper for building mirrored menu-bar/context-menu
  sections if more actions are added.

Possible larger refactors:

- Introduce a transform registry that declares:
  - required input stream ids or record-set ids;
  - generated stream ids;
  - default display policy;
  - configuration UI factory.
- Generalize record-set transforms before adding more multi-column sensor
  families.
- Add optional session/project files if users need to persist graph title,
  ranges, sea-level pressure, declination, or other current-view state.
- Add light GUI regression tests around menu enable/visibility rules and
  preference JSON round trips.

## Testing Notes

Typical local verification:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software-vcpkg-release --target sensorviz
git diff --check
```

When changing SQLite loading or stream metadata, also test with representative
BitTag, BitPresTag, PresTag, and CompassTag logs.
