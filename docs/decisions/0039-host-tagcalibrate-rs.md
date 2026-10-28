---
type: decision
status: accepted
summary: qtcalibrate was ported to Rust as tagcalibrate-rs on Tag Monitor's pattern, with its calibration not ported but moved into sensoranalysis's Qt-free C++ core - shown bit-identical to the Qt code at each step - and reached from Rust through tagcore_c and calibsession-rs; the engine reproduces qtcalibrate's replay log line for line.
---

# 0039. Host: Tag Calibrator in Rust (tagcalibrate-rs)

Date: 2026-10-28

## Context

Tag Programmer ([0035](0035-host-tagprogram-rs.md)) and Tag Monitor
([0037](0037-host-tagmonitor-rs.md)) had been ported to Rust and Tauri, and
their pages moved onto Preact on the shared UI kit
([0038](0038-host-ui-kit-preact.md)). qtcalibrate was next. A web mockup of
its two graphics screens -- the sample sphere and the orientation view -- had
been made earlier, and the user asked for the window to be built from it
first, then for the port.

What made qtcalibrate different from the other two:

- **Its value is arithmetic.** The magnetometer fit (Freescale's magcal
  solver), the eviction policy that decides which samples the solver sees,
  the accelerometer's zero-g fit, the quality figures and the eCompass are
  months of measured work, and every stored calibration and every compass
  display so far came from them. Most of it lived in qtcalibrate and in
  sensoranalysis classes whose interfaces were Qt types.
- **Bit compatibility was already the rule.** sensoranalysis's compass solve
  had been moved onto a Qt-free core that reproduces `QVector3D` and
  `QQuaternion` exactly, and the user had asked that the move to glm or
  Eigen wait until it can be measured.

## Decision

- **Do not port the calibration; move it.** In four steps, each shown to
  change nothing before the next:
  1. the calibration-sweep classes (`Directions`, `MagQuality`,
     `MagRetention`, `GravityFit`, `AccelCalibration`) onto
     `sensoranalysis_core`, with the Qt classes kept as adapters;
  2. the magcal solver from qtcalibrate into the core;
  3. qtcalibrate's `CompassData` into a Qt-free `CalibrationSession`, owning
     the buffer, the solver state (per session, no longer a global),
     retention, the accelerometer fit, the quality figures and the filtered
     compass, with `CompassData` an adapter over it;
  4. the compass display angles (Qt's `toEulerAngles` convention) into the
     core, ported from Qt's source.
- **Reach it from Rust through `tagcore_c`.** A `tcs_*` C interface beside
  the log writer's `tlw_*` (floats in, plain C structs out), wrapped by the
  `calibsession` crate. Rust may depend on C++
  ([rust-over-cpp-libraries](../../host/libraries/tagcore/design/proposals/rust-over-cpp-libraries.md));
  one calibration, not two.
- **Port qtcalibrate's window logic on Tag Monitor's pattern.** A toolkit-free
  engine with a tag thread and an engine thread, a Tauri shell, and a page
  that only draws, every dialog an engine prompt
  ([contract](../../host/applications/tagcalibrate-rs/design/ui-engine-contract.md)).
  Every text is formatted by the engine as qtcalibrate formats it, and each
  quality update logs qtcalibrate's four debug lines in its format.
- **Draw the graphics in the browser.** The sphere in three.js (r159, the
  last classic build, vendored so the page opens from a file with no build
  step), drawn and turned as magPlot draws and turns it; the heading
  indicator in SVG; the tag model in three.js, as `MyAttitude.qml` builds it.
- **Keep qtcalibrate's capture file**, schema and layout, so captures from
  either program replay in both and the fixtures stay one format.

## Alternatives considered

- **Port the calibration to Rust.** A second implementation of the solver,
  retention and quality code would have to be kept in step with the first
  indefinitely, and could not be shown identical to it at the bit level
  without carrying the C++ as an oracle anyway.
- **Call the Qt classes from Rust.** Their interfaces take `QVector3D`, which
  is QtGui: the Rust app would have linked QtGui for arithmetic.
- **Use glm or Eigen in the core now.** Deferred, as the user had decided for
  the compass solve: it changes results in the last bits and is a measured
  change, not part of a port.
- **Draw the sphere as a lit 3D sphere.** Tried in the mockup. Every sample
  lies on the sphere's surface, so samples and surface fight for depth; the
  shipped view draws magPlot's flat shaded disc and pales the far side
  explicitly.

## Evidence

- **Each core move bit-identical.** `test/calibration_dump.cc`, new, prints
  every result of the five classes as hex floats over both CompassTag
  captures and 40 synthetic spheres: 4,913 lines, identical before and after.
  qtcalibrate's replay logs of both fixtures were byte-identical after every
  step. `calibration_session_check` compares the session's filtered compass
  and load-from-tag path with a verbatim copy of `CompassData` (48,702 values;
  a one-ulp change fails 25,022), and `compass_core_parity_check`'s new angles
  group compares the display angles with Qt's compiled `QQuaternion`
  (1,000,008 cases; computing one subtraction in double fails 133,605). The
  Linux build is clean.
- **Rust reproduces qtcalibrate.** `calibsession-rs`'s replay test reaches
  every figure qtcalibrate logged for the reference capture, and
  `tagcalibrate-core`'s replay test logs the same 1,552 quality lines as
  qtcalibrate's replay of it, character for character.
- **On a tag.** The user calibrated an IMUTagNandBmp581 through the app. A
  first capture's dip of -33.8 ± 34.0 degrees exposed firmware built before
  the board's magnetometer axis mapping; after a reflash the capture scored
  63.71 ± 2.86 degrees, matching the CompassTag reference, and is now a
  fixture with a test of its own (`core/tests/imutag_axes.rs`). The same
  session found that quitting mid-stream left the tag in CALIBRATE; Quit now
  waits for IDLE.

## Consequences

- qtcalibrate and Tag Calibrator run the same calibration code. A change to
  it is made once, in `sensoranalysis/core`, and shows in both.
- sensoranalysis's calibration classes have Qt adapters over a Qt-free core,
  as its compass code had. `calibration_dump` stays as the tool for any
  change that must not alter results.
- `tagcore_c` carries a second interface and links `sensoranalysis_core`,
  which is now compiled position-independent.
- The solver still counts its refit cadence in a function-level static,
  shared by every session in a process. One session per process, as both
  apps have, behaves as qtcalibrate always has; making it per session would
  change when refits happen, and is left as a change to measure.
- Not yet done: CMake and packaging (as for Tag Monitor), a user manual
  page, and retiring qtcalibrate.
