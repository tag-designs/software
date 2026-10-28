---
type: readme
status: current
summary: Tag Calibrator in Rust, the port of qtcalibrate to be shipped as tagcalibrate - its window on the host UI kit, the engine over the C++ calibration session, the Tauri shell, how to run it, and how it is checked against qtcalibrate; CMake and packaging are still to come.
---

# Tag Calibrator (Rust)

A port of [qtcalibrate](../qtcalibrate/README.md), to be shipped as
`tagcalibrate`, built the way [Tag Monitor](../tagmonitor-rs/README.md) and
[Tag Programmer](../tagprogram-rs/README.md) were: a generic web UI on
[`host/ui-kit`](../../ui-kit/README.md), an engine that holds every rule, and
a thin Tauri shell.

**Status: runs against a tag; not yet built by CMake or packaged.** The
calibration is the C++ `CalibrationSession` that qtcalibrate's `CompassData`
runs, reached through [`calibsession-rs`](../../libraries/calibsession-rs/README.md).

| Path | What |
| --- | --- |
| `core/src/engine/` | The engine: `calibrator.rs` (qtcalibrate's `MainWindow` rules), `tagthread.rs` (attach, Stream, the 100 ms calibration-log poll, Tag Write and Read), `view.rs` (the view state and menus), `capture.rs` (qtcalibrate's capture file) |
| `core/tests/` | `replay_log.rs`: the engine's quality log for the CompassTag fixture against qtcalibrate's, line for line; `imutag_axes.rs`: the IMUTag fixture keeps the site's dip |
| `app/` | The Tauri shell |

| Path | What |
| --- | --- |
| `ui/index.html`, `app.js`, `app.css` | The window: Calibrate, Orientation and Log tabs and the Tag Connection bar, drawn with Preact from the engine's view state |
| `ui/views.js` | The three graphics: the sample sphere and the tag model (three.js), and the heading indicator (SVG) |
| `ui/three.min.js` | three.js r159, vendored (see its header) |
| `ui/mock-engine.js` | The browser stand-in for the engine, and for now the draft of the view state and commands |
| `ui/mock-capture.js` | The mock's replay data, from qtcalibrate's fixture and its own computed angles |
| `ui/screenshots.sh` | Pictures of the mock scenarios, for the user manual |
| `tools/make_mock_capture.py` | Rebuilds `mock-capture.js` |

## Documents

- [UI/engine contract](design/ui-engine-contract.md): the threads, the view
  state, the prompts, the commands and queries, the capture file, and where
  this differs from qtcalibrate.
- [Decision 0039](../../../docs/decisions/0039-host-tagcalibrate-rs.md): why
  the calibration was moved into the C++ core rather than ported, and the
  evidence that nothing changed.

## Running it

```sh
export TAGCORE_C_LIB_DIR=<build>/Release/lib   # cmake --build <build> --target tagcore_c_static tagcore_c
cargo run -p tagcalibrate                      # attaches on opening, as qtcalibrate does
TAGCALIBRATE_NO_ATTACH=1 cargo run -p tagcalibrate
TAGCALIBRATE_REPLAY=$PWD/../../docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json \
  cargo run -p tagcalibrate                    # replay a capture, no tag
cargo test -p tagcalibrate-core
```

Attaching resets the tag, and only an IDLE tag is calibrated; one left in
CALIBRATE by a program that exited mid-stream is stopped first. Tag Write is
the one action that changes the tag: it appends a calibration record.

The window alone also opens in a browser: open `ui/index.html`. It needs no server, no network and no
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

CMake and packaging as for Tag Monitor, and a user manual page.
