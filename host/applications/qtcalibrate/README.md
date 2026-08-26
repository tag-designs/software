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
5. While calibration is active, `MainWindow` also keeps an in-memory sample
   capture of the raw calibration-window magnetometer stream and any paired
   accelerometer values.
6. If orientation can be computed, `CompassData::eCompass()` applies the current
   calibration, low-pass filters the live vectors, and uses `CompassProcessor`
   for the shared eCompass quaternion solve.
7. `MainWindow` sends the resulting Euler values to `CompassDisplay` and the
   display quaternion to `AttitudeDisplay`.

## Sample Capture

Pressing **Start** begins an in-memory calibration sample capture. Pressing
**Stop**, disabling streaming, or detaching finalizes the capture. The
**File > Save Sample Capture** action is enabled after a stopped capture has at
least one magnetometer sample.

The archive is a JSON file with schema
`tag-designs.qtcalibrate.calibration-capture.v1`. It records the received
magnetometer samples, any paired accelerometer samples, capture start/stop
times, and the fitted calibration constants and quality metrics when the solver
has produced a valid calibration.

## Sample Replay

For documentation and fixture review, `qtcalibrate` can load a saved sample
capture instead of attaching to a USB tag:

```sh
qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json
```

The replay path presents the window as a fake attached tag. Enabling
**Stream** replays captured samples through the same calibration UI path used by
live samples, so **Start** and **Stop** also exercise sample capture.

Use `--replay-percent` to prefill the Calibrate tab for static screenshots:

```sh
qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json --replay-percent 25
```

Supported milestone values are ordinary percentages from 0 to 100. A 0 percent
milestone starts collection without feeding samples. A 100 percent milestone
feeds the full fixture and finalizes the capture.

To generate the baseline Calibrate-tab documentation screenshots and exit:

```sh
qtcalibrate --capture-startup-screenshot

qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json \
  --capture-replay-screenshots

qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json \
  --capture-orientation-screenshot
```

By default this writes `qtcalibrate-startup.png`,
`qtcalibrate-collection-000.png`, `qtcalibrate-collection-025.png`,
`qtcalibrate-collection-050.png`, `qtcalibrate-collection-100.png`, and
`qtcalibrate-orientation-forward.png` and
`qtcalibrate-orientation-backward.png` into `host/docs/src/images/`. Use
`--screenshot-dir` or `--screenshot-prefix` to override the replay milestone
defaults.

Orientation screenshots use a fixed documentation pose by default. Override it
with `--orientation-pose heading,pitch,roll,dip,field,gravity` when a different
heading or attitude is clearer.

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
