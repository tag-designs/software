---
type: readme
status: current
summary: The Qt-free sensordata library - columnar loading of tagcore SQLite logs, min/max pyramids, M4 view queries and exact read-back - and how to build and check it.
---

# sensordata Library

`sensordata` loads a tagcore SQLite sensor log for plotting without handing
every sample to the plot. It reads each table once into a compact columnar
layout, keeps a min/max pyramid per stream, and answers each view with an
**M4 envelope**: for every pixel column, the first, last, minimum and maximum
sample. A polyline through the envelope draws the same pixels as one through
every sample, and has at most four points per column.

It depends only on the sqlite3 C API and builds with `BUILD_HOST`, without
`BUILD_QT_APPS`, so command-line tools and a non-Qt front end can use it as
well as sensorviz. The API and its contracts are in
[`sensordata.h`](sensordata.h); the design and the measurements behind it are
in the sensorviz
[decimating backend proposal](../../applications/sensorviz/design/proposals/decimating-backend.md)
and its [results log](../../applications/sensorviz/design/proposals/decimating-backend-results.md).

## Layout

| Item | Type | Cost per sample |
| --- | --- | --- |
| Time axis, one per table, shared by its columns | `double` seconds, epoch or elapsed | 8 B per row |
| Each value column | `float` | 4 B |
| Each pyramid (32-sample base buckets, halving to the top) | `float` min/max pairs | about 0.5 B |

The `float` copy is for drawing only. Exact values come from SQLite:

- `valueAt()` selects a sample by binary search and reads its value back by
  rowid as the stored `double`;
- `forEachRow()` streams `double` rows to computations that must stay
  bit-identical to reading the database directly, such as the
  [sensoranalysis](../sensoranalysis/README.md) inputs.

Display transforms that can work from the `float` copy, such as a vector
magnitude, use `addComputed()` instead: it maps the stored values of streams
on one time axis, sample by sample, into a new stream, reading nothing from
SQLite and holding no `double`s. Its results differ from the same arithmetic on
the `double`s by the `float` rounding of the inputs, about one part in 10^7.

## Using it

```cpp
std::string error;
std::unique_ptr<sensordata::Log> log = sensordata::Log::open(path, error);
const sensordata::StreamId ax = *log->find("imu_ax");

// What to draw for a 2,000-pixel-wide plot of [t0, t1].
const sensordata::Envelope points = log->view(ax, t0, t1, 2000);

// The exact value under the mouse.
const auto sample = log->valueAt(ax, mouse_time, sensordata::Pick::AtOrAfter);

// A display-only magnitude straight from the stored floats.
log->addComputed({/* id, label, units */}, {ax, ay, az}, [](const float *v) {
    return std::sqrt(double(v[0]) * v[0] + double(v[1]) * v[1] + double(v[2]) * v[2]);
});

// A derived stream on the same time axis, computed from exact inputs.
std::vector<double> magnitude;
log->forEachRow("ImuAccel", {"ax", "ay", "az"}, [&](double, const double *v) {
    magnitude.push_back(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
}, error);
log->addDerived({/* id, label, units */}, ax, magnitude);
```

Rules that are easy to miss:

- **Time order.** Tables are read in rowid order, which is time order for
  every log tagcore writes. A table that is not is sorted once in memory,
  stably; `forEachRow()` then delivers rows in that sorted order.
- **Empty tables** contribute no streams.
- **Gaps.** Each time axis is scanned once at load for gaps: steps larger
  than `kGapFactor` (4) times its typical interval, the median step
  (estimated from at most 65,536 evenly spaced steps). Streams on one axis,
  derived ones included, share its gaps. With `GapMode::Break`, the default,
  `view()` never joins samples across a gap and puts a gap marker (index
  `kGapMarker`, value NaN) between its sides, and `valueAt()` returns nothing
  inside a gap beyond half an interval from either side. `GapMode::Join`
  ignores gaps. The rule is tag-agnostic: an IMUTag storage skip shows up as
  a jump in `ElapsedUs`, so segment ids are not consulted.
- **Column names are checked.** SQLite reads a quoted name that matches no
  column as a string literal, so the loader and `forEachRow()` check every
  column exists rather than silently reading zeros.
- **Threads.** The query calls are const and safe to call concurrently;
  `addDerived()` and `remove()` need exclusive access.
- **Derived values** are kept as `float` only, so `valueAt()` on a derived
  stream returns the narrowed value with `Sample::exact` false.

## Checks

`sensordata_check` is a standalone assertion program, built behind an option
and run by hand, following the pattern in
[`tagcore/test`](../tagcore/test/README.md). It compares `view()`, `range()`,
`valueAt()` and `forEachRow()` against brute force on synthetic logs and on
every committed fixture in `host/docs/fixtures/sensorviz/`:

```sh
cmake -DBUILD_SENSORDATA_CHECKS=ON <build-dir>
cmake --build <build-dir> --target sensordata_check
<build-dir>/Release/bin/sensordata_check
```

It writes its scratch logs to `$TMPDIR` (or `/tmp`). With `--log <path>` it
instead loads that log, reports load time and peak memory, times views of the
longest streams over four window sizes, and checks each against brute force.
With `--gaps <path>...` it prints each table's sample count, typical interval
and number of gaps.
That is how the results log's library rows are produced, on the log built by
[`make-full-log.sh`](../../applications/sensorviz/design/proposals/decimation-bench/make-full-log.sh).
