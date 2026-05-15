# SensorAnalysis Library

`sensoranalysis` contains UI-free sensor-domain processing shared by host
applications.

Current contents:

- `CompassCalibration`: hard-iron and soft-iron magnetometer calibration.
- `CompassRawSample`: raw accelerometer/magnetometer row data.
- `CompassDerivedSample`: magnetic-frame orientation, field, dip, and
  acceleration magnitude derived from raw samples.
- `CompassProcessor`: eCompass orientation computation.

Keep this library independent of widgets, dialogs, QML, plotting, and SQLite.
Those concerns belong in applications or `../sensorui`.
