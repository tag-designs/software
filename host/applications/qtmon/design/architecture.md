---
type: design
status: current
summary: How qtmonitor's code is divided - a Qt-free core (command rule, start checks, monitor model, configuration document, download loop), a tag session whose worker thread owns the tag, and a window that only draws the model and sends commands - with the data flow, the threading rules, and how each part is checked.
---

# qtmonitor architecture

**qtmonitor is three layers.**
- A **tag session** owns the tag. In live use its worker thread makes every
  monitor call.
- A **Qt-free core** holds every rule: which commands are available, the
  start checks, what each status means, the configuration document and the
  download loop.
- The **window** draws the core's model and turns the user's gestures into
  session commands.

The window never touches a tag and never waits on one. The layers are
plain C++ (the core), a Qt interface with two implementations (the session),
and Qt Widgets (the window). Each part is checked on its own, mostly without a
tag. This layout is also the shape a Rust and Tauri port would take; see
[decision 0036](../../../../docs/decisions/0036-host-qtmonitor-engine-model-view.md).

## Layers and files

```
 MainWindow (mainwindow.cpp) ── draws the model; commands → session
   │  ConfigTab (tagconfiguration/) ── panels and dialogs over ConfigDocument
   │  TagStatusStrip, LogWindow ── views
   ▼ commands                         ▲ reports (signals, GUI thread)
 TagSession (session/tagsession.h)
   ├─ LiveTagSession ── TagWorker on its own thread owns tagcore's Tag
   └─ FixtureSession ── replays a captured tag for screenshots
 core/ (qtmon_core, no Qt)
   commands · startchecks · tagrules · monitormodel · configdocument · downloadjob
```

| Part | Files | Holds |
| --- | --- | --- |
| Command rule | `core/commands.h` | `qtmon::Command`, `availability()`: the one rule for every command; `actionStateTable()` |
| Start checks | `core/startchecks.h` | `startCheckFailures()`: clock within 2 s, self-test ALL_PASSED, approved |
| Tag rules | `core/tagrules.h` | Downloadable states, erase sector count, clock offset |
| Monitor model | `core/monitormodel.h` | The attached tag as known: identity, latest status, waits (self-test, start), erase progress, debug lines; `applyStatus()` returns what a status settled |
| Configuration document | `core/configdocument.h` | The Config, source, file, unsaved-changes baseline, approval; reading and writing configuration files |
| Download loop | `core/downloadjob.h` | `DownloadJob::run()`: status, header, pages until the writer consumes nothing; cancel between pages |
| Session interface | `session/tagsession.h` | Commands in, reports out as signals |
| Live session | `session/livetagsession.h` | `TagWorker` (owns the `Tag`, polls every 400 ms, runs commands and downloads) on its own `QThread`; `LiveTagSession` queues commands to it |
| Fixture session | `session/fixturesession.h` | A captured tag from `host/docs/fixtures/qtmonitor/`; commands are logged and ignored |
| Window | `mainwindow.cpp`, `mainwindow_fixture.cpp` | Actions and menus; session reports into the model; `render()`; dialogs; screenshot capture and the wiring check |
| Configuration tab | `tagconfiguration/configtab.h` | The panels over the document, file dialogs, recent files; editability from the tag's state |

## How a status flows

1. **Read.** `TagWorker::poll()`, every `qtmon::kStatusPollMs` (400 ms), reads
   the voltage and `Status` on the worker thread and emits `statusRead`.
2. **Delivered.** The signal crosses to the GUI thread queued, through
   `LiveTagSession`. A `FixtureSession` emits the same signal directly.
3. **Applied.** `MainWindow::onStatus()` calls
   `MonitorModel::applyStatus()`. That records the status and clock offset,
   and returns events for anything it settled:
   - a whole line of the tag's debug text;
   - a self-test result;
   - a start that succeeded or failed;
   - an erase that finished.
4. **Drawn.** `render()` redraws from the model: the Tag page, the status
   strip, the Configuration tab's editability, the actions (through
   `qtmon::availability()`), and erase progress.
5. **Reported.** The window acts on the events: Error Log lines, and
   non-modal message boxes.

A command goes the other way. A menu action calls a window slot, which calls
a `TagSession` method. That returns at once; the worker runs the call and
reports with a signal (`startSent`, `eraseSent`, `commandFailed`,
`downloadFinished`, ...).

## Rules that live in the model, not in the calls

- **Start is judged by the state the tag reaches.** `Tag::Start()` can lose
  its acknowledgement after the tag accepted the command. So the model
  records the start and waits for RUNNING or CONFIGURED. It reports failure
  only if the tag is still in another state `kStartWaitMs` (7 s) later. A
  start error the tag overcame is logged as a warning.
- **A self-test result is taken only from a status at least
  `kTestMinimumMs` (1 s) after sending.** An earlier status can still show the
  previous run's result. With no result within 60 s, the wait times out.
- **Erase progress comes from sRESET statuses.** The maximum is the tag's own
  `erase_sectors_total_plus_one` when it reports one, or an estimate
  otherwise, and never decreases. The erase ends when the tag reports IDLE.
- **Approval belongs to the document.** Any load, reset, clear or edit
  withdraws it; saving does not. The status strip's box only shows it and
  reports the user's clicks.

## Threading

- **One thread owns the tag.** Only `TagWorker`, on the session's thread,
  calls `Tag`, so monitor calls never overlap and need no lock. Commands queue
  on that thread and run in order.
- **A download runs inside the worker's `download()` slot.** It blocks the
  status polls until it ends, which is what the tag needs anyway.
  `cancelDownload()` sets an atomic flag, the one thing another thread may do.
  The job stops after the current page and commits what it wrote.
- **Logging is thread-safe.** The worker logs through `qInfo()` and tagcore's
  `log.c`. `main.cpp`'s `logOutput()` queues any message from another thread to
  the Error Log's text box on the GUI thread.
- **Shutdown detaches on the worker's own thread.** `LiveTagSession`'s
  destructor cancels any download, runs `TagWorker::shutdown()` there
  (blocking), then ends the thread.
- **No nested event loops while attached.** Message boxes that only report
  are shown with `open()`, so a status arriving meanwhile cannot re-enter the
  window. Confirmations stay modal: erase, Start Anyway, choosing a base,
  unsaved changes.

## Checks

| Check | Covers | Needs |
| --- | --- | --- |
| `qtmonitor --check-action-states` | `availability()` against `test/action-states.txt` | nothing |
| `qtmonitor --check-start-checks` | `startCheckFailures()` against `test/start-checks.txt` | nothing |
| `qtmonitor --check-action-wiring` | The window applies the rule to the right actions, found by menu text | a display (offscreen works) |
| `qtmonitor --config-probe` | The Configuration tab over every case, against `test/golden/` | a display |
| `qtmon_core_check` | The document's and the model's rules | nothing (no Qt) |
| `qtmon_session_check` | One whole tag cycle through `LiveTagSession` and `MonitorModel` | a bench tag; changes it |
| Screenshot capture | The window drawn from a `FixtureSession` | a display |

How to run each is in [test/README.md](../test/README.md). The three tables,
the probe goldens and the core checks are language-neutral, so a port of the
core is checked against the same files.
