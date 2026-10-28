---
type: readme
status: current
summary: Contents of the UI-free sensoranalysis library for compass calibration and orientation.
---

# SensorAnalysis Library

`sensoranalysis` contains UI-free sensor-domain processing shared by host
applications.

Current contents:

- `CompassCalibration`: hard-iron and soft-iron magnetometer calibration from
  SQLite calibration JSON or live qtcalibrate constants.
- `CompassRawSample`: raw accelerometer/magnetometer row data.
- `CompassDerivedSample`: magnetic-frame orientation, field, dip, and
  acceleration magnitude derived from raw samples.
- `CompassProcessor`: eCompass orientation computation for raw log samples and
  already-calibrated live samples.
- The calibration-sweep classes qtcalibrate uses: `Directions` (sphere
  patches and evenness), `MagQuality` (coverage, fit residual, attitude
  diversity and dip consistency), `MagRetention` (which sample a full
  magnetometer buffer evicts), and `AccelCalibration` and `GravityFit` (the
  accelerometer zero-g offset).

The arithmetic behind `CompassCalibration` and `CompassProcessor` lives in a
Qt-free core, the `sensoranalysis_core` target in `core/`:

- `vecmath.h`: `sensormath::Vec3f`, `Quatf` and the few operations the solve
  uses, each a copy of Qt's QVector3D/QQuaternion code so results are
  bit-identical to the Qt implementation as macOS built it. (On Linux the Qt
  code computed a few steps in double, by accident of overload resolution;
  the core computes as macOS did everywhere.) It is the one place to change when
  the core moves onto glm or Eigen, which filter work will want; that move
  changes results in the last bits and is measured with
  `compass_core_parity_check --report` ([test/README.md](test/README.md)).
- `compass_core.h`: `compasscore::CalibrationConstants`, magnetometer and
  accelerometer correction, `solveOrientation()`, and
  `calibrationFromLookup()` -- the one mapping from the stored
  CalibrationConstants JSON keys and their defaults to constants, given a
  function that looks a number up.
- `calibration_json.h` (the `sensoranalysis_core_json` target, which adds
  protobuf): `calibrationFromJson()` reads that JSON without Qt, through
  protobuf's generic `Struct`. It keeps every number as the double parsed from
  the text, as Qt's reader does; parsing into the typed message would round
  the soft-iron terms through float and change results.

- `magcal/`: Freescale's magnetometer calibration solver, in C
  (BSD-3-Clause, [notice](../../../LICENSES/host/third-party/freescale-magcal.txt)).
  `MagCal_Run()` fits a 4-, 7- or 10-element ellipsoid to the 650-sample
  buffer in a `MagCalibration_t`; which samples that buffer holds is the
  caller's choice (`MagRetention`).
- `calibration_session.h`: `calibcore::CalibrationSession`, one live
  calibration -- the 650-sample buffer and the solver it feeds, eviction by
  `MagRetention` with a seeded fallback, the accelerometer's own fit, the
  quality figures, and the low-pass filtered live compass. qtcalibrate's
  `CompassData` is a Qt adapter over it, and the Rust Tag Calibrator uses it
  directly, so both calibrate identically. qtcalibrate's replay logs and
  `test/calibration_session_check.cc` showed the move from `CompassData`
  changed nothing.
- `directions_core.h`, `magquality_core.h`, `magretention_core.h`,
  `accelcalibration_core.h`, `gravityfit_core.h`: the calibration-sweep
  classes, in namespace `calibcore`, on `sensormath::Vec3f` and `std::vector`.
  Each header carries the class's reasoning; the Qt header of the same name
  is an adapter. They were moved here unchanged in arithmetic, and
  `test/calibration_dump.cc` showed every result bit-identical before and
  after ([test/README.md](test/README.md)).

The Qt classes convert types and call the core -- including
`CompassCalibration::fromCalibrationJson()`, which is a `QJsonObject` lookup
over `calibrationFromLookup()` -- so there is one implementation. Command-line tools can link `sensoranalysis_core` without Qt.

Keep this library independent of widgets, dialogs, QML, plotting, and SQLite.
Those concerns belong in applications or `../sensorui`.
