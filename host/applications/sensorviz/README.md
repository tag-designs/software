# sensorViz

`sensorviz` is a Qt/QCustomPlot application for viewing SQLite sensor logs. It
is intended to grow into the general viewer for pressure, activity, and other
sensor-oriented tag logs.

For the longer design/history note, see [ROADMAP.md](ROADMAP.md).

## What It Does

- Loads SQLite log files produced by host tag download tools.
- Discovers available streams from the SQLite `streams` metadata table.
- Plots scalar streams such as pressure, activity, voltage, and temperature.
- Provides display transforms such as:
  - altitude from pressure
  - low-pass filtered activity
  - CompassTag heading, acceleration magnitude, pitch, roll, dip, and magnetic
    field strength
- Shows log metadata and Qt diagnostic messages in the File Info tab.
- Shows CompassTag calibration constants from the View menu when a log contains
  calibration data.
- Shows a narrow CompassTag orientation panel beside the plot when compass data
  is loaded.
- Supports print preview, UTC offset display, cursors, and zoom-to-cursor.

## Current Architecture

The code is split by responsibility:

- `main.cpp`: application startup and Qt message logging.
- `mainwindow.*`: static Qt UI construction and application state.
- `dataloading.cpp`: file-open workflow, stream replacement, and metadata
  display.
- `sqlite_loader.*`: SQLite read-only adapter that consumes tagcore stream
  metadata.
- `sensorstream.h`: normalized in-memory data model.
- `stream_actions.cpp`: stream visibility, View actions, and per-stream range
  actions.
- `transforms.cpp`: scalar display transforms such as altitude and activity
  low-pass.
- `compass_transforms.cpp`: CompassTag record-set transforms, heading display
  settings, and QML compass sample updates.
- `plotting.cpp`: QCustomPlot graph and axis rebuilds.
- `interaction.cpp`: cursors, print preview, UTC offset, context menus, and
  mouse readout.
- `controls.cpp`: small shared display helpers and general actions.
- `sensorui`: provides the shared CompassTag calibration dialog and QML
  orientation display used by both `sensorviz` and `compviz`.

## Maintenance Map

Most SensorViz changes should start in one of four places:

- New SQLite table or tag family:
  update the tagcore SQLite stream/table catalog first. Add scalar stream
  metadata for one-value time series, or grouped `record_column` metadata for
  multi-column data that will later feed transforms.
- Table exists but does not load:
  check `sqlite_loader.cpp`. The loader applies the database `streams` metadata
  to the actual SQLite schema; missing referenced tables are schema errors.
- File loads but menu state, default visibility, or metadata is wrong:
  check `dataloading.cpp`. It replaces the active `SensorLog`, rebuilds stream
  actions, clears old custom ranges, and updates File Info.
- Stream visibility or range behavior is wrong:
  check `stream_actions.cpp`.
- Scalar transform behavior is wrong:
  check `transforms.cpp`.
- CompassTag derived streams, declination, or battery direction are wrong:
  check `compass_transforms.cpp`.
- Plot axis layout or scaling is wrong:
  check `plotting.cpp`.
- Cursor, print, UTC, mouse readout, or context-menu behavior is wrong:
  check `interaction.cpp`.

`MainWindow` intentionally remains the coordination object. Avoid adding tag
type checks there when the behavior can be driven from stream ids, table
definitions, or transform definitions.

## Data Model

`SensorStream` is a single plottable scalar time series. This is what the plot
and View menu operate on.

`SensorRecordSet` is a multi-column time-indexed table that is loaded but not
plotted directly. It exists so future data such as compass accel/magnetometer
samples can be loaded first, then converted into streams by transforms.

Compass calibration constants are stored as typed metadata on `SensorLog`.
Keeping calibration beside the raw compass record set lets future compass
transforms derive heading, pitch, roll, and related streams without reparsing
the SQLite JSON.

The SQLite `streams` table describes how stored tables map into these data
structures. Adding a simple one-column sensor table should usually start in
`host/libraries/tagcore/sqlitelog.cc`, not in `MainWindow`.

## Plotting Rules

- Activity defaults to visible and uses a fixed `0-100` axis.
- Voltage defaults off, stays on the right axis, and uses fixed `0-5 V`.
- Core temperature defaults off, stays on the right axis, and uses fixed
  `0-50 C`.
- Other streams default to the left axis unless the database metadata says
  otherwise.
- Autoscaled y-axes get a 5% margin.
- Displayed streams can have explicit y-axis ranges set from the View menu or
  plot context menu.
- Normal redraws preserve the current x-axis range.
- Loading a new file and Reset Zoom expand to the full data range and restore
  default y-axis ranges.

## Stream Actions and Ranges

Raw streams get checkable entries under View > Visible Streams. The checked
state of those actions is the source of truth for which raw streams are plotted.

Altitude is generated automatically from pressure and appears in Visible
Streams; Configuration > Sea-level Pressure changes the mean sea-level pressure
used for that calculation. Low-pass activity is controlled by Configuration
instead of duplicated in the View menu. CompassTag plot streams are generated
automatically from the raw compass record set on load, and then appear in
Visible Streams so heading, acceleration, pitch, roll, dip, and field strength
can be shown independently.
For CompassTag logs, Configuration > Declination adjusts only the displayed
heading stream; Configuration > Battery Forward applies the matching 180 degree
display convention. Raw orientation-derived values remain magnetic-frame data.

The View > Ranges submenu and the plot context-menu Ranges submenu are rebuilt
from the currently displayed streams. Each range action stores the stream id in
`QAction::data()`, and `stream_actions.cpp` uses that id to find the matching
`SensorStream`.

Range precedence is:

1. A user-set custom range in `custom_axis_ranges_`.
2. A fixed metadata range from `SensorStream::axisRange`.
3. The data min/max padded by 5%.

`Reset Zoom` clears custom y-axis ranges and restores defaults. It also resets
the x-axis to the full loaded time range.

Pressure and altitude have one special relationship: if pressure has a custom
range and altitude does not, SensorViz derives an altitude range from the
pressure range so the two views stay visually comparable. Once the user sets an
altitude range directly, that altitude range is treated as independent.

Activity and Activity Filter share the same units, so their custom ranges are
kept equal until the user gives one of them an explicit range of its own.

## Adding A Display Transform

Transforms are still hardcoded in `transforms.cpp` and
`compass_transforms.cpp`, but they follow a consistent pattern:

1. Check that required input streams or record sets exist.
2. Prompt for transform-specific parameters.
3. Build a derived `SensorStream`.
4. Call `addOrReplaceStream()` so plotting, ranges, and context menus update.
5. Remove the derived stream when the transform action is unchecked.

Future transforms should keep computation separate from file loading. The
SQLite loader should only normalize on-disk data into streams, record sets, and
typed metadata; display math belongs in transform code.

## Future Direction

The next major goal is to prepare for compass-like logs without copying all of
`compviz` into this application at once. The intended path is:

1. Load multi-column compass data as a `SensorRecordSet` and load calibration
   constants as typed `SensorLog` metadata.
2. Use the shared `sensoranalysis` compass helpers for eCompass/orientation math.
3. Add an optional orientation panel later, analogous to the current `compviz`
   QML compass view.

## Build Check

Typical local check:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software-vcpkg-release --target sensorviz
git diff --check
```
