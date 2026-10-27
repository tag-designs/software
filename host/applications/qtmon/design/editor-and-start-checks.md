---
type: design
status: current
summary: qtmonitor is a configuration editor as well as a tag monitor -- one document from defaults, a file or a tag, commands as actions in in-window menus, a status strip with Approved, Start and Stop, and Start checks (clock within 2 s, self-test ALL_PASSED, Approved) that warn and let the user continue or cancel.
---

# qtmonitor: Configuration Editor, Menus and Start Checks

qtmonitor edits a configuration **document** that can exist without a tag, and
starts it on an attached tag of the same type. Every command is a `QAction` in
the File, Tag, Log and Help menus, which are in the window. A status strip,
visible on every tab, shows the tag and holds Approved, Start and Stop. Start
checks the clock, the self-test and the approval; if any fails it warns, and
the user may start anyway or cancel. The choices behind this are in
[decision 0033](../../../../docs/decisions/0033-host-qtmonitor-editor-menus-start-checks.md).

## The document

The Configuration tab holds one document: a `Config`, its tag type (which
chooses the panels), its source (defaults, a file or a tag), a file name and an
unsaved-changes flag. The window title shows the name and the flag.

| Command | Effect |
| --- | --- |
| **New ▸** tag type | That type's default configuration (`ConfigShape::defaultConfig()`). |
| **Open…**, **Open Recent** | The file, as that type's defaults with the file merged on top (`ConfigShape::forFile()`, `merge()`). A file without `tag_type` asks for one. |
| **Read Configuration** | The attached tag's configuration. |
| **Save**, **Save As…** | Writes the document as protobuf JSON. |
| **Reset to Defaults** | The document's tag type's defaults. |
| **Start** | Writes the document to the attached tag and starts it. |

Attaching keeps a document from defaults or a file, since the user may be about
to start this tag with it; it replaces only a previous tag's configuration,
asking first if that was modified. "Default" means the tag type's default config
file, which is not always what a freshly reset tag reports. The mechanics are
in [tagconfiguration/README.md](../tagconfiguration/README.md); the
configuration model in
[Tag Configuration Model](../../../../docs/shared/design/configuration-model.md).

## Actions, menus and the status strip

`MainWindow::createActions()` makes every command an action, and
`updateActions()` sets every action's enabled state in one place, applying
the Qt-free rule `qtmon::availability()` (`core/commands.h`), from the
attached tag's state, the document, a running download and a pending
self-test. Menus and popup menus only place the actions, so a command behaves
the same wherever it is chosen.

The menus are in the window (`setNativeMenuBar(false)`), as in sensorviz, so
the apps look alike on every platform. The status strip sits between the tab
bar and the pages: QTabWidget's own bar is hidden and a standalone `QTabBar`
stands in for it. The strip shows Attached or Detached, the tag type (board
UUID in its tooltip), state, clock offset, voltage and self-test result, then
Approved, Start and Stop. Start and Stop stay as buttons because they are the
most-used commands and change the state a user must not miss.

## Start checks

Start is available only with an attached IDLE tag whose type is the document's.
Those conditions cannot be overridden. Start then checks
(`qtmon::startCheckFailures()`, `core/startchecks.h`):

| Check | Passes when |
| --- | --- |
| Clock | The offset at the latest status is within `qtmon::kClockToleranceS`, 2 s. An unset clock is far outside it. |
| Self-test | The tag reports `test_status` ALL_PASSED. The result is the tag's own, kept in `pState`, so it can predate this session; running Self-Test again is one click. |
| Approved | The document is approved: the user ticked the box in the status strip. Approval belongs to the document (`qtmon::ConfigDocument`), which withdraws it on any change to the values (an edit, New, Open, Read, Reset); Save does not. |

The strip shows a failing clock or self-test in red, before Start is pressed.
If any check fails, Start lists the failures, one short line each, and offers
**Start Anyway** or **Cancel**, with Cancel the default. A start made anyway is
logged with the tag type, UUID and the failing checks.

## Help

Every Qt application's Help menu opens its page of the user manual through
`HostUserGuide` (`host/common/qtuserguide.h`), which finds the manual wherever
it is: packaged, in the build tree, or built in the source tree.
`addHelpMenu()` gives qtcalibrate and qtprogram a Help menu with User Guide
and About; sensorviz and qtmonitor build theirs with `open()`.

## Verification

Headless checks, with their committed expected output in `test/`
([test/README.md](../test/README.md)):

- `--config-probe`: the configuration tab against goldens, including New and
  Open with and without a tag;
- `--check-action-states`: which commands are enabled in which state
  (`action-states.txt`);
- `--check-start-checks`: which start checks fail for each clock offset,
  self-test result and approval (`start-checks.txt`).
