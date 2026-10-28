---
type: decision
status: accepted
summary: Compass heading, pitch and roll are computed by compasscore::displayAngles() in sensoranalysis's Qt-free core - a bit-identical copy of Qt's toEulerAngles() and the historical relabelling - instead of by Qt at each display, so the sensorViz compass transform and non-Qt front ends show the same angles; this replaces only that part of 0030, whose quaternion-only storage stands.
---

# 0040. Host: compass display angles are computed in the Qt-free core

Date: 2026-10-28

## Context

[0030](0030-host-orientation-stored-as-quaternion-only.md) made orientation a
quaternion everywhere it is computed or stored, with heading, pitch and roll a
display convention: Qt's `QQuaternion::toEulerAngles()` with the host's
relabelling, applied "where they are shown". It rejected porting the
conversion "as unnecessary: every display is a Qt display".

That premise no longer holds:

- Tag Calibrator has a Rust/Tauri version
  ([0039](0039-host-tagcalibrate-rs.md)) whose orientation view shows the same
  angles without Qt.
- sensorViz is leaving Qt
  ([proposal](../../host/applications/sensorviz/design/proposals/leaving-qt.md)):
  its compass transform, which turns orientation into the heading, pitch and
  roll streams, moved into the Qt-free `sensorviz_core` (toolkit-neutral core,
  phases D and E).

A display without Qt needs the same numbers, or a log would show different
angles in different tools.

## Decision

- The conversion is `compasscore::displayAngles()` in sensoranalysis's
  Qt-free core (`core/compass_core.h`), over `sensormath::toEulerAngles()`
  (`core/vecmath.h`), a copy of Qt 6.10.3's `QQuaternion::toEulerAngles()`
  including its gimbal-lock branch, with the same relabelling (x angle pitch,
  y angle roll, reversed z angle yaw) and the same float and double steps.
- Every C++ display uses it: sensorui's `compassAnglesFromQuaternion()` is an
  adapter over it, and sensorViz's compass transform
  (`host/applications/sensorviz/core/transforms.cpp`) calls it directly.
  Front ends without Qt call it through the C interface.
- The convention itself is unchanged, and storage is unchanged: files carry
  the quaternion only, as 0030 decided. The QML compass panels still convert
  with QML's `quaternion.toEulerAngles()`, the same Qt function, until they
  are replaced.

This replaces 0030's "angles are computed where they are shown, with Qt's own
`toEulerAngles()`" and its rejection of porting the conversion; the rest of
0030 stands.

## Alternatives considered

- **Keep Qt's function at each display.** Every non-Qt front end would need
  its own copy, unverified against Qt.
- **The explicit formulas in the dataprocessing design.** They agree with Qt
  to 3e-4 degrees but differ near ±90 degrees of pitch, so displayed angles
  would change for some tilted tags.
- **A standard Z-Y-X decomposition.** Changes tilted headings everywhere;
  0030 rejected it for that reason, and it remains a separate choice of
  display convention, now a change to one function.

## Evidence

- `compass_core_parity_check`'s angles group (sensoranalysis
  [test README](../../host/libraries/sensoranalysis/test/README.md)), added
  2026-10-28: 100,000 rounds of random, scaled, tiny and near-gimbal-lock
  quaternions, solved orientations and axis-angle rotations, comparing
  `sensormath::toEulerAngles()` with Qt's compiled `QQuaternion` and
  `displayAngles()` with sensorui's conversion as it stood, to the bit.
  Computing the yaw's `360 - z` in double instead of float failed 133,605 of
  its comparisons.
- When sensorViz's compass transform moved onto the core, its `--dump-state`
  output -- every sample of the heading, pitch and roll streams, under
  declination and battery-direction changes -- was identical before and after
  for every log on hand: 18 CompassTag logs derived from raw rows, and a
  CompassTag and an IMUTag log processed by `dataprocessing` (stored
  quaternions).

## Consequences

- One function defines the displayed angles for every host tool, Qt or not.
- Changing the display convention later is a change to `displayAngles()`
  and its parity check, and the QML panels while they last.
