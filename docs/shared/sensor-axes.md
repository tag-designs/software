---
type: design
status: current
summary: Tag firmware rotates each magnetometer and accelerometer part into a common tag frame, and the host orientation code assumes one fixed magnetometer-to-accelerometer alignment; what each family applies, and how to check a capture against it.
---

# Magnetometer and Accelerometer Axes

Firmware rotates each part into the tag frame, close to the part, so the host
can assume one fixed relationship between the two sensors no matter which parts
a tag carries. The host end of that assumption is
[`CompassProcessor::computeOrientation`](../../host/libraries/sensoranalysis/compass_processor.cpp).

This matters because the parts do not agree among themselves. The AK09940A,
BMM350, LIS2DU12 and LSM6DSV16X each define their own axes, and each is placed
on its board at whatever rotation the layout wanted. Nothing makes them line up
by default.

## The tag frame

The tag frame is ENU and right-handed:

| Axis | Direction |
| --- | --- |
| `x` | East -- 90 degrees clockwise from `y`, seen from above |
| `y` | North -- toward the battery |
| `z` | Up |

`x` cross `y` is `z`, and an accelerometer at rest face-up reads `+z`. The
orientation output of
[`CompassData::eCompass`](../../host/applications/qtcalibrate/compassdata.cpp)
is in this frame.

## The contract

A tag emits magnetometer and accelerometer samples such that the magnetometer's
axes map onto the accelerometer's as **`+y, -x, -z`**: the magnetometer's `+y`
lies along the accelerometer's `+x`, its `-x` along `+y`, and its `-z` along
`+z`. The accelerometer is in the tag frame above; the magnetometer is not,
and the host finishes the job.

That residue is not sloppiness in the firmware, and it is not a mounting the
firmware forgot to undo. As a matrix the relation is

```
 0  1  0
-1  0  0
 0  0 -1
```

whose determinant is **-1**. A part placed on a board can only be rotated, and
every rotation has determinant +1, so no placement of any magnetometer can
produce this. It factors as a -90 degree rotation about `z` -- which a mounting
could account for -- composed with a `z` flip, which one cannot. The flip is a
handedness difference between how the magnetometer and the accelerometer define
their axes, and it travels with the part, not the board. That is why firmware
cannot finish the job by rotating alone, and why the correction lives in one
place on the host instead of being duplicated per target.

The host encodes that in three lines that each look like an independent sign
convention:

```cpp
accel = QVector3D(accel[0], -accel[1], accel[2]);
mag   = QVector3D(mag[0], mag[1], -mag[2]);
qacc  = QQuaternion(q0, q2, q1, q3);            // note q2 before q1
```

They are not independent. Composed, they are exactly the `+y, -x, -z`
relabelling and nothing else — verified sample by sample to within 1e-13
degrees by
[`check_orientation_frames.py`](../../host/libraries/sensoranalysis/tools/check_orientation_frames.py).
In particular the `q2`/`q1` ordering in the quaternion is not a typo, and the
two sign flips are not separate NWU conversions; changing any one of them in
isolation breaks the other two.

## What each family applies

| Target | Magnetometer | Accelerometer | Firmware correction |
| --- | --- | --- | --- |
| `CompassTagAT25` | AK09940A | LIS2DU12 | `orient_mag_values`: `x' = -y, y' = -x, z' = -z`; `orient_accel_raw`: `x' = -x, y' = -y` |
| `IMUTagNandBmp581` | BMM350 | LSM6DSV16X | `orient_imu_xyz` on the accelerometer only: `x' = -y, y' = x`, z untouched. **None on the magnetometer.** |

Both are in each family's `sensors.c`. The corrections differ because the
mountings differ; what they have in common is the frame they produce. Note that
the two families do not even share a magnetometer part, so there is no reason to
expect their corrections to look alike.

The magnetometer gap on IMUTag is worth stating plainly. `sensors.c` guards the
BMM350 read with `TAG_SENSOR_MAG_BMM350`, which `IMUTagNandBmp581` sets through
the `sensor_mag_bmm350` module, and neither that branch nor the AK09940A branch
beside it applies any axis correction. The tag therefore relies on the BMM350
being mounted in the tag frame already. That may well be true, but nothing in
the firmware says so, and a reader cannot tell a deliberate no-op from a missing
call. Confirm it with a capture from the tag itself -- see below -- before
trusting headings from a new IMUTag variant.

## Checking a capture

A correct pairing makes the angle between the calibrated field and gravity a
property of the site rather than of the sample, so the inclination is nearly
constant however the tag was turned. A mismatch shows up as a spread of tens of
degrees.

```sh
host/libraries/sensoranalysis/tools/check_orientation_frames.py CAPTURE.json
```

It reports the spread under the host convention, searches all 48 signed axis
permutations, and exits non-zero when a capture disagrees. Run it against a
capture from any new tag variant before trusting its headings.

Check each target separately. The two current targets share no sensor part and
reach the tag frame by different routes, so a capture from one says nothing
about the other.

As of 2026-10-10:

| Target | Checked | Result |
| --- | --- | --- |
| `CompassTagAT25` | scored, [the replay fixture](../../host/docs/fixtures/qtcalibrate/) | agrees: inclination 63.65 degrees, spread 4.11 degrees |
| `IMUTagNandBmp581` | on the bench, through qtcalibrate's orientation view | heading and attitude behave correctly under rotation; no capture scored yet |

Watching the orientation view is a real check and not a weak one: it displays
dip, and dip holding steady while the tag is tumbled is this contract's
invariance property observed directly. It is how the BMM350 path's lack of a
magnetometer correction is known to be deliberate rather than an omission.

Scoring a capture from an IMUTag would still be worth the minute it costs. It
puts a number on what the eye judges, covers the tilted attitudes where a
mismatch is largest, and would let an IMUTag fixture sit beside the CompassTag
one as a regression check on the firmware helpers.

Two cautions when reading the result. The discrimination lives in the tilted
samples: as a tag approaches level, `q1` and `q2` fall to zero and every
candidate alignment collapses to the identity, so a capture held flat will look
healthy whatever its axes are. And the measurement needs gravity, so samples are
kept only while the acceleration magnitude is near one g — that gate belongs on
this metric alone and is not a reason to drop a magnetometer reading, which does
not depend on gravity.

## Captures from before the correction

A capture taken before a family's firmware correction existed does not satisfy
the contract, and replaying one through the current host code gives wrong
headings rather than an error.

The qtcalibrate replay fixture captured on 2026-08-26 is such a capture: it
disagrees with the host convention by 38.55 degrees of spread, and applying
CompassTag's two helpers to it after the fact brings it to 3.78 degrees. Keep
replay fixtures on the current firmware convention, and re-check any capture
older than the correction with the script above before drawing conclusions
from it.
