---
type: readme
status: current
summary: tagsim, the Python tool that replays a recorded ArduPilot flight into a genuine IMUTag SQLite log plus a ground-truth file - installing, running, testing, and what the first slice does not model yet.
---

# tagsim

`tagsim` turns a recorded R/C flight (an ArduPilot DataFlash `.bin` log) into
the SQLite log an IMUTag would have produced flying it, plus a separate file
of the truth the tag could not know. It is testing infrastructure for the host
tools, not a shipped tool. The plan is
[design/proposals/imutag-simulator.md](design/proposals/imutag-simulator.md);
how the built part works is in [design/simulator.md](design/simulator.md).

```sh
tagsim run --log 00000173.BIN --odr 100 --accel-range 4 --gyro-range 1000 -o f173.db3
sensorviz --load-log f173.db3
```

That writes `f173.db3`, a genuine IMUTag log (the pages go through tagcore's
own decoder via [`tag-log-import`](../../commandline/README.md#tag-log-import)),
and `f173.truth.db3`. The first slice is **noise-free**: the samples are the
ideal signals, quantized and saturated as the tag stores them.

## Installing

From a checkout, in a virtual environment, with
[`tagclient`](../tagclient/README.md) (whose runtime protobuf compilation
tagsim uses):

```sh
pip install -e host/python/tagclient -e "host/python/tagsim[test]"
```

It needs numpy and scipy; no pymavlink, since it reads DataFlash itself. It
also needs `tag-log-import`, built with the host tools
(`cmake --build <build-dir> --target tag-log-import`), found through
`TAG_LOG_IMPORT_BIN` or on `PATH`.

## Running

`tagsim run` options, all optional except `--log` and `-o`:

| Option | Default | Meaning |
| --- | --- | --- |
| `--odr` | 100 | IMU rate, Hz: 100, 200, 400, 800 or 1600 |
| `--accel-range`, `--gyro-range` | 4, 1000 | Full scale, g and dps |
| `--mounting` | `RFU` | Body direction (F, B, R, L, U, D) of tag x, y, z; `RFU` is flat, battery end forward |
| `--mag-ned N E D` | 20 -1.5 47 | Constant Earth field, uT (placeholder for a World Magnetic Model value) |
| `--sea-level-hpa` | 1013.25 | ISA sea-level pressure |
| `--ppm` | 0 | `TagInfo.ppm_clock_error` to report; no clock error is simulated yet |
| `--pad` | 10 | Seconds kept either side of the `STAT.isFlying` window |
| `--duration` | whole flight | Stop after this many seconds |
| `--max-accel-g`, `--max-rate-dps`, `--vel-tol`, `--att-tol`, `--bridge-s` | 15, 2000, 5, 10, 0.5 | Glitch thresholds |
| `--pos-sigma`, `--vel-sigma`, `--jerk-sigma` | 5, 0.1, 100 | Position fit weights |
| `--keep-jsonl` | off | Keep the intermediate download (`NAME.jsonl`, the `tag_dwnld.py` format) |

It prints the glitch repairs, the fit residuals and the strapdown consistency
figures, which are also stored in the truth file's `SimRun` row.

**Source logs stay out of git.** They belong to the pilot who flew them and
carry flying-field GPS positions. Keep them outside the repository; the tests
find them through `TAGSIM_LOG_DIR`.

## Tests

```sh
TAG_LOG_IMPORT_BIN=<build-dir>/Release/bin/tag-log-import \
SENSORVIZ_BIN=<build-dir>/Release/bin/sensorviz.app/Contents/MacOS/sensorviz \
TAGSIM_LOG_DIR=~/Documents/ardupilot_data \
pytest host/python/tagsim
```

- `test_pages.py`: page layout, and a two-segment log with missing auxiliary
  slots, a storage-skip resync, an index hole and partial pages decoded back
  through `tag-log-import` to exactly the packed values.
- `test_stationary.py`: a tag at rest reads 1000 mg and zero rate within
  quantization at any attitude, and a constant positive dip under the host's
  axis convention.
- `test_consistency.py`: strapdown integration of a synthetic manoeuvre
  agrees with its spline pose.
- `test_flight.py`: `00000173.BIN` (or the first log found) end to end at
  100 Hz, the truth file, and a sensorviz screenshot.

Tests needing the importer, a log or sensorviz skip without them.

## Not modelled yet

Sensor noise, bias and temperature effects; magnetometer sample age; clock
error; a World Magnetic Model; dynamic pressure and weather drift; vibration;
synthetic primitives; UAV Log Viewer export; the saturation report. See the
proposal's phases.
