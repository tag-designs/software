---
type: proposal
status: proposed
summary: A Python test tool, tagsim, that replays R/C pattern-aerobatics flights through a model of IMUTag's real acquisition and writes genuine IMUTag SQLite logs plus a ground-truth sidecar, for stress-testing the host tools and scoring post-processing filters.
---

# tagsim: Synthetic IMUTag Logs from Pattern Flights

**Status:** proposed, nothing built.

`tagsim` is a Python package of testing infrastructure, next to
[`tagclient`](../../../tagclient/README.md). It is not a shipped tool.

**What it does.**

1. It takes a recorded R/C pattern-aerobatics flight (an ArduPilot `.bin`
   log) and fits a smooth, self-consistent trajectory to it.
2. It passes that trajectory through a model of **how IMUTag actually
   acquires data**:
   - triggered accel and gyro sampling at the configured rate, quantized and
     saturating at the configured full-scale ranges;
   - a free-running magnetometer read once per superframe, with varying
     sample age;
   - a pressure sensor under drifting weather;
   - clock error;
   - pages, checkpoints, segments and gaps.
3. A small C++ importer feeds the result through tagcore's real IMUTag writer,
   so the output is a genuine IMUTag SQLite log that `sensorviz` and
   `dataprocessing` open unchanged.
4. Everything the tag could not know goes into a separate ground-truth file:
   attitude, position, specific force, body rates, the true field, the true
   sea-level pressure and the true magnetometer sample times.

**Why pattern flights.**

- **They cover every attitude.** Loops, rolls, knife-edge and inverted flight
  cross every orientation, including Euler gimbal lock at ±90 degrees of pitch
  and yaw wrap.
- **They are flown to known shapes,** so the same schedule can be replayed
  under different sensor errors.
- **They can be inspected visually.** The trajectory, and any filter's
  estimate of it, can be put into existing ArduPilot tools (UAV Log Viewer)
  and checked by eye as a 3D flight path.

**What it is not.**

- It does not model animal motion. No flapping or stride overlay is planned.
- It does not replace bench recordings. Its noise parameters start from
  datasheets and are refitted from real prototype data when that exists.

## What it is for

| Use | What tagsim provides |
| --- | --- |
| Host tools at scale | Full-flash logs of real-looking motion, at any configured rate, for `sensorviz` performance, hover, gaps and screenshots |
| The download/decode path | Pages, checkpoint flags, resync and storage-skip segments, clock correction, missing auxiliary slots and partial final pages, written through the real decoder |
| Untested cases | Multi-segment IMUTag logs and real gaps (no log on hand has more than one segment), and saturation |
| Calibration | Known hard-iron and soft-iron distortion and accelerometer offset, so calibrated field magnitude and dip can be checked against the model |
| `dataprocessing` | `imu-compass-orientation` scored against true attitude |
| Post-processing filters | Ground truth for the planned forward EKF and backward smoother ([open design](../../../../commandline/TODO.md#imutag-orientation-post-processing)), scored by attitude and heading error |
| Choosing ranges | Per-axis saturation statistics for any ODR and full-scale setting. The defaults (±4 g, ±1000 dps) may be too narrow for aerobatics |

## Architecture

```text
 ArduPilot .bin ──► trajectory ──► tag sensor model ──► pages + header ──► tag-log-import ──► IMUTag .db3
 (pattern flight)    (smooth,        (IMUTag              (JSONL: header,      (C++, tagcore
                     consistent)      acquisition)          Acks)               SqliteTagLogWriter)
                          │                 │
                          └────────► truth sidecar (.truth.db3) + provenance
                                            │
                                            └──► UAV Log Viewer export (eyes on it)
```

Everything left of `tag-log-import` is Python (numpy and scipy). The only C++
is the importer, and it is small.

### 1. Trajectory from a pattern flight

**Source messages.** Read the log with pymavlink, or with PyFlightCoach's
reader, which wraps it.

- **Position:** `POS`.
- **Attitude:** the primary estimate, never `AHR2`, which is ArduPilot's
  backup estimator. Use the EKF3 quaternion where the log has one; otherwise
  convert `ATT` roll, pitch and yaw to quaternions *before* doing anything
  else.
- The fused track is smooth and slow, about 15–25 points per second.

**Fitting.** The track is converted into a trajectory the sensor model can
sample at any rate:

- **Local frame:** positions go to a local ENU frame about the flight's
  centre. A flat-earth approximation is exact enough over a pattern box.
- **Position:** fit a smoothing quintic spline. Velocity, acceleration and
  jerk then come analytically, with no numerical differencing.
- **Attitude:** fit a cumulative quaternion B-spline, after making the
  quaternion signs continuous. The body rate comes analytically from it. This
  avoids the two failures of Euler angles in aerobatics: rates that are not
  body rates, and spikes at yaw wrap and at ±90 degrees of pitch.
- **Mounting:** a fixed rotation from the airframe to the tag, so different
  mountings can be tested.

**Consistency check.** Strapdown-integrate the *ideal* specific force and
body rate from the spline and compare the result with the spline's own pose.
Any drift measures the simulator's own inconsistency, which must be well
below the filter errors being studied. The tool reports it with every
generated log.

**Vibration.** EKF tracks carry none. An optional band-limited vibration term
can be added, off by default. Taking the log's raw `IMU` messages as a
residual is a later option, not a first step.

**Synthetic primitives.** A small generator for loops, rolls and level
flight, chained from the same splines. Tests then do not depend on having a
particular log, and fixtures stay tiny.

### 2. IMUTag sensor model

The model reproduces what the firmware stores, in the stored axes and units,
with the defaults it uses today. Every parameter is configurable and recorded
in the provenance.

**Accel and gyro (LSM6DSV16X).**

- **Rate and timing:**
  - Triggered sampling at the configured ODR: 100–1600 Hz, default 100 Hz.
    The firmware rejects 50 Hz.
  - Sample times come from the sample count, scaled by a simulated RV-3028
    clock error in ppm.
  - The importer's `TagInfo.ppm_clock_error` can be set to the true value or
    to a deliberately wrong one, which tests the host's clock correction.
- **Range, noise and quantization:**
  - Full-scale ranges ±2/4/8/16 g and ±125–4000 dps, default ±4 g and
    ±1000 dps.
  - Quantization to `int16` at tagcore's scales: 0.061–0.488 mg/LSB and
    0.004375–0.14 dps/LSB.
  - Saturation at full scale.
  - White noise, turn-on bias, bias temperature drift and sensitivity error
    at the datasheet figures in
    [Datasheet noise and errors](#datasheet-noise-and-errors).
  - Optional misalignment.
- **Filtering:** the firmware disables LPF2 and LPF1, so filtering is
  modelled only as the sensor's own output-rate bandwidth.

**Magnetometer (BMM350).**

- **Field:** Earth's field from the World Magnetic Model at the flight's
  latitude, longitude, altitude and date, expressed in the tag frame.
- **Distortion and noise:** the configured hard iron V and soft iron A, and
  the datasheet noise and errors. The distortion is applied as the inverse of
  the host's correction M' = A(M − V), so a correct calibration recovers the
  true field exactly.
- **Timing:**
  - The sensor free-runs on its own clock at the rate the firmware picks for
    the ODR: 12.5 Hz at 100 Hz, up to 200 Hz.
  - It has a random phase and a small rate error of its own.
  - Low-noise mode is modelled as averaging over the conversion window.
  - Once per superframe (10 IMU samples) the model takes the most recent
    completed conversion, as the firmware does. Its true time goes to the
    truth file.
  - A miss is written as the exact quiet NaN `0x7fc00000`, which is the only
    pattern the decoder treats as missing.
- **Axes:** the firmware's axis mapping for the target board. For
  IMUTagNandBmp581 that is x' = y, y' = x, z' = −z, under the contract in
  [docs/shared/sensor-axes.md](../../../../../docs/shared/sensor-axes.md).

**Pressure and temperature (BMP581).**

- **Pressure:** the International Standard Atmosphere at the true altitude,
  under a sea-level pressure that drifts as a random walk (about 1 hPa per
  hour, configurable).
- **Noise:** the datasheet noise for the firmware's oversampling (OSR_P 4×,
  OSR_T 2×, IIR off), plus offset, temperature coefficient and short-term
  drift, in [Datasheet noise and errors](#datasheet-noise-and-errors).
- **Rate:** the firmware's rate (25 Hz at 100 Hz ODR), read once per
  superframe like the magnetometer.
- **Temperature:** a slow profile, stored as the page header's `rawtemp`
  (0.01 °C per LSB).

**Axes for accel and gyro.** The firmware's `orient_imu_xyz` (x' = −y,
y' = x) is applied on the tag. The model writes what the page holds.

**Checking the axes.** One check settles the whole axis convention: the
existing `check_orientation_frames.py` must score a simulated log as
agreeing, with inclination equal to the WMM dip at the site.

### Datasheet noise and errors

**Defaults.** The sensor model's default parameters are the typical values
from the IMUTag sensors' datasheets, kept in the hardware repository under
`hardware/BoardDesigns/libraries/datasheets/`:

- `lsm6dsv.pdf` (DS13476 Rev 5);
- `bst-bmm350-ds001.pdf` (rev 1.27);
- `bst-bmp581-ds004.pdf` (rev 1.13).

Each figure is a named parameter, so a bench fit can replace it. The derived
columns are for the firmware defaults: 100 Hz output rate, ±4 g, ±1000 dps.

**Two caveats.**

- **Wrong datasheet.** The board carries an **LSM6DSV16X**, but the datasheet
  on hand is for the **LSM6DSV**. The figures below must be checked against
  the LSM6DSV16X datasheet before phase 2 relies on them.
- **No figure for the firmware's mode.** Neither sheet gives noise for the
  ODR-triggered mode the firmware uses. The high-performance figures are
  assumed, since the sheet aligns its other special mode (high-accuracy ODR)
  with high-performance noise.

**Accelerometer and gyroscope (LSM6DSV, high-performance mode, Table 3):**

| Error | Accelerometer | Gyroscope | Model |
| --- | --- | --- | --- |
| Noise density | 60 µg/√Hz, any FS | 2.8 mdps/√Hz, FS up to ±2000 dps | White noise over BW = ODR/2 (the sheet's convention, footnote 9) |
| RMS noise at 100 Hz | 0.42 mg (3.5 LSB at ±4 g) | 0.020 dps (0.57 LSB at ±1000 dps) | Derived: density × √50 Hz |
| As random walk | 0.035 m/s/√h | 0.17 °/√h | Derived; for filter tuning |
| Zero-g / zero-rate offset | ±12 mg | ±1 dps | Turn-on bias, drawn per run |
| Offset vs temperature | ±0.07 mg/°C | ±0.006 dps/°C | Applied to the temperature profile |
| Sensitivity tolerance | not given | ±0.3 % | Per-axis scale error |
| Sensitivity vs temperature | ±0.01 %/°C | ±0.007 %/°C | Applied to the temperature profile |
| Bias instability | not given | not given | Parameter, default 0 until fitted from a bench recording |

**Gyro quantization is comparable to the noise.** At ±1000 dps one LSB is
0.035 dps, so quantization alone contributes 0.010 dps RMS against 0.020 dps
of noise. At ±2000 dps (0.07 dps per LSB) the two are equal. The model
therefore quantizes after adding noise, as the sensor does. A wider gyro range
costs resolution here.

**Magnetometer (BMM350, low-noise mode = averaging 2, Tables 2-3):**

| Error | Figure | Model |
| --- | --- | --- |
| Output noise | 190 nT RMS x/y, 450 nT RMS z | White, per conversion. The sheet specifies these at ODR 100 Hz; the firmware runs 12.5 Hz at 100 Hz IMU ODR, so the same per-sample figures are assumed |
| Zero-field offset before calibration | ±25 µT | Default scale of the drawn hard iron V |
| Sensitivity error after API compensation | ±1 % x/y, ±3 % z | Default diagonal of the drawn soft iron A |
| Offset temperature coefficient | ±200 nT/K | Applied to the temperature profile |
| Sensitivity temperature drift | ±0.010 %/K | Applied to the temperature profile |
| Output-rate tolerance | ±2 % at room temperature, ±3–4 % over temperature | The magnetometer's own clock-rate error, which sets how its sample age drifts against the IMU |
| Resolution | about 0.1 µT | Below the noise; values stay `float` as on the tag |

**Pressure (BMP581, Tables 2 and 9):**

| Error | Figure | Model |
| --- | --- | --- |
| Noise at OSR_P ×4 | 0.41 Pa RMS (about 3.4 cm of altitude) | White, per conversion |
| Absolute offset | ±30 Pa (-5 to 65 °C) | Constant offset, drawn per run |
| Offset temperature coefficient | ±0.5 Pa/K | Applied to the temperature profile |
| Short-term drift | ±1.5 Pa per 24 h | Slow random walk, separate from weather drift in sea-level pressure |
| Resolution | 1/64 Pa | Quantization |
| Temperature accuracy | ±0.5 K | Offset on the logged temperature |

**Random draws.** Offsets, scale errors and hard and soft iron are drawn once
per run from the stated range, uniform by default and seeded. A test can fix
any of them, for example zero error, to isolate one effect.

### 3. Log structure and the importer

**Pages.** The model packs samples exactly as `t_ImuTagDataLog` does:

- 15 superframes of 10 IMU samples plus one auxiliary slot, 150 samples per
  2,048-byte page;
- page headers carrying the epoch, 1/1024 s ticks and `rawtemp`;
- a final page left partly erased (all `0xFF`) after the last superframe.

**Checkpoints and flags.**

- Checkpoint flags (`RESYNC`, `RESYNC_STORAGE_SKIP`, `RESTART_RECOVERY`)
  appear only on the first page of each 8-page checkpoint group, as on a real
  tag.
- They travel in each Ack's `millisecond` field, which is where tagcore
  reads them.
- Scripted events place resyncs, storage skips (a genuine hole in the data)
  and restart recoveries, so segments and gaps can be tested on demand.

**The importer, `tag-log-import` (new, C++, about 150 lines, in
`host/commandline`).**

- **Input:** a download in the JSONL format `tagclient`'s `tag_dwnld.py`
  already writes. That is one header line (`TagInfo`, `Config`, state log),
  then one Ack per line in protobuf JSON. The header gains an optional
  `calibration` list of `CalibrationConstants`.
- **What it does:** drives `SqliteTagLogWriter` through `writeHeader`,
  `beginLog`, one `writeLog` per Ack, and `endLog`, modelled on
  `uiuctag_end_to_end_check` and the capture rebuild's write loop.
- **Other uses:** it is tag-agnostic, so it also turns any `tagclient`
  download into SQLite. That is useful in its own right, and it means tagsim
  adds no new file format.

**Alternatives considered:**

- *Synthesizing a full `tag-capture` directory for `tag-rebuild`.* That means
  an internal-flash image, backup registers, an ECC index and a manifest.
  It is possible, but heavy.
- *Tag-server's planned simulated backend.* It could later serve tagsim pages
  live, but it is not needed for this proposal.

### 4. Ground truth and provenance

**Where truth goes.** Truth goes to a sidecar SQLite file beside the log
(`name.truth.db3`), so the generated log stays exactly what a real download
would produce. Times are the log's own elapsed microseconds, so truth joins
directly against `ImuAccel.ElapsedUs` and the rest.

**Truth tables:**

| Table | Contents |
| --- | --- |
| `TruthPose` | Position (local ENU, m) and attitude quaternion (tag frame to ENU) at every IMU sample |
| `TruthKinematics` | Ideal specific force (tag frame, mg) and body rate (dps), before noise and quantization |
| `TruthField` | True Earth field in the tag frame (µT), dip and declination at the site |
| `TruthAtmosphere` | True altitude, true sea-level pressure, true pressure |
| `TruthMagSamples` | For every logged magnetometer slot: the true conversion time, or that it was a miss |
| `TruthClock` | True and reported clock error (ppm); scripted events and their times |
| `SimRun` | Tool version, random seed, the full configuration as JSON, source log name and SHA-256, the trajectory consistency figures |

**Reproducibility.** Every run is deterministic for a given seed and
configuration. The `SimRun` row is enough to regenerate the log bit for bit.

### 5. Seeing the result

[UAV Log Viewer](https://github.com/ardupilot/uavlogviewer) is ArduPilot's
browser-based log viewer, with a 3D flight-path view and field plots. It is
the primary tool for checking a trajectory by eye:

- **The source flight.** It opens the source `.bin` as it is.
- **Truth and estimates.** tagsim writes the truth trajectory, or an estimate
  (for example `dataprocessing` orientation combined with the truth
  position), as a log the viewer opens. The true flight and a filter's
  estimate can then be watched side by side, next to the numerical scores.

The export format is settled in phase 3, against what the viewer reads:

- a MAVLink telemetry log of position and attitude messages, which pymavlink
  writes directly;
- or a minimal DataFlash log, whose self-describing `FMT` records make
  writing one simple.

PyFlightCoach's plots are a secondary option for pattern-specific views.
sensorviz shows the generated IMUTag log itself as it shows any other.

## Phases

| Phase | Work | Checked by |
| --- | --- | --- |
| 1 | `tag-log-import`; tagsim page and Ack writer; a stationary trajectory; no noise | The imported log decodes back to exactly the values the model wrote, including flags, segments, NaN slots and partial pages; `sensorviz` and `dataprocessing` open it |
| 2 | Full sensor model (noise, quantization, saturation, clock, magnetometer timing, pressure drift, calibration distortion); truth sidecar and `SimRun` | Allan-variance and spectrum checks of a simulated static log against the configured noise; `check_orientation_frames.py` agrees; calibration recovers the true field |
| 3 | Pattern-flight replay and synthetic primitives; consistency check; UAV Log Viewer export | Strapdown drift under a stated bound; the exported trajectory matches the source flight by eye in the viewer and numerically |
| 4 | pytest harness: small generated fixtures run through the importer, `dataprocessing` and, once it exists, sensorviz's `session_cli`; a saturation report across ODR and range settings | Runs without hardware; the range report informs default configuration |
| Later | Refit noise and bias parameters from a static recording on a prototype; validate against a short real motion recording | Simulated and real noise statistics agree |

Phase 1 is useful on its own: it gives the download path a hardware-free
round-trip test, and it adds `tag-log-import` for `tagclient` downloads.

## Packaging

The package follows `tagclient`'s layout:

- `host/python/tagsim/pyproject.toml`, with a `src/tagsim` package,
  `tests/`, Python 3.10 or later.
- Dependencies: numpy, scipy, protobuf, pymavlink, and a WMM implementation.
- Protobuf types come from `tagclient`'s runtime compilation of `proto/`, so
  no generated code is committed.
- Command line: `tagsim run config.toml -o name.db3`, which writes the JSONL,
  runs `tag-log-import` (found through `TAG_LOG_IMPORT_BIN`, like
  `tagclient`'s `TAG_SERVER_BIN`) and writes the truth file.
- Source flight logs are not committed. Small synthetic-primitive fixtures
  are.

## Open questions

- **Logs.** Which pattern-flight logs can be used? Their licensing and
  provenance need settling before any are used in tests or examples.
- **Which board to target first.** IMUTagNandBmp581 is assumed. Other boards
  differ in the magnetometer part and mapping.
- **Default ranges.** These are left to the phase 4 saturation report, as
  the defaults may be too narrow.
- **Refitting.** Should the model refit noise parameters automatically from
  a bench log once prototypes exist, or keep them hand-set from datasheets?
