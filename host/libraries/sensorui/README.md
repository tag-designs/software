# SensorUI Library

`sensorui` contains reusable Qt presentation pieces for sensor visualization
applications. It can depend on `sensoranalysis`; `sensoranalysis` must not
depend on this library.

Current contents:

- `CompassDisplay`: adapter between C++ compass samples and the QML compass.
- `CompassCalibrationDialog`: read-only dialog for loaded calibration constants.
- `orientation_frame/`: QML/SVG compass display assets.
- `sensorui_resources.*`: explicit resource initialization for static-library
  builds before loading `qrc:/qfi/...` URLs.

Applications decide when these widgets/actions are visible. For example,
CompassTag-specific controls should be hidden unless the loaded log contains
compass data.
