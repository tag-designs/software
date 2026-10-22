---
type: design
status: current
summary: tagsim's Python prototype of the IMUTag post-processing filter - a multi-rate error-state EKF over attitude, gyro and accelerometer bias, relative altitude and vertical velocity, with an RTS smoother - how it orders the sensors' events, its updates and initialisation, and what it does not model.
---

# IMUTag Navigation Filter (Prototype)

`tagsim filter` estimates, from a decoded IMUTag log alone, the tag's
attitude (tag frame to ENU), its gyro and accelerometer biases, and its
altitude relative to the start with vertical velocity. It is a forward error-state EKF followed by a
Rauch-Tung-Striebel smoother over the whole record, written in Python
(`src/tagsim/navfilter.py`) as the prototype for a later port into the Qt-free
`sensoranalysis` core. Scored with tagsim on the recorded pattern flights,
with the simulator's datasheet and assumed scale, misalignment and bias
instability errors, it is good to about 0.8° in flight (median; 1.6° at the
95th percentile) where the single-sample compass is wrong by about 28°; the
figures and their caveats are in
[compass-baseline-results.md](compass-baseline-results.md).

```sh
tagsim filter --log f173.db3 --truth f173.truth.db3 -o f173-filt.db3
tagsim score --truth f173.truth.db3 --estimate f173-filt.db3 --table FilterSmoothed -o f173-smoothed.score.db3
```

## One event stream, any rates

The filter never assumes a ratio between sensor rates. It reads the log into
one time-ordered stream:

| Source | Events | Rate |
| --- | --- | --- |
| `ImuAccel`, `ImuGyro` | one per sample, at its `ElapsedUs` | the configured ODR, 100–1600 Hz |
| `ImuMag` | one per row with finite values | once per 10-sample superframe |
| `ImuPressure` | one per row with a finite value | once per superframe |

A missing slot (the NaN the firmware writes) or an absent row is simply an
absent event, and a gap is a longer stretch between events. The IMU samples
are the stream's backbone: between consecutive samples the filter integrates
the gyro and the vertical channel; an auxiliary event that falls strictly
between two samples splits the interval, with the IMU values interpolated
linearly to its time, so its update is applied at its own time. Event times
are formed in whole microseconds, as `ElapsedUs` is, so an event that falls
on a sample has exactly that sample's time and is applied there.

**Each auxiliary event carries two times**: `t_log`, the row's own
`ElapsedUs`, and `t`, the time the filter applies it at:

- tagcore stamps an auxiliary slot with its superframe's **first** IMU
  sample, but the firmware polls the magnetometer and pressure sensor after
  copying the superframe's **last** one (`sampleDataCollection` in
  `families/IMUTag/src/sensors.c`). So `t = t_log + 9 / ODR` for both. At
  100 Hz that is 90 ms; uncorrected, it would put a 10 m/s climb's altitude
  0.9 m late.
- a magnetometer reading is also older than its poll by an age the log does
  not record: 0 to one conversion period of the free-running BMM350 (80 ms at
  100 Hz; the firmware scales the BMM350's rate with the ODR). The filter
  subtracts the **mean** age, half a nominal period, from `t`, and treats the
  rest as noise (below). `FilterConfig.mag_age_s` overrides the subtracted
  age; it is the hook for a per-sample age correction, which is not built.

Subtracting the mean age matters: on a tag turning steadily at 60 dps, the
attitude error is 1.1–2.4° without it and 0.45° with it.

## State

The nominal state is the attitude quaternion `q` (tag frame to ENU, as
`TruthPose`), the gyro bias `b` (rad/s, tag frame), the accelerometer bias
`ba` (m/s², tag frame), and, with the pressure channel on, the altitude `h`
(m) and vertical velocity `vz` (m/s, up). The error state is
`[dθ, db, dba, dh, dvz]`, eleven states, with the attitude error taken in
ENU: `q_true = exp(dθ) ⊗ q`. Its first two components are tilt and the third
heading, which keeps the update Jacobians simple.

**The accelerometer bias states** (`accel_bias_states`, on by default;
initial sigma 10 mg, random walk 0.01 mg/√s) are there for the smoother. A
bias the filter does not know tilts its attitude on the ground, where the
accelerometer is trusted most, and the smoother then carries that wrong tilt
from the ground segments into the flight: without them the smoothed
in-flight error was worse than the forward pass's (0.34° against 0.17° on
five flights), with them it is better (0.07° against 0.16°). The bias is
observable because the aircraft visits many attitudes and the magnetometer
holds tilt independently of the accelerometer. With accelerometer scale and
misalignment errors the apparent bias depends on attitude, and the states
then cost some heading on the ground while still helping in flight (see
[compass-baseline-results.md](compass-baseline-results.md)).

There is deliberately no gyro scale or misalignment, magnetometer
disturbance, pressure offset or sea-level drift state. Altitude is relative to the first
pressure sample under the ISA with a constant 1013.25 hPa reference, so a
constant sensor offset cancels; sea-level pressure is assumed not to drift
during an experiment.

## Propagation

Over each interval `dt`, with the interval's mean rate `w` and specific force
`f` (both interpolated as above):

```
q   <- q ⊗ exp((w - b) dt)
a    = (R(q) (f - ba))_up - g
h   <- h + vz dt + a dt²/2
vz  <- vz + a dt
```

`g` is the mean `|f|` over the still periods, so the vertical component of
the accelerometer's bias at the start attitude cancels. The error dynamics
are `dθ' = -R db`, `dvz' = (dθ × R f)_up - (R dba)_up`. That last term couples tilt to
the vertical channel: a tilt error rotates part of a large manoeuvring
specific force into the vertical, which pressure then sees, so the pressure
updates also correct tilt during manoeuvres.

Process noise: gyro angle random walk 0.01 dps/√Hz (the datasheet's 2.8
mdps/√Hz with margin for interpolation), bias random walk 1e-4 dps/√s, and a
vertical acceleration noise of 0.2 m/s²/√Hz, which also covers the
accelerometer's scale and misalignment errors. A sample with any accelerometer axis at full
scale adds a large vertical noise (the ±4 g range clips about 2 % of samples
in these flights), since its vertical acceleration is then wrong.

## Updates

**Accelerometer, at every IMU sample.** The specific force less the bias
estimate is compared with gravity, `R(q)^T (0, 0, g)`. It is only gravity when the tag is not
accelerating, so the per-axis sigma grows with how far it is from that:

```
sigma = sqrt(ODR/100) · (0.5 m/s² + 3 · ||f| - g| + 25 m/s · |w - b|)
```

The `|w|` term is the centripetal acceleration of an aircraft turning at
`|w|` at about 25 m/s, which `||f| - g|` alone misses when a turn happens to
keep `|f|` near 1 g. The update is skipped when `||f| - g|` exceeds 1 g or an
axis is saturated. The `sqrt(ODR/100)` factor keeps the information per second
the same at every ODR.

**Magnetometer, per event.** The log's newest `Calibration` row corrects the
stored field, `A (M - V)`, which is mapped to the tag frame (stored
`(B_y, -B_x, B_z)`, see [simulator.md](simulator.md#sensor-model)) and
compared with the Earth field, `R(q)^T B_ENU`. The full vector is used, so it
constrains tilt as well as heading: two of the three rotation axes, and it is
unaffected by acceleration. The update is skipped when `|m|` differs from
`|B|` by more than 5 µT. Its sigma is 1 µT plus `|B| · |w| · σ_age`, the error
the unrecorded part of the sample age causes at the current turn rate, with
`σ_age` the SD of an age uniform over one conversion period.

`B_ENU` is the World Magnetic Model at the site and the log's date
(`--site LAT LON ALT`), a given field (`--field-ned`), or the field a tagsim
truth file was generated with (`--truth`).

**Pressure, per event.** ISA altitude of the pressure, less the first
sample's, against `h`, with a sigma of 6 cm (the BMP581's 0.41 Pa is about
3.5 cm).

## Initialisation

- **Still periods.** A superframe is still when its gyro and accelerometer
  spread are small and its mean rate is no larger than a gyro bias could be
  (3 dps; a steady turn has no spread). Runs of at least 2 s count.
- **Gyro bias** starts at the mean gyro over every still period, before and
  after the flight, with a 0.05 dps sigma; 0 and 1 dps without any.
- **Attitude**, when the record starts still, comes from the accelerometer
  and magnetometer means over that first still run (TRIAD against `g` and
  `B_ENU`), with a 2° sigma.
- **A record that starts moving** (the kept piece of some flights starts in
  the air) is bootstrapped: a first, attitude-only pass is run and smoothed,
  and its estimate at the first sample seeds the real pass with a 3° sigma.
  The initial altitude and vertical velocity come from a line through the
  first 0.5 s of pressure altitude. Without this, the forward pass on
  `00000176` starts with a 30° attitude error and a 58 m/s vertical-velocity
  error.

## Smoother

The forward pass stores, at every node (each IMU sample and each auxiliary
event between samples), the nominal state after the node's updates `x_f,k`,
the RTS gain `C_k = P_k F_k^T P_{k+1|k}^{-1}` to the next node, and where the
node is: its time and the IMU interval it falls in. The backward pass then
sets

```
x_s,k = x_f,k ⊞ C_k (x_s,k+1 ⊟ x_{k+1|k})
```

with `⊟` and `⊞` the error-state difference and injection (`dθ` in ENU). Only
the smoothed mean is computed, not its covariance.

**The prediction `x_{k+1|k}` is recomputed, not stored.** It is `x_f,k`
propagated over the node's interval with the IMU interpolated to the two node
times, which the backward pass rebuilds from the stored time and interval and
the log's samples. The forward pass and the backward pass call the same
propagation function, so the recomputed prediction is bit-identical to the
one the forward pass made, and the smoothed result is exactly what storing it
gave.

**The gains are stored as float32** (`gain_float32`, default on). Against
float64 gains, the smoothed attitude moves by at most 2.7e-4° on five of the
flights (most of them 1e-5° or less; the worst is `f176` without pressure,
whose bootstrapped start carries the difference into the forward pass too),
altitude by under 1 µm and vertical velocity by under 5 µm/s, against a
stated tolerance of 0.001°, 1 mm and 1 mm/s that `test_flight.py` checks.

**Storage per node** is 12 doubles of state (q, gyro bias, `h`, `vz`,
accelerometer bias), the `n x n` gain, a double and an int32: 592 bytes with
all eleven states, 364 with eight (no accelerometer bias states). A
full-flash log at 1600 Hz, about 21.5 million samples and 23.6 million nodes,
needs about 14 GB.

**Above `memmap_threshold_mb` (1024 MiB) it goes to files.** The four
per-node arrays are then `numpy.memmap` files in a temporary directory
(`memmap_dir`, else the system's), deleted when the filter returns or fails.
The forward pass writes them in increasing node order and the backward pass
reads them in decreasing order; each file is mapped a window of
`memmap_window_nodes` (65 536) rows at a time, flushed and unmapped when the
access leaves it, so the process holds one window per file and the operating
system's page cache does the rest. Below the threshold they are plain arrays,
with identical results. The outputs, forward and smoothed states per IMU
sample (192 bytes per sample), stay in memory: they are the result.

| Synthetic run at rest, 1600 Hz | Storage | Peak memory | Time |
|---|---|---|---|
| 200 000 samples (`test_navfilter.py`), in memory | 136 MB bound | +126 MB | 20 s |
| the same, in files | | +22 MB | 20 s |
| 2 000 000 samples (21 min), in memory | 1355 MB bound | 1854 MB | 224 s |
| the same, in files (the default above 1 GiB) | | 590 MB | 226 s |

The first two are growth in `ru_maxrss` during the run; the last two are
macOS's peak memory footprint from `/usr/bin/time -l`, since `ru_maxrss` on
macOS leaves out compressed pages and reported only 606 MB for the in-memory
run. Of the file-backed 590 MB, 366 MB are the outputs and most of the rest
the log's own arrays. The results were identical in both modes and the
files were gone afterwards. Mapping the storage costs no measurable time.

## Output

`tagsim filter -o NAME-filt.db3` writes `FilterForward` and `FilterSmoothed`,
one row per IMU sample with `SegmentId`, `SampleIndex`, `ElapsedUs`,
`qw qx qy qz` (tag frame to ENU), `alt` (m, relative to the first pressure
sample), `vz` (m/s, up), the gyro bias `bx by bz` (dps) and the
accelerometer bias `abx aby abz` (mg), plus `FilterRun`
with the configuration and counters. `tagsim score --table FilterSmoothed`
scores either table directly; `tagsim batch --filter` runs and scores the
filter with and without the pressure channel on every flight. Each segment of
a multi-segment log is filtered on its own.

Speed in Python: about 9 000 IMU samples a second with all eleven states,
so 7–16 s for a ten-minute flight at 100 Hz (more when the start is
bootstrapped, which runs a first pass) and some 40 minutes for a full-flash
1600 Hz log.

## What it does not model

- **Gyro scale and misalignment are not estimated**, and they are what limits
  the filter on the simulated flights: ±0.5° of gyro misalignment alone takes
  the in-flight error from 0.23° to about 1°. The misalignment figure is an
  assumption, so a bench calibration, or states for it, come first.
- No g-sensitivity, temperature dependence or vibration in the scored runs;
  the bias instability figures are assumed.
- **A wrong magnetometer calibration** is not detected or estimated: with
  `--calibration perturbed` logs the error roughly doubles, mostly in
  heading.
- The per-sample magnetometer age is not estimated (see the hook above).
- Tuning was set by hand on a few flights and synthetic cases, not fitted.
