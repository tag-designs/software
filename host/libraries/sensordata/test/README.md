---
type: readme
status: current
summary: How to build and run sensordata_check, the offline assertion program for the sensordata library, and what it covers.
---

# sensordata Offline Checks

`sensordata_check` is a plain `main()` program with its own `CHECK()` macro,
not a registered test framework, because this repository has no test harness
to register with. It needs no tag and no capture: every case is a small
SQLite log it builds itself, or a committed fixture.

```sh
cmake -DBUILD_SENSORDATA_CHECKS=ON <build-dir>
cmake --build <build-dir> --target sensordata_check
<build-dir>/Release/bin/sensordata_check
```

It prints each failing check with its line, then a count, and exits non-zero
if any failed. Scratch logs go to `$TMPDIR`, or `/tmp`.

## What it covers

Every answer is compared with a brute-force computation that shares no code
with the library: linear scans for column edges, extremes and ranges.

| Area | Cases |
| --- | --- |
| `view()` | Random data with spikes at bucket and power-of-two boundaries, a constant run with a repeated extreme, signed zeros, infinities, a NaN run longer than a bucket, a NULL; two segments 1,000 s apart; windows across, inside and outside the data and the gap, zero-width and empty; 1 to 100,000 columns, and 300 random windows |
| `range()`, `StreamInfo` | Every fixed window above; whole-stream min, max, first and last time |
| `valueAt()` | Both picks against a linear scan, values not representable as `float`, non-contiguous rowids, NaN and infinite times |
| Out-of-order tables | Shuffled times with many ties: stable sort, rowid of each sample, `forEachRow()` order |
| Edge cases | A single row, an empty table, and metadata that must be refused: no `streams` table or rows, a missing table or column, an unknown kind, an incomplete row, an ungrouped or mixed record set |
| Derived streams | `forEachRow()` inputs, `addDerived()` on a shared and on its own axis, rejected lengths and times, `find()` preferring the newest, `remove()`, and `forEachRow()` staying available while any stream uses the table |
| Gaps | Irregular steps with injected gaps (at the start, before the last sample, long, clustered several to a column, random): the gap count and typical interval against an independent implementation of the rule; views in both gap modes over windows that start, end or lie inside a gap; gap markers; inheritance by computed streams; `valueAt()` inside and beside gaps; an irregular but continuous table with no gaps |
| Fixtures | Every log in `host/docs/fixtures/sensorviz/`: the stream list equals the metadata rows with data, in order; times match `ORDER BY time`; views match brute force; `forEachRow()` returns the SQLite doubles |

## `--gaps <path>...`

Prints, for each log, every table's sample count, typical interval and gap
count. The counts for the committed fixtures and other logs are in the
[decimating backend results](../../../applications/sensorviz/design/proposals/decimating-backend-results.md).

## `--log <path>`

Loads a real or synthetic log and prints the load time, the peak memory
footprint (macOS) or peak RSS, the cost of 2,000-column views of the longest
streams over the full log, one hour, one minute and ten seconds, and whether
each view matched brute force. Results for the worst-case IMUTag log are in
the
[decimating backend results](../../../applications/sensorviz/design/proposals/decimating-backend-results.md).
