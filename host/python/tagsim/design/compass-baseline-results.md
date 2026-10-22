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
