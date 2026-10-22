---
type: results
status: current
summary: Append-only log of tagsim scores on the recorded pattern flights - dataprocessing's single-sample IMUTag compass, the baseline a post-processing filter has to beat, and the navigation filter prototype against it - with the build, settings and conditions of each run.
---

# Compass Baseline Results

Append-only. One block per scoring run, newest last. How the scores are
computed is in [simulator.md](simulator.md#scoring); how to rerun them is
`tagsim batch` in the [README](../README.md#filtering-scoring-and-viewing).

## 2026-10-21: `imu-compass-orientation` on the first 16 logs

**Conditions.**

- tagsim 0.2.0, branch `claude/dreamy-sammet-c9881f`; `dataprocessing` from
  the same tree; `tagsim batch --seed 0`.
- 100 Hz ODR, ±4 g, ±1000 dps, mounting `RFU`. Datasheet sensor errors with
  true calibration constants in the log, so the compass is calibrated
  perfectly and its errors come from noise, accelerometer bias, the aircraft's
  own acceleration and timing.
- WMM2020 field at each log's origin and date.
- 15 of the 16 logs are usable. `00000168` never sets `STAT.isFlying`.

**What "ground" is.** The 10 s padding either side of the `isFlying` window,
so it contains the take-off run and the roll-out as well as time at rest.
Where a glitch split the flight, the kept piece can start in the air, and the
ground rows are then only after landing (`f167`, `f174`, `f175`, `f176`).
`f169`'s kept piece ends in the air, so its ground rows are only the take-off.
A 95th percentile of 1–3° marks a flight whose ground rows are mostly at
rest; 8–29° marks a take-off run in them.

Each cell is median / 95th percentile of the absolute error, degrees.

| Flight | Min | Ground total | Ground tilt | Ground heading | Flight total | Flight tilt | Flight heading |
|---|---|---|---|---|---|---|---|
| f166 | 13.4 | 3.0 / 21 | 2.0 / 11 | 1.7 / 18 | 13.3 / 150 | 7.6 / 89 | 7.9 / 130 |
| f167 | 10.5 | 0.8 / 2 | 0.5 / 1 | 0.5 / 1 | 22.8 / 145 | 12.3 / 103 | 16.4 / 114 |
| f169 | 4.4 | 1.0 / 29 | 0.7 / 16 | 0.8 / 24 | 46.7 / 160 | 26.2 / 109 | 28.4 / 148 |
| f170 | 7.7 | 1.2 / 18 | 0.8 / 9 | 0.8 / 16 | 28.5 / 156 | 15.6 / 100 | 17.3 / 139 |
| f171 | 8.6 | 5.4 / 19 | 2.9 / 10 | 3.5 / 17 | 15.4 / 135 | 8.2 / 106 | 9.7 / 89 |
| f172 | 9.3 | 3.4 / 20 | 2.1 / 9 | 2.0 / 18 | 20.0 / 142 | 10.9 / 122 | 14.2 / 86 |
| f173 | 10.4 | 2.4 / 23 | 1.0 / 10 | 1.6 / 20 | 28.4 / 149 | 16.1 / 129 | 16.9 / 110 |
| f174 | 8.2 | 0.9 / 2 | 0.4 / 2 | 0.7 / 2 | 27.8 / 152 | 15.4 / 123 | 18.9 / 118 |
| f175 | 8.2 | 0.8 / 3 | 0.4 / 1 | 0.7 / 3 | 25.0 / 144 | 14.6 / 128 | 14.3 / 96 |
| f176 | 3.1 | 1.3 / 2 | 0.5 / 1 | 1.2 / 2 | 56.8 / 152 | 28.8 / 129 | 29.3 / 139 |
| f177 | 10.7 | 1.3 / 15 | 0.7 / 8 | 1.0 / 13 | 11.6 / 141 | 6.5 / 114 | 5.8 / 88 |
| f178 | 7.5 | 1.3 / 16 | 0.6 / 9 | 1.1 / 14 | 48.1 / 155 | 27.3 / 124 | 29.9 / 127 |
| f179 | 10.3 | 0.9 / 3 | 0.5 / 2 | 0.7 / 2 | 22.3 / 145 | 12.4 / 122 | 14.4 / 98 |
| f180 | 5.7 | 0.8 / 9 | 0.5 / 4 | 0.5 / 8 | 54.2 / 157 | 30.8 / 138 | 36.0 / 122 |
| f196 | 9.7 | 1.2 / 2 | 0.6 / 1 | 1.1 / 2 | 28.6 / 141 | 15.2 / 117 | 19.1 / 103 |

Across flights, the median of the per-flight medians:

| | Total | Tilt | Heading |
|---|---|---|---|
| Ground | 1.2° | 0.6° | 1.0° |
| Flight | 27.8° | 15.2° | 16.9° |

**Interpretation.**

- **At rest** the compass is good to about half a degree of tilt and a degree
  of heading. That is the accelerometer turn-on bias: ±12 mg tilts gravity by
  up to 0.7°, and the tilt leaks into heading at this site's 66° dip.
- **In flight** it is not an attitude estimate. A single-sample compass takes
  the superframe's mean specific force as gravity, and on a pattern aircraft
  that is wrong by whole g's in every manoeuvre. The 95th percentiles of
  100–150° are the loops and rolls.
- Every estimate row was decoded (no undecoded rows on any flight), so none
  of this is the algorithm's quaternion convention (see
  [simulator.md](simulator.md#scoring)).

The post-processing filter's target is the flight columns.

## 2026-10-21: the navigation filter (`tagsim filter`) on the same 15 logs

**Conditions.**

- The same logs as the block above, not regenerated: tagsim 0.2.0, seed 0,
  100 Hz, ±4 g, ±1000 dps, `RFU`, phase-2 datasheet errors, true calibration
  constants, WMM2020 field, constant sea-level pressure (`--sea-level-walk 0`,
  the default).
- `tagsim batch --reuse --filter` on branch `claude/interesting-dijkstra-423308`
  (base `9068bcba`): the filter at its default `FilterConfig`, with and
  without the pressure channel; forward and smoothed tables scored at every
  IMU sample (the compass is scored per superframe). The filter is given the
  field the log was generated with.
- Design and tuning: [navigation-filter.md](navigation-filter.md).
- Altitude is scored as a change from the start, the datums matched over the
  estimate's first second; vertical velocity directly. Both against
  `TruthPose`.

**Caveat before reading the numbers.** The simulated gyro has only white
noise and a constant turn-on bias (no scale-factor, misalignment,
g-sensitivity or bias instability), and the magnetometer is calibrated with
the true constants and sees no disturbance. Those are what limit a real
filter in a pattern flight, so these figures say the filter is built right
and handles the rates and timing, not what a real tag will achieve.

Across flights, the median of the per-flight medians / of the per-flight
95th percentiles (degrees, m, m/s):

| Estimate | Phase | Total | Tilt | Heading | Altitude (m) | Vz (m/s) |
|---|---|---|---|---|---|---|
| compass | ground | 1.22 / 14.66 | 0.63 / 8.32 | 1.00 / 13.44 | – | – |
| compass | flight | 27.77 / 149.19 | 15.18 / 122.03 | 16.88 / 114.21 | – | – |
| forward | ground | 0.68 / 1.06 | 0.30 / 0.49 | 0.60 / 0.93 | 0.03 / 0.07 | 0.03 / 0.10 |
| forward | flight | 0.17 / 0.59 | 0.08 / 0.32 | 0.14 / 0.49 | 0.03 / 0.07 | 0.03 / 0.11 |
| smoothed | ground | 0.48 / 0.59 | 0.22 / 0.25 | 0.44 / 0.55 | 0.02 / 0.04 | 0.01 / 0.05 |
| smoothed | flight | 0.23 / 0.59 | 0.10 / 0.29 | 0.21 / 0.55 | 0.02 / 0.04 | 0.01 / 0.04 |
| forward-nopressure | ground | 0.75 / 1.45 | 0.32 / 0.62 | 0.64 / 1.30 | – | – |
| forward-nopressure | flight | 0.27 / 0.93 | 0.15 / 0.40 | 0.22 / 0.87 | – | – |
| smoothed-nopressure | ground | 0.49 / 0.69 | 0.24 / 0.28 | 0.44 / 0.63 | – | – |
| smoothed-nopressure | flight | 0.34 / 0.66 | 0.14 / 0.32 | 0.30 / 0.60 | – | – |

Smoothed, per flight, median / 95th percentile:

| Flight | Ground total | Ground tilt | Ground heading | Flight total | Flight tilt | Flight heading | Flight altitude (m) | Flight vz (m/s) |
|---|---|---|---|---|---|---|---|---|
| f166 | 0.48 / 0.49 | 0.20 / 0.21 | 0.44 / 0.45 | 0.38 / 0.50 | 0.17 / 0.29 | 0.34 / 0.43 | 0.03 / 0.04 | 0.01 / 0.04 |
| f167 | 0.14 / 0.14 | 0.12 / 0.13 | 0.06 / 0.06 | 0.21 / 0.27 | 0.10 / 0.21 | 0.15 / 0.20 | 0.02 / 0.04 | 0.01 / 0.04 |
| f169 | 0.32 / 0.35 | 0.22 / 0.26 | 0.23 / 0.24 | 0.13 / 0.26 | 0.07 / 0.13 | 0.10 / 0.23 | 0.02 / 0.04 | 0.01 / 0.05 |
| f170 | 0.45 / 0.46 | 0.23 / 0.23 | 0.39 / 0.40 | 0.14 / 0.41 | 0.07 / 0.26 | 0.12 / 0.35 | 0.02 / 0.04 | 0.01 / 0.04 |
| f171 | 0.39 / 0.98 | 0.21 / 0.31 | 0.33 / 0.94 | 0.30 / 0.94 | 0.13 / 0.39 | 0.28 / 0.85 | 0.02 / 0.04 | 0.01 / 0.04 |
| f172 | 0.28 / 0.50 | 0.20 / 0.21 | 0.19 / 0.48 | 0.21 / 0.48 | 0.09 / 0.24 | 0.18 / 0.42 | 0.03 / 0.04 | 0.01 / 0.04 |
| f173 | 0.65 / 0.66 | 0.27 / 0.28 | 0.59 / 0.60 | 0.23 / 0.64 | 0.09 / 0.32 | 0.19 / 0.56 | 0.02 / 0.04 | 0.01 / 0.04 |
| f174 | 0.56 / 0.59 | 0.22 / 0.22 | 0.52 / 0.55 | 0.27 / 0.92 | 0.10 / 0.39 | 0.24 / 0.81 | 0.02 / 0.04 | 0.01 / 0.05 |
| f175 | 0.59 / 0.62 | 0.24 / 0.25 | 0.54 / 0.57 | 0.27 / 0.73 | 0.13 / 0.32 | 0.23 / 0.65 | 0.02 / 0.04 | 0.01 / 0.05 |
| f176 | 0.73 / 0.77 | 0.31 / 0.34 | 0.66 / 0.69 | 0.34 / 0.59 | 0.14 / 0.23 | 0.31 / 0.55 | 0.04 / 0.06 | 0.01 / 0.05 |
| f177 | 0.51 / 0.53 | 0.23 / 0.24 | 0.45 / 0.47 | 0.22 / 0.53 | 0.11 / 0.32 | 0.19 / 0.44 | 0.01 / 0.03 | 0.01 / 0.04 |
| f178 | 0.27 / 0.84 | 0.17 / 0.36 | 0.21 / 0.77 | 0.28 / 0.70 | 0.12 / 0.29 | 0.25 / 0.64 | 0.02 / 0.04 | 0.01 / 0.04 |
| f179 | 0.70 / 0.73 | 0.29 / 0.30 | 0.64 / 0.67 | 0.14 / 0.69 | 0.07 / 0.31 | 0.12 / 0.63 | 0.02 / 0.04 | 0.01 / 0.04 |
| f180 | 0.17 / 0.38 | 0.12 / 0.20 | 0.13 / 0.31 | 0.23 / 0.31 | 0.10 / 0.15 | 0.21 / 0.28 | 0.02 / 0.04 | 0.01 / 0.05 |
| f196 | 1.17 / 1.21 | 0.49 / 0.51 | 1.06 / 1.10 | 0.35 / 0.99 | 0.15 / 0.43 | 0.32 / 0.89 | 0.02 / 0.04 | 0.01 / 0.04 |

Forward only, per flight, median / 95th percentile:

| Flight | Ground total | Ground tilt | Ground heading | Flight total | Flight tilt | Flight heading | Flight altitude (m) | Flight vz (m/s) |
|---|---|---|---|---|---|---|---|---|
| f166 | 0.45 / 1.13 | 0.07 / 0.49 | 0.36 / 1.01 | 0.30 / 0.49 | 0.17 / 0.31 | 0.25 / 0.41 | 0.03 / 0.06 | 0.03 / 0.10 |
| f167 | 0.14 / 0.27 | 0.12 / 0.13 | 0.10 / 0.24 | 0.30 / 0.49 | 0.14 / 0.32 | 0.23 / 0.45 | 0.03 / 0.07 | 0.03 / 0.10 |
| f169 | 0.85 / 1.06 | 0.44 / 0.51 | 0.73 / 0.93 | 0.17 / 0.37 | 0.10 / 0.18 | 0.12 / 0.35 | 0.03 / 0.07 | 0.03 / 0.15 |
| f170 | 1.51 / 2.23 | 0.56 / 1.02 | 1.39 / 1.98 | 0.21 / 1.20 | 0.09 / 0.40 | 0.14 / 1.13 | 0.03 / 0.06 | 0.03 / 0.10 |
| f171 | 1.21 / 1.51 | 0.36 / 0.69 | 1.08 / 1.40 | 0.14 / 1.09 | 0.09 / 0.44 | 0.10 / 1.02 | 0.03 / 0.07 | 0.03 / 0.10 |
| f172 | 1.06 / 1.82 | 0.44 / 0.71 | 0.96 / 1.72 | 0.17 / 0.64 | 0.07 / 0.32 | 0.14 / 0.56 | 0.02 / 0.06 | 0.03 / 0.11 |
| f173 | 0.68 / 1.90 | 0.20 / 0.75 | 0.61 / 1.75 | 0.16 / 0.53 | 0.07 / 0.35 | 0.15 / 0.42 | 0.03 / 0.07 | 0.03 / 0.12 |
| f174 | 0.53 / 0.59 | 0.20 / 0.22 | 0.50 / 0.55 | 0.24 / 0.98 | 0.08 / 0.34 | 0.20 / 0.92 | 0.03 / 0.07 | 0.03 / 0.12 |
| f175 | 0.55 / 0.62 | 0.21 / 0.24 | 0.51 / 0.57 | 0.15 / 0.99 | 0.08 / 0.40 | 0.12 / 0.91 | 0.04 / 0.07 | 0.04 / 0.13 |
| f176 | 0.68 / 0.76 | 0.30 / 0.34 | 0.60 / 0.68 | 0.18 / 0.48 | 0.08 / 0.21 | 0.15 / 0.42 | 0.17 / 0.21 | 0.04 / 0.12 |
| f177 | 0.33 / 0.86 | 0.14 / 0.37 | 0.32 / 0.78 | 0.31 / 0.49 | 0.17 / 0.34 | 0.23 / 0.40 | 0.04 / 0.07 | 0.03 / 0.10 |
| f178 | 0.68 / 0.82 | 0.33 / 0.41 | 0.58 / 0.74 | 0.16 / 0.59 | 0.08 / 0.30 | 0.12 / 0.49 | 0.02 / 0.06 | 0.03 / 0.11 |
| f179 | 1.03 / 2.30 | 0.32 / 1.07 | 0.64 / 2.02 | 0.14 / 0.81 | 0.07 / 0.30 | 0.12 / 0.75 | 0.03 / 0.07 | 0.03 / 0.11 |
| f180 | 0.46 / 0.71 | 0.21 / 0.41 | 0.40 / 0.61 | 0.09 / 0.37 | 0.06 / 0.27 | 0.06 / 0.28 | 0.03 / 0.06 | 0.04 / 0.14 |
| f196 | 1.15 / 1.45 | 0.33 / 0.62 | 1.04 / 1.30 | 0.17 / 1.29 | 0.07 / 0.53 | 0.15 / 1.19 | 0.03 / 0.07 | 0.03 / 0.11 |

**Without the mean magnetometer-age correction** (`--set mag_age_s=0`, with
pressure), median of medians: forward 0.68° ground, 0.35° flight (tilt 0.14°,
heading 0.29°); smoothed 0.51° ground, 0.27° flight (tilt 0.14°, heading
0.23°). Subtracting the mean age halves the forward in-flight error.

**Interpretation.**

- **The filter is not worse than the compass anywhere**: in flight, 0.2°
  against 27.8° (median of medians). The smoothed 95th percentile is under 1°
  on every flight (the forward pass's reaches 1.3°), against the compass's
  135–160°. The compass's flight error is the
  aircraft's acceleration taken as gravity; the filter carries attitude
  through the manoeuvres on the gyro and corrects it with the full
  magnetometer vector, which acceleration does not disturb.
- **On the ground** both are limited by the accelerometer's turn-on bias
  (about 0.2–0.5° of tilt, leaking into heading at 66° dip), and the
  smoothed filter is about 0.5° in total against the compass's 1.2°.
- **Smoothing helps on the ground and at the start, not in flight.** The
  smoother removes the forward pass's start-up transient (ground 0.68° to
  0.48°, 95th percentile 1.06° to 0.59°), but its in-flight median is
  slightly worse than the forward pass's (0.23° against 0.17°), with the same
  95th percentile. With no unmodelled gyro error, the forward pass is already
  near its floor in flight, and the smoother spreads the accelerometer-bias
  errors of the ground segments into the flight. The synthetic manoeuvre in
  `test_navfilter.py`, which has no such ground segments, smooths 0.41° to
  0.41° median and 0.46° to 0.42° at the 95th percentile.
- **Pressure** gives altitude changes to 2 cm and vertical velocity to
  1 cm/s (smoothed medians) throughout, with no drift, since sea-level
  pressure is constant here. Through the vertical channel it also improves
  tilt in flight: forward 0.15° to 0.08°, smoothed 0.14° to 0.10° (medians),
  95th percentiles 0.40° to 0.32° forward.
- **Records that start in the air** (`f167`, `f174`, `f175`, `f176`) are
  bootstrapped from an attitude-only smoothing pass and score like the rest.

## 2026-10-22: a harder simulation, and accelerometer bias states

**Conditions.**

- The same 15 logs, **regenerated** with the harder sensor model (branch
  `claude/dreamy-bardeen-82c025`, base `15b4f87f`): the phase-2 errors plus
  gyro scale error ±0.3 % (the LSM6DSV's `G_So%`), accelerometer scale error
  ±1 % (assumed), ±0.5° cross-axis misalignment on both (assumed), and
  Gauss-Markov bias instability of 0.003 dps and 0.05 mg RMS with a 100 s
  correlation time (assumed). No vibration, no temperature effects. Seed 0,
  100 Hz, ±4 g, ±1000 dps, `RFU`, WMM2020 field, constant sea-level pressure.
- Two conditions: the true calibration constants in the log
  (`tagsim batch --filter`), and `--calibration perturbed` (hard iron off by
  up to ±2 µT per axis, soft iron by up to ±1 % per element). The filter is
  not told which.
- Each condition scored twice: the filter as it was (no accelerometer bias
  states, `--filter-set accel_bias_states=false`) and with the accelerometer
  bias states added below, now the default.
- Outputs in `~/Documents/tagsim-output/harder-{true,perturbed}{,-ab}/`
  (outside the repository); the ablation in `.../ablation/`, the smoother
  investigation in `.../smoother/`.

Median of the per-flight medians / of the per-flight 95th percentiles
(degrees, m, m/s):

| Calibration | Filter | Estimate | Phase | Total | Tilt | Heading | Altitude (m) | Vz (m/s) |
|---|---|---|---|---|---|---|---|---|
| true | | compass | ground | 1.46 / 15.19 | 0.74 / 8.44 | 1.24 / 14.00 | – | – |
| true | | compass | flight | 27.67 / 149.14 | 15.14 / 121.82 | 16.77 / 114.15 | – | – |
| true | no accel bias | forward | ground | 1.00 / 1.43 | 0.41 / 0.68 | 0.46 / 1.32 | 0.03 / 0.07 | 0.03 / 0.10 |
| true | no accel bias | forward | flight | 1.13 / 2.65 | 0.58 / 1.66 | 0.84 / 2.15 | 0.03 / 0.07 | 0.04 / 0.14 |
| true | no accel bias | smoothed | ground | 0.69 / 1.21 | 0.46 / 0.60 | 0.45 / 1.12 | 0.02 / 0.04 | 0.01 / 0.05 |
| true | no accel bias | smoothed | flight | 0.85 / 1.72 | 0.47 / 1.18 | 0.53 / 1.46 | 0.02 / 0.04 | 0.01 / 0.04 |
| true | accel bias | forward | ground | 0.76 / 1.22 | 0.50 / 0.56 | 0.66 / 1.11 | 0.03 / 0.07 | 0.03 / 0.10 |
| true | accel bias | forward | flight | 0.95 / 2.53 | 0.58 / 1.59 | 0.61 / 2.04 | 0.03 / 0.07 | 0.04 / 0.14 |
| true | accel bias | smoothed | ground | 0.88 / 0.98 | 0.37 / 0.52 | 0.83 / 0.90 | 0.02 / 0.04 | 0.01 / 0.05 |
| true | accel bias | smoothed | flight | 0.79 / 1.61 | 0.44 / 1.17 | 0.50 / 1.36 | 0.02 / 0.04 | 0.01 / 0.04 |
| true | accel bias | smoothed, no pressure | flight | 0.95 / 2.06 | 0.54 / 1.33 | 0.75 / 1.93 | – | – |
| perturbed | | compass | ground | 4.49 / 13.58 | 0.74 / 8.44 | 4.42 / 12.50 | – | – |
| perturbed | | compass | flight | 27.19 / 149.95 | 15.14 / 121.82 | 17.22 / 114.18 | – | – |
| perturbed | no accel bias | forward | flight | 2.85 / 4.37 | 0.83 / 2.10 | 2.51 / 4.14 | 0.03 / 0.07 | 0.04 / 0.16 |
| perturbed | no accel bias | smoothed | ground | 2.37 / 2.88 | 0.62 / 0.99 | 2.25 / 2.81 | 0.02 / 0.04 | 0.01 / 0.05 |
| perturbed | no accel bias | smoothed | flight | 2.06 / 3.19 | 0.58 / 1.44 | 1.96 / 3.04 | 0.02 / 0.04 | 0.01 / 0.05 |
| perturbed | accel bias | forward | flight | 2.35 / 4.06 | 1.17 / 1.94 | 1.85 / 3.77 | 0.03 / 0.07 | 0.04 / 0.17 |
| perturbed | accel bias | smoothed | ground | 1.79 / 2.17 | 0.82 / 1.23 | 1.35 / 1.76 | 0.02 / 0.04 | 0.01 / 0.05 |
| perturbed | accel bias | smoothed | flight | 1.44 / 2.32 | 0.86 / 1.42 | 1.00 / 2.17 | 0.02 / 0.04 | 0.01 / 0.04 |
| perturbed | accel bias | smoothed, no pressure | flight | 1.40 / 2.24 | 0.98 / 1.50 | 0.85 / 1.91 | – | – |

**One error at a time** (4 flights: f166, f173, f177, f180; the earlier
filter, without accelerometer bias states; each row is the phase-2 errors plus
only the named term). Flight, median of medians / of 95th percentiles:

| Added to phase 2 | Forward total | Smoothed total | Smoothed tilt | Smoothed heading |
|---|---|---|---|---|
| nothing | 0.23 / 0.49 | 0.23 / 0.51 | 0.11 / 0.30 | 0.20 / 0.44 |
| gyro misalignment ±0.5° | 1.07 / 2.62 | 0.97 / 1.77 | 0.40 / 0.98 | 0.78 / 1.67 |
| gyro scale ±0.3 % | 0.78 / 1.99 | 0.75 / 1.50 | 0.37 / 0.83 | 0.58 / 1.40 |
| accel scale ±1 % | 0.21 / 0.41 | 0.19 / 0.38 | 0.10 / 0.28 | 0.16 / 0.30 |
| accel misalignment ±0.5° | 0.24 / 0.45 | 0.20 / 0.31 | 0.13 / 0.30 | 0.07 / 0.18 |
| bias instability (both) | 0.25 / 0.55 | 0.29 / 0.53 | 0.12 / 0.31 | 0.23 / 0.47 |
| all of them | 1.16 / 2.60 | 0.82 / 1.67 | 0.45 / 1.27 | 0.53 / 1.34 |

**The smoother-worse-than-forward anomaly** (5 flights where it was largest:
f166, f173, f176, f180, f196; phase-2 errors only, the earlier model). Median
of medians / of 95th percentiles:

| Simulation | Filter | Forward flight | Smoothed flight | Forward ground | Smoothed ground |
|---|---|---|---|---|---|
| phase 2 | no accel bias | 0.17 / 0.49 | 0.34 / 0.59 | 0.68 / 1.13 | 0.65 / 0.66 |
| phase 2, `accel_bias_mg=0` | no accel bias | 0.09 / 0.30 | 0.04 / 0.08 | 0.10 / 0.43 | 0.06 / 0.08 |
| phase 2 | accel bias | 0.16 / 0.53 | 0.07 / 0.12 | 0.14 / 0.67 | 0.09 / 0.10 |

Smoothed, accelerometer bias states, true calibration, per flight, median /
95th percentile:

| Flight | Ground total | Ground tilt | Ground heading | Flight total | Flight tilt | Flight heading | Flight altitude (m) | Flight vz (m/s) |
|---|---|---|---|---|---|---|---|---|
| f166 | 0.27 / 0.37 | 0.20 / 0.23 | 0.27 / 0.32 | 0.47 / 1.27 | 0.29 / 1.11 | 0.29 / 0.75 | 0.03 / 0.04 | 0.01 / 0.04 |
| f167 | 1.12 / 1.19 | 0.37 / 0.38 | 1.06 / 1.13 | 0.71 / 1.61 | 0.41 / 1.16 | 0.43 / 1.36 | 0.02 / 0.04 | 0.01 / 0.04 |
| f169 | 0.47 / 0.68 | 0.31 / 0.41 | 0.35 / 0.53 | 0.53 / 1.04 | 0.32 / 0.73 | 0.32 / 0.92 | 0.02 / 0.04 | 0.01 / 0.05 |
| f170 | 1.15 / 1.82 | 0.38 / 0.73 | 0.98 / 1.78 | 0.79 / 1.88 | 0.42 / 1.07 | 0.66 / 1.80 | 0.02 / 0.04 | 0.01 / 0.04 |
| f171 | 0.95 / 1.81 | 0.33 / 0.70 | 0.94 / 1.68 | 0.59 / 1.25 | 0.32 / 1.03 | 0.32 / 0.90 | 0.02 / 0.04 | 0.01 / 0.04 |
| f172 | 0.65 / 0.98 | 0.52 / 0.88 | 0.51 / 0.61 | 0.56 / 1.37 | 0.41 / 1.32 | 0.21 / 0.81 | 0.03 / 0.04 | 0.01 / 0.04 |
| f173 | 0.63 / 0.85 | 0.39 / 0.75 | 0.42 / 0.59 | 0.79 / 1.43 | 0.53 / 1.22 | 0.43 / 1.01 | 0.02 / 0.04 | 0.01 / 0.05 |
| f174 | 0.34 / 0.38 | 0.25 / 0.29 | 0.23 / 0.24 | 0.66 / 1.58 | 0.45 / 1.33 | 0.32 / 1.11 | 0.02 / 0.04 | 0.01 / 0.05 |
| f175 | 0.88 / 0.93 | 0.31 / 0.38 | 0.83 / 0.90 | 1.14 / 1.91 | 0.55 / 1.54 | 0.71 / 1.72 | 0.02 / 0.04 | 0.01 / 0.05 |
| f176 | 1.91 / 2.13 | 0.84 / 1.05 | 1.72 / 1.85 | 0.92 / 1.67 | 0.53 / 0.97 | 0.61 / 1.54 | 0.04 / 0.06 | 0.01 / 0.04 |
| f177 | 0.51 / 0.73 | 0.22 / 0.42 | 0.30 / 0.69 | 0.86 / 1.74 | 0.43 / 1.36 | 0.54 / 1.50 | 0.01 / 0.03 | 0.01 / 0.04 |
| f178 | 1.52 / 1.72 | 0.52 / 0.59 | 1.42 / 1.62 | 1.05 / 1.99 | 0.64 / 1.22 | 0.71 / 1.76 | 0.02 / 0.04 | 0.01 / 0.04 |
| f179 | 0.72 / 0.79 | 0.30 / 0.33 | 0.66 / 0.72 | 0.82 / 1.69 | 0.44 / 1.15 | 0.63 / 1.50 | 0.02 / 0.04 | 0.01 / 0.04 |
| f180 | 1.56 / 1.65 | 0.63 / 0.91 | 1.38 / 1.42 | 0.89 / 1.48 | 0.59 / 1.17 | 0.50 / 1.32 | 0.02 / 0.04 | 0.01 / 0.05 |
| f196 | 1.52 / 1.66 | 0.44 / 0.52 | 1.45 / 1.60 | 1.05 / 1.99 | 0.48 / 1.25 | 0.75 / 1.92 | 0.02 / 0.04 | 0.01 / 0.04 |

**Interpretation.**

- **The filter degrades by about a factor of four in flight**, from 0.23° to
  0.85° smoothed (median of medians; 95th percentile 0.59° to 1.72°), and
  0.79° with the accelerometer bias states. It is still some 35 times better
  than the compass (27.7°), whose flight error is unchanged: it is dominated
  by the aircraft's acceleration, not by sensor error.
- **The gyro dominates, and its misalignment most**: ±0.5° of cross-axis
  coupling alone takes the four-flight smoothed median from 0.23° to 0.97°,
  gyro scale error alone to 0.75°. A misaligned or mis-scaled gyro is wrong
  in proportion to the rate, so it bites in the rolls and loops, which reach
  hundreds of dps; neither is a bias, so the bias states cannot absorb it,
  and the magnetometer has to pull the attitude back after each manoeuvre.
  The accelerometer's scale and misalignment errors and the assumed bias
  instability change the scores by no more than the flight-to-flight
  scatter. The gyro misalignment figure is an assumption (the LSM6DSV sheet
  gives none), so it is the first thing a bench calibration should measure.
  Estimating the gyro's scale and misalignment in the filter is the obvious
  next step.
- **A wrong magnetometer calibration costs more than all the inertial errors
  together**: smoothed flight 1.44° (with the accelerometer bias states),
  mostly in heading (1.00°), and 1.8° on the ground, where the compass's
  heading error goes from 1.2° to 4.4°. A ±2 µT hard-iron error is 4 % of the
  52 µT field, a couple of degrees of direction.
- **The smoother anomaly was the accelerometer bias.** With the phase-2
  model and no accelerometer bias (`accel_bias_mg=0`), the smoother beats
  the forward pass in flight, 0.04° against 0.09°; with the bias it was worse,
  0.34° against 0.17°. Not knowing the bias, the filter takes the tilt it
  causes for attitude on the ground, where the accelerometer is trusted most;
  the smoother carries that wrong tilt back from the landing into the
  flight. **With accelerometer bias states** the phase-2 smoother is again
  better than forward in flight, 0.07° against 0.16° (95th percentile 0.12°
  against 0.53°), and on the ground, 0.09° against 0.14°.
- **On the harder model the bias states help in flight but not everywhere.**
  Smoothed flight 0.85° to 0.79° (true calibration) and 2.06° to 1.44°
  (perturbed), forward flight 1.13° to 0.95°; but the true-calibration
  ground median gets worse, 0.69° to 0.88°, all of it heading. With scale and
  misalignment errors the accelerometer's apparent bias depends on attitude,
  so the bias the states learn in flight is not the one at rest. Flight is
  what the filter is for, so the states are on by default.
- **Altitude and vertical velocity are unaffected**: 2 cm and 1 cm/s
  smoothed medians in every condition.

## 2026-10-22: gyro calibration, magnetometer offset and clock states

**Conditions.**

- The filter at commit `659088e0` (branch `claude/kind-antonelli-878bee`),
  with three additions, all on by default
  ([navigation-filter.md](navigation-filter.md)):
  - **gyro calibration**: nine states for the gyro's scale and
    misalignment (priors 0.3 % and 0.5°);
  - **magnetometer offset**: three states for a residual hard iron left by
    the log's calibration (prior 2 µT). This was not in the plan: it was
    added because, without it, the gyro calibration states made the
    perturbed-calibration flights worse (below);
  - **magnetometer clock**: each reading applied at its own conversion
    time, from the BMM350's phase and rate fitted to a first pass, in place
    of the mean-age correction.
- The same 15 logs, **not regenerated**, in the three conditions of the
  earlier blocks: the phase-2 ("simple") errors of the 2026-10-21 block
  (`~/Documents/tagsim-output/`); the harder errors with the true
  calibration; the harder errors with `--calibration perturbed`
  (`.../harder-{true,perturbed}/`). All seed 0, so every flight in a
  condition shares one error draw.
- `tagsim batch --reuse --filter`, with all three additions and with each
  turned off in turn (`--filter-set gyro_cal_states=false`,
  `mag_clock_estimate=false`, `mag_bias_states=false`). Outputs in
  `~/Documents/tagsim-output/final/<condition>/<variant>/`.

Median of the per-flight medians / of the per-flight 95th percentiles,
degrees. Altitude and vertical velocity are 0.02 m and 0.01 m/s smoothed
(0.03 m, 0.03 m/s forward) in every row and are left out.

| Errors, calibration | States | Estimate | Ground total | Flight total | Flight tilt | Flight heading |
|---|---|---|---|---|---|---|
| simple | all | forward | 0.39 / 0.88 | 0.17 / 0.69 | 0.07 / 0.31 | 0.15 / 0.63 |
| simple | all | smoothed | 0.08 / 0.11 | 0.08 / 0.17 | 0.04 / 0.07 | 0.07 / 0.15 |
| simple | all | smoothed, no pressure | – | 0.51 / 0.55 | 0.21 / 0.23 | 0.47 / 0.50 |
| simple | no gyro cal | forward | 0.41 / 0.88 | 0.14 / 0.64 | 0.06 / 0.25 | 0.13 / 0.60 |
| simple | no gyro cal | smoothed | 0.09 / 0.10 | 0.07 / 0.12 | 0.03 / 0.06 | 0.06 / 0.11 |
| simple | no mag clock | forward | 0.31 / 0.88 | 0.18 / 0.68 | 0.09 / 0.36 | 0.16 / 0.61 |
| simple | no mag clock | smoothed | 0.11 / 0.15 | 0.08 / 0.19 | 0.04 / 0.08 | 0.06 / 0.17 |
| simple | no mag offset | forward | 0.27 / 0.84 | 0.17 / 0.65 | 0.07 / 0.25 | 0.15 / 0.59 |
| simple | no mag offset | smoothed | 0.09 / 0.10 | 0.08 / 0.17 | 0.04 / 0.08 | 0.06 / 0.15 |
| harder, true | all | forward | 0.39 / 0.91 | 0.21 / 0.65 | 0.09 / 0.30 | 0.18 / 0.60 |
| harder, true | all | smoothed | 0.14 / 0.21 | 0.12 / 0.21 | 0.05 / 0.11 | 0.10 / 0.19 |
| harder, true | all | smoothed, no pressure | – | 0.54 / 0.64 | 0.21 / 0.26 | 0.49 / 0.58 |
| harder, true | no gyro cal | forward | 0.90 / 1.20 | 1.04 / 2.36 | 0.54 / 1.43 | 0.77 / 1.94 |
| harder, true | no gyro cal | smoothed | 0.79 / 0.95 | 0.73 / 1.62 | 0.43 / 1.06 | 0.49 / 1.44 |
| harder, true | no mag clock | forward | 0.38 / 0.91 | 0.23 / 0.70 | 0.11 / 0.34 | 0.20 / 0.63 |
| harder, true | no mag clock | smoothed | 0.17 / 0.23 | 0.12 / 0.25 | 0.06 / 0.12 | 0.09 / 0.24 |
| harder, true | no mag offset | forward | 0.32 / 0.92 | 0.19 / 0.61 | 0.08 / 0.26 | 0.17 / 0.57 |
| harder, true | no mag offset | smoothed | 0.13 / 0.21 | 0.12 / 0.21 | 0.05 / 0.11 | 0.10 / 0.20 |
| harder, perturbed | all | forward | 1.03 / 1.47 | 0.33 / 0.89 | 0.22 / 0.45 | 0.22 / 0.82 |
| harder, perturbed | all | smoothed | 0.34 / 0.41 | 0.27 / 0.41 | 0.18 / 0.29 | 0.15 / 0.38 |
| harder, perturbed | all | smoothed, no pressure | – | 0.50 / 0.83 | 0.23 / 0.42 | 0.43 / 0.77 |
| harder, perturbed | no gyro cal | forward | 1.47 / 1.82 | 0.99 / 2.31 | 0.56 / 1.46 | 0.74 / 1.90 |
| harder, perturbed | no gyro cal | smoothed | 0.91 / 1.08 | 0.78 / 1.60 | 0.42 / 1.07 | 0.49 / 1.39 |
| harder, perturbed | no mag clock | forward | 1.04 / 1.47 | 0.34 / 0.98 | 0.23 / 0.46 | 0.24 / 0.89 |
| harder, perturbed | no mag clock | smoothed | 0.34 / 0.41 | 0.28 / 0.40 | 0.19 / 0.29 | 0.15 / 0.36 |
| harder, perturbed | no mag offset | forward | 2.44 / 2.73 | 2.11 / 3.91 | 1.46 / 2.44 | 1.31 / 3.22 |
| harder, perturbed | no mag offset | smoothed | 1.98 / 2.14 | 1.62 / 2.53 | 1.30 / 1.83 | 0.73 / 2.21 |

The previous filter on the same logs (the 2026-10-22 block, accelerometer
bias states): smoothed flight 0.07 / 0.12 (simple), 0.79 / 1.61 (harder,
true), 1.44 / 2.32 (harder, perturbed).

Smoothed flight total per flight, median / 95th percentile:

| Flight | simple, all | harder true, all | harder true, no gyro cal | harder true, no mag clock | harder perturbed, all | harder perturbed, no mag offset |
|---|---|---|---|---|---|---|
| f166 | 0.05 / 0.17 | 0.17 / 0.28 | 0.47 / 1.13 | 0.18 / 0.29 | 0.26 / 0.40 | 1.87 / 2.65 |
| f167 | 0.30 / 0.34 | 0.33 / 0.47 | 0.70 / 1.62 | 0.34 / 0.47 | 0.41 / 0.57 | 1.97 / 3.08 |
| f169 | 0.07 / 0.10 | 0.06 / 0.09 | 0.50 / 0.97 | 0.06 / 0.10 | 0.27 / 0.73 | 2.17 / 3.47 |
| f170 | 0.10 / 0.20 | 0.07 / 0.23 | 0.70 / 1.90 | 0.10 / 0.25 | 0.19 / 0.57 | 1.95 / 3.33 |
| f171 | 0.05 / 0.09 | 0.06 / 0.10 | 0.56 / 1.10 | 0.07 / 0.11 | 0.25 / 0.33 | 1.81 / 2.64 |
| f172 | 0.08 / 0.12 | 0.12 / 0.21 | 0.59 / 1.24 | 0.12 / 0.20 | 0.27 / 0.40 | 1.68 / 2.36 |
| f173 | 0.03 / 0.07 | 0.06 / 0.13 | 0.73 / 1.37 | 0.07 / 0.19 | 0.25 / 0.32 | 1.59 / 2.53 |
| f174 | 0.13 / 0.51 | 0.12 / 0.59 | 0.65 / 1.50 | 0.12 / 0.63 | 0.24 / 0.69 | 1.62 / 3.04 |
| f175 | 0.16 / 0.27 | 0.14 / 0.42 | 0.97 / 1.87 | 0.16 / 0.46 | 0.33 / 0.75 | 1.50 / 1.85 |
| f176 | 0.73 / 1.29 | 0.75 / 1.40 | 0.88 / 1.69 | 0.73 / 1.63 | 0.67 / 1.69 | 1.55 / 2.52 |
| f177 | 0.05 / 0.11 | 0.07 / 0.14 | 0.79 / 1.63 | 0.07 / 0.13 | 0.29 / 0.37 | 2.34 / 2.98 |
| f178 | 0.18 / 0.27 | 0.21 / 0.33 | 0.96 / 1.85 | 0.23 / 0.34 | 0.27 / 0.41 | 1.46 / 2.31 |
| f179 | 0.06 / 0.11 | 0.10 / 0.14 | 0.81 / 1.64 | 0.11 / 0.14 | 0.27 / 0.37 | 1.46 / 2.04 |
| f180 | 0.19 / 0.24 | 0.12 / 0.20 | 0.82 / 1.35 | 0.16 / 0.26 | 0.24 / 0.44 | 1.30 / 2.46 |
| f196 | 0.03 / 0.09 | 0.05 / 0.10 | 1.01 / 1.90 | 0.06 / 0.12 | 0.21 / 0.28 | 1.33 / 1.80 |

**The gyro calibration on the harder flights** (all states, smoothed, at
the first sample; the drawn `T - I` has an RMS of 4.05e-3 and a diagonal of
2.08, 1.96, 0.82 e-3 on every flight):

| Flight | `T - I` error RMS / max (1e-3), true cal | estimated diagonal (1e-3), true cal | error RMS (1e-3), perturbed cal |
|---|---|---|---|
| f166 | 0.19 / 0.48 | 2.16 1.90 1.02 | 1.42 |
| f167 | 0.19 / 0.41 | 2.18 1.98 0.41 | 1.14 |
| f169 | 0.22 / 0.43 | 1.83 1.86 0.79 | 1.46 |
| f170 | 0.32 / 0.74 | 2.35 2.11 0.72 | 0.82 |
| f171 | 0.18 / 0.35 | 2.12 1.94 0.52 | 1.48 |
| f172 | 0.15 / 0.30 | 2.17 1.91 1.05 | 1.28 |
| f173 | 0.20 / 0.43 | 2.13 1.83 0.70 | 1.68 |
| f174 | 0.23 / 0.41 | 2.16 1.89 0.51 | 1.59 |
| f175 | 0.24 / 0.47 | 2.06 1.91 0.75 | 1.42 |
| f176 | 0.58 / 1.09 | 1.66 1.88 0.04 | 1.68 |
| f177 | 0.22 / 0.50 | 2.05 1.85 0.89 | 1.91 |
| f178 | 0.22 / 0.45 | 2.31 1.97 0.75 | 1.56 |
| f179 | 0.18 / 0.36 | 2.16 1.98 0.69 | 1.54 |
| f180 | 0.15 / 0.33 | 2.06 1.99 0.89 | 1.28 |
| f196 | 0.19 / 0.32 | 2.08 1.94 0.79 | 1.32 |

With the simple errors (`T = I`) the estimate stays within 0.10–0.45e-3
RMS of zero. Since seed 0 gives every flight the same draw, f173 and f177
were also regenerated with seeds 1–3 (harder errors, true calibration;
`.../final/seeds/`): `T - I` recovered to 0.12–0.26e-3 RMS against
3.9–4.2e-3, smoothed flight 0.08–0.14° against 0.43–1.72° without the
states.

**The magnetometer clock** (all states): the fitted rate error is within
4.4 ppm of the drawn −14 898 ppm on every flight and condition except f176
(7 ppm), and the seed 1–3 runs recover −19 787, +8 629 and −9 029 ppm
(f173 and f177 agree to 2 ppm). Against `TruthMagSamples`, the 99th
percentile of the age error is 0.07–0.68 ms with the simple errors and the
true calibration. With the perturbed calibration it is 0.18–1.6 ms, except
on f171 and f179, where just over 1 % of readings are a whole period off.
The mean-age correction's error is 23.5 ms RMS. 0.02–0.6 % of readings are
a whole period off (0.1–1.4 % with the perturbed calibration). These
readings sit on a conversion boundary, and the filter gives them the age
SD of a whole period.

**Interpretation.**

- **The gyro calibration is what finishes the filter on the harder
  model**: smoothed flight 0.79° to 0.12° with the true calibration, 1.44°
  to 0.27° with the perturbed one, about 230 and 100 times better than the
  compass. Turning it off alone takes them back to 0.73° and 0.78°. The
  states are observable on every flight: the pattern's rolls and loops
  reach hundreds of dps about every axis, and the magnetometer and
  accelerometer see the attitude error that a wrong scale or misalignment
  builds up during them. With no gyro error to find (simple), the states
  cost a little: 0.07° to 0.08°, 95th percentile 0.12° to 0.17°.
- **The gyro states need the magnetometer offset states.** With a
  perturbed calibration and no offset states, the residual hard iron's
  attitude-dependent error looks like a gyro error, and the gyro states
  absorb it: on four flights (f166, f173, f177, f180) the smoothed flight
  median went from 1.06–1.95° without gyro states to 1.36–2.26° with them.
  With the offset states it is 0.27°. The offset is estimated as about
  (1.1, 1.7, −1.0) µT on every perturbed flight. With the true calibration
  the offset states change nothing in flight. The forward pass's ground
  error rises (0.32° to 0.39°) because the offset is not observable at rest
  before the first manoeuvre. The gyro calibration is still 1.4e-3 RMS off
  with the perturbed calibration, which the offset cannot take up: the
  soft-iron error also scales the field.
- **The magnetometer clock helps a little, mostly on the ground and at the
  95th percentile**: harder, true calibration, smoothed ground 0.17° to
  0.14°, flight 95th percentile 0.25° to 0.21°; simple, smoothed ground
  0.11° to 0.08°. The median in flight is unchanged. The mean-age
  correction already removed the bias, and with the mean age the rest of
  the age is modelled as noise: about 23 ms RMS, a few tenths of a µT at
  typical turn rates.
- **No step made flight error worse in all conditions.** The gyro states
  cost 0.01° in flight on the simple errors. The clock is neutral to
  slightly better everywhere.
- **The records that start in the air, f176 and f167, are now the worst**
  (smoothed 0.75° and 0.33° with the true calibration, against 0.05–0.21°
  for the rest). They are bootstrapped, and f176's gyro calibration is the
  least accurate (0.58e-3 RMS).
- **Without pressure** the smoothed flight error is 0.50–0.54° in all three
  conditions, against 0.95° and 1.40° before with the harder errors. With
  the simple errors it is worse than before: 0.51° now, against 0.34° for
  the filter before accelerometer bias states. Every variant here shows the
  same figure, so it comes from the accelerometer bias states (the
  2026-10-22 block), not from these additions. The vertical channel is what
  makes the accelerometer bias observable in flight.
