---
type: results
status: current
summary: Append-only log of tagsim scores for dataprocessing's single-sample IMUTag compass on the recorded pattern flights - the attitude and heading errors a post-processing filter has to beat, with the build, settings and conditions of each run.
---

# Compass Baseline Results

Append-only. One block per scoring run, newest last. How the scores are
computed is in [simulator.md](simulator.md#scoring); how to rerun them is
`tagsim batch` in the [README](../README.md#scoring-and-viewing).

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
