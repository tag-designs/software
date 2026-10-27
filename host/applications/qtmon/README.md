---
type: readme
status: current
summary: qtmonitor live and fixture screenshot modes, screenshot commands and sizing.
---

# QtMonitor

`qtmonitor` is the Qt host application for inspecting an attached tag, editing
its configuration, issuing basic state-control commands, viewing the tag error
log, and starting downloads from finished or aborted logs.

## Operating Modes

How the code is divided -- the Qt-free core, the tag session and its worker
thread, and the window -- is in [design/architecture.md](design/architecture.md).

### Live Tag Mode

With no maintainer options, `qtmonitor` operates against real hardware:

1. The window's tag session is a `LiveTagSession`
   (`session/livetagsession.h`): a worker thread that owns the `tagcore::Tag`
   and makes every monitor call. The window never touches the tag.
2. At startup the window asks the session for the USB tag bases and attaches
   (asking which base when there are several); the session reports the tag's
   `TagInfo` and `Config`.
3. While attached, the worker reads the status every 400 ms. Each status goes
   through `qtmon::MonitorModel` (`core/monitormodel.h`), and the window draws
   the model: the status strip, the Tag tab and which commands are available.
4. Read Configuration, Start, Stop, Synchronize Clock, Self-Test, Erase and
   Download Data are session commands; each returns at once and reports its
   outcome later, so the window never waits on the tag.

This is the packaged user-facing mode.

### Commands, menus and the status strip

Design and rationale: [Configuration Editor, Menus and Start Checks](design/editor-and-start-checks.md).

Every command is a `QAction`, created in `MainWindow::createActions()` and
placed in the File, Tag, Log and Help menus (in the window, as in sensorviz) and on
popup menus by `createMenus()`. The status strip (`tagstatusstrip.cpp`), which
`placeStatusStripBelowTabs()` puts between the tab bar and the pages, shows
Attached or Detached, the tag type, state, clock
offset, voltage and self-test result, with the Approved box and Start and
Stop as tool buttons bound to their actions. Start runs
`MainWindow::startWithChecks()`: `qtmon::startCheckFailures()` lists any failing
check (clock beyond `qtmon::kClockToleranceS`, a reported self-test
other than ALL_PASSED, Approved not ticked), and the user may start anyway,
which is logged. Approval belongs to the configuration document
(`qtmon::ConfigDocument`, `core/configdocument.h`), which withdraws it when
the values change; the Approved box shows it. The document's commands are
`ConfigTab` slots (`NewFromDefaults()`, `Open()`, `Save()`, `SaveAs()`,
`ResetToDefaults()`; see `tagconfiguration/README.md`); Read Configuration and
Start are the window's, through the tag session. The error log's commands are
`LogWindow` actions.

`qtmon::availability()` (`core/commands.h`) is the one rule that decides
which actions are enabled, from whether a tag is attached, its state and type,
whether a download or self-test is pending, and whether the Configuration tab
holds a configuration; `MainWindow::updateActions()` applies it.
`qtmonitor --check-action-states` prints its decisions for every combination
of those, and `--check-action-wiring` checks that the window applies them; the expected table is
[`test/action-states.txt`](test/action-states.txt) (see
[`test/README.md`](test/README.md)).

### Fixture Screenshot Mode

Fixture mode is a maintainer-only path for generating documentation screenshots
from captured fake-tag data. It is enabled with `--fake-fixture`, which makes
the window's tag session a `FixtureSession` (`session/fixturesession.h`) in
place of the live one; no tag is touched. The screenshot capture is in
`mainwindow_fixture.cpp`.

The fixture session reads `host/docs/fixtures/qtmonitor/*.json`, parses the
embedded protobuf JSON objects into `TagInfo`, `Config`, and named `Status`
messages, and reports them as a live session would: attached, then a status.
The window handles them exactly as it handles a real tag's -- the same model,
the same drawing, the same command rule -- so screenshots match the real UI.
Commands sent to the fixture session are logged and ignored.

Screenshot generation uses the fixture's status of the requested name
(`idle`, `running` or `finished`) when it has one, and otherwise falls back to
its `idle` status. The state field is always forced to the requested state, and
zero log counts are replaced with placeholder counts for `running` and
`finished`, so a fixture with only an `idle` status still yields all three
screens.

### Configuration Probe Mode

`--config-probe <cases> --config-probe-out <dir>` runs the Configuration tab
headless over a set of test cases, with no tag attached. For each case it
records what Start would send, a fingerprint of the visible controls, and any
dialog raised, then exits. Comparing the results with the committed goldens
shows whether a change to the Configuration tab altered anything a tag or a
user could see. Cases, goldens, scripts and procedure are in
[`test/`](test/README.md).

`--check-config-panels` checks which configuration panels each tag type shows
against every tag's default configuration (see
[`tagconfiguration/README.md`](tagconfiguration/README.md)), prints any
mismatch, and exits non-zero if there is one.

## Screenshot Commands

Build the app first:

```sh
cmake --build <build-dir> --target qtmonitor
```

`<build-dir>` is your host build directory. On macOS the executables are app
bundles. The command line tools, such as `qtmonitor-fixture-capture`, also get
a `<build-dir>/bin/<tool>` symlink to the binary inside the bundle, so the
commands below work as written. For `qtmonitor`, use
`<build-dir>/bin/qtmonitor.app/Contents/MacOS/qtmonitor` in place of
`<build-dir>/bin/qtmonitor`.

Capture the representative Tag tab screens, in idle, running and finished states:

```sh
<build-dir>/bin/qtmonitor \
  --fake-fixture host/docs/fixtures/qtmonitor/compasstag.json \
  --capture-main-screenshots
```

This writes:

- `host/docs/src/images/qtmonitor-main-idle.png`
- `host/docs/src/images/qtmonitor-main-running.png`
- `host/docs/src/images/qtmonitor-main-finished.png`

Capture the idle Configuration screens for the fixture tag:

```sh
<build-dir>/bin/qtmonitor \
  --fake-fixture host/docs/fixtures/qtmonitor/compasstag.json \
  --capture-config-screenshots
```

This writes:

- `host/docs/src/images/qtmonitor-config-compasstag.png`

The window is sized to show the whole panel page, up to 1100 by 1400 pixels.

Capture the disconnected startup screen:

```sh
<build-dir>/bin/qtmonitor \
  --capture-startup-screenshot
```

Capture the disconnected Error Log screen:

```sh
<build-dir>/bin/qtmonitor \
  --capture-error-log-screenshot
```

The screenshot directory defaults to `host/docs/src/images/`. Use
`--screenshot-dir <dir>` to write elsewhere.

## Screenshot Sizing

Screenshot capture uses Qt layout sizing before grabbing the native window:

1. Select the tab to capture.
2. Call `adjustSize()` on the active tab, the main tab widget, and the window.
3. Compute the natural size from `sizeHint()` and `minimumSizeHint()`.
4. Apply a per-capture maximum client size as a documentation guardrail.
5. Grab the native window frame with `QScreen::grabWindow()`.

The Error Log tab has explicit `LogWindow::sizeHint()` and
`LogWindow::minimumSizeHint()` overrides. Without those hints, `QTextEdit` and
the designer geometry can make the tab much taller than useful, while simple
image cropping can clip the bottom of the log pane. Keep those hints in sync
with `logwindow.ui` if the Error Log controls change. The `logTextEdit` widget
also has a minimum height so the screenshot helper can fix the actual window
size instead of cropping the saved image.
