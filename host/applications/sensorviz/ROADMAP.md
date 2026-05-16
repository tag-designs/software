# sensorViz Development Notes

This note captures the current design state so future work can resume without
reconstructing the whole conversation from git history.

## Current State

`sensorviz` is a Qt/QCustomPlot application for viewing SQLite sensor logs from
multiple tag types. It currently handles scalar streams such as pressure,
activity, voltage, and temperature, plus display-only transforms such as
altitude, low-pass activity, and CompassTag-derived scalar streams.

Recent checkpoint:

- `9b7bba8 Refactor sensorviz profiles and plot ranges`
- SQLite log stream/table definitions have moved into tagcore. New logs carry
  a mandatory `streams` table that sensorViz uses as the schema authority.

The important architecture pieces are:

- `sensorstream.h`: loaded data model.
  - `SensorStream` is a plottable scalar time series.
  - `SensorRecordSet` is a multi-column time-indexed table for future data such
    as compass accel/magnetometer samples.
  - `SensorLog` contains metadata, streams, and record sets.

- `sqlite_loader.*`: SQLite adapter.
  - Reads tagcore's mandatory `streams` metadata table.
  - Loads scalar stream rows into `SensorStream`.
  - Loads grouped `record_column` rows into `SensorRecordSet`.
  - Missing metadata or referenced tables are schema errors.

- `dataloading.cpp`: file load workflow.
  - Replaces current `SensorLog`.
  - Builds View menu actions from loaded streams.
  - Uses stream metadata for default visibility.

- `stream_actions.cpp`: stream visibility and range actions.
  - Builds and clears View stream actions.
  - Builds View > Ranges from currently displayed streams.
  - Owns custom y-axis range dialogs and pressure/altitude range coupling.

- `transforms.cpp`: scalar display transforms.
  - Handles altitude and activity low-pass transform toggles.
- `compass_transforms.cpp`: CompassTag record-set transforms.
  - Handles the CompassTag derived-stream family from `compass_raw` plus
    calibration metadata.
  - Altitude and activity filter are controlled from Configuration only, while
    CompassTag derived streams are generated from Configuration and then exposed
    individually in View.

- `plotting.cpp`: QCustomPlot graph and axis rebuilds.
  - Normal redraws preserve the current x-axis range; load/reset use full range
    and restore default y-axis ranges.
  - Autoscaled y-axes receive a 5% margin.

- `interaction.cpp`: post-load user interaction.
  - Handles cursors, printing, UTC offset, mouse readout, and context menus.

- `controls.cpp`: shared display helpers and general actions.

## Current UI Decisions

- Activity defaults to visible and uses a fixed `0-100` axis.
- Voltage defaults off, stays on the right axis, and uses fixed `0-5 V`.
- Core temperature defaults off, stays on the right axis, and uses fixed
  `0-50 C`.
- Other streams default to the left axis unless the SQLite stream metadata says
  otherwise.
- Cursors are hidden until data is loaded.
- Altitude and Activity Filter live under Configuration and are not duplicated
  in the View menu.
- CompassTag plot streams are created automatically on load and appear in
  Visible Streams with the other plotted series.
- Declination is a CompassTag-only Configuration action that adjusts the
  displayed heading stream without reloading the file.
- Battery Forward is a CompassTag-only Configuration action that rotates the
  heading display and QML compass convention without changing loaded samples.
- A narrow CompassTag orientation panel appears beside the plot when compass
  data is loaded and follows the nearest sample under the mouse.

## Future Compass / compViz Integration

The plan is not to copy `compviz` wholesale into `sensorviz`. Instead:

1. Keep `compviz` working as-is for now.
2. Use `SensorRecordSet` to load compass raw data:
   - table: `Compass`
   - metadata kind: `record_column`
   - columns: `ax`, `ay`, `az`, `mx`, `my`, `mz`
   - group id: `compass_raw`
3. Load the latest CompassTag calibration row into typed `SensorLog` metadata.
4. Use the shared `sensoranalysis` eCompass/orientation helpers from a transform
   module.
5. Generate scalar streams for heading, acceleration magnitude, pitch, roll,
   dip, and magnetic field strength.
6. Continue validating CompassTag defaults against real logs.

## Likely Next Refactors

- Move transform parameter dialogs behind transform-specific configuration
  helpers.
- Add persistent transform/display settings with `QSettings`.
- If more multi-column tables appear, generalize record-set transforms before
  adding new UI-specific code.

## Testing Notes

The usual local verification command has been:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software-vcpkg-release --target sensorviz
```

Also run:

```sh
git diff --check
```
