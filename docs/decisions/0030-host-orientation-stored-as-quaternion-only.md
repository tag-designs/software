---
type: decision
status: accepted
summary: Compass orientation is computed and stored as a quaternion only; heading, pitch and roll are a display convention derived with Qt's conversion at each point of display, dataprocessing no longer writes yaw, pitch or roll, and SensorViz builds every compass view from quaternions, stored or derived.
---

# 0030. Host: orientation is stored as a quaternion; Euler angles are display-only

Date: 2026-10-20

## Context

`CompassProcessor::deriveSample()` in `sensoranalysis` returned a quaternion
and, beside it, yaw, pitch and roll produced by `QQuaternion::toEulerAngles()`.
Those angles were written into the `CompassOrientation` and
`ImuCompassOrientation` tables by `dataprocessing`, and qtcalibrate repeated the
same conversion in its live path.

Qt's convention is particular: it decomposes `q = Ry * Rx * Rz`, and the host
relabels the y angle as roll and the reversed z angle as yaw. It is not the
aerospace Z-Y-X order and disagrees with it whenever the tag is tilted about
both horizontal axes. Another library -- the obvious ones for a Qt-free
`dataprocessing` are Eigen and glm -- would produce different numbers from the
same quaternion, so angles stored in a file depended on the library that wrote
them, and nothing in the file said which convention they followed.

The Euler conversion was also the one part of the compass math that could not
be replaced mechanically when moving `dataprocessing` off `QVector`,
`QVector3D` and `QQuaternion`.

## Decision

- `CompassDerivedSample` carries `q`, dip, field and acceleration magnitude
  only. `sensoranalysis` computes no Euler angles.
- Angles are computed where they are shown, with Qt's own
  `toEulerAngles()` and the historical relabelling, so every display shows what
  it showed before. C++ displays -- SensorViz's heading, pitch and roll
  streams and qtcalibrate -- call `compassAnglesFromQuaternion()` in
  `host/libraries/sensorui/compass_angles.h`. The QML compass is handed the
  quaternion and calls the QML `quaternion.toEulerAngles()`, the same Qt
  function.
- `dataprocessing` processor version 2 writes the quaternion, dip, field and
  acceleration, and no `yaw`, `pitch` or `roll`. Each orientation table is
  registered as one `record_column` record set, quaternion included. Replacing
  an older output removes the retired stream ids.
- SensorViz builds its compass streams and panel from one list of quaternion
  samples: a processed file's stored record set when present, otherwise
  quaternions it derives from raw CompassTag rows.

## Alternatives considered

- **Port Qt's conversion as explicit formulas, in C++ and in QML.** Rejected
  as unnecessary: every display is a Qt display, so Qt's conversion is
  available wherever angles are shown. The formulas are written down in the
  dataprocessing design only for tools outside the host.
- **Keep the angles in `sensoranalysis` and reimplement Qt's convention there
  without Qt.** Possible, but it keeps a display convention in the data layer
  and in stored files, where its meaning is invisible.
- **Store angles in a standard Z-Y-X convention instead.** Changes every
  displayed pitch, roll and tilted heading at once, and still freezes one
  convention into the data. Choosing a better display convention remains open
  and is now a change to one function.

## Evidence

On 2026-10-20 the version 1 `dataprocessing` output for a CompassTag log
(6 rows) and an IMUTag log (1005 rows) was kept as a baseline.

- `compassAnglesFromQuaternion()` applied to the stored quaternions reproduced
  the stored yaw, pitch and roll of all 1011 rows exactly.
- The version 2 output matched the baseline in every remaining column:
  quaternion, dip, field, acceleration and, for IMUTag, the row keys and
  `AccelSamples`.
- The explicit formulas in the design document agreed with Qt to 3e-4 degrees.
- Running version 2 with `--if-exists replace` over a version 1 output left no
  yaw, pitch or roll rows in `streams`.
- SensorViz showed the same plot and compass panel (heading 5, roll 10,
  pitch -5) for the raw CompassTag fixture, for the same fixture processed, and
  in the documentation screenshot made before the change. A processed IMUTag
  log showed a heading stream and a panel whose angles match its stored
  quaternion. qtcalibrate's orientation screenshot matched its documentation
  screenshot (heading 315, roll -18, pitch 12).

## Consequences

- Processed files written before version 2 keep their angle columns; version 2
  files do not. Nothing in this tree read those columns back -- SensorViz
  recomputed orientation from the raw `Compass` table.
- A processed IMUTag log now gets the full compass view in SensorViz, which it
  previously had only as plain yaw, pitch and roll streams. A processed
  CompassTag log is shown from its stored quaternions rather than re-derived
  from raw rows.
- What remains Qt-specific in `dataprocessing` is vector and quaternion
  arithmetic, which can be replaced mechanically.
