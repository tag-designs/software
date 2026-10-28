---
type: readme
status: current
summary: sensorViz in Rust, to replace the Qt sensorviz - so far the plot spike, which settled the renderer (uPlot in the page, from M4 envelopes) and printing (jsPDF, 300 dpi); how to run it and what it showed.
---

# sensorViz (Rust)

The Rust/Tauri sensorViz, step 4 of
[leaving Qt](../sensorviz/design/proposals/leaving-qt.md). Until the C++
core's `Session` exists (steps 1 and 3), this directory holds **the plot
spike** (step 2): the sensorViz plot drawn in the page, against a mock
engine, to settle the renderer before anything depends on it.

| Path | What |
| --- | --- |
| `ui/plot.js` | The plot: draws a PlotSpec with uPlot from the engine's M4 envelopes, with sensorViz's interactions, and exports a PDF |
| `ui/index.html`, `app.js`, `app.css` | A window around it: menus, Plot and File Info tabs, a status line with each redraw's timings |
| `ui/mock-engine.js` | The engine's part: envelopes and hover values as sensordata answers them, a spec as sensorviz_core builds one, derived streams as the transforms make them |
| `ui/uPlot.iife.min.js`, `uPlot.min.css`, `jspdf.umd.min.js` | uPlot 1.6.32 and jsPDF 2.5.2, vendored (see their headers) |
| `tools/export_mock_data.py` | Writes `ui/mock/data-*.js` from the fixture logs (generated, not committed) |
| `tools/print-spike/` | The plotters prototype (not shipped): the same plot drawn by plotters, as SVG and PNG |

## Running the spike

```sh
cd host/applications/sensorviz-rs
for n in imutag compasstag; do
  tools/export_mock_data.py ../../docs/fixtures/sensorviz/$n.db3 $n ui/mock/data-$n.js
done
open ui/index.html            # ?log=imutag (default), compasstag or big
```

`big` is a synthetic log of 5,000,000 samples at 1 kHz with a gap and
event markers. Drag pans time, the wheel zooms it, double-click and
shift-double-click place the cursors, View has Reset Zoom, Zoom to Cursors,
Show Cursors and Break Lines at Gaps, and File ▸ Export PDF writes the view.

## What it showed (2026-10-28)

**uPlot draws the sensorViz plot.** Each sensorViz feature checked: stacked
y axes on both sides, each in its trace's colour, the spec's first axis
outermost, the grid from the innermost; the elapsed time axis and the epoch
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
