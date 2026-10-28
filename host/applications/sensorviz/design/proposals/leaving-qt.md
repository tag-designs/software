---
type: proposal
status: accepted
summary: The path for sensorviz, the last Qt-only host application, to a Rust/Tauri front end on the shared UI kit - finish the Qt-free C++ core first (toolkit-neutral-core phases B-F) with the Qt app shipping throughout, a plot spike that chose uPlot in the page and jsPDF for printing, then a C interface, the Tauri app, and retiring Qt from the packages.
---

# sensorviz: Leaving Qt

**Status:** accepted 2026-10-28. Step 2, the plot spike, is done
([results](../../../sensorviz-rs/README.md#what-it-showed-2026-10-28)): uPlot
for the plot, jsPDF for printing. Step 1 is under way: phases B and C of
the toolkit-neutral core, the log reader and preference files, are built.

sensorviz is the last host application with no non-Qt version: Tag
Programmer, Tag Monitor and Tag Calibrator each have a Rust/Tauri port on the
shared [UI kit](../../../../ui-kit/README.md)
([0035](../../../../../docs/decisions/0035-host-tagprogram-rs.md),
[0037](../../../../../docs/decisions/0037-host-tagmonitor-rs.md),
[0039](../../../../../docs/decisions/0039-host-tagcalibrate-rs.md)). Once
sensorviz has one, the packages can drop Qt entirely: the Qt applications,
`sensorui`, QCustomPlot, sensoranalysis's Qt adapters, the Qt licence notices
and aqt in CI.

sensorviz is the best prepared of the four. Its samples, decimation and
hover values are in the Qt-free [`sensordata`](../../../../libraries/sensordata/README.md);
its plot policy and view state are in `sensorviz_core`
([toolkit-neutral core](toolkit-neutral-core.md), phase A); QCustomPlot is
confined to `plot_view.cpp`; and the compass display convention, which that
proposal's phase E needed copied from Qt, is now in the core
(`compasscore::displayAngles()`, parity-checked against Qt's own code).

## Decision

**A Rust/Tauri app on the UI kit, over a C++ core the Qt app also runs** --
the pattern Tag Calibrator used: the logic moved into the C++ core, shown
unchanged at each step, and a thin Rust engine over it.

Alternatives weighed (2026-10-28):

| Option | Why not |
| --- | --- |
| Stay on Qt | sensorviz alone would keep Qt in every package |
| A C++ webview (Saucer, webview/webview) over the core | A second shell technology beside the other three apps' Tauri |
| A native Rust GUI (egui with egui_plot, iced, Slint) | A different look and stack; Slint was declined earlier |
| Port the view model and transforms to Rust | Duplicates logic the Qt app needs during the overlap; transforms call sensoranalysis and `sensordata` callbacks, awkward across FFI; Rust may use C++ rather than port it |
| A Rust plot library for the interactive plot | plotters draws static images (every hover would be a round trip to Rust, or a WebAssembly build in the page); egui_plot needs egui; plotly.rs and charming generate JavaScript anyway |

**The plot is drawn in the page.** `sensordata` already reduces each redraw
to at most four points per pixel column, so the renderer draws a few thousand
points per trace whatever the log's size; what remains is interaction (hover,
cursors, zoom, dragging the metadata box and legend), which belongs where the
mouse is. uPlot is the candidate, to be confirmed by the spike; a small
hand-written canvas renderer is the fallback.

**Printing and export are a PDF made in the page with jsPDF** (decided after
the spike, 2026-10-28): the view drawn again offscreen by uPlot at the page's
size and 300 dpi, from envelopes fetched for that width -- as `PlotView`
refills for Qt printing today -- placed as one image, with the title,
metadata box and legend as PDF text. The plot itself is a bitmap. The spike
also drew the plot with plotters, which works but is a second renderer, and
showed the browser's own print to be unusable for a canvas plot. Vector
printing is to be investigated later: jsPDF's vector `context2d` under uPlot
first, plotters in the engine for print alone if that fails.

## Steps

Each step leaves the Qt app shipping.

1. **Finish the C++ core** -- [toolkit-neutral core](toolkit-neutral-core.md)
   phases B-F: the stream catalog and loader (B), preferences byte-compatible
   (C), the transform registry with parameters as data (D), the compass
   transform on the core with a decision record superseding part of
   [0030](../../../../../docs/decisions/0030-host-orientation-stored-as-quaternion-only.md)
   (E), and the `Session` facade with `session_cli` (F). Checked as that
   proposal says: capture suites pixel-identical, hover readouts and
   preference bytes identical.
2. **The plot spike**, early and in parallel: a page on the UI kit drawing the
   fixture logs with uPlot from envelopes, against a mock engine, checking each
   QCustomPlot feature sensorviz uses -- the epoch (with UTC offset) and
   elapsed time axes, several y axes on both sides with shared group axes, gap
   breaks, two cursors and zoom to cursor, event markers, the title, the
   draggable metadata box and legend, the hover readout, drag zoom and reset --
   and redraw speed on a multi-million-sample stream; and a plotters rendering
   of the same plot for print. It settles the renderer before step 4 depends
   on it.
3. **A C interface to `Session`** in `tagcore_c` (`svz_*`): the spec and hover
   rows as JSON, envelopes as float arrays; and a Rust crate over it. Merge
   `taglogwriter` and `calibsession` into one crate over `tagcore_c` at the
   same time, since only one crate in a build may declare `links =
   "tagcore_c"`.
4. **The Tauri app** (`sensorviz-rs`), as Tag Monitor is built: an engine with
   the menus, prompts and the generic transform-parameter form from the core;
   a page drawing the plot and reusing Tag Calibrator's heading dial and tag
   model for the compass panel; envelopes as binary IPC responses (about
   0.4 MB for a six-stream full redraw); a browser mock and `screenshots.sh`
   in place of the Qt capture hooks.
5. **Ship it beside the Qt sensorviz**, then retire the Qt applications one by
   one; with the last, remove Qt from the packages and CI.

## Open

- Vector printing (see above).
- Whether to retire all four Qt applications, or keep them available for a
  time after their Rust versions ship.
