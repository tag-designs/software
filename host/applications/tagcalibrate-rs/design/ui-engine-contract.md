---
type: design
status: current
summary: The contract between Tag Calibrator's Rust engine and its web UI - the threads, where the calibration runs, the view state the engine publishes, the prompts that replace qtcalibrate's dialogs, and the commands and queries the UI sends.
---

# Tag Calibrator (Rust): UI/engine contract

Tag Calibrator in Rust is split as [Tag Monitor](../../tagmonitor-rs/design/ui-engine-contract.md)
is:

- **`core/`** (`tagcalibrate-core`): the engine (`src/engine`). No UI toolkit.
  Every rule and all state are here.
- **`ui/`**: a page on [`host/ui-kit`](../../../ui-kit/README.md) that draws
  the view state -- including the sample sphere, the heading indicator and
  the tag model -- and turns clicks into commands. It holds no rules and
  computes nothing about the tag.
- **`app/`**: the Tauri shell, which runs the engine, forwards commands and
  queries, emits the engine's events, and turns a window close into `quit`.

The behaviour is qtcalibrate's (`MainWindow`, and `CompassData` beneath it).
The calibration itself is not ported: it is the C++
`calibcore::CalibrationSession` that qtcalibrate's `CompassData` now runs, in
sensoranalysis's Qt-free core, reached through `tagcore_c`'s `tcs_*` functions
and the [`calibsession`](../../../libraries/calibsession-rs/README.md) crate.
Where this port differs from qtcalibrate, it says so below.

## Threads

| Thread | Owns | Does |
| --- | --- | --- |
| tag (`engine/tagthread.rs`) | the `tagcore::Tag` | attaches; while streaming, reads the calibration log every 100 ms (qtcalibrate's sample timer); otherwise reads the status every second, which keeps the tag awake; writes and reads constants |
| engine (`engine/calibrator.rs`) | the calibration session, the sample capture, the display settings, the prompt, the timers | applies commands, the tag thread's reports and the timers; publishes the view state (`engine/view.rs`) |

The engine thread never touches the tag. While collecting from a tag it
recomputes the quality figures every 200 ms (qtcalibrate's quality timer).

**Stopping a stream waits for IDLE.** The tag keeps its CALIBRATE state in
its backup registers and acknowledges Stop before its main thread acts on it,
so a tag detached on the acknowledgement stays calibrating through the next
attach's reset, and refuses everything but Stop. The tag thread therefore
polls the status after Stop until the tag reports IDLE (up to 2 s), whether
the stream ends by Stream off, Detach or the thread ending; `quit` waits for
the detach before the app exits (up to 5 s); and Attach stops a tag it finds
still calibrating rather than refuse it.

## Replay

`TAGCALIBRATE_REPLAY=<capture.json>` replays a sample capture
(qtcalibrate's `tag-designs.qtcalibrate.calibration-capture.v1`) instead of
attaching, as qtcalibrate's `--replay-capture` does: the window looks attached
to the file, Stream feeds one sample per 100 ms, and the quality update runs
every second sample rather than on a timer, so the result depends on the
capture alone. At the end of the capture the stream turns itself off. The
engine's `replay_period` shortens the tick for tests; nothing else depends on
it.

## Transport

As Tag Monitor's: `invoke("command", {cmd, args})`, `invoke("query",
{name, args})`, and events `state` and `log`. In a plain browser
`ui/mock-engine.js` stands in, with scenarios (`?scenario=`).

Nothing happens until the page sends `ready`. `ready` starts a replay when
one is configured, or attaches (unless `TAGCALIBRATE_NO_ATTACH` is set), as
qtcalibrate does when its window opens.

## View state (event `state`)

```jsonc
{
  "title": "Tag Calibrator v1.0",
  "link": { "attached": true, "text": "IMUTAG  ·  UUID 2035..." },   // or "Replay: <file>"
  "stream": { "on": false, "enabled": true },                       // the Stream check box
  "actions": {                        // { visible, enabled } for each command
    "attach": ..., "detach": ..., "start": ..., "stop": ..., "clear": ...,
    "tag_write": ..., "tag_read": ..., "save_capture": ...
  },
  "calibration": {                    // "--" until a calibration exists
    "offset": ["+0.027", "+30.893", "+210.320"],            // V, µT, %+.3f
    "mapping": ["+0.980 -0.000 +0.014", "...", "..."],      // rows of A
    "field": "45.66",                                       // B, µT
    "accelOffset": { "text": "-29.4 -5.2 -67.1 = 73.4 mg", "warn": false }  // "  CHECK", warn, beyond 250 mg
  },
  "quality": {                        // "--" before the first quality update
    "spread": "0.43", "worst5": "0.94", "hardIron": "0.02", "fitError": "0.43",
    "coverage": "90/100", "evenness": "0.72", "attitude": "2.06",
    "dip": "63.5 ± 2.7"               // "no accel" without accelerometer figures
  },
  "sphere": {
    "radius": 45.66,                  // B, µT: the fit's, or 50 before one
    "points": [x, y, z, ...],         // the buffer (up to 650), calibrated, µT
    "newest": [x, y, z]               // the sample last collected, as plotted; or null
  },
  "orientation": {                    // the live compass, while streaming
    "valid": true,
    "heading": 315.2,                 // degrees, declination and battery applied: the dial
    "quaternion": [w, x, y, z],       // the tag model's rotation
    "readouts": { "heading": "315", "roll": "-18", "pitch": "12",
                  "dip": "63", "field": "46", "gravity": "1,038" }
  },
  "settings": { "declination": 0.0, "declinationText": "0.00", "batteryForward": true },
  "prompt": null                      // see Prompts
}
```

Every text is formatted by the engine as qtcalibrate formats it: the
constants and figures as its labels do (`calibration_update`,
`updateMagQualityDisplay`), the readouts as `MyCompass.qml` does -- heading
plus declination, and with the battery backward the heading turned round and
pitch and roll negated, each rounded to a whole number with thousands
separators. The heading, pitch and roll come from `display_angles()` and the
model's rotation from `model_rotation()`, both the C++ conversions qtcalibrate
uses.

The page turns the sphere as magPlot does: the newest point to the centre by
the shortest rotation, plus the user's drag. That is drawing, not a rule, so
it is the page's.

## Prompts

As in Tag Monitor: the engine shows one prompt at a time with a fresh `id`,
and the page answers with `answer {id, button, index?, path?}`.

| `kind` | Fields | Buttons |
| --- | --- | --- |
| `alert` | `title`, `text`, `informative`, `style` (`warning`, `info`) | `ok` |
| `chooseBase` | `title`, `label`, `items[]` (each base with its identity, as in Tag Monitor) | `ok` with `index`, `cancel` |
| `saveFile` | `title`, `suggested` (`qtcalibrate-samples-<stop time>.json` in the home folder), `filters` | `ok` with `path`, `cancel` |

## Commands

| `cmd` | `args` | Does |
| --- | --- | --- |
| `ready` | -- | the page is listening: start the replay, or attach |
| `attach`, `detach` | -- | find the bases (choosing when there are several) and attach; or detach, ending any capture and stream |
| `set_stream` | `{on}` | the Stream box: on sets the tag's clock and enters calibration; off acts as Stop first, then stops the tag |
| `start`, `stop`, `clear` | -- | collect samples into the session and the capture; stop; discard the samples, the fit and the capture |
| `tag_write` | -- | append the constants to the tag: the magnetometer's, and the accelerometer offset beside them (zero, meaning no correction, without a fit) |
| `tag_read` | -- | install the tag's last stored constants; the field is recomputed from the buffer |
| `save_capture` | -- | asks where, then writes the capture in qtcalibrate's schema |
| `set_declination` | `{degrees}` | degrees east, added to the heading |
| `set_battery_forward` | `{on}` | which end of the tag is forward |
| `answer` | `{id, button, index?, path?}` | answers the shown prompt |
| `quit` | -- | File ▸ Quit and window close: stops the stream, detaches, then emits `Exit` |
| `set_log_level` | `{level}` | TRACE..ERROR |
| `save_text` | `{path, text}` | Save Log |

Each command is ignored unless `actions` enables it. Attach refuses a tag
that is not IDLE after the recovery above: one running a deployment or
holding data is not one to calibrate.

## Queries

| `name` | Returns |
| --- | --- |
| `menus` | `{menus: [{title, entries: [{kind, id, label, tooltip, shortcut}]}]}` |
| `about` | `{title, text}` |
| `user_guide_url` | URL |

## Logging

Each quality update logs qtcalibrate's four `DEBUG` lines -- `magquality:`,
`residual:`, `gravity:`, `retention:` -- in its format, so a replay's log can
be compared with qtcalibrate's line for line (`core/tests/replay_log.rs`).

## The capture file

Written as qtcalibrate's `saveSampleCapture` writes it: the same keys, sorted
as `QJsonObject` sorts them, four-space indentation, each float written as the
double it widens to, and a `tag` block without the UUID or source path, so a
capture is safe to commit as a fixture. A capture saved from a replay carries
the replayed file's `tag` block. Captures from either program replay in both.

## Deliberate differences from qtcalibrate

- The sphere is three.js, drawn and turned as magPlot draws and turns it.
  It shows the buffer, calibrated with the current fit, rather than
  magPlot's list, which grows by each point as collected until the next fit
  replaces it.
- The quality figures are a labelled grid rather than Courier columns.
- The menus add a Tag menu (the collection commands, Stream, Tag Write and
  Read) and a Log menu, as in Tag Monitor; qtcalibrate's File ▸ Save
  Calibration and Read Data File had no handlers and are not carried over.
- The bottom bar names the attached tag, or the replayed file.
- Quit stops the stream and waits for IDLE, and Attach recovers a tag left
  calibrating; qtcalibrate does neither.
- No screenshot automation in the engine: `ui/screenshots.sh` captures
  pictures from the browser mock.
