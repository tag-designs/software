---
type: readme
status: current
summary: Tag Calibrator in Rust, the port of qtcalibrate to be shipped as tagcalibrate - its window on the host UI kit, run against a mock engine; the engine and Tauri shell are still to come.
---

# Tag Calibrator (Rust)

A port of [qtcalibrate](../qtcalibrate/README.md), to be shipped as
`tagcalibrate`, built the way [Tag Monitor](../tagmonitor-rs/README.md) and
[Tag Programmer](../tagprogram-rs/README.md) were: a generic web UI on
[`host/ui-kit`](../../ui-kit/README.md), an engine that holds every rule, and
a thin Tauri shell.

**Status: the window only.** `ui/` draws the whole window from a view state
and runs in a browser against a mock engine that replays a recorded capture.
There is no engine, no Tauri shell and no build yet.

| Path | What |
| --- | --- |
| `ui/index.html`, `app.js`, `app.css` | The window: Calibrate, Orientation and Log tabs and the Tag Connection bar, drawn with Preact from the engine's view state |
| `ui/views.js` | The three graphics: the sample sphere and the tag model (three.js), and the heading indicator (SVG) |
| `ui/three.min.js` | three.js r159, vendored (see its header) |
| `ui/mock-engine.js` | The browser stand-in for the engine, and for now the draft of the view state and commands |
| `ui/mock-capture.js` | The mock's replay data, from qtcalibrate's fixture and its own computed angles |
| `ui/screenshots.sh` | Pictures of the mock scenarios, for the user manual |
| `tools/make_mock_capture.py` | Rebuilds `mock-capture.js` |

## Running the window

Open `ui/index.html` in a browser. It needs no server, no network and no
build. `?scenario=` picks a canned state (startup, idle, streaming,
collecting, collected with `&percent=`, orientation with `&pose=`,
prompt-base, alert), `?page=` a tab, and a switcher in the corner changes
the scenario live. `mock-engine.js` lists them all.

## What changes from qtcalibrate

- The graphics come from the web mockup of qtcalibrate's two graphics
  screens: the sphere is three.js but shaded and scaled as magPlot draws
  it, and the QML compass and attitude views are an SVG dial and a three.js
  model.
- The quality figures are a labelled grid rather than Courier columns.
- The menus add a Tag menu (the Calibrate tab's commands and Stream) and a
  Log menu, as in Tag Monitor. qtcalibrate's File ▸ Save Calibration and
  Read Data File have no handlers and are not carried over.
- The Tag Connection bar names the attached tag.

## Next

The engine (on tagcore-rs, with the magcal solver and `sensoranalysis`'s
eCompass reused through FFI rather than ported), the UI/engine contract as
a design document, and the Tauri shell and packaging, as for Tag Monitor.
