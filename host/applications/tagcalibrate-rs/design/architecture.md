---
type: design
status: current
summary: How Tag Calibrator in Rust is put together - the engine and tag threads, where the calibration arithmetic runs (the C++ session, through calibsession), a map of every file and the qtcalibrate code it follows, how a sample and a command travel, and where to make common changes.
---

# Tag Calibrator (Rust): architecture and source map

Tag Calibrator (`tagcalibrate`) is qtcalibrate's behaviour in three layers,
on the pattern Tag Monitor set
([its architecture](../../tagmonitor-rs/design/architecture.md)):

- **`core/`**, the crate `tagcalibrate-core`: every rule and all state, with
  no UI toolkit. There is no `model/` directory as in Tag Monitor, because
  the model of a calibration is the calibration session itself, which is
  C++: `calibcore::CalibrationSession` in `sensoranalysis/core`, reached
  through the [`calibsession`](../../../libraries/calibsession-rs/README.md)
  crate and `tagcore_c`. The Rust engine owns a `Session` and drives it as
  qtcalibrate's `CompassData` did.
- **`ui/`**: a page on the [UI kit](../../../ui-kit/README.md) that draws the
  view state, including the sample sphere and the tag model (three.js) and
  the heading dial (SVG), and sends commands. It holds no rules and computes
  nothing about the tag; even the sphere's rotation is drawing, not a rule.
- **`app/`**, the crate `tagcalibrate`: the Tauri shell.

The protocol between engine and page is the
[UI/engine contract](ui-engine-contract.md); why the calibration was moved
into C++ rather than ported is [decision 0039](../../../../docs/decisions/0039-host-tagcalibrate-rs.md).

## The shape of it

```text
 page (ui/)                    engine thread (engine/calibrator.rs)          tag thread (engine/tagthread.rs)
 ─────────                     ────────────────────────────────────          ───────────────────────────────
 click ── command ───────────▶ Calibrator::command ── TagCmd ──────────────▶ attach, Stream on/off, Tag Write/Read
                                 feeds samples to calibsession::Session       reads the calibration log every 100 ms
 draws ◀── state (whole JSON) ── view.rs builds the view state   ◀── Report ── samples, status, constants
 dialog ◀── state.prompt         quality tick every 200 ms (replay: every 2 samples)
 answer ── command "answer" ──▶ continues what the prompt interrupted
```

- **One owner per thing.** The tag thread is the only code that touches the
  `tagcore::Tag`. The engine thread owns the `Session`, the sample capture,
  the display settings, the prompt and the timers.
- **The arithmetic is not here.** `Session::add_sample`, `update_quality`,
  `orientation` and the rest run the C++ code qtcalibrate runs; the engine
  only decides when to call them and formats their results as qtcalibrate's
  labels did.
- **A replay needs no tag.** `TAGCALIBRATE_REPLAY=<capture.json>` makes the
  engine feed a saved capture one sample per tick, so the whole engine runs,
  and is tested, without hardware.

## Source map

Paths are relative to `host/applications/tagcalibrate-rs`; qtcalibrate's to
`host/applications/qtcalibrate`.

| File | Holds | Follows |
| --- | --- | --- |
| `core/src/lib.rs` | The crate's documentation and the `engine` module | |
| `core/src/engine/mod.rs` | `Engine` (the shell's handle), `EngineConfig`, `Msg`, `Event`, `spawn`, the engine loop, `UiLogger` | `mainwindow.cpp` (wiring), as Tag Monitor's `engine/mod.rs` |
| `core/src/engine/calibrator.rs` | `Calibrator`: the engine thread's state; every command, report and timer; the prompts and what follows them | `MainWindow`'s slots and `CompassData`'s use of the session |
| `core/src/engine/tagthread.rs` | `TagCmd` in, `Report` out; attach, the stream (calibrate state, 100 ms calibration-log reads), the 1 s status poll that keeps the tag awake, Tag Write and Read, the wait for IDLE after Stop | `MainWindow`'s sample timer and tag calls |
| `core/src/engine/view.rs` | The view state JSON: link bar, actions, calibration constants, quality figures, sphere points, orientation readouts, settings, prompt; the `menus` query | `calibration_update`, `updateMagQualityDisplay`, `MyCompass.qml`'s formatting |
| `core/src/engine/capture.rs` | qtcalibrate's capture file: reading one for replay, writing one as `saveSampleCapture` does | `saveSampleCapture`, `--replay-capture` |
| `core/tests/replay_log.rs` | Replays the CompassTag fixture and compares the engine's four quality log lines with qtcalibrate's, line for line | |
| `core/tests/imutag_axes.rs` | The IMUTag fixture keeps the site's dip, so a firmware axis mapping regression is caught | |
| `app/src/main.rs` | The Tauri shell: engine, `command` and `query`, `state` and `log` events, quit through the engine, the user guide, App Nap | Tag Monitor's shell |
| `app/build.rs` | Stages `ui/` and `host/ui-kit/` for Tauri; the rpath to a shared `tagcore_c` | Tag Monitor's |
| `ui/index.html`, `app.js`, `app.css` | The window: Calibrate, Orientation and Log tabs and the Tag Connection bar, rendered with Preact from the state | |
| `ui/views.js` | The three graphics: sphere and tag model (three.js), heading dial (the kit's `HeadingDial`) | `magplot.cpp`, `MyAttitude.qml`, `MyCompass.qml` |
| `ui/mock-engine.js`, `ui/mock-capture.js` | The browser stand-in and its replay data; `tools/make_mock_capture.py` rebuilds the latter | |
| `ui/three.min.js` | three.js r159, vendored | |
| `CMakeLists.txt`, `cmake/Info.plist.in` | The CMake target (`host_rust_app`) and the per-platform install, as Tag Monitor's | |

## How a sample travels

1. With Stream on, the tag thread calls `Tag::calibration_log` every 100 ms
   and reports each `SensorData` as a `Report::Sample`.
2. `Calibrator::report` passes the magnetometer and accelerometer vectors to
   `Session::orientation` (the live compass, always) and, while collecting,
   to `Session::add_sample`, and appends the sample to the capture.
3. Every 200 ms the quality timer calls `Session::update_quality` and logs
   the four `DEBUG` lines qtcalibrate logs.
4. The loop rebuilds the view state from the session (`constants`,
   `quality`, `accel_fit`, `points`, the orientation) and sends it if it
   changed; the page redraws the figures and turns the sphere.

## How a command travels: Tag Write

1. The page sends `tag_write`; `Calibrator::command` checks the action is
   enabled (a fit exists, a tag is attached, not streaming).
2. The engine builds `CalibrationConstants` from `Session::constants` and the
   accelerometer offset and sends `TagCmd::WriteConstants`.
3. The tag thread calls `Tag::write_calibration`, which the firmware accepts
   only in IDLE, and reports the outcome; a refusal becomes an alert prompt.

## Where to change what

| To change | Edit | Then check |
| --- | --- | --- |
| The calibration itself (solver, retention, quality, compass) | `sensoranalysis/core` (C++), then `tagcore_c`'s `tcs_*` if the interface changes, then `calibsession` | `calibration_dump`, `calibsession`'s replay test, `core/tests/replay_log.rs` |
| When a command is enabled, or what a prompt does | `engine/calibrator.rs` | the mock in a browser; `cargo test -p tagcalibrate-core` |
| A text or figure the window shows | `engine/view.rs`, then `ui/app.js`; keep `ui/mock-engine.js` and the contract in step | the mock |
| The stream, the status poll or the stop-and-wait | `engine/tagthread.rs` | a tag on the bench |
| A graphic | `ui/views.js` | the mock's `orientation` and `collected` scenarios |

## Checking a change

```sh
cd host   # the workspace
TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo test -p tagcalibrate-core
TAGCALIBRATE_REPLAY=$PWD/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json \
  TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo run -p tagcalibrate
open applications/tagcalibrate-rs/ui/index.html      # the page against the mock
```
