---
type: readme
status: current
summary: What sensorviz does, its architecture, design rules, data model, plotting rules, preferences, transforms, documentation capture hooks, known limitations and build check.
---

# sensorViz

`sensorviz` is the general Qt/QCustomPlot viewer for the SQLite sensor logs
written by the host download tools: BitTag, BitPresTag, PresTag, UIUCTag,
CompassTag and IMUTag. It replaces the retired `compviz`.

Open work is in [TODO.md](TODO.md). The original plan for the user-guide
screenshot hooks is kept, as history, in
[screenshot-capture-plan.md](design/proposals/screenshot-capture-plan.md); the
hooks as built are described under
[Documentation Capture Hooks](#documentation-capture-hooks).

## What It Does

- Loads SQLite log files produced by host tag download tools.
- Discovers available streams from the SQLite `streams` metadata table.
- Plots scalar streams such as pressure, activity, voltage, and temperature
  from BitTag, BitPresTag, PresTag, UIUCTag, CompassTag, and IMUTag SQLite
  logs.
- Supports both epoch-time logs and IMUTag elapsed-time logs.
- Provides display transforms such as:
  - altitude from pressure
  - low-pass filtered activity
  - IMUTag acceleration, gyroscope, and magnetic field magnitudes
  - CompassTag heading, acceleration magnitude, pitch, roll, dip, and magnetic
    field strength
- Shows log metadata and Qt diagnostic messages in the File Info tab.
- Shows CompassTag calibration constants from the View menu when a log contains
  calibration data.
- Shows a narrow CompassTag orientation panel beside the plot when compass data
  is loaded.
- Supports print preview, UTC offset display, draggable plot metadata, cursors,
  cursor hiding, and zoom-to-cursor.
- Shows an editable graph title above the plot. The title defaults to the
  loaded file name and can be hidden from the Configuration menu or the plot
  context menu's Configuration submenu.
- Applies stored magnetometer calibration to IMUTag magnetometer axes on load
  (`applyCalibrationToImuMagnetometer()` in `sqlite_loader.cpp`), so the axis
  streams and the derived magnitude use the same corrected data.
- Stores per-tag display preferences as sparse, formatted JSON overrides.

## Current Architecture

The code is split by responsibility:

- `main.cpp`: application startup and Qt message logging.
- `mainwindow.*`: static Qt UI construction, persistent actions, and shared
  application state.
- `dataloading.cpp`: File > Load workflow, active `SensorLog` replacement,
  default graph title, stream-action creation, File Info updates, and initial
  plot refresh.
- `sqlite_loader.*`: read-only SQLite adapter. It consumes tagcore's mandatory
  `streams` metadata table, loads scalar streams, groups multi-column
  `record_column` rows into `SensorRecordSet`, loads CompassTag calibration
  metadata, applies it to IMUTag magnetometer axes, and records
  collection-start metadata for elapsed-time IMUTag logs.
- `sensor_preferences.*`: sensorViz display defaults
  (`defaultDisplayForStream()`), in-memory per-tag overrides, and JSON
  load/store for those overrides.
- `sensorstream.h`: normalized in-memory data model (see
  [Data Model](#data-model)).
- `stream_actions.cpp`: visible-stream, axis-side, color and range dialogs,
  derived-stream ordering, and range coupling between related streams.
- `transforms.cpp`: scalar display transforms such as altitude, activity
  low-pass, and IMUTag vector magnitudes.
- `compass_transforms.cpp`: CompassTag record-set transforms, heading
  declination, battery-forward convention, and compass-panel sample updates.
- `plotting.cpp`: QCustomPlot graph rebuild, dynamic axes, metadata box
  contents, title placement, cursor placement, and range reset behavior.
- `interaction.cpp`: context menu, print preview, cursor interaction, draggable
  metadata box, UTC offset, and mouse readout.
- `controls.cpp`: general actions and small shared helpers, including graph
  title editing and calibration-constant display.
- `documentation_capture.cpp`: maintainer-only screenshot automation (see
  [Documentation Capture Hooks](#documentation-capture-hooks)).
- `sensorui`: provides the shared CompassTag calibration dialog and QML
  orientation display used by `sensorviz` and `qtcalibrate`.

## Design Rules

- SQLite logs describe the data contract: stream ids, labels, units, table
  names, time columns, value columns, and stream kind. Viewer policy (colors,
  default visibility, axis side, fixed display ranges) belongs in `sensorviz`,
  in `defaultDisplayForStream()`, never in the SQLite file.
- Feature availability is driven by loaded stream ids, record sets, or
  calibration metadata rather than hardcoded tag-type checks.
- Time-domain behavior is part of the normalized stream model. Epoch logs use
  date/time x-axis labels; IMUTag elapsed logs use elapsed seconds and show the
  absolute collection start only as plot metadata.
- The menu bar has `File`, `View`, `Configuration`, and `Help`, with
  `File > Preferences` as a submenu. The plot context menu mirrors `File`,
  `View` and `Configuration` but has no `Help`; its `File` submenu carries
  `About` instead.
  `View` groups the stream-display controls (Visible Streams, Axis Sides,
  Colors, Ranges); `Configuration` owns current-view and session parameters
  (graph title, UTC offset, sea-level pressure, activity filter, declination,
  battery-forward).
- Tag-specific controls stay hidden until the loaded log supports them:
  Sea-level Pressure when pressure data exists, Activity Filter when activity
  data exists, Declination and Battery Forward for CompassTag logs, and
  Calibration Constants only when calibration metadata exists.

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
- Default stream color, initial visibility, axis side, or fixed range is wrong:
  check `sensor_preferences.cpp`, especially `defaultDisplayForStream()`.
- Preference files load/store incorrectly:
  check `sensor_preferences.cpp`. Preference files are formatted JSON and store
  only overrides from the sensorViz defaults.
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
- Graph title editing or visibility is wrong:
  check `controls.cpp` for the dialog/state update and `mainwindow.cpp` for the
  title element created in the static plot layout.

`MainWindow` intentionally remains the coordination object. Avoid adding tag
type checks there when the behavior can be driven from stream ids, table
definitions, or transform definitions.

## Data Model

`SensorStream` is a single plottable scalar time series. This is what the plot
and View menu operate on.

Streams carry a time domain. Most existing tags use Unix epoch seconds; IMUTag
uses elapsed seconds converted from SQLite `ElapsedUs` columns. Plotting and
cursor readout choose the matching x-axis automatically.

Elapsed-time logs also carry an absolute collection-start timestamp when the
SQLite log provides one. SensorViz uses that value only as plot metadata; it
does not convert high-rate elapsed IMUTag samples back to wall-clock time.

`SensorRecordSet` is a multi-column time-indexed table that is loaded but not
plotted directly. CompassTag accelerometer/magnetometer samples are loaded this
way and then converted into streams by transforms.

Compass calibration constants are stored as typed metadata on `SensorLog`.
Keeping calibration beside the raw compass record set lets the compass
transforms derive heading, pitch, roll, and related streams without reparsing
the SQLite JSON.

The SQLite `streams` table describes how stored tables map into these data
structures. Adding a simple one-column sensor table should usually start in
`host/libraries/tagcore/sqlitelog/`, not in `MainWindow`.

## Plotting Rules

- Activity defaults to visible and uses a fixed `0-100` axis.
- Voltage defaults off, stays on the right axis, and uses fixed `0-5 V`.
- Core temperature defaults off, stays on the right axis, and uses fixed
  `0-50 C`.
- Other streams default to the left axis unless `defaultDisplayForStream()`
  assigns a different display policy.
- IMUTag raw pressure/temperature and `x/y/z` component streams default hidden
  on the right axis; derived IMUTag summary streams default visible on the left.
- Autoscaled y-axes get a 5% margin.
- Visible IMUTag `x/y/z` families share a y-axis range within each family so
  component traces are comparable.
- Displayed streams can have explicit y-axis ranges set from the View menu or
  plot context menu.
- Normal redraws preserve the current x-axis range.
- Loading a new file and Reset Zoom expand to the full data range and restore
  default y-axis ranges.
- Each loaded file starts with a visible graph title using the file name rather
  than the full path. The title is current-view state, not a saved preference.
- The metadata box shows only contextual rows. Start time appears for
  elapsed-axis logs, declination appears when heading is visible, and sea-level
  pressure appears when altitude is visible. The box can be dragged within the
  plot; the position is session state and is not saved.

## Preferences

sensorViz has two layers of display preference:

1. Built-in defaults in `defaultDisplayForStream()`.
2. Per-tag user overrides stored in memory and optionally written to JSON.

The SQLite log describes the data contract: stream id, label, units, table, time
column, and value column. It does not describe viewer policy such as color,
default visibility, axis side, or preferred fixed display range. Those defaults
belong to sensorViz so another analysis tool can interpret the same SQLite file
without inheriting this application's UI choices.

The File > Preferences submenu contains:

- `Load...`: replace the current in-memory preference set from a JSON file.
- `Store...`: write formatted JSON containing only overrides from defaults.
- `Load Defaults`: remove overrides for the currently loaded tag type and
  reapply built-in defaults.

Saved preference files may contain:

- `visible_streams`: present only when visibility differs from defaults.
- `colors`: stream id to color string for user-chosen colors.
- `axis_sides`: stream id to `left` or `right` for user-chosen axis placement.

Saved preference files intentionally do not contain y-axis ranges, sea-level
pressure, declination, UTC offset, or battery-forward. Those are session or
analysis context rather than durable tag-type display preferences.

At startup, `File > Load` and the `Help` menu are enabled. The rest of the menu
structure remains visible but disabled so users can see what controls will be
available after a log is loaded. Tag-specific actions (calibration constants,
sea-level pressure, activity filter, declination, battery forward) are hidden,
not disabled, until a loaded log supports them.

## Stream Actions and Ranges

Raw streams get checkable entries under View > Visible Streams. The checked
state of those actions is the source of truth for which raw streams are plotted.

Altitude is generated automatically from pressure and appears in Visible
Streams; Configuration > Sea-level Pressure changes the mean sea-level pressure
used for that calculation. For IMUTag logs, altitude is generated from
`imu_pressure` and uses the elapsed-time header temperature stream when
available.

IMUTag acceleration, gyroscope, and magnetic field magnitudes are generated
automatically from their corresponding `x/y/z` streams. They appear adjacent to
their source axes in Visible Streams.

Low-pass activity is controlled by Configuration instead of duplicated in the
View menu. CompassTag plot streams are generated automatically from the raw
compass record set on load, and then appear in Visible Streams so heading,
acceleration, pitch, roll, dip, and field strength can be shown independently.
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

Derived streams that should be listed beside their inputs need an entry in
`sourceOrderForStream()` in `stream_actions.cpp`; otherwise they append to the
end of the stream list.

## Documentation Capture Hooks

SensorViz includes maintainer-only command-line hooks for user-guide
screenshot generation. With no capture options, startup remains the normal
interactive workflow.

Supported options:

```sh
sensorviz \
  --load-log <path> \
  --load-preferences <path> \
  --capture-screenshot <name> \
  --capture-suite <startup|menus|imutag|compasstag|bitprestag|all> \
  --screenshot-dir <dir> \
  --no-user-prompts
```

`--load-log` uses the same SQLite loader and `applyLoadedLog()` path as
File > Load, but runs synchronously so capture code can wait until stream
actions, transforms, metadata, and the plot are ready. `--load-preferences`
loads the normal SensorViz JSON preference file and reapplies it to the loaded
tag before capture.

`documentation_capture.cpp` owns the automation flow. It starts after the main
window is shown, optionally captures the startup window before loading a log,
loads fixture data, waits for Qt paint events to settle, and writes PNG files.
The default output directory is `host/docs/src/images`; pass
`--screenshot-dir` for test runs or staging.

Named captures currently supported:

```text
startup
main-window
file-info
file-menu
preferences-menu
view-menu
ranges-menu
configuration-menu
help-menu
popup-menu
imutag-plot
imutag-file-info
compasstag-plot
compasstag-file-info
compass-view
bitprestag-plot
bitprestag-file-info
```

Menu captures reuse the live top-level `QMenu` objects created in
`mainwindow.cpp`. The plot context menu is built by `createPlotContextMenu()`
in `interaction.cpp`, which is shared by right-click interaction and capture
automation. Keep that builder as the single source of truth when adding context
menu entries.

There is no dialog screenshot suite. The dialogs in `stream_actions.cpp`,
`controls.cpp`, and `transforms.cpp` are blocking, so they cannot be shown
modelessly, captured, and closed without user input; the refactor that would
allow it is in [TODO.md](TODO.md).

Example local checks:

```sh
sensorviz --capture-screenshot startup --screenshot-dir /tmp/sensorviz-shots --no-user-prompts
sensorviz --load-log host/docs/fixtures/sensorviz/imutag.db3 \
  --capture-suite imutag --screenshot-dir /tmp/sensorviz-shots --no-user-prompts
sensorviz --load-log host/docs/fixtures/sensorviz/compasstag.db3 \
  --capture-suite compasstag --screenshot-dir /tmp/sensorviz-shots --no-user-prompts
```

Only `imutag.db3` and `compasstag.db3` are committed under
`host/docs/fixtures/sensorviz/`; there is no BitPresTag fixture, so the
`bitprestag` suite needs a BitPresTag log supplied with `--load-log`.

## Known Limitations

- Transform definitions are hardcoded in `transforms.cpp` and
  `compass_transforms.cpp`, and transform parameter dialogs are simple
  action-specific dialogs, not a common transform configuration framework.
- Display preferences are loaded and stored manually from JSON files; there is
  no automatic recent or default preference file.
- Current-view session parameters (graph title, ranges, sea-level pressure,
  declination, UTC offset, battery-forward, metadata-box position) are
  deliberately not persisted.
- Multi-column record sets are used mainly for CompassTag data.
- There are no automated GUI tests for menu organization, preference
  load/store, metadata-box interaction, or plot-title behavior.

Planned work on these is in [TODO.md](TODO.md).

## Build Check

Typical local check:

```sh
cmake --build <build-dir> --target sensorviz
git diff --check
```

When changing SQLite loading or stream metadata, also test with representative
BitTag, BitPresTag, PresTag, CompassTag, and IMUTag logs.
