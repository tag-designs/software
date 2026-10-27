---
type: readme
status: current
summary: How to run qtmonitor's configuration probe over the committed cases, compare the results with the golden set, and regenerate cases or goldens.
---

# qtmonitor Configuration Probe

The configuration screen takes in a `Config`: from the tag's `GetConfig`, a
fixture, a tag type's defaults, or an opened file. It hands one back to Start. No hardware is
involved in between. The probe runs that screen headless over a fixed set of
cases and records, for each case:

- what Start would send;
- a fingerprint of every visible control;
- any dialog raised.

Comparing two builds' results shows whether a change to the screen changed
anything a tag or a user could see. It is how the configuration screen is
checked without testing every tag on the bench; see
[Tag Configuration Model](../../../../docs/shared/design/configuration-model.md)
and [decision 0032](../../../../docs/decisions/0032-host-config-shape-and-fixed-panels.md).

| Path | What it is |
| --- | --- |
| `cases/<tag>.json` | The committed cases, one file per tag type. Generated; do not edit by hand. |
| `golden/<tag>.json` | Results from the reference build. Every comparison is made against these. |
| `expected-differences.md` | Every accepted change to the goldens, with its reason. |
| `action-states.txt` | Which commands are enabled in which state; see below. |
| `start-checks.txt` | Which start checks fail for which clock, self-test and approval; see below. |
| `probe.sh` | Runs a qtmonitor build over `cases/`. |
| `compare.py` | Compares two result directories. |
| `config_probe_cases.cc` | Generates `cases/`. |

## Checking a change

Build qtmonitor, run the probe, and compare the results with the goldens:

```sh
cmake --build <build-dir> --target qtmonitor
host/applications/qtmon/test/probe.sh \
    <build-dir>/Release/bin/qtmonitor.app/Contents/MacOS/qtmonitor /tmp/probe
host/applications/qtmon/test/compare.py host/applications/qtmon/test/golden /tmp/probe
```

On Linux and Windows the executable is `<build-dir>/<config>/bin/qtmonitor`.
The probe takes about 40 seconds. `compare.py` exits 0 when nothing differs.
Otherwise it prints each differing case, key by key, followed by a count per
key. `--max N` limits the detail, and `--ignore KEY` leaves out a key that a
stage is expected to change, such as `layout`.

A difference is either a defect or an intended change. An intended change is
recorded in `expected-differences.md`, and then the goldens are regenerated
from the new build:

```sh
host/applications/qtmon/test/probe.sh <qtmonitor> host/applications/qtmon/test/golden
```

Commit the goldens and the `expected-differences.md` entry together with the
code change, so a reviewer can see what moved and why.

## What a case and a result contain

Every case has an `id`, a fixed `now` (seconds since the epoch, UTC), the tag
`state`, and the `config` as the tag would send it (zero-valued fields
omitted). Optionally:

- `"new": true`: the document is New from `config`'s tag type's defaults,
  instead of `config` itself;
- `"open"`: the text of a file to open (File ▸ Open), which then becomes the
  document. A file naming no tag type opens as `config`'s type;
- `"attached": false`: no tag is attached, so the tab is used as an editor.

For each case, the probe builds a fresh `ConfigTab`, as a qtmonitor that has
just started would. It fixes the schedule's clock at `now`, loads the
document, reports the tag's state unless the case is detached, opens the file
if there is one, and calls `GetConfig()`, exactly as Start does. The result
holds:

| Key | Meaning |
| --- | --- |
| `sent`, `sent_hex` | What Start would send, as JSON with every field and as serialised bytes |
| `get_config_ok` | `GetConfig()`'s return value |
| `document` | The document's source (`Defaults`, `File` or `Tag`), tag type, and whether it has unsaved changes |
| `open`, `open_detail` | `Opened`, `ParseError` or `NoDefaults`, and why (`restore`, `restore_detail` in results from before File ▸ Open) |
| `ui` | One line per visible control: enclosing group titles, then kind, label, value, range, step, decimals, suffix, enabled and tooltip. Sorted, so moving a control does not change it. A control scrolled out of view counts as visible. |
| `layout` | Which panels are shown, and whether the tab as a whole is visible and enabled; changes when the layout does |
| `dialogs` | Text of each message box raised. The probe records and dismisses it. |

Goldens from before the single-page layout (2026-10-23 and earlier entries in
`expected-differences.md`) also had `sent_with_schedule_tab` and
`ui_with_schedule_tab`, from running each case once per sub-tab.

The panel table is checked separately, with no cases:
`qtmonitor --check-config-panels` exits non-zero if any tag type's panels and
default configuration disagree.

## The cases

`config_probe_cases` writes them from the tags' default configs
(`embedded/proto-c/*-proto-c/default-config.json`) and the captured fixtures
(`host/docs/fixtures/qtmonitor/`). For each tag type, the cases are:

- **the base configuration,** in IDLE and in RUNNING. It is taken from the
  tag's fixture when there is one, which is real firmware output, and from its
  default config otherwise (BitTagNG, UIUCTag);
- **each field the default config lists,** set in turn to:
  - for an enum, every defined value, 0, and the undefined value 99;
  - for a number, one of a fixed list covering 0, negatives, values off the
    step, and values out of range;
- **schedule starts and ends** around `now`, and **hibernation rows**: unset,
  forever, in order, inverted, in the past, and 0, 1 or 3 rows;
- **the ADXL362 fields that constrain each other:** range against both
  thresholds, and sample rate against inactivity time;
- **New** from the tag type's defaults, with and without a tag attached;
- **opening files:**
  - the tag's own full file, as Save writes it, with and without a tag;
  - each field alone;
  - each field as an explicit zero;
  - every other tag's file;
  - an empty object, malformed JSON, an unknown key, and a wrong type.

The fields come from walking each default config against the `Config`
descriptor, so a field added to a default config is covered after the cases
are regenerated:

```sh
cmake -DBUILD_QTMON_CHECKS=ON <build-dir>
cmake --build <build-dir> --target config_probe_cases
<build-dir>/Release/bin/config_probe_cases . host/applications/qtmon/test/cases
```

Run it from the repository root. The output is deterministic, so an unchanged
tree reproduces the committed files exactly. **New cases need goldens from the
reference build, not from the build under test.** Otherwise a change and its
own check would come from the same code. Regenerate the goldens from the
commit before the change, then compare the change against them.

## Which commands are available when

`qtmonitor --check-action-states` prints which actions
`MainWindow::updateActions()` enables, disables or hides, for every
combination of:

- attached or detached;
- each tag state;
- two tag types (BitTag LE and CompassTag);
- a configuration loaded or not;
- a download running;
- a self-test pending.

Compare it with the committed table:

```sh
QT_QPA_PLATFORM=offscreen <qtmonitor> --check-action-states | \
    diff - host/applications/qtmon/test/action-states.txt
```

No output means nothing changed. The table was checked, row for row, against
the enabling rules of the buttons it replaced. A change to when a command is
available updates the table in the same commit, so the review shows exactly
which rows moved.

## Which start checks fail when

`qtmonitor --check-start-checks` prints which of Start's checks fail
(`MainWindow::startCheckFailures()`) for every combination of a clock offset
on and either side of the 2 s tolerance, a reported self-test result, and
Approved ticked or not:

```sh
QT_QPA_PLATFORM=offscreen <qtmonitor> --check-start-checks | \
    diff - host/applications/qtmon/test/start-checks.txt
```

No output means nothing changed.

## What the probe does not cover

- **Typing into a widget.** A user's edit reaches `GetConfig()` by the same
  load-and-store path the cases exercise. The one thing missed is a range that
  narrows while the user types, such as ADXL362 thresholds following the range
  buttons. That needs direct widget checks.
- **One session attaching several tags.** Each run uses a fresh `ConfigTab`,
  so state left by one tag type is never seen by the next.
- **The USB attach, Read and Start plumbing.** One live attach on any tag
  covers it.
- **What the firmware does with the configuration.** The probe shows what
  qtmonitor sends, not whether the tag accepts it.
