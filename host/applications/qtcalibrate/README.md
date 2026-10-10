---
type: readme
status: current
summary: qtcalibrate source layout, runtime flow, sample capture and replay, calibration constants and menus.
---

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

Every command below is run from the build output directory, where the binary
lands -- `<build-dir>/<config>/bin` -- hence `./qtcalibrate`. A fixture path
written relative to the repository root still resolves from there:
`resolveReplayCapturePath()` tries the path as given, then the same path under
the source tree, whose location is compiled in. Output paths such as
`--log-file` are not resolved that way and land in the current directory.

For documentation and fixture review, `qtcalibrate` can load a saved sample
capture instead of attaching to a USB tag:

```sh
./qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json
```

The replay path presents the window as a fake attached tag. Enabling
**Stream** replays captured samples through the same calibration UI path used by
live samples, so **Start** and **Stop** also exercise sample capture.

Use `--replay-percent` to prefill the Calibrate tab for static screenshots:

```sh
./qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json --replay-percent 25
```

Supported milestone values are ordinary percentages from 0 to 100. A 0 percent
milestone starts collection without feeding samples. A 100 percent milestone
feeds the full fixture and finalizes the capture.

To generate the baseline Calibrate-tab documentation screenshots and exit:

```sh
./qtcalibrate --capture-startup-screenshot

./qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json \
  --capture-replay-screenshots

./qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json \
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

## Sample Retention

Once the calibration buffer is full every new sample displaces an old one, and
which one is discarded decides what the solver ever sees.

[`MagRetention`](../../libraries/sensoranalysis/magretention.h) discards the
sample of lowest leverage -- the one contributing least to determining the fit
-- or, ahead of that, one whose studentized residual says the reading itself is
wrong, with a patch floor and a probation window as guards. It declines when
there are too few samples or the design is one the data cannot determine, and
the caller then takes a slot at random: that is exactly when discarding on
leverage would be guesswork.

This replaced an inherited nearest-pair scan, which was selected by a
`--leverage-retention` flag while the two were compared and is gone along with
`magcal/quality.c`, whose gap figure it branched on. The comparison is in
[the investigation](../../docs/design/investigations/2026-10-qtcalibrate-quality-experiments.md).

A replay is reproducible, which is what let that comparison mean anything:

```sh
./qtcalibrate --replay-capture <capture.json> --replay-exit --log-file run.txt
```

`--replay-exit` starts the sweep, runs it to the end of the capture and quits,
and `--log-file` writes the `DEBUG` lines the log window would show. Without
them a run ends wherever **Save Log** is clicked, and two runs of one capture
differ by more than most of the things worth measuring.

`log_set_quiet(true)` means the `DEBUG` lines never reach stdout or stderr, so
`--log-file` is the only way to capture them without the window.

A run logs a `retention:` line with the eviction count and how many each rule
decided, a `gravity:` line with the fitted accelerometer offset, and the
`magquality:` metrics.

## Calibration Constants

Calibration constants move between three representations:

- inherited solver state in the global `magcal` struct,
- protobuf `CalibrationConstants` on the tag: `MagConstants` (hard-iron V,
  soft-iron A) and `AccelConstants` (zero-g offset, mg),
- UI labels in the calibration tab.

`CompassData::getCalibrationConstants()` and
`CompassData::setCalibrationConstants()` are the boundary between `MainWindow`
and inherited solver state. **Save** writes the magnetometer constants and the
accelerometer offset together; without an accelerometer fit the offset is
written as zero. The tag does not store the field magnitude B, so **Load**
recomputes it from any buffered samples. A loaded accelerometer offset is
installed with `CompassData::setAccelOffset()` and stands until a live fit
replaces it.

## Menus And Logging

The orientation view has a top-level `Configuration` menu and matching context
menu entries for declination and battery-forward display convention. Those are
display settings only; they do not change stored calibration constants.

The log level combo in the log pane selects what reaches the log window; it
opens at `INFO`.

Streaming pitch/roll/yaw samples are logged at `TRACE` through `log_trace()` so
normal log levels do not flood the log window: there is one such line per
streamed sample.

The calibration quality metrics from `sensoranalysis`'s `MagQuality` are logged
at `DEBUG`, once per quality timer tick, as a line beginning `magquality:`.
They are deliberately a level above the per-sample flood so they can be read
without it. They are logged rather than displayed because they are computed
alongside the inherited four metrics while the two are compared; see
[the replacement proposal](../../docs/design/proposals/qtcalibrate-quality-replacement.md).
