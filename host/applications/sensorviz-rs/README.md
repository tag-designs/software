---
type: readme
status: current
summary: sensorViz in Rust, the Tauri port of the Qt sensorviz over sensorViz's C++ core - the engine, the shell and the uPlot page; how to build and run it, how it differs from the Qt app, what is still to do, and what the plot spike that chose uPlot and jsPDF showed.
---

# sensorViz (Rust)

The Rust/Tauri sensorViz, step 4 of
[leaving Qt](../sensorviz/design/proposals/leaving-qt.md): the Qt sensorviz's
window over the same C++ core, which it reaches through
[`sensorviz-session`](../../libraries/sensorviz-session-rs/README.md). Built
as Tag Calibrator is ([`tagcalibrate-rs`](../tagcalibrate-rs/README.md)): a
toolkit-free engine, a thin Tauri shell, and a page on the shared
[UI kit](../../ui-kit/README.md).

| Path | What |
| --- | --- |
| `core/src/engine.rs` | The engine: one thread owning the open log's `Session` and the per-tag `Preferences`; commands in, the whole view state out after every change, and the `envelope`, `hover`, `range` and `about` queries. Its doc comment lists every command and query |
| `core/tests/engine.rs` | The engine on the committed fixtures: descriptions, File Info and the status line, view commands, preference files, failures as messages, saving the PDF |
| `app/` | The Tauri shell (`main.rs`), its build script (stages `ui/` and the kit), configuration, capabilities and icons |
| `ui/index.html`, `app.js`, `app.css` | The window: the Qt app's menus and plot context menu, Plot and File Info tabs, the dialogs, the status line |
| `ui/plot.js` | The plot: draws the core's PlotSpec with uPlot from the engine's M4 envelopes, with sensorViz's interactions, and exports a PDF |
| `ui/mock-engine.js` | The engine's part in a browser preview, on fixture data `tools/export_mock_data.py` writes into `ui/mock/` (generated, not committed, not shipped) |
| `ui/uPlot.iife.min.js`, `uPlot.min.css`, `jspdf.umd.min.js` | uPlot 1.6.32 and jsPDF 2.5.2, vendored (see their headers) |
| `tools/print-spike/` | The plotters prototype from the spike (not shipped) |

## Building and running

```sh
cmake -DBUILD_TAGCORE_C=ON <build-dir>
cmake --build <build-dir> --target tagcore_c_static tagcore_c
export TAGCORE_C_LIB_DIR=<build-dir>/Release/lib
cargo test -p sensorviz-core
cargo run -p sensorviz -- [log.db3] [--load-preferences prefs.json]
```

The page also opens as a file in a browser, against the mock:

```sh
for n in imutag compasstag; do
  tools/export_mock_data.py ../../docs/fixtures/sensorviz/$n.db3 $n ui/mock/data-$n.js
done
open ui/index.html            # ?log=imutag (default), compasstag or big
```

## What it does

Everything the Qt app computes comes from the core, so the numbers are the
Qt app's: the streams and transforms (altitude, IMU magnitudes, compass), the
plot spec (axes, groups, fixed and linked ranges, metadata box, title), the
hover readout, the preference file, File Info and the status line. The menus
are the Qt app's -- File (Load, Preferences ▸ Load / Store / Load Defaults,
Export PDF), View (Visible Streams, Axis Sides, Colors, Ranges ▸ per stream,
Reset Zoom, Zoom to Cursors, Show Cursors, Break Lines at Gaps, Calibration
Constants), Configuration (Graph Title, Show Graph Title, UTC Offset, then one
item per transform parameter, built from the core's `ParameterSpec`s), Help --
and the plot's context menu repeats View and Configuration.

How it differs from the Qt app:

- **Auto axes** take the data range padded by 5%. The Qt plot also widens an
  auto axis to include the range the axis showed before, and a new
  QCustomPlot axis starts at 0 to 5, so there every auto axis includes zero
  (an IMUTag altitude axis spans -100 to 350 m around 245 m). Kept on
  purpose (owner's decision, 2026-10-28): the trace fills its axis.
- **Printing** is File ▸ Export PDF (below), not the system print dialog.
- **The x axis title** is one line; uPlot draws one.
- **No activity filter**: it stays in the Qt app
  ([toolkit-neutral core](../sensorviz/design/proposals/toolkit-neutral-core.md), revised 2026-10-28).
- **Axis Sides** lists streams, not axis groups.

For a compass log the Plot page has the Qt app's compass panel beside the
plot: the heading dial (the kit's `HeadingDial`, shared with Tag Calibrator)
and the heading, roll, pitch, dip, field, gravity, declination and battery
direction of the sample at or after the hover time (the engine's `compass`
query, from the core's display angles, with declination and battery
direction applied as MyCompass.qml applies them).

Still to do (step 4b): screenshots, packaging and the user manual page.

## The plot spike (2026-10-28)

What the spike that chose the renderer showed, before the engine existed.

**uPlot draws the sensorViz plot.** Each sensorViz feature checked: stacked
y axes on both sides, each in its trace's colour, stacked outward in the
spec's order with the grid from the innermost; the elapsed time axis and the epoch
one with two-line `HH:MM` over `MM/DD/YY` labels in UTC or an offset; drag to
pan, the wheel to zoom, the cursors and Zoom to Cursors; event markers; gap
breaks; the title; the draggable metadata box and legend; the hover readout.

Redraws on the 5-million-sample log took 6-22 ms end to end, almost all of it
the mock's linear envelope scan in JavaScript; uPlot's own drawing was under a
millisecond. The real engine answers from sensordata's min/max pyramids.

Three things uPlot needs, all in `plot.js`:

- **One time array for all traces.** uPlot breaks a line at any missing value,
  so a trace's entries at the other traces' times are filled by linear
  interpolation between its own neighbours -- points on the segment it draws
  anyway -- and only the engine's gap markers become `null`.
- **Re-setting the time range after `setData(..., false)`**, which keeps the
  scales but not the visible index ranges.
- **Drawing large rather than at a high pixel ratio.** uPlot 1.6 has one
  pixel ratio, the screen's, for every chart, so a print chart is drawn k
  times larger at ratio 1 with every size scaled by k.

**Printing: jsPDF, at print resolution.** File ▸ Export PDF draws the view
again offscreen at A4 landscape and 300 dpi, from envelopes fetched for that
width, and places it as one image; the title, metadata box and legend are PDF
text at their places on screen. About 0.7 s and 0.4-1 MB a page. The plot is
a 300 dpi bitmap, the text vector.

Rejected for printing: the browser's print, which prints the screen's canvas
bitmap, unscaled, across two pages, and cannot wait for a refill at print
width; and plotters, which works (`tools/print-spike`, 55 ms for an SVG) but
is a second renderer, with the stacked axes drawn by hand, since its chart
builder has only two y axes. Vector printing is left for later: jsPDF's
vector `context2d` under uPlot, or plotters in the engine for print alone.
