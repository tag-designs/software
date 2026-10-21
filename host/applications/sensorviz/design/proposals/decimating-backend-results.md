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
