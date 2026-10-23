---
type: decision
status: accepted
summary: qtmonitor became a configuration editor (Open makes a file the document, replacing Restore's merge into the tag), its buttons became actions in in-window menus with Start and Stop kept beside a status strip, and Start checks clock (2 s), the tag's reported self-test and an Approved box, warning with continue or cancel rather than refusing; Help menus share one user-guide finder.
---

# 0033. Host: qtmonitor as a configuration editor, with menus and Start checks

Date: 2026-10-23

## Context

qtmonitor's Configuration tab showed only the attached tag's configuration and
was disabled without a tag, so a configuration could not be prepared before
the tag was on the bench. Restore merged a file into the attached tag's
configuration; with no tag there was nothing to merge into. Fifteen buttons on
three tabs were each enabled by their own code, and the menu bar was empty.
Nothing stopped a tag being started with an unset clock, a failed self-test,
or a configuration nobody had reviewed.

## Decision

- **Open makes a file the document.** The tab holds one document, from a tag
  type's defaults, a file or a tag. A file opens as its tag type's defaults
  with the file merged on top, so partial files still give a complete
  configuration. Applying a file to a tag is Open, then Start. Attaching keeps
  a document from defaults or a file and replaces only a previous tag's.
- **Commands are actions in in-window menus,** File, Tag, Log and Help, as in
  sensorviz, with one function setting every action's enabled state. File has
  Open Recent (ten files). The menu holding only log settings is named Log.
- **Start and Stop stay as buttons,** in a status strip between the tab bar
  and the pages, visible on every tab, with Attached or Detached, type, state,
  clock, voltage, self-test and the Approved box.
- **Start checks warn; they do not refuse.** The clock must be within 2 s, the
  tag's self-reported `test_status` must be ALL_PASSED, and Approved must be
  ticked. On failure a dialog lists what failed and offers Start Anyway or
  Cancel (the default); a start made anyway is logged. An attached IDLE tag of
  the document's type remains a hard requirement.
- **Approved is cleared by any change to the values,** not by Save.
- **One user-guide finder,** `HostUserGuide` in `host/common`, for every Qt
  application's Help menu. btdataviz has none, as it is being deprecated.

The design as built is
[qtmonitor: Configuration Editor, Menus and Start Checks](../../host/applications/qtmon/design/editor-and-start-checks.md).

## Alternatives considered

- **Keep Restore, merging a file into the attached tag.** It cannot work
  without a tag, and it hid what would be sent behind a merge with whatever the
  tag held.
- **Start refuses when a check fails.** Rejected by the user: a field session
  may have a good reason to start anyway, and a warning that is logged keeps
  the record without blocking.
- **Track self-tests qtmonitor ran itself.** The tag's own `test_status`
  survives resets in `pState`, so it can predate the session; that was judged
  acceptable, since rerunning Self-Test is one click and the tag's report is
  the authority.
- **Native (macOS global) menu bar.** In-window menus look the same on every
  platform and match sensorviz.
- **Every command as a menu item, Start and Stop included.** Start and Stop
  change the state a user must not miss and are the most used, so they stay
  visible as buttons beside the status they change.

## Evidence

Headless checks with committed expected output pin the behaviour: the
configuration probe (New and Open cases added; only foreign-file opens
changed, as listed in `host/applications/qtmon/test/expected-differences.md`),
`--check-action-states` (matched the old buttons' rules in all 32 rows before
the editor added its own) and `--check-start-checks`. Each step was also tested
interactively on a tag.
