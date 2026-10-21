---
type: readme
status: current
summary: How to build and run plotspec_check and viewmodel_check, the Qt-free checks of sensorViz's plot policy and view state against the committed fixtures.
---

# sensorViz Offline Checks

Two checks of `sensorviz_core`, the Qt-free half of sensorViz. Both link only
the core (and through it `sensordata`), so they also prove the core builds
without Qt. They open no window.

Like the other checks in this tree they are plain `main()` programs with their
own `CHECK()` macro, built behind an option and run by hand:

```sh
cmake -DBUILD_SENSORVIZ_CHECKS=ON <build-dir>
cmake --build <build-dir> --target plotspec_check viewmodel_check
<build-dir>/Release/bin/plotspec_check
<build-dir>/Release/bin/viewmodel_check
```

Each prints every failing check with its line, then a count, and exits
non-zero if any failed.

## `plotspec_check`

Checks `buildPlotSpec()` ([`plot_spec.h`](../core/plot_spec.h)), the function
that decides what the plot shows. It opens the committed fixtures in
`host/docs/fixtures/sensorviz/` through `sensordata`, makes their streams with
the built-in display defaults, and checks the spec built for a range of plot
states. Expected ranges are recomputed from the `sensordata` stream summaries,
not taken from the code under test.

| Area | Cases |
| --- | --- |
| Traces and axes | State order, unknown ids ignored, one axis per trace, plot data handles, pen widths |
| Linked ranges | The IMU accel family linked to the padded union of its ranges; a custom range on one member leaving the other two linked; a lone member auto-scaling; independence from listing order |
| Fixed and auto ranges | Activity and voltage defaults, a custom range overriding a default, pressure auto-scaling on its data range |
| Preferences | Colour and side overrides |
| Time and markers | The time extent over all traces; event markers only with traces visible |
| Metadata box and title | The start row on elapsed-time logs only, sea level with altitude, declination with heading, a hidden title, the gap mode |
| Formatting | The collection-start text (UTC, rounded to the second) and range padding |

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
