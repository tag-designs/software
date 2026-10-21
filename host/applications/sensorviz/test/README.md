---
type: readme
status: current
summary: How to build and run plotspec_check, the window-free check of sensorViz's plot policy (buildPlotSpec) against the committed fixtures.
---

# sensorViz Offline Checks

`plotspec_check` checks `buildPlotSpec()` ([`plot_spec.h`](../plot_spec.h)),
the function that decides what the plot shows. It loads the committed fixtures
in `host/docs/fixtures/sensorviz/` with the real SQLite loader and checks the
spec built for a range of plot states. It opens no window and links no
QCustomPlot.

Like the other checks in this tree it is a plain `main()` with its own
`CHECK()` macro, built behind an option and run by hand:

```sh
cmake -DBUILD_SENSORVIZ_CHECKS=ON <build-dir>
cmake --build <build-dir> --target plotspec_check
<build-dir>/Release/bin/plotspec_check
```

It prints each failing check with its line, then a count, and exits non-zero
if any failed.

| Area | Cases |
| --- | --- |
| Traces and axes | State order, unknown ids ignored, one axis per trace, plot data handles, pen widths |
| Linked ranges | The IMU accel family linked to the padded union of its ranges; a custom range on one member leaving the other two linked; a lone member auto-scaling; independence from listing order |
| Fixed and auto ranges | Activity and voltage defaults, a custom range overriding a default, pressure auto-scaling on its data range |
| Preferences | Colour and side overrides |
| Time and markers | The time extent over all traces; event markers only with traces visible |
| Metadata box and title | The start row on elapsed-time logs only, sea level with altitude, declination with heading, a hidden title |

Expected ranges are recomputed from the `sensordata` stream summaries, not
taken from the code under test. What the spec looks like drawn is checked by
comparing the documentation capture suites before and after a change; see
[Documentation Capture Hooks](../README.md#documentation-capture-hooks).
