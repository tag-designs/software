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

The Qt classes convert types and call the core -- including
`CompassCalibration::fromCalibrationJson()`, which is a `QJsonObject` lookup
over `calibrationFromLookup()` -- so there is one implementation. Command-line tools can link `sensoranalysis_core` without Qt.

Keep this library independent of widgets, dialogs, QML, plotting, and SQLite.
Those concerns belong in applications or `../sensorui`.
