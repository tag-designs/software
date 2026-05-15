# qtcalibrate

`qtcalibrate` is the host application for collecting live magnetometer samples,
fitting magnetometer calibration constants, saving/loading those constants on a
tag, and previewing the resulting compass orientation.

## Source Layout

| Path | Purpose |
| --- | --- |
| `mainwindow.*` and `mainwindow.ui` | Qt Widgets application shell: tag attach/detach, calibration controls, log window, menus, timers, and the embedded QML orientation views. |
| `compassdata.*` | Application-owned live calibration state. It feeds raw magnetometer samples into the inherited magcal solver, exposes calibration constants to the UI/tag, applies calibration to displayed points, and delegates eCompass orientation solving to `sensoranalysis::CompassProcessor`. |
| `magplot.*` | Interactive 2D widget that draws calibrated magnetometer samples as a rotatable sphere projection during calibration. |
| `magcal/` | Inherited C calibration code: solver, matrix helpers, and quality metrics. Keep algorithm changes isolated and well documented. |
| `sinbin.*` | Small trigonometric lookup/helper code used by the inherited calibration routines. |
| `Magnetic Calibration.pdf` | Reference notes for the calibration approach. |

The app no longer owns local compass/attitude QML. Runtime orientation display
comes from `host/libraries/sensorui`:

- `CompassDisplay` wraps `orientation_frame/MyCompass.qml`.
- `AttitudeDisplay` wraps `orientation_frame/MyAttitude.qml`.

The app calls `initializeSensorUiResources()` before loading the shared
`qrc:/qfi/...` QML URLs.

## Runtime Flow

1. `MainWindow::Attach()` finds a base, attaches to a tag, and reads tag config
   and status.
2. The stream checkbox starts periodic `TriggerUpdate()` polling.
3. Each streamed calibration sample may contain magnetometer and accelerometer
   readings.
4. If calibration is active, magnetometer samples go through
   `CompassData::addData()`, which updates the inherited magcal solver and then
   refreshes `magPlot`.
5. If orientation can be computed, `CompassData::eCompass()` applies the current
   calibration, low-pass filters the live vectors, and uses `CompassProcessor`
   for the shared eCompass quaternion solve.
6. `MainWindow` sends the resulting Euler values to `CompassDisplay` and the
   display quaternion to `AttitudeDisplay`.

## Calibration Constants

Calibration constants move between three representations:

- inherited solver state in the global `magcal` struct,
- protobuf `CalibrationConstants_MagConstants` on the tag,
- UI labels in the calibration tab.

`CompassData::getCalibrationConstants()` and
`CompassData::setCalibrationConstants()` are the boundary between `MainWindow`
and inherited solver state.

## Menus And Logging

The orientation view has a top-level `Configuration` menu and matching context
menu entries for declination and battery-forward display convention. Those are
display settings only; they do not change stored calibration constants.

Streaming pitch/roll/yaw samples are logged at `TRACE` through `log_trace()` so
normal log levels do not flood the log window.
