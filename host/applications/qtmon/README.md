# QtMonitor

`qtmonitor` is the Qt host application for inspecting an attached tag, editing
its configuration, issuing basic state-control commands, viewing the tag error
log, and starting downloads from finished or aborted logs.

## Operating Modes

### Live Tag Mode

With no maintainer options, `qtmonitor` operates against real hardware:

1. The main window probes USB tag bases during startup.
2. `MainWindow::Attach()` reads `TagInfo`, `Config`, and `Status` through
   `tagcore::Tag`.
3. The Tag State tab is refreshed by a 400 ms polling timer.
4. The Configuration tab is attached to the live `Tag` object so Read and Start
   issue real monitor commands.

This is the packaged user-facing mode.

### Fixture Screenshot Mode

Fixture mode is a maintainer-only path for generating documentation screenshots
from captured fake-tag data. It is enabled with `--fake-fixture` and never calls
`Tag::Attach()`.

The fixture loader reads `host/docs/fixtures/qtmonitor/*.json`, parses the
embedded protobuf JSON objects into `TagInfo`, `Config`, and named `Status`
messages, then populates the same widgets used by live mode:

- `populateTagInfo()` fills the Tag Information group.
- `attachConfig()` calls `ConfigTab::SetConfig()` with the captured/default
  configuration.
- `applyStatus()` applies the same state-dependent control visibility rules used
  by live polling.

Fake mode intentionally leaves buttons visually enabled or disabled according
to the selected tag state, so screenshots match the real UI. The button slots
guard against fake mode and return without issuing hardware commands.

If a fixture has only an `idle` status, screenshot generation derives
display-only `running` and `finished` states from it by changing state and log
counts. Future fixtures may store real captured `running` or `finished`
statuses and those will be used instead.

## Screenshot Commands

Build the app first:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target qtmonitor
```

Capture the representative Tag State screens:

```sh
/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --fake-fixture host/docs/fixtures/qtmonitor/compasstag.json \
  --capture-main-screenshots
```

This writes:

- `host/docs/src/images/qtmonitor-main-idle.png`
- `host/docs/src/images/qtmonitor-main-running.png`
- `host/docs/src/images/qtmonitor-main-finished.png`

Capture the idle Configuration screens for the fixture tag:

```sh
/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --fake-fixture host/docs/fixtures/qtmonitor/compasstag.json \
  --capture-config-screenshots
```

This writes:

- `host/docs/src/images/qtmonitor-config-compasstag-schedule.png`

The Sensors screenshot is written only when the fixture tag exposes
user-configurable sensor controls. Tags with no active sensor controls hide the
Sensors tab and skip that capture.

Capture the disconnected startup screen:

```sh
/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --capture-startup-screenshot
```

Capture the disconnected Error Log screen:

```sh
/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
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
