---
type: design
status: current
summary: How tagsim's first slice turns an ArduPilot flight into an IMUTag log - glitch repair, the attitude and position splines, the noise-free sensor model and its axes, page packing, and the truth file.
---

# tagsim: How the First Slice Works

One recorded flight goes in; an IMUTag SQLite log and a truth file come out,
with **no sensor noise**. The pipeline is:

1. read the EKF track;
2. repair its glitches;
3. fit splines;
4. sample ideal signals at the IMU rate;
5. quantize them into pages;
6. write a `tag_dwnld.py` download and import it with `tag-log-import`.

What is still planned is in the
[proposal](proposals/imutag-simulator.md); how to run it is in the
[README](../README.md).

## Source track

`ardupilot.py` reads the log with its own DataFlash reader (`dataflash.py`,
from the log's `FMT` records):

- **Core 0:** `XKQ1` (attitude) and `XKF1` (`PN PE PD`, `VN VE VD`), joined on
  `TimeUS`. This is the truth.
- **Core 1:** `XKQ2` and `XKF6`, interpolated to core 0's times. It is used
  only for repair.
- **`ORGN`:** the origin. Altitude is `ORGN.Alt - PD`.
- **`STAT.isFlying`:** the flight window, from the first to the last flying
  sample, padded by `--pad`.
- **`GPS`:** UTC is `GWk`, `GMS` less 18 leap seconds. It is fitted as a
  median offset to `TimeUS` and sets the page epochs.

The `XKQ` quaternion rotates body FRD vectors into NED. It is the EKF's own
frame: `XKF1`'s Euler angles differ from it by the autopilot trim, about half a
degree in the first logs.

## Glitch repair

A forward pass over each core flags a sample as **implausible** when, against
the last plausible sample, it implies more than 15 g or 2000 dps. A run that
stays implausible for longer than 0.5 s takes its current sample as the new
reference, so one persistent step flags one run, not the rest of the flight.

Then, sample by sample:

| Case | Action |
| --- | --- |
| Core 0 implausible, core 1 plausible | Substitute core 1, then check the merged series again |
| Both plausible, but disagreeing by more than 5 m/s or 10 degrees | Untrusted |
| Still implausible after substitution | Untrusted |

Untrusted runs are then repaired by length:

- **Up to 0.5 s:** bridged. The samples are dropped and the fit spans them.
- **Longer:** the flight is split, and the longest piece overlapping the
  flight window is used.

Every action is a `Repair` record in `SimRun.RepairsJson`.

On the first logs:

| Log | What happens |
| --- | --- |
| `00000167` | Its 26 g sample is bridged. Its neighbours still carry part of the step, so about 17 g survives there. |
| `00000176` | Both cores glitch every 8–10 s and diverge by up to 200 m/s between 78 and 354 s. tagsim keeps the 184 s after that stretch. |
| `00000174` | Its 1202 dps attitude step is under the 2000 dps default bound, so it is not flagged and comes through as a 664 dps peak. `--max-rate-dps 1000` catches it. |

## Splines

`trajectory.py` fits two splines.

**Attitude** is a cumulative cubic B-spline on unit quaternions, with uniform
control points at the source rate. The source samples are slerped onto that
grid after the signs are made continuous. The body rate comes analytically
from the cumulative-basis recursion, so there are no Euler angles anywhere and
nothing breaks at ±90 degrees pitch.

**Position** is a quintic B-spline fitted by least squares. The fit minimises

```
|p − PN,PE,PD|² / σp²  +  |p' − VN,VE,VD|² / σv²  +  ∫ |p'''|² / σj²
```

with σp = 5 m, σv = 0.1 m/s and σj = 100 m/s³. Velocity and acceleration are
its derivatives.

**Why not an exact interpolant.** An exact quintic Hermite through both
position and velocity cannot be used. On EKF output the two disagree by
2–7 cm per 40 ms step, and an interpolant that honours both swings to 44 g
between samples.

**Why the position weight is loose.** EKF position and integrated velocity
also drift metres apart over a flight. Weighted tightly, the fit buys position
back with violent velocity excursions wherever the samples have a hole: a
0.2 s dropout in `00000167` became 150 g at σp = 0.5 m.

**What the weights give.** At these weights the first logs have:

- specific force of 4.0 g at the 99th percentile and 5–7 g at peak, matching
  the differenced EKF velocity;
- position within 2–5 m of the EKF's (median).

The spline is the truth, so that offset is not an error in the simulation.

**Known artefact.** While the aircraft sits still, EKF velocity corrections at
GPS updates come through as brief specific-force spikes of up to about 0.5 g.

## Sensor model

`sensors.py` computes the ideal signals at every IMU sample, `t_first + i/ODR`:

- **Specific force:** `R_tn (a_n − g_n)`, in mg.
- **Body rate:** in dps.
- **Field:** a constant NED field. This is a placeholder for a World Magnetic
  Model value, and it is exact for one site and date.
- **Pressure:** the ISA troposphere at the true altitude, with a fixed
  sea-level pressure.
- **Page temperature:** the ISA temperature at the true altitude.

The magnetometer and pressure are taken at each superframe's first sample, in
one auxiliary slot per ten IMU samples, and none is missing.

The mounting is a rotation from body FRD to the tag frame (x right, y toward
the battery, z up). The default `RFU` is the tag flat with its battery end
forward.

**Quantization.**

- Accel and gyro are rounded to tagcore's LSB for the configured range
  (`sqlitelog/imutag.cc`).
- They saturate at full scale, or at the `int16` limit where that is lower.
  At ±4 g, the int16 limit is 3997.6 mg.

**Axes.** The model goes tag frame, then each part's axes, then the firmware's
mapping in `families/IMUTag/src/sensors.c`, so the stored values are what the
firmware writes:

- **IMU.** `orient_imu_xyz` maps `x' = −y, y' = x`. The part therefore reads
  `(y, −x, z)`, and the stored values are in the tag frame.
- **Magnetometer, on IMUTagNandBmp581.** The `IMUTAG_MAG_TAG_*` macros map
  `x' = y, y' = x, z' = −z`. The stored field is `(B_y, −B_x, B_z)`, so the
  part reads `(−B_x, B_y, −B_z)`.

**Where the magnetometer sign comes from.** It is fixed by real data, not by a
literal reading of
[the axis contract](../../../../docs/shared/sensor-axes.md). The contract's
`+y, −x, −z` relabelling was established by an inclination-spread check, which
cannot see an overall sign. Read literally, the relabelled stored field would
equal **B**, and the host then reports this site's dip as −66.9 degrees.

Real IMUTagNandBmp581 data says it is **−B**:

- the qtcalibrate captures of 2026-10-11 score +63.8 degrees;
- a flat bench download of 2026-10-02 reads `mz = +46.8 uT`. It was made
  before the macros existed, so its stored values are the part's own axes.

With the sign taken from that data, `dataprocessing imu-compass-orientation`
reports the configured dip, +66.89 degrees, constant to 0.01 degrees over
random static attitudes, as for a real tag.

## Pages and the download

`pages.py` packs `t_ImuTagDataLog` byte for byte:

- **Header:** 8 bytes: epoch, 1/1024 s ticks of the page's first sample, and
  the temperature in 0.01 °C.
- **Body:** 15 superframes per page.
- **Missing auxiliary values:** written as exactly `0x7fc00000`.
- **Last page:** partly erased, `0xFF`.

Each page becomes the Ack `data_logAck()` would serve. Its `millisecond` field
carries the page ticks, OR'd with checkpoint flags on the first page of an
8-page group only.

Multi-segment logs are supported for tests:

- a later segment starts on the next group's first page;
- that page carries `RESYNC`, plus `RESYNC_STORAGE_SKIP` or `RESTART_RECOVERY`;
- the indices skipped in between are absent, as after a storage skip.

**The header line** carries:

- `TagInfo`: board `IMUTagNandBmp581`, a UUID derived from the seed and the
  source log's SHA-256, a 256 MiB external flash, and `ppm_clock_error`;
- `Config` with the `lsm6` ODR and ranges;
- `FINISHED` status;
- a state log: `CONFIGURED`, then `RUNNING` at the first sample, then
  `FINISHED`;
- one identity calibration.

`tag-log-import` writes the log, adding `source = import`, `simulator` and
`tagsim_source` to its `info` table.

## Truth file

`NAME.truth.db3` is keyed like the log by `SegmentId` and `SampleIndex`. Its
`ElapsedUs` is the true time since sample 0, which equals the log's own while
no clock error is simulated or reported.

| Table | Contents |
| --- | --- |
| `TruthPose` | ENU position and velocity about the EKF origin; attitude quaternion, tag frame to ENU |
| `TruthKinematics` | Ideal specific force (mg) and body rate (dps), tag frame, before quantization |
| `TruthAtmosphere` | Per superframe: altitude AMSL, sea-level pressure, pressure, temperature |
| `SimRun` | Version, seed, full configuration JSON, source name and SHA-256, repairs, consistency figures, UTC of sample 0 |
| `units` | Units and meaning of the columns above |

## Consistency check

`consistency.py` strapdown-integrates the ideal signals at the IMU rate:

- **Attitude** uses a rotation vector with the coning term, over the whole
  run.
- **Velocity and position** are restarted from truth every 10 s.

On `00000173` at 100 Hz:

- attitude agrees to 0.02 degrees over 627 s;
- velocity drift has a median of 0.4 mm/s per 10 s;
- position drift has a median of 3 mm per 10 s.
