---
type: results
status: current
summary: Measured memory, load time and view-query cost of current sensorviz and of a decimating-backend prototype on a synthetic full-NAND IMUTag log (19.7M accel+gyro samples).
---

# Decimating Backend Measurements

Raw measurements behind the
[decimating backend proposal](decimating-backend.md). Append new runs below
with their conditions; do not edit old ones.

## 2026-10-21: worst-case IMUTag log, accel and gyro

**Conditions.** Apple M2, 24 GB, macOS 27.0.1. Source at `aaa95576`.
sensorviz built Release against Qt 6.10.3 (`arm64-osx-static` vcpkg triplet).
The prototype is [`decimation-bench/bench.cc`](decimation-bench/bench.cc),
built with `clang++ -std=c++17 -O2 ... -lsqlite3` against the system SQLite
3.54.0. Memory is the macOS *physical footprint* (`/usr/bin/time -l` "peak
memory footprint", or `TASK_VM_INFO` `ledger_phys_footprint_peak` inside the
prototype). It counts compressed pages; `ru_maxrss` does not, and
under-reported the current loader by about 0.7 GB.

**Log.** Built by
[`decimation-bench/make-full-log.sh`](decimation-bench/make-full-log.sh) from
the committed fixture `host/docs/fixtures/sensorviz/imutag.db3`.
`ImuAccel` and `ImuGyro` hold 19,660,800 rows each. That is a full 2 Gbit
GD5F2GM7RE (131,072 pages × 150 samples) at 400 Hz, spanning 49,152 s.
Values sit on the writer's int16 × scale grid. Other tables keep the
fixture's 3,555 mag and pressure rows. The file is 2,158,678,016 bytes,
109.8 bytes per accel+gyro sample. A one-million-row synthetic `ImuAccel`
table measured 54.7 bytes per row after `VACUUM`.

### Current sensorviz

```text
sensorviz --load-log full.db3 --capture-screenshot imutag --no-user-prompts
```

| Visible streams | Wall time | Peak footprint |
| --- | --- | --- |
| Defaults: accel, gyro and field magnitudes; components hidden | 38.1 s | 2,944,568,776 B |
| All six components plus the three magnitudes (`--load-preferences`, `visible_streams`) | 41.2 s | 4,834,431,776 B |

The wall time covers the load, the transforms and rendering one screenshot.

### Prototype: the current loader alone (`bench current`)

This is one `SELECT ElapsedUs, <col> ... ORDER BY ElapsedUs` per stream into
two `double` vectors, which is what `sqlite_loader.cpp` does. It holds no
plot copies.

```text
  ImuAccel.ax: 19660800 rows in 4.4 s
  ImuAccel.ay: 19660800 rows in 4.6 s
  ImuAccel.az: 19660800 rows in 4.6 s
  ImuGyro.gx: 19660800 rows in 4.8 s
  ImuGyro.gy: 19660800 rows in 4.7 s
  ImuGyro.gz: 19660800 rows in 4.8 s
current: load 27.8 s, peak RSS 1781 MB, footprint 2440 MB, peak footprint 2449 MB
```

### Prototype: proposed layout (`bench proposed`)

Each table is read once in rowid order, into a shared `int64` time axis and
`float32` values. A min/max pyramid with 32-sample base buckets is built
over it. M4 views are 2,000 columns wide, for all six streams, averaged over
20 repetitions. "Brute force" means a direct scan of the same columns for
one stream. Every view was checked against brute force for min, max, first
and last index. Output is trimmed of its per-view KB column.

The prototype omits two parts of the proposed design. It splits columns by
sample index rather than by time, which is equivalent for this log because it
has one segment at a uniform rate. It also does not locate the sample time of
each column's min and max (step 3 of the view query), so the view timings
exclude that step.

```text
  ImuAccel: 19660800 rows in 1.8 s
  ImuGyro:  19660800 rows in 1.8 s
proposed: load 3.6 s, pyramid build 0.10 s, pyramids 59 MB, data 786 MB, peak footprint 848 MB
  view full log  :  19660800 samples/stream, 7.20 ms for 6 streams, 388800 entries touched, 48000 points out; brute force 24 ms/stream; exact
  view 1 hour    :   1440000 samples/stream, 3.42 ms for 6 streams, 245244 entries touched, 48000 points out; brute force 2 ms/stream; exact
  view 1 minute  :     24000 samples/stream, 2.02 ms for 6 streams, 144000 entries touched, 48000 points out; brute force 0 ms/stream; exact
  view 10 seconds:      4000 samples/stream, 0.36 ms for 6 streams, 24000 entries touched, 24000 points out; brute force 0 ms/stream; exact
```

A first run used a different real IMUTag log as the template. It gave the
same picture: proposed 849 MB and 5.0 s; current loader 2,440 MB and 29.3 s;
sensorviz with defaults 2.94 GB, and with all streams 4.83 GB.

## 2026-10-21: phase 0, one query per table

**Conditions.** As above: Apple M2, 24 GB, macOS 27.0.1, Qt 6.10.3,
`arm64-osx-static`, Release. Both binaries were built from the same
configuration; "before" is `4395236e`, "after" is the phase 0 loader change
(`sqlite_loader.cpp` reads each table once, `ORDER BY rowid`, and the streams
of a table share one time `QVector` by implicit sharing). The log is a fresh
[`make-full-log.sh`](decimation-bench/make-full-log.sh) output,
2,158,653,440 bytes.

```text
/usr/bin/time -l sensorviz --load-log full.db3 --capture-screenshot imutag --no-user-prompts
```

| Visible streams | Before: wall | Before: peak footprint | After: wall | After: peak footprint |
| --- | --- | --- | --- | --- |
| Defaults | 43.7 s | 2,937,261,488 B | 10.7 s | 2,299,054,696 B |
| All six components plus magnitudes | 41.5 s | 4,835,152,672 B | 10.3 s | 4,196,503,488 B |

The saving is 0.64 GB in both cases: four of the six component time vectors
(4 × 157 MB) no longer exist. The proposal predicted 0.6 GB and a load of
about 4 s; the remaining wall time is the transforms and the screenshot.

**Equivalence.** The `imutag` and `compasstag` capture suites, run on the
committed fixtures with `-platform offscreen` so the capture is a widget
grab rather than a screen grab, are pixel-identical before and after apart
from the screenshot directory printed on the File Info page. A copy of the
IMUTag fixture with `ImuAccel` reversed and `ImuMag` shuffled in rowid order
(exercising the in-memory sort fallback) renders the same plot as the
original, apart from the file name in the title.

Screen-grab captures of the full log are unreliable for comparison: a
40-second load lets other windows come to the front, and they are captured.

## 2026-10-21: phase 1, the sensordata library

**Conditions.** As above. `sensordata_check --log full.db3` from
[`host/libraries/sensordata`](../../../../libraries/sensordata/README.md),
Release, linked against the vcpkg SQLite. The log is the phase 0 one. The
library loads all eleven metadata streams, not only accel and gyro: 117,979,257
samples in all. Views are 2,000 columns wide over the six accel and gyro
streams, centred on the log, averaged over 20 repetitions, and **include**
locating the sample of each column's minimum and maximum, which the prototype
left out.

```text
load 4.00 s, 11 streams, 117979257 samples, peak footprint 851 MB
  view full log  : 6 streams, 8.59 ms, 47737 points out
  view 1 hour    : 6 streams, 2.86 ms, 47671 points out
  view 1 minute  : 6 streams, 0.59 ms, 43954 points out
  view 10 seconds: 6 streams, 0.17 ms, 24012 points out
  imu_ax: exact
  ...
  imu_gz: exact
```

Every view of every accel and gyro stream matched a brute-force M4 computed
from `forEachRow()` doubles narrowed to `float`. Load time varied from 4.0 s
to 6.2 s across four runs on a machine also in interactive use; memory and
view times did not vary. `/usr/bin/time -l` reports a 1,087 MB peak for the
whole program, which includes the brute-force reference copies made after the
851 MB figure is taken.

Two implementation findings, both now in the library:

- **SQLite's serialized mode cost 1.6 times the load.** In
  `SQLITE_OPEN_FULLMUTEX` mode every `sqlite3_column_*()` call takes the
  connection mutex. The same build loaded in 6.3 s and 7.0 s with it and
  4.0 s and 4.3 s with `SQLITE_OPEN_NOMUTEX`, so the library opens without it
  and serialises `valueAt()` itself; `forEachRow()` opens its own connection.
- **Column edges by plain binary search cost half the full-log view.**
  2,001 binary searches per stream over a 157 MB time axis are mostly cache
  misses. Searching from a guess that each column holds as many samples as
  the previous one, galloping outward, brought the full-log view from 20 ms
  to 8.6-10 ms.

## 2026-10-21: phase 2, sensorviz plots through sensordata

**Conditions.** As above. sensorviz at the phase 2 change: graphs are filled
from `view()` after every QCustomPlot layout pass, one column per device
pixel; hover uses `valueAt()`; y ranges use the pyramids; printing refills at
the printer's resolution. The `SensorStream` `double` copies remain as
transform inputs (phase 3 removes them), so the log is read twice.

```text
/usr/bin/time -l sensorviz --load-log full.db3 --capture-screenshot imutag --no-user-prompts
```

| Visible streams | Before (`4395236e`) | Phase 0 | Phase 2 |
| --- | --- | --- | --- |
| Defaults | 43.7 s, 2,937,261,488 B | 10.7 s, 2,299,054,696 B | 11.4-13.0 s, 2,547,239,768 B |
| All six components plus magnitudes | 41.5 s, 4,835,152,672 B | 10.3 s, 4,196,503,488 B | 11.1-11.5 s, 2,548,796,272 B |

Peak memory no longer depends on what is visible. It is 0.25 GB above
phase 0 for the default view: the backend's 0.85 GB and the magnitudes'
`float` copies are added, while only the two visible magnitudes' QCustomPlot
copies (0.63 GB) and their `double`s go. The peak is reached while the loader's
`double`s and the backend coexist. A first cut that also kept each
transform output's `double`s peaked at 2.71 GB in both views; those are now
dropped once registered, since no transform reads another's output.

**Equivalence.** Offscreen widget grabs (`-platform offscreen`), before
against after:

| Capture | Differing pixels | Largest channel difference |
| --- | --- | --- |
| `imutag` suite, fixture | 12 in the plot | 2 |
| `compasstag` suite, fixture | 0 in the plot | 0 |
| Full log, defaults | 0 | 0 |
| Full log, all streams | 203, along the top edge of the dense accel traces | 5 |

The File Info pages differ only in the screenshot directory they print.

A temporary test hook, not committed, set the x range and printed the hover
readout of every visible stream at chosen times, in the phase 0 build and the
phase 2 build. Over two fixture windows, one CompassTag window and three
full-log windows (one hour, ten seconds, and one running past the end of the
data), the 69 hover lines were identical, including the derived magnitudes,
which the backend holds as `float`. The renders were identical or within 3
levels, except the ten-second window: 131 pixels differ, all but one in the
last two pixel columns at the right edge of the plot, by up to 74 levels.
QCustomPlot's adaptive sampling bins that final column differently from the
M4 columns. Drawing every sample with adaptive sampling switched off is not a
usable reference: anti-aliased segments pile up, and that render differs
from both by over 100,000 pixels.

**Print.** The same hook rendered to a `QPrinter::HighResolution` PDF. The
envelope was refilled at 9,532 columns for the fixture and 9,731 for a
one-hour window of the full log. Printing previously went through
`QWidget::render()`, which copies the screen's raster buffers; it now uses
`QCustomPlot::toPainter()` for vector output, painting in the screen's
logical DPI so text keeps its size, with QCustomPlot's adaptive sampling off
while it draws so the printer-resolution envelope is not thinned back to
screen density.

## 2026-10-21: phase 2 restructured into PlotSpec and PlotView

**Conditions.** As above. The phase 2 plot code was split into
`buildPlotSpec()` (pure plot policy) and `PlotView` (the only QCustomPlot
code), with no intended change of behaviour.

**Equivalence**, against the phase 2 build:

- `imutag` and `compasstag` capture suites (offscreen): every plot and menu
  capture is pixel-identical; the File Info pages differ only in the
  screenshot directory they print.
- Full log, default and all-streams captures: pixel-identical.
- The five zoomed windows and hover times of the phase 2 comparison
  (re-run with the same temporary hook, reaching the plot through
  `PlotView`): renders pixel-identical, all 57 hover lines identical.
- Printing the full log to PDF still refills at the printer's resolution:
  both magnitude traces are about 37,000-point vector paths spanning the
  plot's full width.

`plotspec_check` (89 checks against the fixtures) covers the policy that
moved into `buildPlotSpec()`.

## 2026-10-21: phase 3, transforms read the backend

**Conditions.** As above, with `main` at `bd5fd193` merged. `SensorStream`
holds no samples, and the loader no longer makes its own pass over the log.
The IMUTag magnitudes and the acceleration calibration offset are computed
from the backend's `float` columns with `sensordata::Log::addComputed()`.
The magnetometer calibration also works from the `float`s. Altitude, the
activity filter and the CompassTag orientation read SQLite `double`s through
`forEachRow()`; the pressure pass is cheap at the IMUTag pressure rate.

```text
/usr/bin/time -l sensorviz --load-log full.db3 --capture-screenshot imutag --no-user-prompts
```

| Visible streams | Before (`4395236e`) | Phase 2 | Phase 3 |
| --- | --- | --- | --- |
| Defaults | 43.7 s, 2,937,261,488 B | 11.4-13.0 s, 2,547,239,768 B | 5.7-8.2 s, 1,120,094,008-1,133,578,040 B |
| All six components plus magnitudes | 41.5 s, 4,835,152,672 B | 11.1-11.5 s, 2,548,796,272 B | 5.9-6.1 s, 1,129,219,896-1,130,923,832 B |

The peak is the backend's 0.85 GB for the eleven stored streams, the two
IMU magnitudes (2 × 89 MB of `float`s and pyramids), and Qt. The calibrated
accel and mag axes replace their stored copies, one axis at a time, so they
add nothing to the steady state.

**Magnitudes from `float`s.** On the IMUTag fixture's 35,550 gyro samples,
6,792 magnitudes (19%) differ from the `double` computation, each by one
`float` step, at most 1.19 × 10^-7 relative. The calibrated acceleration axes
differ the same way. The magnetometer calibration is exact, because
`CompassCalibration::apply()` takes a float `QVector3D`, which is what the
stored `float` already is.

**Equivalence**, against the PlotSpec/PlotView build (`234c8fbf`):

| Capture | Differing pixels | Largest channel difference |
| --- | --- | --- |
| `imutag` suite, fixture plot | 4, in one pixel column | 1 |
| `compasstag` suite | 0 | 0 |
| Full log, defaults | 0 | 0 |
| Full log, all streams | 0 | 0 |
| Zoomed windows (five, as in phase 2) | 0, 9, 0, 1, 0 | 3 |

The 57 hover lines over those windows are identical, the magnitudes included:
at the two decimals the readout shows, the `float` step does not reach the
last digit. The CompassTag heading, acceleration, pitch, roll, dip and field
streams render identically, as their `double` inputs are unchanged.

## 2026-10-21: phase 4, lines break at gaps

**Conditions.** As above. Gaps are found once per time axis at load: a step
larger than 4 × the median step, estimated from at most 65,536 evenly spaced
steps. `SegmentId` is not consulted (see the proposal's
[Phases](decimating-backend.md#phases)). sensorviz breaks lines at gaps by
default (View > Break Lines at Gaps).

**Gaps found** (`sensordata_check --gaps`). The two committed fixtures and
eight other logs on the development machine, read only:

| Log | Tables and gaps |
| --- | --- |
| `fixtures/sensorviz/imutag.db3` | 5 tables (2.5 ms to 375 ms), 0 gaps |
| `fixtures/sensorviz/compasstag.db3` | 4 tables (15 s to 615 s), 0 gaps |
| Worst-case log (`make-full-log.sh`) | 5 tables, 0 gaps |
| IMUTag field log and its processed copy (`imutag-10-11-26`) | 5 and 6 tables, 0 gaps |
| IMUTag bench log at 100 Hz (`2026-10-02-imutag-nand-bmp581`) | 5 tables, 0 gaps |
| CompassTag walk and battery-run logs | 4 tables each, 0 gaps |
| BitTag download (`2026-10-03-bittag-d1`) | 3 tables, 0 gaps |
| PresTag reference download (`2026-10-02-prestag-d1/d1-reference`) | 0 gaps |
| PresTag rebuild-check download | Pressure and Temperature 1 gap each, a 56 s pause in 1 s data; Voltage 0 gaps, though irregular (1 s to 85 s steps around 60 s) |
| UIUCTag rebuild-check download | Activity 1 gap, a 660 s step in 60 s data (ten missing slots); 0 elsewhere |

Neither gap is spurious; both are holes in the samples. No IMUTag log on hand
has more than one segment, so no resync was available to test.

**Equivalence.** For logs without gaps, against `e8cf7836`: every plot and
menu capture in the `imutag` and `compasstag` suites is pixel-identical,
except the View menu, which has gained the new item. Both full-log captures,
the five zoomed windows and their 57 hover lines are identical too.

**A log with gaps.** The IMUTag fixture with every IMU table shifted by +5 s
after t = 40 s, and accel rows from 65 s to 66 s deleted, has one gap per
table and two in `ImuAccel`. With breaks on, every trace stops at 40 s and
restarts at 45 s (447 pixels differ from `e8cf7836` with all accel streams
shown). Hover readouts at 42.5 s and 44.999 s are empty for every stream. At
65.5 s only the acceleration magnitude is empty, and at 40.001 s, within half
an interval of the last sample, every stream reads. With breaks off (the
preference `break_lines_at_gaps: false`), the render is pixel-identical to
`e8cf7836` and the readout at 42.5 s reaches across the gap, as before.

**Cost.** `sensorviz --load-log full.db3 --capture-screenshot imutag`, two
runs each, interleaved:

| Build | Defaults | All streams |
| --- | --- | --- |
| `e8cf7836` | 7.5 s, 5.7 s; 1,116,112,696 B, 1,126,631,224 B | 6.2 s, 5.8 s; 1,129,826,104 B, 1,123,632,952 B |
| Phase 4 | 5.8 s, 5.6 s; 1,126,598,432 B, 1,127,040,824 B | 5.6 s, 5.6 s; 1,122,748,192 B, 1,123,600,184 B |

The library alone (`sensordata_check --log`) still loads in 4.3 s at 851 MB
and draws six full-log streams in 8.0 ms.
