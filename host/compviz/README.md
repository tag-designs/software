# CompViz Architecture Notes

CompViz displays SQLite logs from CompassTags. It shows scalar streams in a
`QCustomPlot` window and shows the current orientation sample in a QML compass
widget. The code is organized so that file loading, compass math, QML display,
and MainWindow UI coordination stay separate.

## File Map

- `mainwindow.cpp/.h` owns the top-level Qt window, menu wiring, graph creation,
  loaded vectors, and user settings such as UTC offset and declination.
- `sqliteload.cpp` is the MainWindow load action. It opens the selected SQLite
  file, loads simple scalar streams, delegates compass-specific loading to
  `compass_sqlite_loader.cpp`, then pushes vectors into the graphs.
- `sqlite_utils.h` contains the small RAII wrappers around `sqlite3` database
  and statement handles. These wrappers are intentionally UI-free and shared by
  the MainWindow load path and the compass loader.
- `../sensoranalysis/compass_types.h/.cpp` defines the data passed between layers:
  `CompassCalibration`, `CompassRawSample`, `CompassDerivedSample`, and
  `CompassLogData`.
- `compass_sqlite_loader.cpp/.h` reads the CompassTag calibration and compass
  sample tables. It returns magnetic-frame derived samples and does not know
  about graphs, menus, QML, declination, or battery direction.
- `../sensoranalysis/compass_processor.cpp/.h` owns calibration application and
  the eCompass quaternion algorithm. It is the only place that should contain
  compass orientation math.
- `../sensorui/compass_display.cpp/.h` is a thin adapter between C++ samples and
  `../sensorui/orientation_frame/MyCompass.qml`. It hides QML method names from
  `MainWindow` and `controls.cpp`.
- `controls.cpp` owns user actions after data is loaded: graph visibility,
  declination changes, battery-forward changes, cursor zooming, printing, and
  mouse-over display updates.
- `../sensorui/compass_calibration_dialog.cpp/.h/.ui` displays the currently
  loaded magnetometer calibration constants.
- `../sensorui/orientation_frame/` contains the QML compass display and image
  assets.

## Data Flow

1. `MainWindow::on_pb_load_clicked()` opens the SQLite file.
2. `sqliteload.cpp` verifies the `Info` table reports `COMPASSTAG`.
3. Simple scalar streams are loaded directly: activity, voltage, and core
   temperature.
4. `loadCompassData()` reads the most recent calibration row and the raw
   `Compass` rows.
5. `CompassProcessor` applies the magnetometer calibration and computes the
   orientation quaternion, dip, field strength, acceleration magnitude, and
   magnetic yaw for each row.
6. `sqliteload.cpp` stores those derived samples in `MainWindow::orientation`
   and calls `updateHeadingGraph()`.
7. `updateHeadingGraph()` applies user display settings to magnetic yaw:
   declination converts magnetic north to true north, and battery direction can
   rotate the displayed heading by 180 degrees.
8. Mouse movement over the plot selects a nearby orientation sample and passes
   it to `CompassDisplay`, which updates the QML widget.

## Key Design Decisions

Loaded compass samples are magnetic-frame data. Declination is not part of the
SQLite loader or the derived sample because it is a user-selected display
setting. This keeps the file-derived orientation reproducible and lets the user
change true-north display without reloading or recomputing calibration.

Battery-forward is also a display setting. It changes the heading graph and QML
display behavior, but it should not alter the stored orientation samples.

The eCompass algorithm was moved almost verbatim from the old `compassdata.cpp`
into `CompassProcessor`. Future fixes to compass math should happen there, and
callers should treat `CompassProcessor::deriveSample()` as the boundary between
raw rows and displayable orientation data.

Calibration is represented by `CompassCalibration`, not by loose `Hcal` and
`Scal` arrays on `MainWindow`. This makes calibration parsing, display, and math
share a single type.

The QML compass widget is isolated behind `CompassDisplay`. If the QML method
names or properties change, update `../sensorui/compass_display.cpp` rather
than spreading `QMetaObject::invokeMethod()` calls through the app.

## Common Maintenance Tasks

To change how SQLite rows are read, start in `compass_sqlite_loader.cpp`.

To change orientation math, calibration application, yaw convention, or computed
field values, start in `../sensoranalysis/compass_processor.cpp`.

To change how declination or battery direction affects the plotted heading,
start with `MainWindow::updateHeadingGraph()` in `controls.cpp`.

To change what appears in the mouse-over tooltip or when the QML compass updates,
look at `MainWindow::onMouseMove()` in `controls.cpp`.

To change the visual QML compass itself, edit
`../sensorui/orientation_frame/MyCompass.qml`. If only the C++ to QML bridge
changes, edit `../sensorui/compass_display.cpp`.

## Future Refactoring Notes

The compass processor and types are deliberately UI-free and now live in
`../sensoranalysis`. Reusable Qt UI pieces live in `../sensorui`. SensorViz
should depend on those libraries when it absorbs CompassTag support rather than
copying CompViz-specific code.

The simple scalar-stream loading in `sqliteload.cpp` is still MainWindow-owned.
If CompViz grows beyond CompassTag logs, those streams could be moved into a
small log loader similar to `compass_sqlite_loader.cpp`.
