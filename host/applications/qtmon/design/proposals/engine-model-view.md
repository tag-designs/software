---
type: proposal
status: historical
superseded-by: host/applications/qtmon/design/architecture.md
summary: Restructure qtmonitor into an engine that owns the tag on its own thread, a Qt-free model that holds every rule (action states, start checks, the configuration document), and views that only draw the model and send commands - done in Qt first, step by step under the existing goldens, so a Rust and Tauri port becomes a translation of each layer.
---

# qtmonitor: engine, model and views

> **Built, 2026-10-27.** qtmonitor now has this structure; the design as built
> is [architecture.md](../architecture.md) and the decision is
> [0036](../../../../../docs/decisions/0036-host-qtmonitor-engine-model-view.md).
> This proposal is kept as the record of the reasoning, and describes the code
> as it was before.

**Proposal:** split qtmonitor into three layers, and do it in Qt first.

1. An **engine** owns the `Tag` on its own thread. It takes commands, polls
   status, and reports events.
2. A **model** is Qt-free C++ holding every rule: which commands are
   enabled, the start checks, the configuration document and its edits.
3. **Views** draw the model and turn gestures into commands; they decide
   nothing.

This is the shape Tag Programmer already has (its engine design is
`host/applications/tagprogram-rs/design/engine.md`, on the `tagprogram-rs`
branch), and the shape the
[configuration form mockup](config-form-mockup/README.md) proposes for the
Configuration tab. Done in Qt, each step is checked by the goldens qtmonitor
already has. Afterwards, a Rust and Tauri version is a translation of each
layer, not a redesign of the application. It also makes the Qt version easier
to follow, whether or not the port happens.

## What is tangled today

`MainWindow` (1,109 lines, plus 692 in `mainwindow_fixture.cpp`) is view,
controller, tag driver and test harness at once:

- **The tag is driven on the GUI thread.** The 400 ms status poll
  (`TriggerUpdate()`), the attach sequence, Stop, Synchronize Clock,
  Self-Test, Calibrate and Erase all make blocking monitor calls from the
  event loop. Erase's progress dialog follows that same poll. A slow or failing call freezes the window. Only the
  download has its own thread (`AbstractDownload`).
- **State lives in widgets.** The approval that Start checks is
  `TagStatusStrip::isApproved()`, a checkbox. The download is enabled
  through `datadownloadgroupBox`. Visibility of the external log count is set
  inside `applyStatus()`.
- **Rules are computed inside rendering.** `applyStatus()` updates labels and
  also computes the clock offset, settles a pending self-test with its
  1-second minimum and 60-second timeout, and then calls `updateActions()`.
- **Tag state is held in three places.** `MainWindow::current_state`,
  `ConfigTab::old_state_` and `attached_`, and the status strip each keep
  their own copy.
- **The document is a widget.** `ConfigTab` (820 lines) holds the
  configuration document: the `Config`, source, path, baseline, modified
  flag and recent files. It also holds the panels that show it, and does its
  own tag I/O: `StartTag()`, `ReadFromTag()`, and a start poll on a timer of
  its own (`pollStartResult()`).
- **Dialogs sit inside logic.** `Attach()` shows three message boxes and a
  base chooser in the middle of the attach sequence.
- **The test harness is woven into the window.** Fixture replay and
  screenshot capture are `fake*` members and branches in `MainWindow`
  (`fakeMode`, `attachConfig(config, fixtureMode)`), rather than a fake tag
  behind the same interface.

Two things are already right and are the base to build on:

- `updateActions()` sets every command's state in one place.
- That table, the start checks and the configuration tab are pinned by
  headless goldens: `test/action-states.txt`, `test/start-checks.txt` and
  the `--config-probe` cases.

## The three layers

### Engine: the only code that touches the tag

One object on its own thread owns the `Tag`. Nothing else holds a reference
to it.

| Commands in | Events out |
| --- | --- |
| `listBases`, `attach(base)`, `detach` | `basesListed(devs)`, `attached(info, config)`, `detached(reason)` |
| `readConfig` | `configRead(config)` |
| `start(config)` | `startResult(ok, state, message)`, after polling for a state other than IDLE, as `pollStartResult()` does now |
| `stop`, `syncClock`, `selfTest` | `commandDone(op, ok, message)` |
| `erase` | `progress(op, value, max)`, `commandDone` |
| `download(path)` | `progress`, `commandDone` |
| (its own 400 ms poll) | `status(Status, voltage, host_time)`, `tagLog(text)` |

The engine makes no decisions about the UI. It does not know about
approval, documents or menus. It answers what it was asked, and reports
failures with the tag's own message.

In Qt it is a `QObject` moved to a `QThread`, connected by queued signals,
as `AbstractDownload` already is. In Rust it is Tag Programmer's pattern: an
engine thread, a message channel, and slow work on worker threads whose
results come back as messages.

### Model: every rule, no toolkit

The model is plain C++ in `qtmon/core/`, as sensorviz's `core/` is. It is
updated only from engine events and user commands.

| Part | Holds |
| --- | --- |
| `Connection` | detached, attaching or attached; `TagInfo`; the last `Status`, voltage and time |
| `Derived` | clock offset; self-test pending, started and settled; erase sector maximum (`eraseSectorMaximum()`) |
| `Operation` | none, erasing, downloading or starting, with progress |
| `ConfigDocument` | the `Config`, tag type, source, path, baseline, modified flag, approval; New, Open (merge onto defaults), Read, Reset, Save; the field rules from the panels' `load()` and `store()` |
| Rules (pure functions) | `actionStates(model)`, which gives each command enabled, disabled or hidden; `startCheckFailures(model)`; the window title |

Approval moves out of the status strip and into the document, where the
rule "any change to the values clears it" belongs.

The goldens become tests of the model alone, with no window:
`action-states.txt` is `actionStates()` over its cases, and
`start-checks.txt` is `startCheckFailures()`.

### Views: draw the model, send commands

The window, the status strip, the Tag page, the Configuration panels and
the log tab each read the model and send commands. A dialog is a view
asking the user a question the model raised: confirm discard, choose a base,
Start Anyway, or confirm erase. None of them decides anything. The panels go
the way the mockup shows: the fields as data, a generic renderer, and edits
sent as `setField` to the document.

### The harness: a fake engine

Fixture replay becomes a `FakeEngine`. It answers the same commands from a
fixture JSON with no tag, and the screenshot and probe modes drive the real
window through it. `mainwindow_fixture.cpp`'s special cases go away, and the
screenshots show exactly what a user sees.

## Steps, each checked by what exists

| Step | Change | Must not change |
| --- | --- | --- |
| 1 | Move the action table and start checks into `core/` as functions of a model struct. `updateActions()` just applies the result. | `action-states.txt`, `start-checks.txt` |
| 2 | Extract `ConfigDocument` from `ConfigTab`, approval included; the panels read and write it. | `--config-probe` goldens |
| 3 | Move tag I/O into the engine thread; `MainWindow` handles events. Dialogs leave the logic. | goldens; a hardware pass of attach, start, stop, test, erase, download |
| 4 | Replace fixture mode with `FakeEngine`. | screenshot set |
| 5 | Panels from a field spec (the mockup's split). | `--config-probe` goldens |

After step 3 the window cannot freeze on the tag. After step 4,
`MainWindow` should be a few hundred lines of wiring.

Much of today's structure comes from Qt's model rather than from choices
made carelessly. Widgets are touched only from the GUI thread, so the
natural unit of code is a slot that calls the tag, updates widgets and
decides what is enabled. Moving work off that thread costs a queued signal
each time. Tauri forces the opposite: the UI is a webview that can only send
messages. Steps 1, 2 and 4 work with Qt's model and give most of the
clarity, plus the oracle for a port. Step 3 works against it. If the Qt app
is to be replaced rather than kept, step 3 can be left to the Rust version,
where the split comes for free.

## What the port then is

| Layer | Qt after the restructure | Rust and Tauri |
| --- | --- | --- |
| Engine | `QThread` and `Tag` | an engine thread on tagcore-rs, as in Tag Programmer; downloads through `taglogwriter` |
| Model | `qtmon/core` C++ | Rust, checked against the same goldens: `action-states.txt`, `start-checks.txt` and the `--config-probe` outputs are language-neutral text |
| Views | widgets | the ui-kit; the Configuration tab from the field spec, as in the mockup |
| Harness | `FakeEngine` | a fake engine behind the same messages |

Each layer has an oracle before it is translated, which is what made Tag
Programmer's port tractable.

## Found while reading

These are outside this proposal, but worth deciding on their own:

- `TriggerUpdate()` read the calibration log on every poll while the tag was
  in CALIBRATE and printed the samples with `qInfo()`. That, and the Calibrate
  command, are removed: `tag-cal` streams the same samples for debugging, and
  qtcalibrate does calibration.
- `Attach()` checks `qtmonitor_min_version` against a hard-coded `2.0`; the
  tag's own value is commented out.

## Questions for the owner

- Should the restructure be done in Qt first, as proposed, or should the
  Rust port be written straight to this shape, with the Qt app frozen?
- Should a Rust model reproduce the goldens exactly, or may the port change
  behaviour where the Qt app is accidental, such as the calibration-log
  printing?
