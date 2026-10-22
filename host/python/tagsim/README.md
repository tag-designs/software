---
type: readme
status: current
summary: tagsim, the Python tool that replays a recorded ArduPilot flight into a genuine IMUTag SQLite log plus a ground-truth file, prototypes the post-processing attitude and altitude filter, scores estimates against that truth and exports both for UAV Log Viewer - installing, running, testing, and what it does not model yet.
---

# tagsim

`tagsim` turns a recorded R/C flight (an ArduPilot DataFlash `.bin` log) into
the SQLite log an IMUTag would have produced flying it, plus a separate file
of the truth the tag could not know. It is testing infrastructure for the host
tools, not a shipped tool. The plan is
[design/proposals/imutag-simulator.md](design/proposals/imutag-simulator.md);
how the built part works is in [design/simulator.md](design/simulator.md).
It also holds the Python prototype of the IMUTag post-processing filter,
`tagsim filter` ([design/navigation-filter.md](design/navigation-filter.md)).

```sh
tagsim run --log 00000173.BIN --odr 100 --accel-range 4 --gyro-range 1000 -o f173.db3
sensorviz --load-log f173.db3
```

That writes `f173.db3`, a genuine IMUTag log (the pages go through tagcore's
own decoder via [`tag-log-import`](../../commandline/README.md#tag-log-import)),
and `f173.truth.db3`. The samples carry the datasheet sensor errors an
orientation filter has to cope with, drawn from `--seed`:

- accel and gyro white noise, a turn-on bias, scale and misalignment errors,
  and bias instability (optionally vibration);
- the World Magnetic Model field at the log's origin and date, with hard and
  soft iron, noise, and the sample age of a free-running magnetometer;
- pressure noise and offset, under a constant or drifting sea-level pressure.

`--ideal` turns the errors off. The log's `Calibration` table carries the
true hard and soft iron, so `dataprocessing` recovers the WMM field
magnitude; `--calibration perturbed` writes deliberately wrong constants.

## Installing

From a checkout, in a virtual environment, with
[`tagclient`](../tagclient/README.md) (whose runtime protobuf compilation
tagsim uses):

```sh
pip install -e host/python/tagclient -e "host/python/tagsim[test]"
```

It needs numpy, scipy and [pygeomag](https://pypi.org/project/pygeomag/) (the
World Magnetic Model, pure Python); no pymavlink, since it reads DataFlash
itself. It also needs `tag-log-import`, built with the host tools
(`cmake --build <build-dir> --target tag-log-import`), found through
`TAG_LOG_IMPORT_BIN` or on `PATH`.

## Running

`tagsim run` options, all optional except `--log` and `-o`:

| Option | Default | Meaning |
| --- | --- | --- |
| `--odr` | 100 | IMU rate, Hz: 100, 200, 400, 800 or 1600 |
| `--accel-range`, `--gyro-range` | 4, 1000 | Full scale, g and dps |
| `--mounting` | `RFU` | Body direction (F, B, R, L, U, D) of tag x, y, z; `RFU` is flat, battery end forward |
| `--seed` | 0 | Seeds every random term; a run is reproducible from its seed and options |
| `--ideal` | off | No sensor errors: no noise, bias, hard or soft iron, offsets or magnetometer rate error |
| `--calibration` | `true` | `Calibration` table: the true hard and soft iron, or `perturbed` ones |
| `--set-error NAME=VALUE` | | Override one error parameter (see [errors.py](src/tagsim/errors.py)), e.g. `accel_bias_mg=0` or `vibration_accel_mg=50`; repeatable |
| `--mag-ned N E D` | WMM | Earth field, uT, in place of the World Magnetic Model value |
| `--sea-level-hpa` | 1013.25 | ISA sea-level pressure |
| `--sea-level-walk` | 0 | Sea-level pressure random walk, hPa per √h; 0 holds it constant |
| `--ppm` | 0 | `TagInfo.ppm_clock_error` to report; no clock error is simulated yet |
| `--pad` | 10 | Seconds kept either side of the `STAT.isFlying` window |
| `--duration` | whole flight | Stop after this many seconds |
| `--max-accel-g`, `--max-rate-dps`, `--vel-tol`, `--att-tol`, `--bridge-s` | 15, 1000, 5, 10, 0.5 | Glitch thresholds |
| `--pos-sigma`, `--vel-sigma`, `--jerk-sigma` | 5, 0.1, 100 | Position fit weights |
| `--keep-jsonl` | off | Keep the intermediate download (`NAME.jsonl`, the `tag_dwnld.py` format) |

It prints the glitch repairs, the WMM field, the per-axis fraction of accel
and gyro samples at full scale, the fit residuals and the strapdown
consistency figures. All of them, the error parameters and the values drawn
from them are stored in the truth file's `SimRun` row.

**Source logs stay out of git.** They belong to the pilot who flew them and
carry flying-field GPS positions. Keep them outside the repository; the tests
find them through `TAGSIM_LOG_DIR`. Generated logs, truth files and viewer
files carry the same positions, so they stay out too.

## Filtering, scoring and viewing

```sh
dataprocessing -i f173.db3 -o f173-proc.db3 -p imu-compass-orientation
tagsim score --truth f173.truth.db3 --estimate f173-proc.db3 -o f173.score.db3
tagsim filter --log f173.db3 --truth f173.truth.db3 -o f173-filt.db3
tagsim score --truth f173.truth.db3 --estimate f173-filt.db3 --table FilterSmoothed -o f173-smoothed.score.db3
tagsim view --truth f173.truth.db3 --score f173-smoothed.score.db3 -o f173.view.bin
```

- **`tagsim filter`** estimates attitude, gyro and accelerometer bias,
  altitude relative to the start and vertical velocity from the log alone: a multi-rate error-state
  EKF, then an RTS smoother ([design](design/navigation-filter.md)). It
  writes `FilterForward` and `FilterSmoothed` to `-o`. It needs the Earth
  field: `--site LAT LON ALT` (the WMM there on the log's date),
  `--field-ned N E D`, or `--truth` (the field the truth file was made with).
  `--no-pressure` drops the altitude channel, `--no-smooth` the backward
  pass, and `--set NAME=VALUE` overrides any `FilterConfig` parameter in
  [navfilter.py](src/tagsim/navfilter.py).

- **`tagsim score`** reports attitude error (total, tilt and heading) and,
  where the estimate has them, velocity, altitude-change and
  vertical-velocity error: median, 95th percentile and maximum, for flight
  and ground separately. Altitude is compared as a change from the start,
  the two datums matched over the estimate's first second. It writes the error series and
  summary to `NAME.score.db3` (`ScoreSeries`, `ScoreSummary`, `ScoreRun`) for
  sensorviz or a notebook. By default it scores `dataprocessing`'s
  `ImuCompassOrientation`; `--table T` scores any table with `ElapsedUs` and
  `qw qx qy qz`, tag frame to ENU, plus optional `ve vn vu`, `alt` and `vz`,
  such as the filter's.
- **`tagsim view`** writes a DataFlash log that
  [UAV Log Viewer](https://github.com/ardupilot/uavlogviewer) opens: the truth
  as `POS` and `ATT` (the viewer's defaults) and, with `--score`, the estimate
  as `AHR2` along the truth path. Choose `AHR2` as the trajectory and attitude
  source to watch the estimate fly the same pattern.
- **`tagsim batch --log-dir DIR --out-dir OUT`** does all of it for every log
  in a directory, at the defaults: `NAME.db3`, `NAME-proc.db3`,
  `NAME.truth.db3`, `NAME.score.db3` and `NAME.view.bin` per flight, and a
  table of every score in `OUT/scores.csv`, one row per flight and
  estimate. `--filter` also runs the filter with and without pressure
  (`NAME-filt.db3`, `NAME-filt-nop.db3`) and scores its forward and smoothed
  tables (`NAME-forward.score.db3`, …, and `NAME-smoothed.view.bin`);
  `--reuse` scores existing logs instead of regenerating them;
  `--only f166 f173` limits it to some logs; `--calibration` and
  `--set-error` are passed to `tagsim run` and `--filter-set NAME=VALUE` to
  `tagsim filter --set`, for ablations. It needs
  `dataprocessing`, found through `DATAPROCESSING_BIN` or on `PATH`.

How the compass's quaternion is decoded, and why it has to be, is in
[design/simulator.md](design/simulator.md#scoring). The compass baseline and
the filter's scores are in
[design/compass-baseline-results.md](design/compass-baseline-results.md).

## Running the tests

From the repository root, with the virtual environment kept outside it:

```bash
python3 -m venv ~/.venvs/tagsim
```

```bash
~/.venvs/tagsim/bin/pip install pytest -e host/python/tagclient -e "host/python/tagsim[test]"
```

```bash
cmake --build <build-dir> --target tag-log-import
```

```bash
export TAG_LOG_IMPORT_BIN=<build-dir>/Release/bin/tag-log-import
export TAGSIM_LOG_DIR=../data/ardupilot/pattern-2023   # the private data repository, checked out beside this one
export SENSORVIZ_BIN=<build-dir>/Release/bin/sensorviz.app/Contents/MacOS/sensorviz
export DATAPROCESSING_BIN=<build-dir>/Release/bin/dataprocessing
```

```bash
~/.venvs/tagsim/bin/pytest host/python/tagsim/tests
```

Each variable has a fallback:

- `TAG_LOG_IMPORT_BIN` falls back to `tag-log-import` on `PATH`.
- `TAGSIM_LOG_DIR` defaults to `ardupilot/pattern-2023` in the private data repository, checked out beside this one (`<repo>/../data`). Ask for access; the data is not public.
- `SENSORVIZ_BIN` is optional and falls back to `sensorviz` on `PATH`. On
  Linux and Windows it is `<build-dir>/Release/bin/sensorviz`.
- `DATAPROCESSING_BIN` is optional and falls back to `dataprocessing` on
  `PATH` (`cmake --build <build-dir> --target dataprocessing`).

| Test | Checks | Needs |
| --- | --- | --- |
| `test_pages.py::test_pack_layout` | Page layout, checkpoint flags, partial pages, the exact NaN pattern | nothing |
| `test_pages.py::test_round_trip` | A two-segment log with missing auxiliary slots, a storage-skip resync, an index hole and partial pages decodes back to exactly the packed values | `tag-log-import` |
| `test_stationary.py` | A tag at rest reads 1000 mg and zero rate within quantization at any attitude, and a constant positive dip under the host's axis convention | nothing |
| `test_errors.py` | On 300 s at rest, at 100 and 400 Hz: accel, gyro, magnetometer and pressure noise match the configured RMS, the means match the drawn biases and offset, the true calibration recovers the field, magnetometer age sweeps one conversion period; seeding, missed conversions, perturbed calibration, the sea-level walk | nothing |
| `test_consistency.py` | Strapdown integration of a synthetic manoeuvre agrees with its spline pose | nothing |
| `test_glitches.py` | On a synthetic DataFlash log, a one-sample spike is bridged and a lasting step is split at | nothing |
| `test_dataflash.py` | The reader's scaling agrees with the log's own `FMTU`/`MULT` unit records | a log |
| `test_flight.py::test_log_and_truth` | `00000173.BIN` (or the first log found) end to end at 100 Hz, and its truth file | a log and `tag-log-import` |
| `test_flight.py::test_calibrated_field_is_wmm` | `dataprocessing imu-compass-orientation` on that log: the calibrated field's median is the WMM magnitude | a log, `tag-log-import` and `dataprocessing` |
| `test_navfilter.py` | The filter's event stream (poll offsets, ordering, the age hook) at 100, 400 and 1600 Hz; a tag at rest; constant-rate rotation about three axes; a constant-rate climb; smoothed no worse than forward on a manoeuvre; the same manoeuvre at 100 and 400 Hz; a third of the magnetometer and pressure rows dropped, a 3 s gap, and magnetometer events between IMU samples | nothing |
| `test_score.py` | The compass decode inverts the algorithm at every attitude; the tilt and heading split; a compass at rest scores zero without sensor errors and the bias tilt with them; the viewer log reads back | nothing |
| `test_flight.py::test_score_and_view` | The Python port of the compass matches `dataprocessing`'s quaternions on that log; the flight scores, fully decoded, and exports | a log, `tag-log-import` and `dataprocessing` |
| `test_flight.py::test_sensorviz_loads` | sensorviz opens the generated log and writes a screenshot | a log, `tag-log-import` and sensorviz |

A test whose needs are missing is skipped, not failed. `pytest -rs` lists
the skips and their reasons.

## Not modelled yet

Temperature effects; clock error; dynamic pressure; BMP581 sampling detail;
the sensor's own filtering and aliasing of vibration; synthetic primitives.
The accelerometer scale and misalignment, gyro misalignment and both bias
instabilities are assumed figures, not fitted to bench data. See the
proposal's [scope](design/proposals/imutag-simulator.md#scope-keep-it-simple)
and phases.
