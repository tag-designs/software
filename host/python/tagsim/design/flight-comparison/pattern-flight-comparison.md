---
type: results
status: current
summary: Testing the IMUTag post-processing filter on simulated IMUTag data from recorded aerobatic flights - the purpose and approach, per-flight results under three sensor-error levels with side-by-side attitude visualisations, how the 25 Hz ArduPilot Kalman-filter output becomes simulated sensor logs and the sensor error models, the filter and its handling of asynchronous sampling, and where this leads (2026-10-22).
---

# Pattern-Flight Comparison: Filter Against Truth

**Date:** 2026-10-22. **Build:** tagsim 0.2.0, at the commit that adds this
document. **How to rebuild it:** [reproducing.md](reproducing.md).

## Contents

1. [Purpose and approach](#1-purpose-and-approach)
2. [Results](#2-results)
3. [The input data and the simulation](#3-the-input-data-and-the-simulation)
4. [The filter](#4-the-filter)
5. [Summary and next steps](#5-summary-and-next-steps)

## 1. Purpose and approach

We are developing a Kalman filter with a smoothing step that will process
real data from IMUTags flown on animals: an estimate of the tag's attitude
and altitude through a whole record, computed after the tag is downloaded.

To test the proposed filter before animal data exists, we generate
**simulated** IMUTag sensor data from ArduPilot logs of real flights made
with an aerobatic R/C aircraft. Those logs do not contain raw sensor values.
They contain the 25 Hz output of the autopilot's own Kalman filter: its
estimate of the aircraft's attitude, position and velocity. We take that
estimate as **ground truth** and combine it with noisy sensor models built
from the datasheets of the sensors the IMUTag uses.

An important step is **upsampling** the 25 Hz ground truth. The tag samples
its accelerometer and gyro at 100 Hz or faster, and its magnetometer and
pressure sensor on their own schedules, so the truth has to be evaluated at
arbitrary times, with rates and accelerations that are consistent with the
attitude and position. We fit smooth splines through the 25 Hz samples and
take the splines as the truth from then on.

The upsampled truth drives the sensor models, which produce simulated log
files in the same SQLite format our tools use for downloads from real tags.
These simulated logs are then processed by our filter, a forward Kalman
filter followed by a reverse (Rauch-Tung-Striebel) smoothing pass, which
sees only the simulated log, never the truth.

To compare the ground truth with the filter's output, we made a set of
visualisations: two 3D aircraft models on one timeline, one driven by the
ground truth and one by the filter's output, with their altitudes and the
errors between them.

```text
 ArduPilot log (25 Hz EKF output)
        │ glitch repair, spline fit ("upsampling")
        ▼
 ground truth: attitude, position, velocity, at any time
        │ ideal sensor signals + datasheet sensor error models
        ▼
 simulated IMUTag log (SQLite) ──► filter + smoother ──► estimate
        │                                                  │
        └──────────── compare, visualise ◄─────────────────┘
```

The ground truth's position, horizontal as well as vertical, is used only to
generate the sensor inputs: its accelerations become the accelerometer
signal, and its altitude the pressure. It is never fed into our filter, which
sees only the simulated sensor log, and the comparison uses only attitude and
altitude change.

## 2. Results

**In brief:** the smoothed filter recovers the airframe's attitude to a few
hundredths of a degree with idealised sensor errors, under a tenth of a
degree with realistic ones, and about a quarter of a degree when the
magnetometer calibration is also wrong (medians in flight). Altitude change
is within about 2 cm. These are results on **simulated** sensors driven by
an autopilot's Kalman-filtered estimate of the flight, not on recorded sensor
data ([section 3](#what-this-means-for-the-signals) says what that implies).
The filter does not estimate horizontal position, and nothing here compares
it.

### Three error levels

Every flight was simulated three times, differing only in the sensor errors
([section 3](#sensor-error-models) gives the models):

| Level | Errors | Pages |
| --- | --- | --- |
| **Basic** | White noise, turn-on biases, magnetometer hard and soft iron with an exact calibration in the log, pressure noise and offset. Perfect scale and axis alignment, constant biases | `-basic` |
| **Realistic** | Basic, plus gyro and accelerometer scale error, axis misalignment and bias instability | `-realistic` |
| **Miscalibrated** | Realistic, with a deliberately wrong magnetometer calibration in the log, as a real field calibration might be | `-miscalibrated` |

Within a flight the three share one random seed, so the turn-on biases and
magnetometer distortion are the same in all three.

### What is compared

At every filter output sample (100 Hz), the smoothed attitude and altitude
against the truth at the same time:

- **Attitude error:** the angle of the rotation between estimated and true
  attitude.
- **Tilt error:** the angle between the estimated and true direction of
  "up", seen in the tag frame; the part the accelerometer constrains.
- **Heading error:** the twist about the vertical of the same error
  rotation; the part the magnetometer constrains.
- **Altitude error:** estimated minus true altitude *change*. The filter's
  altitude is relative to its start, so the two are matched over the first
  second of the record.

Statistics are of absolute values over **flight** only (the autopilot's
`STAT.isFlying` window): median, 95th percentile and maximum.

### The visualisations

Each flight has one page per error level. A page shows, on one timeline:

- two 3D aircraft models, **ground truth on the left** and **the filter's
  smoothed estimate on the right**, seen from the same camera;
- a pair of bars with true and estimated altitude above the start;
- strips of tilt, heading and altitude error over the whole flight, with
  flying time shaded.

The models move at the source log's 25 Hz. They show only what the filter
estimates: attitude and altitude, with no horizontal track. Play, pause
(space bar) and the scrubber move both models; drag either view to turn both
cameras; click an error strip to jump to that time; `#t=SECONDS` on the URL
opens a page at a time. The pages need a browser with WebGL and an internet
connection (three.js loads from a CDN); they contain only attitude, altitude
above the start and the errors.

### The flights modelled

The source set is 16 pattern-aerobatics flights of about 10 minutes each,
loops, rolls, knife-edge and vertical figures; 15 are usable. Three are shown
here: two clean flights, and one that shows the cost of a bad start. Across
all 15 the medians are 0.08° (basic), 0.12° (realistic) and 0.27°
(miscalibrated) ([navigation-filter.md](../navigation-filter.md),
[user guide](../../user-guide.md#how-well-it-does)).

#### f173

10.4 min simulated, starting on the ground 10 s before take-off; no glitch
repairs; the accelerometer is at full scale (±4 g) on 2.1 % of samples on its
worst axis, in the pulls.

| Level | Attitude, median / 95 % / max | Tilt, median | Heading, median | Altitude, median / max | Visualisation |
| --- | --- | --- | --- | --- | --- |
| Basic | 0.03° / 0.07° / 0.09° | 0.02° | 0.02° | 1.8 cm / 5.9 cm | [f173-basic](pages/f173-basic.html) |
| Realistic | 0.06° / 0.13° / 0.18° | 0.04° | 0.04° | 1.7 cm / 5.7 cm | [f173-realistic](pages/f173-realistic.html) |
| Miscalibrated | 0.25° / 0.32° / 0.41° | 0.21° | 0.11° | 1.7 cm / 5.7 cm | [f173-miscalibrated](pages/f173-miscalibrated.html) |

#### f177

10.7 min simulated, starting on the ground 10 s before take-off; no glitch
repairs; 1.8 % of samples at full scale on the worst axis. Of the eight
flights with no glitch repairs, this one has the largest median error when
miscalibrated.

| Level | Attitude, median / 95 % / max | Tilt, median | Heading, median | Altitude, median / max | Visualisation |
| --- | --- | --- | --- | --- | --- |
| Basic | 0.05° / 0.11° / 0.15° | 0.03° | 0.04° | 1.5 cm / 7.7 cm | [f177-basic](pages/f177-basic.html) |
| Realistic | 0.07° / 0.14° / 0.15° | 0.04° | 0.05° | 1.5 cm / 7.7 cm | [f177-realistic](pages/f177-realistic.html) |
| Miscalibrated | 0.29° / 0.37° / 0.39° | 0.26° | 0.15° | 1.5 cm / 7.7 cm | [f177-miscalibrated](pages/f177-miscalibrated.html) |

#### f176: a flight that starts in the air

Both of this log's EKF3 cores glitch every 8–10 s and diverge by up to
200 m/s between 78 s and 354 s. The glitch repair
([section 3](#from-the-log-to-a-simulated-imutag)) splits the flight 23 times
and keeps the last 3.1 minutes, which begin in mid-aerobatics. 1.9 % of
samples are at full scale on the worst axis.

| Level | Attitude, median / 95 % / max | Tilt, median | Heading, median | Altitude, median / max | Visualisation |
| --- | --- | --- | --- | --- | --- |
| Basic | 0.73° / 1.29° / 1.44° | 0.28° | 0.67° | 4.4 cm / 21.6 cm | [f176-basic](pages/f176-basic.html) |
| Realistic | 0.75° / 1.40° / 1.59° | 0.29° | 0.69° | 4.3 cm / 22.6 cm | [f176-realistic](pages/f176-realistic.html) |
| Miscalibrated | 0.67° / 1.69° / 1.91° | 0.43° | 0.59° | 4.3 cm / 22.6 cm | [f176-miscalibrated](pages/f176-miscalibrated.html) |

### Observations

- **Sensor scale and misalignment cost little once estimated.** The filter's
  gyro scale and misalignment states recover them from the flight's large
  rotations: realistic errors roughly double a very small error. Without
  those states the realistic median across flights is 0.73°
  ([navigation-filter.md](../navigation-filter.md)).
- **A wrong magnetometer calibration shows up mostly as tilt,** not heading,
  on both clean flights. The filter estimates the hard iron the calibration
  left over, but not a wrong soft iron, which scales the field; the design
  note records that this also pulls the gyro calibration off
  ([navigation-filter.md](../navigation-filter.md#what-it-does-not-model)).
  The tilt is probably that pull showing through. In the strips it is a
  nearly constant offset, not a drift.
- **The error strips follow the flight's content.** On f177 the error is
  smooth through the first half and oscillates with the manoeuvres in the
  second; scrubbing the page to those stretches shows which figures they
  are.
- **Altitude is the same at every error level.** It depends only on
  pressure, whose errors do not change between levels. The 1.5–1.8 cm median
  is below the BMP581's single-sample noise (0.41 Pa, about 3.4 cm), from the
  filter's combining it with the vertical acceleration.
- **f176's error comes from its start, not its sensors.** It is about the
  same at all three levels. With no settled start, the filter takes its
  initial attitude from a first pass over moving data, learns the gyro
  biases in flight, and sets its altitude datum mid-manoeuvre; the first
  minute shows it settling. A tag that is switched on, or whose record
  resumes, in flight will look like this.

## 3. The input data and the simulation

### What the flight logs contain

The source logs are ArduPlane 4.0.9 DataFlash files (`.bin`): a binary
stream of self-describing records, each message type defined by an `FMT`
record (name, field types, column names) and labelled by `UNIT`, `MULT` and
`FMTU` records. They are kept in a private data repository.

**No raw inertial or barometric data was logged.** The logging mask on these
flights omitted the `IMU`, `ACC`, `GYR` and `BARO` messages, so the
autopilot's accelerometer, gyro and barometer readings are not in the files.
What is there, in `00000173.BIN`:

| Message | Rate | Content | Used here |
| --- | --- | --- | --- |
| `XKQ1`, `XKF1` | 25 Hz | EKF3 core 0: attitude quaternion; NED position and velocity | **Yes: the ground truth** |
| `XKQ2`, `XKF6` | 25 Hz | EKF3 core 1, the same states | Only to detect and repair core 0's glitches |
| `XKF2`–`XKF5`, `XKF7`–`XKF9`, `XKV*`, `XKT*` | 25 Hz, ~2 Hz | EKF3 biases, wind, field estimates, innovations, variances, timing | No |
| `NKF1`–`NKF9`, `NKQ1`, `NKQ2` | 25 Hz | EKF2, two cores, running alongside | No |
| `ATT`, `AHR2`, `POS` | 25 Hz | The autopilot's attitude (with trim), its secondary attitude, its position | No (`ATT` only as a cross-check) |
| `MAG`, `MAG2`, `MAG3` | 10 Hz | Three raw magnetometers, with offsets | No |
| `GPS`, `GPA` | 5 Hz | Fix, position, velocity, accuracy, GPS time | Only `GWk`/`GMS`, to put the log on UTC |
| `STAT` | 5 Hz | `isFlying`, armed state | The flight window |
| `ORGN` | once | EKF origin | Position datum; the magnetic field model's location |
| `PIDR/P/Y/S`, `MODE`, `PARM`, `MSG`, … | various | Controller and configuration | No |

**Why the EKF state drives the simulation.** There are no raw inertial data
to replay, and even if there were they would belong to a different sensor,
at a different place on the airframe, sampled and filtered differently. The
EKF's attitude, position and velocity describe the motion of the airframe
itself, which is what an IMUTag riding on it would feel. The simulation
asks: *if the aircraft moved exactly as its EKF says, what would an IMUTag
have recorded?*

**Why the logged magnetometers are not used.** The tag's own magnetometer
sees the Earth's field through its own distortion, not the autopilot's. The
simulation takes the field from the World Magnetic Model at the log's origin
and date (51.7 µT, dip 66.6°, declination −4.6° for these flights), and
applies an IMUTag magnetometer's distortion and noise to it.

### From the log to a simulated IMUTag

The full design is in [simulator.md](../simulator.md); in brief:

1. **Read the EKF track.** EKF3 core 0's quaternion (body FRD to NED) and
   NED position and velocity, joined on `TimeUS`.
2. **Repair glitches.** A sample implying more than 15 g or 1000 °/s against
   the last good one is implausible; core 1 is substituted where it is
   plausible; where both cores are plausible but disagree by more than 5 m/s
   or 10°, neither is trusted. Untrusted runs up to 0.5 s that return to the
   previous track are bridged; anything longer, or a step that persists (an
   EKF reset), splits the flight, and the longest piece overlapping the
   flight window is kept. Every repair is recorded. Most flights need none;
   f176 needs 46.
3. **Upsample: fit splines.** Attitude is a cumulative cubic B-spline on unit
   quaternions with control points at the 25 Hz samples, so body rate comes
   analytically from the spline, with no Euler angles. As an approximating
   (not interpolating) uniform cubic B-spline its magnitude response is
   sinc⁴(f/25 Hz): −0.4 dB at 2 Hz, −3.4 dB at 6 Hz, −9.7 dB at 10 Hz.
   Position is a quintic B-spline fitted by least squares to EKF position
   (σ 5 m) and velocity (σ 0.1 m/s) with a jerk penalty (σ 100 m/s³);
   acceleration is its second derivative. Position is weighted loosely
   because EKF position and integrated velocity drift metres apart over a
   flight; the fit stays within 2–5 m of the EKF position (median). An exact
   interpolant through both position and velocity is not used: on EKF output
   the two disagree by centimetres per step, and honouring both swings to
   tens of g between samples. **The splines are the ground truth** from here
   on.
4. **Compute the ideal signals** at every IMU sample:
   - specific force `R_tn (a_n − g_n)`, in the tag frame;
   - body rate, from the attitude spline;
   - the WMM field rotated into the tag frame at each magnetometer
     conversion's own time;
   - static pressure from the ISA troposphere at the true altitude, with
     constant sea-level pressure; the sensor is assumed shielded from the
     airflow;
   - the tag mounted flat on the airframe, battery end forward.

   **Where the gyro signal comes from.** The gyro's ideal signal is the body
   rate of the attitude spline, which is fitted to the EKF's attitude
   quaternions (`XKQ1`). It is computed analytically from the spline's
   cumulative-basis recursion, as the angular velocity of the spline's
   rotation, then rotated through the mounting into the tag frame. So the
   gyro input is the time derivative of the EKF's attitude, smoothed by the
   spline's sinc⁴ response (step 3), and owes nothing to position. Because
   the EKF's attitude is itself an estimate that integrated the autopilot's
   own gyros, this is a smoothed reconstruction of the true rotation rate,
   not a gyro recording: it has the attitude's bandwidth (99.9 % of the power
   below 4.9 Hz) and none of the vibration a real gyro picks up.

   **Where the accelerometer signal comes from: the position track.** The
   accelerometer's ideal signal combines both splines. The acceleration comes
   from position, and the attitude spline rotates `a_n − g_n` into the tag
   frame. The full 3D position spline, not just altitude, is what produces
   it: `a_n` is the second derivative of the north, east and down position.
   Its horizontal parts
   carry the centripetal acceleration of turns, loops and pulls, most of the
   4–7 g peaks. Without them the accelerometer would see only gravity and
   vertical acceleration, and the filter's hardest problem, telling gravity
   from manoeuvring acceleration, would disappear. Because the spline follows
   the EKF velocity tightly (σ 0.1 m/s) and its position loosely (σ 5 m),
   these accelerations are those of the EKF's velocity; the truth's
   horizontal track stays within 2–5 m of the EKF position, not on it. Beyond
   that, position as a location is barely used: altitude sets the pressure,
   and the WMM field is evaluated once at the log's origin, since it changes
   by only nanotesla over the flying area. The filter never sees the truth,
   and the comparison scores only attitude and altitude change.

   A whole-flight strapdown check integrates these ideal signals back up from
   the true start, with no restarts: on f173 (627 s) attitude stays within
   0.02°, velocity within 1.9 cm/s and position within 5.2 cm of the spline.
   That measures the simulation's own arithmetic, not sensor errors.
5. **Sample as the IMUTag firmware does.** Accelerometer and gyro at 100 Hz
   (±4 g, ±1000 °/s). Magnetometer and pressure polled once per 10-sample
   superframe, after its last IMU sample. The BMM350 magnetometer free-runs
   at 12.5 Hz on its own clock (random phase, ±2 % rate error), so each poll
   takes the newest completed conversion and its age sweeps 0–80 ms as the
   two clocks beat; the BMP581 converts fast enough that its age is
   negligible.
6. **Add the sensor errors** (below), quantize to the stored LSBs, saturate
   at full scale (3997.6 mg at ±4 g), map to the firmware's axes, and pack
   into the 2 KB flash pages the tag writes.
7. **Decode with the production importer.** The pages go through
   `tag-log-import`, the same code as a real download, so the result is a
   genuine IMUTag SQLite log. Everything the tag could not know (true
   attitude, position, field, conversion times, seed, every setting) goes to
   a separate truth file.

### What this means for the signals

The ground truth is a 25 Hz Kalman-filtered estimate, smoothed further by
the spline fit. It is self-consistent and physically plausible, but
band-limited and smoother than the real motion:

- **Body rate** has 99 % of its power below 2.3 Hz and 99.9 % below 4.9 Hz.
- **Specific force** has 99 % below 5.9 Hz, with a low plateau out to about
  12 Hz (−11 dB relative to 1 Hz at 5 and 10 Hz, −21 dB at 12.5 Hz), mostly
  the EKF's velocity corrections at GPS updates. At 20 Hz it is −61 dB.
- **Nothing above the source's 12.5 Hz Nyquist frequency** is real: no
  propeller or engine vibration, no structural modes, no turbulence beyond
  what the EKF tracked.

(Welch spectra of f173's ideal signals at 100 Hz, summed over axes, mean
removed; [reproducing.md](reproducing.md#the-figures-behind-the-text).) The
simulated data is therefore a clean, band-limited signal plus modelled
sensor error, with none of the in-band mechanical noise a real airframe or a
flapping animal adds.

### Aside: why the IMU rate is 100 Hz

The simulated tag samples its accelerometer and gyro at 100 Hz. That is
tagsim's default and the IMUTag's lowest setting (the firmware offers 100,
200, 400, 800 and 1600 Hz), not a rate chosen for animal work, and it is
adequate here only because these inputs are so smooth.

**Why it is adequate for these flights:**

- **The truth has no content to lose.** As measured above, body rate has
  99.9 % of its power below 4.9 Hz and specific force essentially nothing
  above 12.5 Hz. At 100 Hz the Nyquist frequency is 50 Hz, so nothing
  aliases, and a 6 Hz component gets about 16 samples per cycle. A higher
  rate would only sample the same smooth curves more densely.
- **Integration error stays small.** A 500 °/s roll turns 5° per sample at
  100 Hz, which is coarse, but the integration includes the coning
  correction, and the whole-flight consistency check holds attitude within
  0.02° over 627 s.
- **The noise statistics do not depend on the rate.** Noise is modelled as a
  density, so more samples carry more noise each, while the information per
  second is unchanged.

**Where 100 Hz is not neutral, even here:**

- **The magnetometer and pressure rates scale with the IMU rate.** At 100 Hz
  they are polled at 10 Hz, and a magnetometer reading's age is uncertain by
  up to 80 ms. At 400 Hz those become 40 Hz and 20 ms. So 100 Hz is the
  *hardest* case for the filter's auxiliary updates and magnetometer clock
  fit.
- **Run time.** The Python filter takes about 70 s per pass per flight at
  100 Hz, and scales with the sample count.

**For animals it is the wrong rate.** Wingbeats run from a few hertz to
tens of hertz, depending on species, with harmonics well above that, and
landings and impacts are broadband. At 100 Hz those would alias into the
band the filter uses. Real deployments will probably need 400 Hz or more,
traded against flash capacity and battery life. That trade can only be
judged once the simulator has a flapping-motion model: on these inputs every
rate would look the same.

**What has been tested at other rates.** The filter's unit tests run on
synthetic motion at 100, 400 and 1600 Hz, including the same manoeuvre at
100 and 400 Hz ([navigation-filter.md](../navigation-filter.md)). No pattern
flight has been run above 100 Hz.

### Sensor error models

Each error is drawn once per run from a seeded generator (seed 0 here), so
every run is reproducible. Ranges are half-widths of uniform draws. Datasheet
figures are typical values for the IMUTag's parts (ST LSM6DSV16X, Bosch
BMM350, Bosch BMP581); **assumed** figures have no datasheet value and are to
be replaced by bench measurements.

| Sensor | Error | Value | Model | Levels |
| --- | --- | --- | --- | --- |
| Accelerometer | White noise | 60 µg/√Hz (0.42 mg RMS at 100 Hz) | Per sample, before quantization | all |
| | Turn-on bias | ±12 mg per axis | Constant | all |
| | Scale error | ±1 % per axis (**assumed**) | `T = I + diag(s) + M` applied to the true signal | realistic, miscalibrated |
| | Misalignment | ±0.5° per off-diagonal element (**assumed**) | The `M` in `T` | realistic, miscalibrated |
| | Bias instability | 0.05 mg RMS, τ = 100 s (**assumed**) | First-order Gauss-Markov | realistic, miscalibrated |
| Gyroscope | White noise | 2.8 mdps/√Hz (0.020 °/s RMS at 100 Hz) | Per sample | all |
| | Turn-on bias | ±1 °/s per axis | Constant | all |
| | Scale error | ±0.3 % per axis (datasheet `G_So%`) | As the accelerometer | realistic, miscalibrated |
| | Misalignment | ±0.5° per off-diagonal element (**assumed**) | As the accelerometer | realistic, miscalibrated |
| | Bias instability | 0.003 °/s RMS, τ = 100 s (**assumed**) | First-order Gauss-Markov | realistic, miscalibrated |
| Magnetometer | Hard iron | ±25 µT per axis | Constant offset | all |
| | Soft iron | diagonal ±1 % (x, y), ±3 % (z); symmetric off-diagonal ±0.5 % | Applied as `A⁻¹`, the inverse of the host's correction | all |
| | Noise | 190 nT (x, y), 450 nT (z) RMS | Per conversion | all |
| | Clock | ±2 % rate error, random phase | Conversion times | all |
| | Calibration in the log | exact; or wrong by up to ±2 µT per axis (hard iron) and ±1 % per element (soft iron) | The constants the host corrects with | exact: basic, realistic; wrong: miscalibrated |
| Pressure | Noise | 0.41 Pa RMS | Per poll | all |
| | Offset | ±30 Pa | Constant | all |
| | Quantization | 1/64 Pa | After noise and offset | all |

The accelerometer and gyro measurement is
`T · true + turn-on bias + Gauss-Markov + white noise`, then quantization and
saturation. A ±0.5° misalignment couples up to 0.9 % of a rate into each
other axis, about 4 °/s in a 500 °/s roll; it is the error that matters most
to the filter. Not modelled: temperature dependence, vibration (the model
has it; it is off here), the sensors' own anti-alias filtering, clock error,
dynamic pressure.

## 4. The filter

The filter under test is tagsim's Python prototype of the IMUTag
post-processing filter; the full design is in
[navigation-filter.md](../navigation-filter.md). It reads only the IMUTag
log: no truth, no autopilot data. The only external input is the Earth's
field at the site, here the same WMM field the simulation used.

**Structure.** A multi-rate error-state extended Kalman filter runs forward
through the record, then a Rauch-Tung-Striebel smoother runs backward over
it, so every estimate uses the whole record. The results above are the
smoothed estimate: what post-processing a download can do, as opposed to a
real-time filter.

**What it tracks:**

| State | Purpose |
| --- | --- |
| Attitude, tag frame to ENU | The output |
| Gyro bias | Turn-on bias and its slow wander |
| Gyro scale and misalignment (full 3 × 3 correction) | Learned from the flight's large rotations |
| Accelerometer bias | So the gravity reference is unbiased |
| Magnetometer offset | The hard iron the log's calibration left over |
| Altitude relative to the start, vertical velocity | The output; ties pressure to the vertical acceleration |

**The sensor updates.**

- **Gyro:** propagates the attitude between samples, corrected by the bias
  and scale states.
- **Accelerometer, every IMU sample:** compared with gravity. Its noise grows
  with how far the specific force is from 1 g and with the turn rate, so a
  5 g pull or a steady turn is not mistaken for gravity; it is skipped beyond
  1 g of departure or when an axis is saturated.
- **Magnetometer:** the full calibrated vector against the Earth's field, so
  it constrains tilt as well as heading; skipped when its magnitude is wrong
  by more than 5 µT (a disturbance).
- **Pressure:** ISA altitude change against the altitude state.

**Asynchronous sampling.** The filter never assumes a ratio between sensor
rates:

- **One event stream.** Every reading becomes an event at its own time:
  accelerometer and gyro at each IMU sample, magnetometer and pressure at
  each row that has a value. A missing reading (the NaN the firmware writes)
  or a gap is simply an absent event. The same code runs at every ODR from
  100 to 1600 Hz.
- **Events between IMU samples.** An auxiliary event that falls between two
  IMU samples splits the interval: the filter propagates to the event's time,
  with the IMU values interpolated linearly, applies the update there, then
  continues.
- **The logged time is corrected first.** The log stamps magnetometer and
  pressure rows with their superframe's *first* IMU sample, but the firmware
  polls them after its *last*, so each is moved 9 IMU periods later (90 ms at
  100 Hz). Uncorrected, a 10 m/s climb's altitude would be 0.9 m late.
- **The magnetometer's own clock is fitted.** Each magnetometer reading is
  older than its poll by 0–80 ms, set by the BMM350's free-running clock,
  which the log does not record. A first, attitude-only smoothing pass gives
  a reference attitude; while the tag turns faster than 30 °/s, the age that
  best fits each reading to that reference is found; a weighted periodogram
  and least squares then fit the clock's period and phase to all of them.
  Each reading is applied at its estimated conversion time. On these flights
  the fitted rate is within a few ppm of the simulated one, and 99 % of
  readings are placed within 0.7 ms of their true time, against 23.5 ms RMS
  for a fixed mean age. Correcting the age matters: on a tag turning at
  60 °/s an uncorrected age costs 1.1–2.4° of attitude.

**Initialisation.** A record that starts still takes its attitude from the
mean accelerometer and magnetometer over the still period. One that starts
moving, as f176 does, is bootstrapped from the first, attitude-only pass.

## 5. Summary and next steps

On simulated data driven by real aerobatic flights, the filter with
smoothing recovers attitude to 0.03–0.07° with exact calibration and about
0.25–0.3° with a wrong magnetometer calibration, and altitude change to about
2 cm, across sustained loops, rolls and vertical figures with accelerometer
clipping. It handles the tag's real sampling scheme: three sensors on
different schedules, one on its own unrecorded clock. A record that starts in
mid-flight is much worse: 0.7° instead of 0.03–0.07° with exact calibration.

These figures are optimistic in known ways:

- **The ground truth is an autopilot's estimate,** band-limited and smooth.
  Real data, and especially a flapping animal, adds in-band vibration and
  structural motion the simulation lacks, which will chiefly degrade the
  accelerometer's gravity reference.
- **Several error sizes are assumed:** accelerometer scale, both
  misalignments and both bias instabilities. Bench data from the prototypes
  will replace them; gyro misalignment matters most.
- **Constant sea-level pressure.** Weather moves real pressure by roughly
  1 hPa (8 m) over hours.
- **Clipping at ±4 g** loses the peaks of the pulls (1.8–2.1 % of samples).
  Attitude survives it, but any use of the acceleration itself needs a wider
  range.
- **The filter is given the field the simulation used;** on a real tag the
  WMM only approximates the local field.

**Where this leads.** The longer-term goal is meaningful visualisation for
researchers exploring flight-dynamics data from animals. The visualisations
here, a 3D model driven by estimated attitude beside one driven by truth,
with altitude and errors on one timeline, are a first step and already the
clearest way we have found to see what the filter is doing. The planned
experiments are in an aviary with known launch points, compared with video
from fixed cameras over tens of seconds. That sets the next steps:

1. **Short horizontal tracks.** The filter does not yet estimate horizontal
   position. Integrating the accelerometer twice from the true start drifts
   by about 4 m after 10 s, 50–70 m after 60 s and kilometres by the end of a
   flight, and no better with the true attitude: the accelerometer's errors
   and clipping dominate. Known launch and landing points, and stationary
   periods on perches, are the fixes that make tens of seconds tractable;
   they will be added to the filter as measurements and scored on windows
   cut from these flights.
2. **A virtual camera** matching the aviary's video cameras, so the 3D model
   can be overlaid on, or set beside, the video, with a time-synchronisation
   event (a tap visible in both) designed in from the start.
3. **Acceleration outputs** researchers need for biodynamics: angular rate
   and acceleration, and gravity-removed linear acceleration, all already
   available inside the filter.
4. **A flapping-motion model** in the simulator before the acceleration
   figures are trusted for animals, and bench data from the prototypes to
   replace the assumed sensor errors.
