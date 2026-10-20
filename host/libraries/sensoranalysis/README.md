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
  bit-identical to the Qt implementation. It is the one place to change when
  the core moves onto glm or Eigen, which filter work will want; that move
  changes results in the last bits and is measured with
  `compass_core_parity_check --report` ([test/README.md](test/README.md)).
- `compass_core.h`: `compasscore::CalibrationConstants`, magnetometer and
  accelerometer correction, and `solveOrientation()`.

The Qt classes convert types and call the core, so there is one
implementation. Command-line tools can link `sensoranalysis_core` without Qt.

Keep this library independent of widgets, dialogs, QML, plotting, and SQLite.
Those concerns belong in applications or `../sensorui`.
