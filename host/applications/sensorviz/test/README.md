---
type: readme
status: current
summary: How to build and run catalog_check, plotspec_check, preference_check, transforms_check and viewmodel_check, the Qt-free checks of sensorViz's log reader, plot policy, preference files, display transforms and view state.
---

# sensorViz Offline Checks

Five checks of `sensorviz_core`, the Qt-free half of sensorViz. They link
only the core (and through it `sensordata` and sensoranalysis's Qt-free
core; `catalog_check` also sqlite3), and are defined in
[`core/CMakeLists.txt`](../core/CMakeLists.txt), so they build in a host build
without `BUILD_QT_APPS` and prove the core needs no Qt. They open no window.

Like the other checks in this tree they are plain `main()` programs with their
own `CHECK()` macro, built behind an option and run by hand:

```sh
cmake -DBUILD_SENSORVIZ_CHECKS=ON <build-dir>
cmake --build <build-dir> --target catalog_check plotspec_check preference_check \
    transforms_check viewmodel_check
<build-dir>/Release/bin/catalog_check
<build-dir>/Release/bin/plotspec_check
<build-dir>/Release/bin/preference_check
<build-dir>/Release/bin/transforms_check
<build-dir>/Release/bin/viewmodel_check
```

Each prints every failing check with its line, then a count, and exits
non-zero if any failed.

## `catalog_check`

Checks `openLog()` ([`stream_catalog.h`](../core/stream_catalog.h)), which
reads a log into a `StreamCatalog`. It opens the committed fixtures, then
edits a temporary copy of the IMUTag fixture (in the system temporary
directory, removed afterwards) to reach what no fixture holds.

| Area | Cases |
| --- | --- |
| Description | Tag type and profile name, `info` rows, time domain, collection start (IMUTag header; earliest epoch sample), the `compass_raw` record set's table, columns and time source |
| Calibration | The newest row read, its epoch and constants; an empty table, text that is not JSON, and JSON with no magnetometer each leaving no calibration and the expected warning |
| IMU corrections | Every magnetometer sample calibrated with `compasscore::applyMagnetometer()`, and every acceleration sample less the zero-g offset, compared bit for bit with the stored rows corrected independently; uncorrected when there is no calibration |
| Events | `RESYNC*` and `RESTART_RECOVERY`, in any case, becoming the `imu_resync` marker in time order; other events ignored |
| Failure | A missing file refused with the open error, the caller's catalog untouched |

When `openLog()` replaced the Qt loader, a one-off comparison of the two over
every log on hand (54, five tag types) found every field and every envelope
point identical; see the [toolkit-neutral core](../design/proposals/toolkit-neutral-core.md)
proposal.

## `plotspec_check`

Checks `buildPlotSpec()` ([`plot_spec.h`](../core/plot_spec.h)), the function
that decides what the plot shows. It opens the committed fixtures in
`host/docs/fixtures/sensorviz/` through `sensordata`, makes their streams with
the built-in display defaults, and checks the spec built for a range of plot
states. Expected ranges are recomputed from the `sensordata` stream summaries,
not taken from the code under test.

| Area | Cases |
| --- | --- |
| Traces and axes | State order, unknown ids ignored, one shared axis per group and one per ungrouped stream, each axis's member traces, plot data handles, pen widths |
| Shared axes | The IMU accel group linked to the padded union of its ranges and labelled with the group name; the first member's custom range fixing the whole axis; a lone member with its own label, auto-scaling; a magnitude joining its group, its fixed default range kept alone and dropped when shared; the first side override placing the axis; different units never sharing; independence from listing order |
| Fixed and auto ranges | Activity and voltage defaults, a custom range overriding a default, pressure auto-scaling on its data range |
| Preferences | Colour and side overrides |
| Time and markers | The time extent over all traces; event markers only with traces visible |
| Metadata box and title | The start row on elapsed-time logs only, sea level with altitude, declination with heading, a hidden title, the gap mode |
| Formatting | The collection-start text (UTC, rounded to the second) and range padding |

## `preference_check`

Checks `PreferenceStore` and `colorFromString()`
([`preference_store.h`](../core/preference_store.h)). The expected file text
and colors are what Qt's `QJsonDocument` and `QColor` produced for the same
input, taken when the store replaced the Qt code.

| Area | Cases |
| --- | --- |
| Writing | A two-tag file with every kind of override byte for byte, an empty store, escaped strings, and keys above U+FFFF ordered before U+E000 as UTF-16 orders them |
| Reading | The written file read back and rewritten identically; no version; sides trimmed in any case; color names; entries without overrides dropped |
| Entries | `remember()` storing and removing, `forget()` |
| Refusals | Each error and its message, the first fault in key order, and the store left unchanged |
| Colors | Hex forms including 12- and 16-bit channels rounded as Qt rounds them, names ignoring case, spaces and tabs, and the strings Qt refused |

When the store replaced the Qt code, a one-off comparison against that code
found identical output for 3,000 random preference sets and identical results
for 63,000 loads of valid and mutated files, apart from the malformed-JSON
differences listed in the
[toolkit-neutral core](../design/proposals/toolkit-neutral-core.md#phase-c-as-built)
proposal, and identical colors for about 300,000 strings.

## `transforms_check`

Checks the display transforms and `TransformRegistry`
([`transforms.h`](../core/transforms.h)). It opens the fixtures with
`openLog()`, sets up a `ViewModel` as sensorViz does after a load, and runs
the registry through a sink that adds each output to the `ViewModel` and
records it.

| Area | Cases |
| --- | --- |
| Applicability | IMUTag: altitude and magnitudes, never the compass; CompassTag: the compass, no altitude |
| Outputs | Which streams each fixture gets, in publishing order, shown or hidden |
| Values | Every acceleration-magnitude sample against its three stored axes; every compass sample against `compasscore::solveOrientation()` of its raw row; heading and pitch against `compasscore::displayAngles()` |
| Parameters | Sea-level pressure remaking only altitude and carrying a pressure range over; declination and battery direction remaking only the heading, keeping its visibility, by the right amount; compass parameters changing nothing on IMUTag; a new run starting again |
| Specs | The parameter specs the dialog and a web form are built from, and the `ViewModel`'s parameters by id |

Whether the outputs equal those of the Qt code they replaced is a question
for sensorViz's `--dump-state`
([State dump](../README.md#state-dump)); this check holds the contracts.
When the transforms moved, the dumps of 51 logs and two `dataprocessing`
outputs were identical before and after.

## `viewmodel_check`

Checks `ViewModel` ([`view_model.h`](../core/view_model.h)), the view state
behind the menus, with synthetic streams.

| Area | Cases |
| --- | --- |
| Streams | Load defaults, a derived stream listed after its inputs, replacement in place with the old plot data reported, removal clearing the stream's range |
| Visibility | Default visibility, derived streams always shown, unknown ids ignored, drawing order |
| Changes | Every setter returns, and reports to the listener, exactly what changed, and nothing when nothing did |
| Ranges and appearance | Explicit and linked ranges, clearing, color and side overrides over defaults |
| Session settings | Title, UTC offset, cursors, gap mode and transform parameters, kept across a new log |
| Preferences | What is captured (including the visibility rule that counts derived streams' defaults, kept so saved files match earlier versions), what is applied, unknown ids dropped, round trips, empty preferences restoring defaults |
| Plot | The PlotState and PlotSpec built from the state |

What the spec looks like drawn, and the Qt adapter around the ViewModel, are
checked by comparing the documentation capture suites before and after a
change; see
[Documentation Capture Hooks](../README.md#documentation-capture-hooks).
