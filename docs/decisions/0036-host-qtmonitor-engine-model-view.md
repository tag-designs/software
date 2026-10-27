---
type: decision
status: accepted
summary: qtmonitor was restructured into a Qt-free core (command rule, start checks, monitor model, configuration document, download loop), a tag session whose worker thread owns the tag, and a window that only draws the model and sends commands; the Calibrate command was dropped. Behaviour was held fixed by the existing goldens, pixel-identical screenshots, a new core check and a full tag cycle on hardware.
---

# 0036. Host: qtmonitor as a core, a tag session and a window

Date: 2026-10-27

## Context

qtmonitor's `MainWindow` was the view, the controller, the tag driver and
the test harness at once:
- **The tag was driven from the GUI thread.** The 400 ms status poll, attach
  and every command made blocking monitor calls, so a slow or failing call
  froze the window. Only the download had its own thread, with its own
  plumbing.
- **State lived in widgets.** The Start approval was a checkbox.
- **Rules were computed while drawing.** The clock offset and the self-test
  wait were worked out inside the code that updated labels.
- **The tag's state was held in three places.**
- **The configuration tab was the document as well as its widgets,** and
  sent Start and Read to the tag itself, with a start poll on a timer of its
  own.
- **Fixture replay was woven through the window as special cases.**

That shape grew from Qt's model, where the natural unit is a slot on the GUI
thread that calls the tag, updates widgets and decides what is enabled. It
made the code hard to follow, and it does not translate to a Rust and Tauri
application, where the UI can only send messages. The
[proposal](../../host/applications/qtmon/design/proposals/engine-model-view.md)
set out the split. The Calibrate command, and the calibration-log printing
the status poll did while a tag was in CALIBRATE, duplicated `tag-cal` (a
debugging tool) and qtcalibrate (which does calibration).

## Decision

- **A Qt-free core,** `qtmon_core` in `host/applications/qtmon/core/`. It
  holds the command rule (`availability()`), the start checks, small tag
  rules, the monitor model (statuses, the self-test and start waits, erase
  progress, the tag's debug lines), the configuration document (approval
  included), and the download loop (`DownloadJob`).
- **A tag session interface,** commands in and reports out, with two
  implementations:
  - **`LiveTagSession`,** whose `TagWorker` on its own thread owns the `Tag`,
    polls every 400 ms, runs every command and runs downloads;
  - **`FixtureSession`,** which replays a captured tag for screenshots.
- **The window** draws the model in one `render()` and sends commands. It
  never touches a tag. Message boxes that only report are non-modal, so no
  status re-enters the window while one is open.
- **Start is judged by the state the tag reaches** (RUNNING or CONFIGURED
  within 7 s), as the configuration tab's start poll did, now as a model
  rule.
- **Calibrate is dropped.** Stop stays available in CALIBRATE, so a tag left
  there by `tag-cal` can still be stopped.
- **The restructure was done in Qt, step by step,** each step held to the
  existing checks, rather than freezing qtmonitor and writing the Rust
  version to the new shape.

The structure as built is in
[architecture.md](../../host/applications/qtmon/design/architecture.md).

## Evidence

- **Tables:** `action-states.txt` lost only its `calibrate` column.
  `start-checks.txt` and all eight configuration-probe golden files are
  unchanged at every step.
- **Wiring check:** the new `--check-action-wiring` finds each action by its
  menu text. A deliberately swapped pair was reported, and the restored code
  passes.
- **Screenshots:** all eleven documentation screenshots, captured from the
  fixtures, are pixel-identical between the build before the session layer
  and the build after, captured back to back.
- **Core checks:** the new `qtmon_core_check` (44 checks, no Qt) covers the
  document's and the model's rules.
- **Hardware:** `qtmon_session_check` ran one full cycle on a UIUCTag
  (breakout, UUID `2036354B3032500800520028`, firmware `7c508f46`, 3.28 V)
  through `LiveTagSession` and the model, and every step passed:
  - attach; sync (offset −0.019 s); self-test ALL_PASSED; start (CONFIGURED);
  - 360 s run; stop (FINISHED, 2 records);
  - download (45,056 bytes, as `tag-dwnld` wrote from the same tag that
    morning); erase to IDLE in 7.3 s; detach;
  - 927 statuses at the 400 ms cadence.

## Consequences

- **The window cannot freeze on the tag.** A download, an erase or a slow
  call no longer blocks the GUI.
- **Behaviour changes:**
  - a refused self-test is reported at once, where it used to wait out the
    60 s timeout;
  - a download that cannot read the status reports it, where it used to end
    silently;
  - a failed page read reports the tag's message, as `tag-dwnld` does,
    instead of "Unsupported tag type?".
- **Logging from the worker thread is queued to the Error Log** on the GUI
  thread. Before, the download thread wrote to the widget directly.
- **The core and its checks are the oracle for a Rust port.** The tables,
  goldens and core checks are language-neutral.
- **The configuration panels still hold their own load and store rules.**
  Moving them into a field spec, as the
  [configuration form mockup](../../host/applications/qtmon/design/proposals/config-form-mockup/README.md)
  shows, is the remaining step before the Configuration tab is as portable as
  the rest.
