---
type: design
status: current
summary: How tagsim turns an ArduPilot flight into an IMUTag log - glitch repair, the attitude and position splines, the sensor model and its axes, the datasheet sensor errors, magnetometer timing, page packing, the truth file - and how it scores an orientation estimate and exports truth and estimate for UAV Log Viewer.
---

# tagsim: How It Works

One recorded flight goes in; an IMUTag SQLite log and a truth file come out.
The pipeline is:

1. read the EKF track;
2. repair its glitches;
3. fit splines;
4. sample ideal signals at the IMU rate, and the magnetometer at its own
   conversion times;
5. add the datasheet sensor errors;
6. quantize them into pages;
7. write a `tag_dwnld.py` download and import it with `tag-log-import`.

The sensor errors are only those the planned orientation filter's behaviour
depends on (the proposal's
[scope](proposals/imutag-simulator.md#scope-keep-it-simple)).

Two tools then use the truth file: `tagsim score` compares an orientation
estimate with it, and `tagsim view` writes truth and estimate as a DataFlash
log for UAV Log Viewer. `tagsim batch` runs the whole chain over a directory
of logs; the scores for `dataprocessing`'s compass on the first logs are in
[compass-baseline-results.md](compass-baseline-results.md).

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
the last plausible sample, it implies more than 15 g or 1000 dps. A run that
stays implausible for longer than 0.5 s takes its current sample as the new
reference, so one persistent step flags one run, not the rest of the flight.

Then, sample by sample:

| Case | Action |
| --- | --- |
| Core 0 implausible, core 1 plausible | Substitute core 1, then check the merged series again |
| Both plausible, but disagreeing by more than 5 m/s or 10 degrees | Untrusted |
| Still implausible after substitution | Untrusted |

Untrusted runs are then repaired by length:

- **Up to 0.5 s, and the jump does not last:** bridged. The samples are
  dropped and the fit spans them. This is a spike: the sample after the run
  is back near the sample before it.
- **Up to 0.5 s, but the jump lasts:** split, recorded as
  `split: lasting step`. A step lasts when the change from the sample before
  the run to the sample after it is at least half of the largest jump into
  the run. The test uses attitude or velocity, whichever jumped more relative
  to a clean track's sample-to-sample change (about 1 degree, 0.1 m/s). An
  EKF reset is the usual cause, and bridging one would only spread the jump
  over the gap.
- **Longer:** split.

After splitting, the longest piece overlapping the flight window is used.

Every action is a `Repair` record in `SimRun.RepairsJson`.

On the first logs:

| Log | What happens |
| --- | --- |
| `00000167` | Its 26 g sample is a lasting velocity step: 10 m/s that stays. The flight is split there, and the kept piece starts at 49.1 s, 4.4 s after `isFlying`. A 17 g peak remains elsewhere, at 589 s. |
| `00000174` | A 48° attitude jump in one 40 ms interval (1202 dps), 1.3 s after `isFlying` starts, that lasts like an EKF yaw reset. The flight is split there and the kept piece starts at 12.2 s. Peak rate falls from 542 to 277 dps, in normal flight. |
| `00000176` | Both cores glitch every 8–10 s and diverge by up to 200 m/s between 78 and 354 s. tagsim keeps the 184 s after that stretch. |
| `00000169`, `00000178`, `00000180` | One lasting attitude step each on the ground, before the kept window. Their output does not change. |

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
- **Field:** the World Magnetic Model field (`wmm.py`), in NED, evaluated
  once at the log's `ORGN` and the UTC date of the first sample, rotated into
  the tag frame at each magnetometer conversion's true time (below).
  `--mag-ned` replaces it with a given field.
- **Pressure:** the ISA troposphere at the true altitude, under a sea-level
  pressure that is constant or a random walk (`--sea-level-walk`). The sensor
  is assumed shielded from the airflow, so it sees static pressure only.
- **Page temperature:** the ISA temperature at the true altitude.

**The World Magnetic Model** comes from
[pygeomag](https://pypi.org/project/pygeomag/), a pure-Python port of NOAA's
reference code. It was chosen because it ships the WMM2015, WMM2020 and
WMM2025 coefficients, needs nothing compiled and is maintained. tagsim takes
the model whose five-year epoch contains the flight date, so the 2023 logs use
WMM2020. Over a pattern box the field changes by a few nanotesla, below the
magnetometer noise, so one value per log is enough. `00000173` gets
51.72 µT, dip 66.55°, declination −4.62°.

**Auxiliary timing.** The firmware polls the magnetometer and the pressure
sensor once per superframe, after copying the IMU block
(`sampleDataCollection` in `families/IMUTag/src/sensors.c`). The model polls
at the superframe's last IMU sample. The log puts the slot at the
superframe's first sample, so the truth file keeps the true poll time.

- **Magnetometer.** The BMM350 free-runs at the rate `select_mag_rate` picks:
  12.5 Hz at 100 Hz ODR, scaling with the ODR, so 1.25 conversions per poll.
  Its clock has a random phase and a rate error drawn per run. Each poll takes
  the newest completed conversion; if none completed since the last poll, the
  slot is the missing-value NaN, as on the tag. At the firmware's rates that
  never happens, so the conversion's age sweeps 0–80 ms at 100 Hz as the two
  clocks beat. The conversion is treated as instantaneous.
- **Pressure** is taken at the poll time. The BMP581 converts at about 250 Hz,
  so its age is negligible.

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

## Sensor errors

`errors.py` turns the ideal signals into measurements. Each figure is a
named parameter of `ErrorConfig`, defaulting to the datasheet typical value
(the proposal's
[Datasheet noise and errors](proposals/imutag-simulator.md#datasheet-noise-and-errors));
`--set-error` overrides any of them and `--ideal` zeroes them all. Ranges are
half-widths of uniform draws, made once per run.

| Sensor | Error | Default | Applied |
| --- | --- | --- | --- |
| Accelerometer | White noise | 60 µg/√Hz over BW = ODR/2: 0.42 mg RMS at 100 Hz | Per sample, tag frame, before quantization and saturation |
| | Turn-on bias | ±12 mg per axis | Constant |
| Gyroscope | White noise | 2.8 mdps/√Hz over ODR/2: 0.020 dps RMS at 100 Hz | As the accelerometer |
| | Turn-on bias | ±1 dps per axis | Constant |
| Magnetometer | Hard iron V | ±25 µT per axis | Stored axes |
| | Soft iron A | diagonal ±1 % x, y, ±3 % z; symmetric off-diagonal ±0.5 % | Stored axes, as `A⁻¹` |
| | Noise | 190 nT x, y; 450 nT z RMS | Per conversion, stored axes |
| | Rate error | ±2 % | The BMM350's clock |
| Pressure | Noise | 0.41 Pa RMS | Per poll |
| | Offset | ±30 Pa | Constant |
| | Quantization | 1/64 Pa | After noise and offset |

**The magnetometer distortion is the inverse of the host's correction.**
`sensoranalysis` corrects a stored sample as `M' = A(M − V)`. The model writes
`M = A⁻¹ M_true + V + noise`, with `M_true` the true field in the stored
axes, so the correct constants recover the true field up to `A` times the
noise. The stored z axis is the BMM350's −z, so the z figures go there.

**The Calibration table** carries the drawn `V` and `A` by default. With
`--calibration perturbed` it carries `V` plus up to ±2 µT per axis and `A`
plus a symmetric perturbation of up to ±1 % per element, for testing a
filter against a wrong calibration. Which one was written is in `SimRun`.

**Saturation.** The run prints, and `SimRun.SaturationJson` keeps, the
fraction of samples per tag axis that read full scale after noise. On
`00000173` at ±4 g, 2.2 % of z samples clip, in the pulls; x, y and the gyro
at ±1000 dps never do.

**Why not gnss-ins-sim.** Its `acc_gen` and `gyro_gen` would do this same
arithmetic, but the package is not on PyPI, would be pinned to a git commit,
and brings drift and vibration models the scope defers. `errors.py` is the
small equivalent with the same meanings: its turn-on bias is gnss-ins-sim's
`b`, and gnss-ins-sim's white-noise `vrw` and `arw`, whose per-sample sigma
is `vrw·√fs`, are this module's datasheet density divided by √2.

**Determinism.** Every random term has its own generator, seeded from
`(seed, stream)`, so a run is reproducible from `SimRun`'s seed and
configuration, and changing one term's parameters leaves the others' draws
alone.

**What it gives on `00000173`** (seed 0, true calibration):

| Field magnitude | 1 % | Median | 99 % | SD |
| --- | --- | --- | --- | --- |
| Stored, before calibration | 24.6 µT | 58.5 µT | 79.9 µT | 12.6 µT |
| `dataprocessing` calibrated | 50.8 µT | 51.72 µT | 52.7 µT | 0.38 µT |

The WMM magnitude is 51.72 µT. The calibrated spread is the magnetometer
noise projected on the field.

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
- one calibration: the run's hard and soft iron, true or perturbed.

`tag-log-import` writes the log, adding `source = import`, `simulator` and
`tagsim_source` to its `info` table.

## Truth file

`NAME.truth.db3` is keyed like the log by `SegmentId` and `SampleIndex`;
per-superframe rows use the log's key for the auxiliary slot, the
superframe's first sample. Its
`ElapsedUs` is the true time since sample 0, which equals the log's own while
no clock error is simulated or reported.

| Table | Contents |
| --- | --- |
| `TruthPose` | ENU position and velocity about the EKF origin; attitude quaternion, tag frame to ENU |
| `TruthKinematics` | Ideal specific force (mg) and body rate (dps), tag frame, before quantization |
| `TruthAtmosphere` | Per superframe: true poll time, altitude AMSL, sea-level pressure, true pressure, temperature |
| `TruthMagSamples` | Per superframe: true poll and conversion times, the sample's age, whether it was missed, and the true field in the tag frame and in the stored axes (what a correct calibration recovers) |
| `SimRun` | Version, seed, full configuration JSON (sensor, errors, the values drawn, WMM field, the `STAT.isFlying` window as `flying_elapsed_us`), source name and SHA-256, repairs, consistency figures, saturation, UTC of sample 0 |
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

## Scoring

`score.py` compares an estimate, a series of attitudes on the log's
`ElapsedUs`, with truth slerped to the same times. For each row the error
rotation `E = q_est q_true⁻¹` (both tag frame to ENU) is split about the
vertical:

| Metric | Meaning |
| --- | --- |
| Total | The angle of E |
| Tilt | The angle between the estimated and true vertical in the tag frame: the roll and pitch error |
| Heading | E's twist about the vertical, signed clockwise as a compass heading error |
| Velocity | `|v_est − v_true|`, when the estimate has velocity |

The twist is defined for every attitude, so heading error stays meaningful on
vertical lines, where Euler yaw is not.

Rows inside the source's `STAT.isFlying` window are **flight**, the padding
either side **ground**. Each metric is summarised per phase by median, 95th
percentile and maximum of its absolute value. `tagsim score` writes
`NAME.score.db3`:

- `ScoreSeries`: one row per estimate row, with the errors, the phase, and
  the estimate's attitude in tagsim's convention, for plotting;
- `ScoreSummary`: the per-phase statistics;
- `ScoreRun`: inputs and how many rows could not be decoded.

**Estimates.** A future filter should write a table with `ElapsedUs`,
`qw qx qy qz` (tag frame to ENU, as `TruthPose`) and optionally `ve vn vu`;
`--table` scores it directly.

**dataprocessing's compass.** `dataprocessing -p imu-compass-orientation`
solves each magnetometer row with the superframe's mean accelerometer, through
`compasscore::solveOrientation`. Its quaternion is in the algorithm's own
convention, and that convention is **not a fixed change of frame**:

- where the stored accelerometer z is negative, the quaternion is exactly
  `A R_et^T B` for two constant rotations;
- where it is positive, no constant A and B fit, because the algorithm's
  swap of the tilt quaternion's x and y terms, together with its
  magnetometer flips, relabels gravity and field differently in that branch.

So the quaternion cannot be turned into an attitude by any constant
rotation. `compass.py` instead finds the attitude it **implies**: the tag
attitude for which the same algorithm, given ideal inputs (gravity only, the
true field, a correct calibration), returns the stored quaternion. It is
solved per row by Gauss-Newton from the true attitude, with random restarts
up to 90° away for the few rows near the branch switch. On random attitudes
it recovers every one to 1e-3°, and on the 15 recorded flights every row
decodes. The errors are then physical attitude errors, and they include the
algorithm's handling of declination, since the ideal inputs carry the full
field.

`compass.py`'s port of `solveOrientation` agrees with the `dataprocessing`
binary to 1e-6 per quaternion component; `test_flight.py` checks it on a
flight. With `--ideal` errors, a tag at rest after landing scores 0.03–0.5°.

**Timing.** The compass row's `ElapsedUs` is its superframe's first sample,
while the accelerometer it used is the mean over the superframe and the
magnetometer conversion is up to 80 ms older than the poll. Truth is taken at
`ElapsedUs`, the time the host gives the row, so that offset is part of the
score.

## UAV Log Viewer export

`viewer.py` writes `NAME.view.bin`, a minimal DataFlash log that
[UAV Log Viewer](https://github.com/ardupilot/uavlogviewer) opens. A DataFlash
file was chosen over a MAVLink tlog because the viewer's
`dataflashDataExtractor.js` offers both trajectory sources and attitude
sources by message name, so one file can carry truth and estimate side by
side, and tagsim already reads the format.

| Message | Content | In the viewer |
| --- | --- | --- |
| `POS`, `ATT` | Truth position and airframe attitude, at 25 Hz | Default trajectory and attitude |
| `AHR2` | The estimate's attitude, at the truth position, at the estimate's rows | Select `AHR2` as trajectory and attitude source |
| `GPS` | Truth at 5 Hz, with `GWk`/`GMS` | Start time and date |
| `MSG` | "ArduPlane …" | Aeroplane model |
| `UNIT`, `MULT`, `FMTU` | Units of the above | Plot labels |

Attitude is the airframe's (FRD to NED, ArduPilot roll, pitch, yaw), back
through the run's mounting. Positions go back to latitude and longitude about
the EKF origin with the simulator's flat-earth approximation. `TimeUS` is the
source log's, so the file lines up with the source flight.

Checks:

- the viewer's own parser (JsDataflashParser) and extractor, run under node
  on `f173.view.bin`, find an aeroplane, the trajectory sources `POS`, `AHR2`
  and `GPS`, the attitude sources `ATT` and `AHR2`, and the flight's date;
- against the source log's own `ATT`, the exported yaw agrees to 0.01°
  (median) and roll and pitch to 0.4° and 0.8°, the autopilot trim between
  `ATT` and the EKF quaternion; position agrees within the 2–5 m position-fit
  offset.
