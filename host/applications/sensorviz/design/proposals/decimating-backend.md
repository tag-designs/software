---
type: proposal
status: proposed
summary: A Qt-free decimating data backend for sensorviz - one-pass columnar loading, a min/max pyramid and per-view M4 envelopes - with a memory and performance analysis of the worst-case IMUTag log (4.8 GB originally, about 1 GB proposed); all phases are built.
---

# Decimating Data Backend for sensorviz

**Status:** all phases, 0 to 4, are built. See
[Phases](#phases). The library is
[`host/libraries/sensordata`](../../../../libraries/sensordata/README.md),
whose header is now the authority on the API; it differs from the
[sketch](#api-sketch) below in detail. A benchmark prototype is in
[`decimation-bench/`](decimation-bench/bench.cc). Measurements of the
prototype and of each built phase are in
[decimating-backend-results.md](decimating-backend-results.md).

sensorviz should stop handing every sample to the plot. A new Qt-free library,
`sensordata`, would sit next to [`sensoranalysis`](../../../../libraries/README.md).
It reads each SQLite table once into a compact columnar layout, keeps a
min/max pyramid per stream, and answers each view with an **M4 envelope**:
for every pixel column, the first, last, minimum and maximum sample. The plot
then draws at most four points per pixel per stream, whatever the log's
length, and the result matches drawing every sample.

On a full 2 Gbit IMUTag log, the worst case is all accel and gyro streams
visible. sensorviz now peaks at **4.83 GB** and takes 41 s to open. The
prototype layout holds the six component streams in **0.85 GB** peak, about
1 GB once the derived magnitudes are added. It loads in 3.6 s and answers a
full-log view of six streams in 7 ms.

The backend does not depend on the UI toolkit. It is needed whether sensorviz
keeps QCustomPlot or moves to a webview front end such as Tauri with uPlot,
because neither can take gigabytes of points across a draw call or an IPC
boundary.

## Problem

### Worst case

| | Value | Source |
| --- | --- | --- |
| Flash | GD5F2GM7RE, 2 Gbit = 131,072 pages × 2,048 B | [`plot_recording_limits.py`](../../../../../embedded/tags/families/IMUTag/design/plot_recording_limits.py) |
| IMU samples per page | 150 (15 superframes × 10) | [`imutag_log_format.h`](../../../../../include/imutag_log_format.h) |
| Samples in a full log, N | **19,660,800** per stream | 131,072 × 150 |
| Duration at 400 Hz | 13.65 h | A lower rate fills more slowly but reaches the same N, so N is the worst case at every rate |
| Raw accel and gyro on flash | 236 MB | 12 B per sample (six `int16`) |
| SQLite, `ImuAccel` + `ImuGyro` | 2.16 GB | measured: about 55 B per row per table ([results](decimating-backend-results.md)) |

Magnetometer and pressure are logged at one tenth of the IMU rate, 1.97M rows
each. This analysis covers accel and gyro, which dominate. Everything below
applies unchanged to the slower streams.

### Where the memory goes today

[`sqlite_loader.cpp:352-410`](../../sqlite_loader.cpp) loads each scalar
stream with its own `SELECT time, value FROM table ORDER BY time` into
`SensorStream::time` and `SensorStream::value`, both `QVector<double>`.
[`plotting.cpp:236`](../../plotting.cpp) then calls
`graph->setData(stream.time, stream.value, true)`. That copies every point
into QCustomPlot's 16-byte `QCPGraphData`.

One `double` per sample is N × 8 B = 157 MB. The model is:

| Item | Size | Notes |
| --- | --- | --- |
| Six components: separate time and value vectors | 12 × 157 MB = 1.89 GB | The time column is identical across `ax`, `ay`, `az` (and `g*`), but is loaded six times |
| Accel and gyro magnitudes | 2 × 157 MB = 0.31 GB | Value only; time is shared with the X component through `QVector` copy-on-write ([`transforms.cpp:131`](../../transforms.cpp)) |
| QCustomPlot copy, per visible graph | 315 MB each | |
| **Default view** (magnitudes visible, components hidden) | **2.83 GB** | measured **2.94 GB** |
| **All six components + magnitudes visible** | **4.72 GB** | measured **4.83 GB** |

The model accounts for the measured footprint to within about 0.1 GB. The
remainder is Qt, QML and vector growth slack.

### Load time and rendering

- **Load.** Opening the log takes about 40 s. About 28 s of that is the six
  per-stream queries. `ElapsedUs` has no index, so each `ORDER BY` sorts
  19.7M rows. Reading each table once in rowid order takes 1.8 s.
- **Rendering.** Every repaint hands QCustomPlot every visible point:
  about 157M points with all streams shown. QCustomPlot's adaptive sampling
  bounds what is drawn, but it still walks the visible data on each repaint.
  Repaint time was not measured.

## Goals and non-goals

**Goals**

- Peak memory for the worst-case IMUTag log of about 1 GB, with every
  stream visible, against 4.8 GB now.
- View updates under about 10 ms for any zoom level, so pan and zoom track
  the mouse.
- Plots identical to drawing every sample, at the resolution being drawn.
- Exact values for hover readout and cursors.
- Bit-identical sensoranalysis results. Derived streams must see the same
  `double` inputs they see today; see
  [sensoranalysis](../../../../libraries/sensoranalysis/README.md).
- No Qt dependency, so the same library serves the command-line tools and a
  future webview UI.
- One code path for every tag type, from a 2,000-row BitTag log to a full
  IMUTag log.

**Non-goals**

- Logs larger than RAM. The worst case fits comfortably once laid out
  compactly. [Out-of-core extension](#out-of-core-extension-stored-pyramids)
  describes how it would be added later without changing the API.
  would be added.
- Changing the SQLite schema or tagcore's writer.
- Display policy: colors, visibility and axis sides stay in sensorviz's
  [preferences](../../README.md#preferences).

## Design

### Placement and dependencies

The library is `host/libraries/sensordata`, target `sensordata`, written in
C++17. It depends only on the sqlite3 C API, which the tree already gets from
vcpkg. It builds whenever `BUILD_HOST` is on, independent of
`BUILD_QT_APPS`.

It takes over the *data* half of today's `sqlite_loader.cpp`: the `streams`
metadata table, time domains, scalar streams and record sets. sensorviz keeps
display defaults, preferences and transforms.

Dependency direction after the change:

```text
sensorviz -> sensorui, sensoranalysis, sensordata, qcustomplot
sensordata -> sqlite3
```

### Data model

A **table** is one SQLite table named by the `streams` metadata. It holds one
time axis and one or more value columns. `ImuAccel` is one table with three
columns. A CompassTag record set is one table with as many columns as it
has.

| Field | Type | Per-sample cost |
| --- | --- | --- |
| Time axis | `double`, seconds in the table's time domain (epoch or elapsed), shared by every column of the table | 8 B per row, once per table |
| Each value column | `float32` | 4 B |
| Each pyramid | `float32` min and max per bucket, all levels | about 0.5 B |

**Time.** Time stays `double`. `float32` would resolve only about 4 ms at
13.7 h of elapsed time. `double` matches today's `SensorStream::time`, so the
hover and cursor arithmetic does not change.

**Values.** Values are `float32` because the plot never needs more:

- Accel and gyro are `int16` × scale on the tag. For example, at
  0.061 mg/LSB a reading near 2,000 mg is rounded by under 1/500 LSB.
- Magnetometer and pressure are already `float32` on the tag (`t_ImuTagAuxSample`),
  so storing them as `float32` is lossless.

Exact readouts and derived streams do not use the `float32` copy. They read
`double`s from SQLite; see [Exact values](#exact-values-and-derived-streams).

For one worst-case accel+gyro log this is 2 × 157 MB of time,
6 × 79 MB of values and 59 MB of pyramids: **0.85 GB measured**, against
4.83 GB today.

### Loading

Each table is read with one statement:

```sql
SELECT <time>, <col1>, <col2>, ... FROM <table> ORDER BY rowid
```

The row count comes from `SELECT count(*)` first, so every vector is
reserved once and never reallocated.

The writer inserts rows in sample order, and `ElapsedUs` comes out
non-decreasing in rowid order ([`imutag.cc`](../../../../libraries/tagcore/sqlitelog/imutag.cc)
clamps the segment base to the current end). The loader checks this as it
reads. If a table is ever out of order, it falls back to one sort of the
loaded columns by time. That is still a single in-memory sort, not one
SQLite sort per stream.

Loading runs off the UI thread and reports progress as rows read out of the
row count. Today sensorviz shows only a loading state, with no fraction
complete, for a load that can take 40 s.

### The min/max pyramid

**Structure.** For each stream, level 0 holds the min and max of every
32-sample bucket. Each level above halves the bucket count, up to a single
bucket. That is 25 levels for N = 19.7M. The total is about
2 × N/32 × 8 B ≈ N/2 bytes per stream: 9.8 MB at worst case.

**Cost.** Building all six pyramids took 0.10 s.

**Updates.** Derived streams get pyramids when they are registered. Nothing
is updated in place: changing a transform's parameter (sea-level pressure,
filter seconds) re-registers that one stream.

**Layout.** Each level is a contiguous array of interleaved `float32` min/max
pairs. This is the same layout a stored pyramid would use
([Out-of-core extension](#out-of-core-extension-stored-pyramids)), so a
pyramid read from disk can replace one built in memory with no change to
`view()`.

### View queries: M4

A view request names a stream, a time window `[t0, t1]` and a column count
`P`, the plot's width in device pixels.

1. **Column boundaries.** Split `[t0, t1]` into `P` equal time intervals. Find
   each boundary's sample index by binary search on the time axis: P + 1
   searches. Because columns are split by time, not by sample count, gaps
   between segments stay where they belong.
2. **Min and max per column.** Combine pyramid buckets over each column's
   index range, taking the largest bucket that fits at each step, and scan
   raw samples only at the unaligned edges. When a column holds fewer than
   about 64 samples, scan it directly. The prototype shows that in that range
   the pyramid walk costs more than the scan (1-minute row in the results).
3. **Times of the min and max.** For the bucket that supplied each extremum,
   descend the pyramid to level 0 and scan its 32 samples to find the sample
   index. That is about 25 steps plus 32 reads, twice per column. No index
   is stored, so the pyramid stays at N/2 bytes.
4. **Emit.** Per column, emit first, then min and max in their time order,
   then last, dropping duplicates. Also emit one sample before `t0` and one
   after `t1`, so lines run off the edge of the plot instead of stopping
   short.

**Why M4.** With exact first, last, min and max per pixel column, a polyline
through those points rasterises to the same pixels as a polyline through
every sample. See Jugel et al.,
[M4: A Visualization-Oriented Time Series Data Aggregation](https://www.vldb.org/pvldb/vol7/p797-jugel.pdf),
PVLDB 7(10), 2014. Min/max-only decimation drops the joins between columns;
LTTB and similar methods drop extremes. Either would be visible on IMU data,
which is spiky.

**Output.** At most 4P + 2 points per stream: about 8,000 for a 2,000-pixel
plot, or 48,000 for six streams. That is 0.4 MB as `double` time plus
`float32` value.

### Exact values and derived streams

**Hover and cursors.** `valueAt(stream, t)` binary-searches the time axis for
the nearest sample. It returns that sample's index and time, and its value
read back from SQLite as `double`, so the readout matches the database
exactly. The per-sample lookup costs microseconds. This assumes each sample
index maps to a rowid: the loader records the rowid of row 0 and checks the
rowids are contiguous, falling back to a stored rowid column otherwise.

**Derived streams.** The magnitudes, compass orientation, altitude and the
activity low-pass filter are computed by the application, not loaded.

- *Registration.* The application registers each result with
  `addDerived()`, giving a table (an existing time axis, shared, or a new
  one) and `double` values. The backend narrows them to `float32` for
  storage and builds the pyramid.
- *Inputs.* Transforms must keep seeing today's inputs, so they read them
  through `forEachRow(table, columns, fn)`, which streams `double` rows
  straight from SQLite rather than from the `float32` copy. This keeps
  `CompassProcessor` and the other sensoranalysis paths bit-identical. For
  IMUTag a streaming pass costs about 1.8 s per table.

### API sketch

```cpp
namespace sensordata {

enum class TimeDomain { EpochSeconds, ElapsedSeconds };
using StreamId = uint32_t;

struct StreamInfo {
    std::string id, label, units, group;  // from the `streams` table
    TimeDomain domain;
    size_t samples;
    double first_time, last_time;
    float min, max;                       // whole stream, from the pyramid root
    bool derived;
};

struct Envelope {                         // ready to draw, in time order
    std::vector<double> time;
    std::vector<float> value;
};

struct Sample { size_t index; double time; double value; };

class Log {
public:
    static std::unique_ptr<Log> open(const std::string &path, std::string &error,
                                     const std::function<void(double)> &progress = {});
    const std::vector<StreamInfo> &streams() const;

    Envelope view(StreamId, double t0, double t1, int columns) const;
    std::optional<Sample> valueAt(StreamId, double t) const;
    std::pair<float, float> range(StreamId, double t0, double t1) const;  // y auto-range

    bool forEachRow(const std::string &table, const std::vector<std::string> &columns,
                    const std::function<void(double time, const double *values)> &fn,
                    std::string &error) const;
    StreamId addDerived(StreamInfo info, StreamId time_from, std::vector<double> values);
    StreamId addDerived(StreamInfo info, std::vector<double> time, std::vector<double> values);
    void removeDerived(StreamId);
};

}  // namespace sensordata
```

`Log` is immutable apart from derived-stream registration, which needs
exclusive access. `view()`, `valueAt()` and `range()` are const and safe to
call concurrently, so a UI can compute views off its main thread.

### sensorviz integration

**As built (phase 2).** The plot was split while adopting the backend, so the
integration differs from the list below in shape:

- `buildPlotSpec()` ([`plot_spec.h`](../../plot_spec.h)) is a pure function
  from the streams, display preferences and a `PlotState` (visible streams,
  custom ranges, UTC offset, title) to a `PlotSpec` of plain data: traces
  naming `StreamId`s, one y axis each with an Auto, Linked or Fixed range,
  the time domain and extent, event markers, metadata-box rows and the title.
  The policy that used to sit inside `rebuildPlot()` lives there, and
  `plotspec_check` checks it against the fixtures without a window.
- `PlotView` ([`plot_view.h`](../../plot_view.h)) is the only code that
  touches QCustomPlot. It draws a spec, refills graphs from `view()` after
  each layout pass, and reports hover, cursor, range and context-menu events
  as signals. A webview front end would replace `PlotView` and reuse the spec
  and the backend.
- MainWindow keeps menus and dialogs, formats the hover readout from
  `valueAt()`, and rebuilds the spec when its state changes. Stream
  visibility still lives in the View menu's actions. Since phase 3,
  `SensorStream` carries no samples.

The proposal as first written:

- **Load.** `loadSensorLog` becomes `sensordata::Log::open` on the existing
  `QtConcurrent` path. `SensorLog` keeps its metadata, but `SensorStream`
  loses its `time`/`value` vectors in favour of a `StreamId`.
- **Plot.** `rebuildPlot()` creates the graphs and axes as now, but fills
  them from `view()`. It refills them on the x axis's `rangeChanged` and on
  resize. Refills are coalesced to one per event-loop pass, so a drag
  produces one refill per frame. QCustomPlot then holds about 8,000 points
  per graph.
- **Readouts and ranges.**
  - The hover tooltip ([`interaction.cpp:379-448`](../../interaction.cpp)) and
    the cursors use `valueAt()`.
  - Fixed y ranges, linked IMU groups and "reset zoom" use `range()` and
    `StreamInfo::min`/`max`, so they don't need to scan data.
- **Print.** The print preview asks for a view at the printer's horizontal
  resolution, so vector output stays exact.

### Webview integration

A Tauri (or similar) shell exposes the same four calls as commands. `view()`
returns its two arrays as one raw binary response, a `Float64Array` and a
`Float32Array`, not JSON. That is 0.4 MB per full six-stream redraw.
uPlot takes those arrays directly. Nothing about the backend changes.

## Analysis

All figures are from [the results log](decimating-backend-results.md),
on an M2 laptop.

### Memory

| | Today | Proposed |
| --- | --- | --- |
| Per sample, per component stream, plotted | 16 B (time + value) + 16 B (QCustomPlot) = 32 B | 8/3 B (shared time) + 4 B (value) + 0.5 B (pyramid) ≈ 7.2 B |
| Worst case, default view | 2.94 GB | about 1.0 GB |
| Worst case, all streams | 4.83 GB | about 1.0 GB |
| Held by the plot | up to 2.8 GB | about 0.1 MB per graph |

The proposed figure is the measured 0.85 GB for the six components, plus the
accel and gyro magnitudes' `float32` values and pyramids (2 × 89 MB). It
scales linearly with N and does not depend on what is visible: hiding
streams saves nothing, but showing them costs nothing either.

### Time

| Operation | Today | Proposed |
| --- | --- | --- |
| Read accel+gyro from SQLite | 27.8 s (six sorted queries) | 3.6 s (two rowid scans) |
| Build pyramids, six streams | — | 0.10 s |
| Open log to first plot | 38–41 s | estimated 5–8 s (load plus the transforms' `double` pass, not yet measured end to end) |
| Full-log view, six streams | QCustomPlot walks 118M points per repaint | 7.2 ms |
| 1-hour view, six streams | | 3.4 ms |
| 1-minute view, six streams | | 2.0 ms (direct scan would be under 1 ms) |

The prototype does not locate the sample times of the min and max (step 3 of
the view query). That adds about 2P descents of about 60 operations each,
roughly 0.2M operations per stream per view, which is under 1 ms but
unmeasured.

**How much the pyramid buys.** A direct scan of a full-log column range
costs 24 ms per stream: about 150 ms for six streams, and 220 ms for nine.
That is usable for a click but too slow to drag. The pyramid brings the full
view to 7 ms for 59 MB of memory, about 7% of the data. The entries touched
stay close to the 2P × 25 bound however long the log is, so the cost holds
for any N.

**When to skip the pyramid.** Below about 64 samples per column, the
pyramid's per-step level search costs more than the samples it skips. At
1 minute, the prototype touched 144,000 entries for 24,000 samples. The
direct-scan cutoff in [View queries](#view-queries-m4) handles this.

### Precision

| Quantity | Tag representation | `float32` error |
| --- | --- | --- |
| Accel, gyro | `int16` × scale | under 0.002 LSB at any scale: full scale is 2^15 LSB and `float32` rounds to 2^-24 relative |
| Mag, pressure | `float32` | none |
| Time | `double` s | below 1 µs over the 13.7 h IMUTag span; about 0.24 µs at epoch magnitudes |

Only plotting and auto-ranging see `float32`. Hover values and transform
inputs are `double` from SQLite, so nothing a user reads or a transform
computes changes.

## Alternatives considered

- **Keep QCustomPlot's adaptive sampling and fix only the loader.** One
  query per table, with the time `QVector` shared across columns by
  copy-on-write. This is a small change to `sqlite_loader.cpp`, and it is
  worth doing first (phase 0 below): load time drops from 28 s to about 4 s,
  and the component vectors from 1.89 GB to 1.26 GB (two time axes and six
  value vectors). It leaves the QCustomPlot
  copies (up to 2.8 GB) and full-data repaints, and gives a webview UI
  nothing to call.
- **M4 by direct scan, no pyramid.** Simpler, and exact, but too slow to drag
  at full zoom (150–220 ms). Because the pyramid sits behind the same
  `view()` call, it could come in a later phase without changing the API.
- **Windowed SQL queries against the log.** This means an index on
  `ElapsedUs` (a writer change, plus about 0.4 GB per table) and a scan of
  the window on every view. A full-log view is a 1.8 s scan, so this is only
  interactive with pre-aggregated tables in the file. That variant is the
  [out-of-core extension](#out-of-core-extension-stored-pyramids) below, and
  it needs no `ElapsedUs` index.
- **DuckDB over the SQLite file.** It expresses M4 neatly as a `GROUP BY`.
  Against that:
  - its SQLite extension reads row by row, so there is no columnar speed
    unless the data is copied in first;
  - derived streams would have to be copied in too;
  - it adds about 10 MB compressed to the package, plus an extension that
    must be statically linked;
  - DuckDB documents a risk with two SQLite copies in one process.

  It is worth reconsidering if ad hoc SQL analysis becomes a sensorviz
  feature.
- **`int16` storage with per-stream scale.** This halves accel and gyro
  values (236 MB instead of 472 MB), but the schema does not carry the scale
  factor, and the other tags' streams are not integer-valued. `float32` is
  general and already well inside the memory goal.

### Out-of-core extension: stored pyramids

Not part of phases 0-2. This records how the backend would grow if logs
outgrow memory, so the in-memory design does not close it off.

**What it is.** The pyramids are written into the log as a separate table,
and sensorviz stops loading raw samples:

1. **Open.** Read only the stored pyramids, about 60 MB for the six worst-case
   streams, and draw the full-log view at once.
2. **Zoom.** Use pyramid levels while columns hold at least about 64 samples.
3. **Below that, fetch raw rows.** Read the visible window straight from
   SQLite by rowid range, `WHERE rowid BETWEEN ? AND ?`. SQLite fetches a
   rowid range directly from the table's own B-tree, so this needs no
   `ElapsedUs` index. The deepest window is at most about 64 × P rows:
   128,000 for 2,000 pixels. The measured scan rate of about 11M rows/s per
   table puts that at roughly 12 ms per table.
4. **Map time to rowid.** Use the bucket times: store the start time of each
   level-0 bucket (N/32 `double`s, 4.9 MB per table). For IMUTag alone, the
   per-segment linear timing in `ImuSegment` would do. A small LRU cache of
   recent raw windows makes panning at full zoom smooth.

**Memory.** About 60 MB of pyramids, plus bucket times, plus a bounded window
cache: roughly 100 MB however large the log is, against about 1 GB for the
in-memory design and 4.8 GB today. Opening needs no pass over the raw data.

**When it pays.**

- NAND larger than 2 Gbit: the in-memory design scales linearly, about 2 GB
  at 4 Gbit.
- Long deployments at lower sample rates, with more segments.
- A browser or webview build where memory is tighter. Pair it with SQLite
  WASM reading the file, or the native backend serving windows.
- Several tools sharing one log without each building pyramids.

For the 2 Gbit worst case it saves nothing that matters. Building the
pyramids in memory took 0.10 s, and the raw samples it avoids loading fit
within the memory goal.

**Derived streams are the obstacle.** IMUTag's default view is the accel,
gyro and field magnitudes, which are computed, not stored. Opening without a
raw pass works only if derived streams have stored pyramids too. Otherwise
sensorviz still makes a full `forEachRow()` pass, about 1.8 s per table,
before the default view can draw.

- **Magnitudes** are a fixed function of the data, so storing them is
  consistent with the rule that the schema describes data
  ([AGENTS.md](../../../../../AGENTS.md#cross-cutting-notes)).
- **Parameterised transforms** depend on user choices and cannot be stored
  ahead. These are altitude from a chosen sea-level pressure, compass
  orientation with a chosen declination, and the activity filter's time
  constant. They stay computed on demand: a full pass when first shown,
  then held in memory.

**Where the pyramids would be computed.**

| Place | Assessment |
| --- | --- |
| tagcore's SQLite writer, during download | Preferred. Log writing lives here ([`sqlitelog/README.md`](../../../../libraries/tagcore/sqlitelog/README.md)). Min/max accumulates while streaming, and every new log gets the pyramids |
| `dataprocessing` command-line tool | Good for adding pyramids to existing logs in bulk. It already writes SQLite and links no Qt |
| sensorviz on first open | Avoid writing into the log. sensorviz opens logs read-only, and a viewer rewriting the user's data file is risky. If wanted, keep a cache file outside the log (per-user cache directory) keyed on the log's size, modification time and row counts |

**Format sketch.** One row per stream and level, with the level stored as a
BLOB, not one row per bucket. 614,400 level-0 buckets per stream would cost
more in SQLite row overhead than the data itself.

```sql
CREATE TABLE stream_pyramid (
    stream_id   TEXT    NOT NULL,  -- matches streams.stream_id
    level       INTEGER NOT NULL,  -- 0 = finest
    bucket_size INTEGER NOT NULL,  -- samples per bucket at this level
    sample_rows INTEGER NOT NULL,  -- rows the pyramid was built from (staleness check)
    format      INTEGER NOT NULL,  -- layout version
    minmax      BLOB    NOT NULL,  -- little-endian float32 min,max pairs
    PRIMARY KEY (stream_id, level)
);
CREATE TABLE stream_bucket_time (
    table_name  TEXT    NOT NULL,
    bucket_size INTEGER NOT NULL,
    first_rowid INTEGER NOT NULL,
    times       BLOB    NOT NULL,  -- little-endian float64 start time of each bucket
    PRIMARY KEY (table_name)
);
```

**Rules for the stored format.**

- *Fallback.* Logs written before this change have no pyramid tables, so the
  in-memory build stays as the fallback, permanently.
- *Staleness.* The loader treats a stored pyramid as a cache. It uses one
  only when `format` is known and `sample_rows` equals the table's row
  count. Otherwise it ignores the stored pyramid and builds in memory.
- *Bit-compatibility.* The stored values must match an in-memory build
  exactly: the same `float32` narrowing and the same bucket boundaries. The
  `sensordata_check` program compares the two on the fixtures.
- *Schema contract.* Adding tables is a writer change, so it updates
  [`sqlite-logs.md`](../../../../docs/src/reference/sqlite-logs.md) and
  goes through the writer rules in the sqlitelog README.

## Testing

Following the standalone assertion-program pattern in
[`tagcore/test`](../../../../libraries/tagcore/test/README.md), with an
option-gated `sensordata_check`:

- `view()` against a brute-force M4 on random and adversarial data:
  spikes at column edges, constant runs, NaN-free extremes at bucket
  boundaries. Compare every column's first, last, min and max, and the
  sample index of each extremum.
- Column boundaries across segment gaps, windows partly or wholly outside the
  data, a single sample, an empty table, and P larger than the sample count.
- `valueAt()` returns exactly the SQLite `double`.
- The out-of-order fallback, on a hand-built table with a non-monotonic
  time column.
- Loading every committed fixture in `host/docs/fixtures/sensorviz/`, with
  stream lists identical to today's loader.

For sensorviz itself:

- Screenshot suites (`--capture-suite imutag`, `compasstag`) before and after,
  compared visually. They should be pixel-identical apart from anti-aliasing.
- The benchmark's two modes rerun on the full log, appended to the results
  log, including an end-to-end `/usr/bin/time -l` of sensorviz.

## Phases

Phases 0 to 3 are built. sensorviz peaks at about 1.1 GB on the worst-case
log whatever is visible, against 4.8 GB with every stream shown before, and
loads in about 6 s against 41 s
([results](decimating-backend-results.md)). Phase 4 is built too.

As built, phase 3 splits the transforms by what their inputs must be. The
IMUTag magnitudes and the acceleration calibration offset are computed from
the backend's `float` columns with `addComputed()`, a display-only difference
of one `float` step from the `double` arithmetic. The magnetometer
calibration also works from the `float`s, which is exact because its
`QVector3D` is float. Altitude, the activity filter and the CompassTag
orientation read SQLite `double`s through `forEachRow()`, so they and every
sensoranalysis input are unchanged.

0. **Loader fix in place.** In `sqlite_loader.cpp`: one query per table,
   ordered by rowid, with the time vector shared by copy-on-write. This is
   independent of everything else and is the fastest win: load drops from
   28 s to about 4 s and memory by 0.6 GB.
1. **`sensordata` library:** loading, pyramid, `view()`, `valueAt()`,
   `range()`, the check program and Doxygen.
2. **Plot adoption in sensorviz:** graphs filled from `view()`, the hover
   readout, cursors, ranges and print.
3. **Derived streams through `addDerived()` and `forEachRow()`:** the
   magnitudes, altitude, activity filter and compass. This lines up with the
   transform registry in [TODO.md](../../TODO.md#larger-refactors).
4. **Optional:** break lines at segment gaps by emitting a gap marker (NaN
   for QCustomPlot, `null` for uPlot). Today's plot joins across gaps.

As built, phase 4 finds gaps from time steps alone: once per time axis at
load, a step larger than four times the axis's median interval is a gap.
`SegmentId` boundaries are not gaps in themselves. On a resync tagcore starts
the new segment at the later of the wall-clock elapsed time and the current
end, so samples lost to a `RESYNC_STORAGE_SKIP` already appear as a forward
jump in `ElapsedUs`, while a plain `RESYNC` is a timestamp correction with
no data lost. `view()` splits columns at gaps and puts a NaN-valued marker
between their sides; `valueAt()` returns nothing inside a gap beyond half an
interval from either side. Breaking is sensorviz display policy: the View
menu's "Break Lines at Gaps", on by default, persisted per tag type only when
turned off.

## Open questions

- **Is a reported 2 Gbit full log the real worst case?** The figure assumes
  every NAND page holds data. Bad-block reserve makes it slightly smaller,
  never larger. A larger NAND on a future board scales everything linearly:
  a 4 Gbit part would be about 2 GB proposed, against 9.7 GB today.
- **Should segment gaps break lines (phase 4)?** Settled: by default yes,
  with a View-menu toggle; see [Phases](#phases).
- **Should the time axis be computed rather than stored?** For IMUTag it is a
  per-segment linear function of sample index, which would save 157 MB per
  table. Storing it keeps the backend tag-agnostic. Revisit only if memory
  becomes a concern again.
