---
type: design
status: current
summary: How Tag View (sensorViz in Rust) is put together - one engine thread over sensorViz's C++ core through sensorviz-session, a page that draws the plot with uPlot from M4 envelopes, a map of every file and the Qt sensorviz code it follows, how a view change and a redraw travel, and where to make common changes.
---

# Tag View (sensorViz in Rust): architecture and source map

Tag View (`tagview`) is the Qt sensorviz's window in three layers, on the
pattern the other Rust applications share
([Tag Monitor's architecture](../../tagmonitor-rs/design/architecture.md)):

- **`core/`**, the crate `sensorviz-core`: the engine, with no UI toolkit.
  It owns the open log as a [`sensorviz_session::Session`](../../../libraries/sensorviz-session-rs/README.md),
  which is sensorViz's C++ core (`applications/sensorviz/core`, through
  `tagcore_c`'s `svz_*` functions): the log reader (`sensordata`), the
  display transforms, the plot spec, the hover readout and the preference
  file. Everything the Qt application computes, this one computes in the
  same code.
- **`ui/`**: a page on the [UI kit](../../../ui-kit/README.md). It draws the
  core's plot spec with uPlot from envelopes the engine supplies, carries
  the Qt application's menus and dialogs, and exports a PDF with jsPDF.
- **`app/`**, the crate `tagview`: the Tauri shell.

There is no tag thread: Tag View never talks to a tag. It opens files.

## The shape of it

```text
 page (ui/)                                 engine thread (core/src/engine.rs)
 ─────────                                  ──────────────────────────────────
 menu ── command (open, set_visible_streams, ...) ──▶ Viewer::command ── Session setter ── Changes bits
 draws ◀── state (catalog, view, spec, status) ─────  view state rebuilt after every command
 plot ── query envelope {t0, t1, width} ───────────▶ Session::envelope per trace: M4 envelopes (sensordata)
 hover ── query hover {t} ─────────────────────────▶ Session::hover: the readout rows, formatted by the core
 compass panel ── query compass {t} ───────────────▶ the sample at or after t, angles from calibsession::display_angles
 Export PDF ── command save_file {path, base64} ──▶ writes the bytes the page made
```

- **One thread owns all state**, the `Viewer`: the `Session`, the per-tag
  `Preferences`, the status line and the outstanding message. `Session` is
  `Send` and not `Sync`, which this respects by never sharing it.
- **The page asks for data, the engine sends policy.** The view state says
  what to draw (the spec: traces, axes, ranges, title, markers); the page
  asks for the samples it needs at the width it has (`envelope`) and for
  readouts at the pointer (`hover`, `compass`). sensordata's min/max
  pyramids make every redraw a few thousand points whatever the log's size.
- **Every setter returns what changed.** `Session`'s setters return
  `Changes` bits, so the engine republishes only when something did and the
  page redraws only what must.

## Source map

Paths are relative to `host/applications/sensorviz-rs`; the Qt application's
to `host/applications/sensorviz`.

| File | Holds | Follows |
| --- | --- | --- |
| `core/src/lib.rs` | The crate's documentation and the `engine` module | |
| `core/src/engine.rs` | `Engine`, `EngineConfig`, `Event`, `spawn`, the loop; `Viewer`: every command and query (its doc comment tables them), the status line, File Info, preference handling, the PDF save | `mainwindow.cpp` (menus and slots), `dataloading.cpp`, `sensor_preferences.cpp` |
| `core/tests/engine.rs` | The engine on the committed fixtures: descriptions, File Info and the status line, view commands, preference files, failures as messages, saving the PDF | |
| `app/src/main.rs` | The Tauri shell: `tagview [log.db3] [--load-preferences prefs.json]`, `SENSORVIZ_PREVIEW_DIALOG` for screenshots | Tag Calibrator's shell, without the tag |
| `app/build.rs` | Stages `ui/` and the kit; the rpath to a shared `tagcore_c` | |
| `ui/index.html`, `app.js`, `app.css` | The window: the Qt menus and plot context menu, Plot and File Info tabs, the dialogs (Visible Streams, Axis Sides, Colors, Ranges, Calibration Constants), the status line, the compass panel | `mainwindow.cpp`, `controls.cpp`, `stream_actions.cpp` |
| `ui/plot.js` | The plot: builds a uPlot chart from the spec (stacked y axes per side, elapsed or epoch x axis, markers, gap breaks, title, metadata box, legend), fetches envelopes for the visible range and width, the cursors, zoom and pan, and the PDF export at 300 dpi | `plot_view.cpp`, `plotting.cpp`, `interaction.cpp` |
| `ui/mock-engine.js` | The engine's part in a browser preview, over fixture data `tools/export_mock_data.py` writes into `ui/mock/` (generated, not committed) | |
| `ui/uPlot.iife.min.js`, `uPlot.min.css`, `jspdf.umd.min.js` | uPlot 1.6.32 and jsPDF 2.5.2, vendored | |
| `tools/print-spike/` | The plotters prototype from the renderer spike; not shipped, not in the workspace | |
| `CMakeLists.txt`, `cmake/Info.plist.in` | The CMake target (`host_rust_app`) and the per-platform install, installed as `tagview` | |

## How a view change travels: a stream range

1. View ▸ Ranges ▸ *stream* opens the page's range dialog, which first asks
   the engine `range {id}` for where the dialog should start (the core's
   rule, not the page's).
2. The page sends `set_stream_range {id, lower, upper}`.
3. `Viewer::command` calls `Session::set_stream_range`, which applies the
   range and any ranges linked to it (pressure and altitude, say) and
   returns `Changes::RANGES`.
4. The loop rebuilds the view state; the spec's axes now carry the fixed
   range; the page rebuilds the chart's scales and refetches envelopes for
   the new visible range.

## How a redraw travels

1. uPlot reports a new visible time range (zoom, pan, Reset Zoom) or the
   window changes width.
2. `plot.js` asks `envelope {t0, t1, width}`; the engine answers with each
   drawn trace's M4 envelope at that many columns, gaps as `null`.
3. The page merges the traces onto one time array (uPlot needs one),
   interpolating each trace at the others' times along the segment it would
   draw anyway, and calls `setData`, then re-sets the time range.

## Where to change what

| To change | Edit | Then check |
| --- | --- | --- |
| A display transform, the plot policy, the hover text, the preference format | `applications/sensorviz/core` (C++), so the Qt application changes too; then `sensorviz_c.h` and `sensorviz-session` if the interface or JSON changes | the Qt core's checks and `session_cli`; `sensorviz-session`'s tests; `cargo test -p sensorviz-core` |
| A command, a query or the status line | `core/src/engine.rs`; keep the doc-comment tables and `ui/mock-engine.js` in step | `core/tests/engine.rs` |
| How the plot looks or behaves | `ui/plot.js` | the page against the mock (`?log=imutag`, `compasstag`, `big`) |
| A menu or dialog | `ui/app.js` (the structure is the Qt application's) | the mock |
| Printing | `plot.js`'s export (jsPDF) | File ▸ Export PDF in the app |

## Checking a change

```sh
cd host   # the workspace
TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo test -p sensorviz-core
TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo run -p tagview -- docs/fixtures/sensorviz/imutag.db3
cd applications/sensorviz-rs && for n in imutag compasstag; do
  tools/export_mock_data.py ../../docs/fixtures/sensorviz/$n.db3 $n ui/mock/data-$n.js; done
open ui/index.html
```

What the renderer spike showed, and the deliberate differences from the Qt
application, are in the [README](../README.md).
