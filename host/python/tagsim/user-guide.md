---
type: procedure
status: current
summary: A plain-language guide to tagsim for its users - how a recorded flight becomes a simulated IMUTag log with ground truth, how the post-processing filter estimates attitude and altitude from that log, how to run a simulation, filter and score it, and how to run the tests.
---

# tagsim User Guide

tagsim answers one question: **how well can we recover a tag's attitude and
altitude from what an IMUTag actually records?** It works on simulated data.

1. It takes a real aircraft flight, a logged R/C pattern-aerobatics flight,
   whose motion is known.
2. It computes what an IMUTag riding along would have recorded, with
   realistic sensor errors.
3. It writes that out as a genuine IMUTag log, plus a separate truth file.
4. The post-processing filter then estimates attitude and altitude from the
   log alone, and tagsim scores the estimate against the truth.

This guide is for running it. How each part works in detail is in:

- [design/simulator.md](design/simulator.md): the simulator;
- [design/navigation-filter.md](design/navigation-filter.md): the filter;
- [design/compass-baseline-results.md](design/compass-baseline-results.md):
  every score so far;
- [README.md](README.md): every command and option.

tagsim is testing infrastructure, not a shipped tool.

## The simulation path

```text
 ArduPilot flight log (.bin)
        │  1. read the flight
        ▼
 smooth trajectory (truth): attitude, position, velocity
        │  2. what the sensors would feel
        ▼
 ideal signals: specific force, rotation rate, magnetic field, pressure
        │  3. add realistic sensor errors, sample as IMUTag does
        ▼
 IMUTag pages ──► tag-log-import ──► IMUTag SQLite log (.db3)
        │                                  │
        └──► truth file (.truth.db3)       └──► sensorviz, dataprocessing, tagsim filter
```

### 1. Read the flight

The source is an ArduPilot DataFlash log. The first set is 16 pattern flights
by a pattern pilot, kept in the private data repository.

- **What it reads:** the autopilot's own fused estimate of attitude
  (quaternion), position and velocity, 25 times a second.
- **Units:** taken from the log's own unit records.
- **Glitches:** estimator glitches are repaired before use, and every repair
  is recorded. A one-sample spike is bridged. A jump that persists, like an
  estimator reset, splits the flight there.

### 2. Fit a smooth trajectory: this is the truth

tagsim fits smooth curves through the flight: a quaternion spline for
attitude, and a spline through position and velocity together.

- **Why curves:** rates and accelerations then come straight from the curves
  instead of by differencing noisy samples. Nothing breaks when the aircraft
  points straight up or rolls inverted.
- **Truth is the fitted trajectory,** not the autopilot's estimate itself. So
  "truth" is exact for the simulation, even where the autopilot's estimate
  differed from what the aircraft really did.
- **A built-in check:** integrating the ideal signals back up must reproduce
  the trajectory. On a typical flight it agrees to 0.02° of attitude over ten
  minutes.

### 3. Turn the trajectory into what an IMUTag records

**Ideal signals first.** From the trajectory, tagsim computes the signals a
perfect tag would see, mounted on the airframe in a chosen orientation (by
default flat, battery end forward):

- specific force (what an accelerometer measures);
- rotation rate;
- Earth's magnetic field, from the World Magnetic Model at the flying site
  on the flight's date;
- static air pressure for the altitude, from the standard atmosphere. The
  sensor is assumed shielded from the airflow.

**Then the tag's own imperfections,** from the IMUTag sensors' datasheets or,
where a sheet gives no figure, a labelled assumption:

| Sensor | Errors |
| --- | --- |
| Accelerometer and gyro (LSM6DSV16X) | white noise, a turn-on bias, scale and misalignment errors, slow bias wander; optional vibration |
| Magnetometer (BMM350) | hard and soft iron, noise; it runs on its own clock, so each reading is up to one conversion period old |
| Pressure (BMP581) | noise, an offset; sea-level pressure constant by default |

**Then the tag's own way of recording:**

- the configured sample rate and full-scale ranges, with readings clipping
  at full scale;
- magnetometer and pressure read once per ten IMU samples;
- the firmware's axis conventions;
- data packed into the same 2 KB pages the tag writes to flash.

**Output.** The pages go through `tag-log-import`, which uses the same tagcore
code as a real download. So the result is a genuine IMUTag SQLite log that
sensorviz and `dataprocessing` open unchanged. Everything the tag could not
know (true attitude, position, the true field, each magnetometer reading's
true time, the random seed and every setting) goes into the separate truth
file.

**Reproducibility:** a run is exactly reproducible from its seed and options.
`--ideal` turns all sensor errors off. `--calibration perturbed` writes
deliberately wrong calibration constants, as a real field calibration might.

## The filter

`tagsim filter` estimates from the IMUTag log alone, with no truth and no
autopilot data:

- the tag's attitude;
- its gyro biases, and the gyro's own scale and misalignment errors;
- its accelerometer biases;
- any calibration error left in the magnetometer;
- the magnetometer's clock, so each reading can be placed at the moment it
  was actually taken;
- its altitude relative to the start, and its vertical speed.

### Different sample rates

The tag's sensors don't sample together: accelerometer and gyro at the
configured rate (100 Hz by default), magnetometer and pressure once per ten
of those. The filter merges every reading into **one time-ordered stream**,
each reading at its own time:

- **The IMU drives it.** Between IMU samples the filter carries the attitude
  forward with the gyro, and the altitude with the vertical part of the
  acceleration.
- **Other readings are applied at their own time.** When a magnetometer or
  pressure reading falls between two IMU samples, the filter steps exactly to
  its time and applies it there.
- **Its timestamp is corrected first.** The firmware reads those sensors at
  the end of each group of ten samples, not the start. A magnetometer reading
  is also older than when it is read, by up to one conversion period. The
  filter works out the magnetometer's own clock from the data, and places
  each reading at its true time.
- **Missing readings are simply missing.** Nothing assumes a fixed ratio
  between sensors, so the same filter works at every rate from 100 to
  1600 Hz.

### What each sensor contributes

| Sensor | Role |
| --- | --- |
| Gyro | Carries the attitude through everything, including the hardest manoeuvres. Its bias is estimated, mostly while the tag sits still, and its scale and misalignment errors from the large rotations of the flight itself |
| Accelerometer | Says which way is down, but only when the tag isn't accelerating. Its weight is turned down smoothly during manoeuvres, so a loop's 5 g pull isn't mistaken for gravity |
| Magnetometer | Says which way is north, after calibration, compared with the expected field. Any calibration error left over is estimated. Readings with the wrong magnitude (a disturbance) are ignored |
| Pressure | Gives altitude changes, a few centimetres per sample. It also keeps the vertical part of the acceleration honest, which helps the tilt estimate during manoeuvres |

### Two passes

- **The forward pass** runs through the flight in time order, as a real-time
  filter would.
- **The smoother** runs backwards over the whole flight, so every moment's
  estimate also uses what came *after* it. Level flight after a manoeuvre
  corrects the attitude during the manoeuvre, which a real-time filter
  cannot do. This is the main advantage of processing a download afterwards.

**Memory:** a full-flash log needs no more memory than your machine has. The
history the smoother needs goes to temporary files on disk above 1 GiB.

### How well it does

On the 15 simulated pattern flights, comparing the filter's smoothed attitude
with truth, median across flights, in the air:

| Condition | Error |
| --- | --- |
| Simple sensor errors | 0.08° |
| Realistic errors (scale, misalignment, bias wander) | 0.12° |
| Realistic errors plus imperfect calibration | 0.27° |
| Today's one-sample compass, for comparison | 28° |

Altitude is within about 2 cm, and vertical speed within about 1 cm/s.

**What makes the difference:**

- *The gyro calibration states:* without them, the realistic case is 0.73°.
- *Estimating leftover magnetometer calibration error:* without it, the
  imperfect-calibration case is 1.6°.
- *Pressure:* without it, every case is about 0.5°.

**Treat these as provisional.** They come from simulated data. Several error
sizes are assumptions until they're measured on a real tag, gyro
misalignment above all. Real prototype data, and a flight with a tag aboard,
will settle them.

## Running it

### Set up once

You need:

- **A build of the host tools,** for `tag-log-import` and `dataprocessing`,
  and sensorviz if you want to look at the logs.
- **Python 3.10 or later.**
- **Access to the private data repository,** checked out beside this
  repository as `data/`, for the flight logs.

```bash
python3 -m venv ~/.venvs/tagsim
```

```bash
~/.venvs/tagsim/bin/pip install pytest -e host/python/tagclient -e "host/python/tagsim[test]"
```

```bash
cmake --build <build-dir> --target tag-log-import dataprocessing
```

```bash
export TAG_LOG_IMPORT_BIN=<build-dir>/Release/bin/internal/tag-log-import
export DATAPROCESSING_BIN=<build-dir>/Release/bin/dataprocessing
export TAGSIM_LOG_DIR=../data/ardupilot/pattern-2023
```

**Keep generated files out of the repository.** Write them to a directory
outside it, such as `~/Documents/tagsim-output`. They carry the flying
site's positions, like the source logs.

### Simulate, filter and score one flight

```bash
~/.venvs/tagsim/bin/tagsim run --log $TAGSIM_LOG_DIR/00000173.BIN -o f173.db3
```

That writes `f173.db3` (open it in sensorviz) and `f173.truth.db3`. It prints
any glitch repairs, how many samples clipped at full scale, and the
consistency check.

```bash
~/.venvs/tagsim/bin/tagsim filter --log f173.db3 --truth f173.truth.db3 -o f173-filt.db3
```

```bash
~/.venvs/tagsim/bin/tagsim score --truth f173.truth.db3 --estimate f173-filt.db3 --table FilterSmoothed -o f173.score.db3
```

The score reports attitude error (total, tilt, heading), altitude and
vertical-speed error, each as a median, 95th percentile and maximum, for
flight and ground separately. The per-sample error series in
`f173.score.db3` can be plotted.

`--truth` on `tagsim filter` only supplies the Earth's field for the site;
the filter never sees the true attitude. On a real log, give the site
instead with `--site LAT LON ALT`.

### Look at it

To see what the filter is doing, write the comparison page and open it in a
browser:

```bash
~/.venvs/tagsim/bin/tagsim compare --truth f173.truth.db3 --score f173.score.db3 -o f173-compare.html
```

It shows only what the filter estimates, truth beside estimate:

- two 3D models of the aircraft, truth on the left and the filter's
  estimate on the right, from the same camera (drag to turn both);
- a pair of bars with the true and estimated altitude above the start;
- strips of the tilt, heading and altitude error over the whole flight,
  with flying time shaded. Click one to jump there.

Play, pause (space bar), the speed and the scrubber move both together.

There is no horizontal track: the filter doesn't estimate horizontal
position. Integrating the accelerometer twice drifts by metres within ten
seconds and kilometres within ten minutes, so it needs outside fixes, such as
a known launch point.

As a sanity check of the simulation itself, `tagsim view --score` writes a
`.view.bin` that [UAV Log Viewer](https://plot.ardupilot.org/) and FlightCoach
open beside the original flight log; its timeline matches the original's. It
draws the estimate along the *true* path, so it shows that the simulated
flight matches the real one, not how good the estimate is. The provider of
the first logs has agreed to their being opened on that public site.

### Run everything

To simulate, process, filter and score every flight in a directory:

```bash
~/.venvs/tagsim/bin/tagsim batch --log-dir $TAGSIM_LOG_DIR --out-dir ~/Documents/tagsim-output --filter
```

That writes one table of every score, `scores.csv`. `--only f166 f173` limits
it to some flights, and `--reuse` rescores existing logs without regenerating
them.

## Running the tests

From the repository root, with the setup above:

```bash
~/.venvs/tagsim/bin/pytest host/python/tagsim/tests
```

**What needs what.** Tests that need something you haven't set up are
skipped, not failed (`pytest -rs` lists them with reasons):

| Needs | Covers |
| --- | --- |
| Nothing | Most tests: page format, sensor error statistics, the stationary tag, trajectory consistency, glitch handling, scoring, and the filter on synthetic motion (rest, rotation, climbing, dropped readings, several rates, disk-backed storage) |
| `tag-log-import` | Round trips through the real decoder |
| Plus a flight log | The end-to-end runs on `00000173` |
| Plus `dataprocessing` | The calibration and compass checks |
| Plus `SENSORVIZ_BIN` | sensorviz opening a generated log |

The README's test table lists each test and what it checks.

**Run them** after changing tagsim, `tag-log-import`, the IMUTag SQLite
writer, or `dataprocessing`'s compass processor. A failure there usually
means the log format and its readers have drifted apart.
