---
type: readme
status: current
summary: Contents of the sensorui library of reusable Qt and QML sensor widgets.
---

# SensorUI Library

`sensorui` contains reusable Qt presentation pieces for sensor visualization
applications. It can depend on `sensoranalysis`; `sensoranalysis` must not
depend on this library.

Current contents:

- `compassAnglesFromQuaternion()` (`compass_angles.h`): heading, pitch and
  roll for C++ displays such as plotted streams. Orientation is computed and
  stored as a quaternion; Euler angles are a display convention, derived with
  Qt's `toEulerAngles()` where they are shown.
- `CompassDisplay`: adapter between C++ compass samples and the QML compass. It
  passes the quaternion; `MyCompass.qml` converts it to angles itself, using
  the same Qt conversion and the same relabelling.
- `AttitudeDisplay`: adapter between C++ orientation quaternions and the QML
  3D tag attitude view.
- `CompassCalibrationDialog`: read-only dialog for loaded calibration constants.
- `orientation_frame/`: QML/SVG compass display assets.
- `sensorui_resources.*`: explicit resource initialization for static-library
  builds before loading `qrc:/qfi/...` URLs.

Applications decide when these widgets/actions are visible. For example,
CompassTag-specific controls should be hidden unless the loaded log contains
compass data.
