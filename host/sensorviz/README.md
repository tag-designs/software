# sensorViz

`sensorviz` is a Qt/QCustomPlot application for viewing SQLite sensor logs. It
is intended to grow into the general viewer for pressure, activity, and other
sensor-oriented tag logs.

For the longer design/history note, see [ROADMAP.md](ROADMAP.md).

## What It Does

- Loads SQLite log files produced by host tag download tools.
- Discovers available streams from the tables present in the file.
- Plots scalar streams such as pressure, activity, voltage, and temperature.
- Provides display transforms such as:
  - altitude from pressure
  - low-pass filtered activity
- Shows log metadata and Qt diagnostic messages in the File Info tab.
- Supports print preview, UTC offset display, cursors, and zoom-to-cursor.

## Current Architecture

The code is split by responsibility:

- `main.cpp`: application startup and Qt message logging.
- `mainwindow.*`: static Qt UI construction and application state.
- `dataloading.cpp`: file-open workflow, stream action creation, and metadata
  display.
- `sqlite_loader.*`: SQLite read-only adapter.
- `sensorstream.h`: normalized in-memory data model.
- `sensorprofile.*`: known stream/table definitions, default visibility, axis
  defaults, record-set definitions, and transform metadata.
- `controls.cpp`: plotting, stream visibility, transform actions, ranges,
  cursors, printing, context menus, and mouse readout.

## Data Model

`SensorStream` is a single plottable scalar time series. This is what the plot
and View menu operate on.

`SensorRecordSet` is a multi-column time-indexed table that is loaded but not
plotted directly. It exists so future data such as compass accel/magnetometer
samples can be loaded first, then converted into streams by transforms.

`SensorProfile` describes how SQLite tables map into those data structures.
Adding a simple one-column sensor table should usually start in
`sensorprofile.cpp`, not in `MainWindow`.

## Plotting Rules

- Activity defaults to visible and uses a fixed `0-100` axis.
- Voltage defaults off, stays on the right axis, and uses fixed `0-5 V`.
- Core temperature defaults off, stays on the right axis, and uses fixed
  `0-50 C`.
- Other streams default to the left axis unless the profile says otherwise.
- Autoscaled y-axes get a 5% margin.
- Normal redraws preserve the current x-axis range.
- Loading a new file and Reset Zoom expand to the full data range.

## Future Direction

The next major goal is to prepare for compass-like logs without copying all of
`compviz` into this application at once. The intended path is:

1. Load multi-column compass data as a `SensorRecordSet`.
2. Move eCompass/orientation math into reusable helper code.
3. Add transforms that produce scalar streams such as heading, acceleration
   magnitude, pitch, roll, dip, and field strength.
4. Add an optional orientation panel later, analogous to the current `compviz`
   QML compass view.

## Build Check

Typical local check:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software-vcpkg-release --target sensorviz
git diff --check
```
