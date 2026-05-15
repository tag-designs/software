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

The important architecture pieces are:

- `sensorstream.h`: loaded data model.
  - `SensorStream` is a plottable scalar time series.
  - `SensorRecordSet` is a multi-column time-indexed table for future data such
    as compass accel/magnetometer samples.
  - `SensorLog` contains metadata, streams, and record sets.

- `sensorprofile.*`: profile and table definitions.
  - Defines scalar stream table mappings.
  - Defines record set mappings such as future `compass_raw`.
  - Defines transform metadata for altitude and activity low-pass.
  - Owns default visibility, axis side, and fixed axis ranges.

- `sqlite_loader.*`: SQLite adapter.
  - Reads profile definitions and loads optional tables when present.
  - Missing tables are normal; present malformed tables are errors.
  - Loads multi-column record sets but does not process them yet.

- `dataloading.cpp`: file load workflow.
  - Replaces current `SensorLog`.
  - Builds View menu actions from loaded streams.
  - Uses stream metadata for default visibility.

- `stream_actions.cpp`: stream visibility and range actions.
  - Builds and clears View stream actions.
  - Builds View > Ranges from currently displayed streams.
  - Owns custom y-axis range dialogs and pressure/altitude range coupling.

- `transforms.cpp`: display-only derived streams.
  - Handles altitude and activity low-pass transform toggles.
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
- Other streams default to the left axis unless the profile says otherwise.
- Cursors are hidden until data is loaded.
- Altitude and Activity Filter live under Configuration and are not duplicated
  in the View menu.
- Compass Derived Streams lives under Configuration and creates individual View
  actions for heading, acceleration, pitch, roll, dip, and magnetic field.
- Declination is a CompassTag-only Configuration action that adjusts the
  displayed heading stream without reloading the file.

## Future Compass / compViz Integration

The plan is not to copy `compviz` wholesale into `sensorviz`. Instead:

1. Keep `compviz` working as-is for now.
2. Use `SensorRecordSet` to load compass raw data:
   - table: `Compass`
   - columns: `ax`, `ay`, `az`, `mx`, `my`, `mz`
   - record id: `compass_raw`
3. Load the latest CompassTag calibration row into typed `SensorLog` metadata.
4. Use the shared `sensoranalysis` eCompass/orientation helpers from a transform
   module.
5. Generate scalar streams for heading, acceleration magnitude, pitch, roll,
   dip, and magnetic field strength.
6. Later, add a battery-direction display setting for heading.
7. Later, add an optional specialized orientation panel analogous to the current
   `compviz` QML compass view.

## Likely Next Refactors

- Generate transform actions from `SensorTransformDefinition` instead of keeping
  altitude/activity filter actions hardcoded in `MainWindow`.
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
