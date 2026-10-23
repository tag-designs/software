---
type: proposal
status: proposed
summary: Make qtmonitor a configuration editor (new from a tag type's defaults, open, edit, save, and start a matching tag), move its commands from buttons to in-window menus and popup menus with Start and Stop kept beside a status display, have Start warn about clock sync, self-test and an approval checkbox and offer to continue or cancel, and give every app one way to open its user guide.
---

# qtmonitor: Configuration Editor, Menus and Start Checks

**Proposal.** qtmonitor becomes a configuration editor as well as a tag
monitor:

- **The Configuration tab holds a document.** A configuration can be started
  from a tag type's defaults, opened from a file, or read from a tag; then
  edited, saved, and started on an attached tag of the same type.
- **Commands move to menus.** Most of them move to File, Tag, View and Help
  menus and popup menus, as in sensorviz. Start and Stop stay as buttons,
  beside a status display.
- **Start checks before it starts.** It expects a synchronized clock, a passed
  self-test, and a "configuration approved" box ticked. If any is missing, a
  dialog warns and offers to continue or cancel the start.
- **Help.** A shared helper opens each app's user guide.

The configuration panels, `ConfigShape` and the merge are unchanged
([Tag Configuration Model](../../../../../docs/shared/design/configuration-model.md)).

## 1. The configuration editor

### Today

The Configuration tab shows only the attached tag's configuration. It is filled
by attaching, and `ConfigTab::Detach()` disables and hides it, so there is
nothing to edit without a tag. If the attach at startup fails, the tab is left
half set up: enabled, every panel hidden, buttons live. Restore merges a file
into the attached tag's configuration; with no tag, there is nothing to merge
into.

### The document

The tab holds one **document**:

- a `Config`;
- its tag type, which chooses the panels as today;
- its source: defaults, a file or a tag;
- a file name;
- an unsaved-changes flag.

The window title shows the source, the file name and the flag. The tab is
usable with no tag attached.

| Action | Effect |
| --- | --- |
| **New ▸ \<tag type\>** | The document becomes that type's default configuration. tagcore already embeds every `default-config.json`; it is parsed into a `Config`. Each tag type has one default (the two PresTag defaults are identical). |
| **Open…** | The document becomes the file, with the file's tag type choosing the panels. Implemented as that type's defaults with the file merged on top (`ConfigShape::forFile()` and `merge()`). A full file saved by qtmonitor loads as written; a partial hand-written file still yields a complete configuration. A file with no `tag_type` asks for one, offering the attached tag's type first. |
| **Read from Tag** | The document becomes the attached tag's configuration, as attaching does today. |
| **Save / Save As…** | Writes the document as today's Save does. |
| **Reset to Defaults** | Replaces the document's values with its tag type's defaults. |
| **Start** | Writes the document to the attached tag and starts it. The protocol has no "store configuration without starting", so writing to a tag is Start. See section 3. |

Opening a file no longer merges it into the attached tag. "Apply a file to this
tag" becomes Open, then Start.

**Attaching while a document is open** keeps the document. If the tag's type
matches, Start becomes available. If the document has unsaved changes, qtmonitor
asks before replacing it with the tag's configuration. With no document, or one
with no changes, the tag's configuration is read in as today.

**"Default" means the default config file, not what a reset tag reports.** For
BitTag and BitTagNG they are the same, because those firmwares report the
compiled-in file in IDLE. The other families build their IDLE configuration in
code, so a freshly reset tag may differ in fields its default config does not
describe (UIUCTag's `accel_type`, for one). The user guide says so.

## 2. Menus, actions and the status display

qtmonitor has 15 controls on three tabs, each enabled by its own code
(`applyStatus()`, `setAttachedUiEnabled()`, `ConfigTab::StateUpdate()`), and
an empty menu bar. Each command becomes one `QAction`, so the same action can
appear in a menu, a popup menu and a toolbar. One function sets every action's
enabled state from the attached tag's state and the document.

| Menu | Actions |
| --- | --- |
| **File** | New Configuration ▸ (tag types), Open Configuration…, Open Recent ▸, Save Configuration, Save Configuration As…, Download Data…, Save Error Log…, Quit |
| **Tag** | Attach, Detach, Read Configuration, Start, Stop, Synchronize Clock, Self-Test, Calibrate (tags that support it), Erase… (confirmed, since it destroys the log) |
| **Log** | Log Level ▸, Clear Error Log |
| **Help** | qtmonitor User Guide, About |

The menus are in the window, as in sensorviz (`setNativeMenuBar(false)`), so
the apps look the same on every platform.

**Open Recent** lists the last ten configuration files opened or saved,
newest first, kept in `QSettings`. A file that has since gone is dropped from
the list when chosen.

**Popup menus:**

- on the configuration page: Read from Tag, Open, Save, Reset to Defaults;
- on the error log: Save, Clear, Log Level.

**Start and Stop stay as buttons,** beside a **status display**, because they
are the most-used commands and change the state a user must not miss. Every
other button is removed in favour of its action. The status display shows:

| Item | Shows |
| --- | --- |
| Attached | the tag type and UUID, or "no tag" |
| State | the tag's state, as now |
| Clock | the offset from the host clock, and whether it is within tolerance |
| Self-test | the tag's reported self-test result |
| Configuration | the document's source and whether it matches the tag's type |
| Approved | the approval checkbox (section 3) |

## 3. Start checks

Start is **unavailable**, with the reason shown in the status display, unless:

- a tag is attached and IDLE;
- the document's tag type is the tag's type.

These cannot be overridden.

Start then **checks three conditions**:

| Check | Passes when |
| --- | --- |
| **Clock synchronized** | The offset at the latest status poll (the status's `millis` against the host clock, today's "Clock Error") is within a tolerance, proposed as 2 s. An unset clock fails. |
| **Self-test passed** | The tag reports `test_status` `ALL_PASSED`. The firmware keeps the result in `pState`, which survives resets until the retained state is reinitialised (`main.c`), so a pass can predate the current session. That was judged acceptable: the result is the tag's own, and running Self-Test again is one click. |
| **Configuration approved** | The user has ticked "Configuration approved". The tick is cleared by any change to the document: an edit, New, Open, Read, Reset, or a different tag type. |

If all three pass, Start proceeds as today. If any fails, a warning dialog
lists the failing checks and offers to **continue** or **cancel** the start,
with cancel the default. A start continued past a warning is recorded in the
error log, with the checks that failed.

## 4. Opening the user guide

sensorviz finds its packaged or built user guide and opens it
(`MainWindow::showUserGuide`, `host/applications/sensorviz/controls.cpp`).
The lookup is private to sensorviz. It moves to `host/common` as
`openUserGuide("apps/<page>")`, and qtmonitor's Help menu uses it. qtcalibrate,
qtprogram and btdataviz have no help access today; each can gain a Help menu
using the same helper.

## Order of work

Each step leaves qtmonitor working:

1. **Actions and menus, behaviour unchanged** (done 2026-10-23). The strip
   shows Attached or Detached first, then type, state, clock, voltage and
   self-test, and sits between the tab bar and the pages; the View menu is
   named Log, since it holds only log settings (all at the user's request). The Tag State tab becomes "Tag",
   holding identity and log counts. `qtmonitor --check-action-states` and
   `test/action-states.txt` pin which commands are available when; the table
   matched the old buttons' rules in all 32 rows. Self-test feedback was
   added: the strip shows Running until a finished result arrives (the tag's
   TEST state usually lasts less than one status poll), and the Error Log
   records each run's start and result with the board UUID.
   Planned as: The status display goes in;
   Start and Stop stay as buttons; every other button becomes an action. The
   configuration probe and its goldens are unaffected, since the panels do not
   change. The documentation screenshots gain menu captures, as sensorviz's do.
2. **The help helper,** moved to `host/common` and used by qtmonitor.
3. **The document model:** New, Open, Reset, Read, Save, the tab usable with
   no tag, and attach-while-editing. The probe gains cases for New from each
   tag type's defaults and for Open with no tag attached.
4. **Start checks:** the three checks, the approval box, the override dialog.
   A headless check covers which actions are enabled, and whether Start's
   checks pass, for each combination of tag state, attached or not, document
   type, clock offset, reported self-test result and approval.
5. **Help menus for the other apps,** using the helper.

## Decisions

Settled with the user on 2026-10-23:

- **Open makes a file the document.** It replaces Restore's direct merge into
  the attached tag; applying a file to a tag is Open, then Start.
- **The menus are in the window,** as in sensorviz.
- **The clock tolerance is 2 s.**
- **The self-test check uses the tag's reported result,** not a run tracked by
  qtmonitor.
- **File has Open Recent.**
- **Start and Stop stay as buttons.** Start's checks warn and offer to continue
  or cancel; they do not refuse.
