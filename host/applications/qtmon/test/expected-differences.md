---
type: results
status: current
summary: Every accepted change to the qtmonitor configuration-probe goldens, newest first, with the commit, the cases affected and the reason.
---

# Expected Differences in the Configuration Probe

Each entry records a change to `golden/` that a code change was meant to make:
which cases changed, how, and why. A difference that `compare.py` reports
without an entry here is a defect. See [README.md](README.md).

## 2026-10-23: the Configuration tab becomes an editor; Open replaces Restore

Change: step 3 of
[the editor, menus and start-checks proposal](../design/proposals/editor-menus-and-start-checks.md).
The tab holds a document, from a tag type's defaults, a file or a tag.
File ▸ Open replaces Restore: the file becomes the document, as its tag type's
defaults with the file merged on top, instead of being merged into the tag's
configuration.

The cases change to match:

- `restore:*` cases are renamed `open:*`, and their `restore` key becomes
  `open` (`Restored` becomes `Opened`).
- Every result gains a `document` key.
- 24 cases are new: `new` and `new@detached` (New from defaults, with and
  without a tag) and `open:own-full@detached`, one of each per tag type. In
  all three the document is unmodified, every control is enabled, and what is
  sent equals the base case, or New with a tag.

Compared with the previous goldens, with ids and keys mapped, only the 56
`open:foreign:*` cases differ. Opening another tag type's file now makes it
the document, with that type's panels and that type's configuration sent.
Before, its shared fields were merged into the attached tag's configuration.
No base, field, schedule or ADXL-combination case changed. Every own-type
`open:*` case (full file, one field, explicit zero) sends exactly what Restore
sent.

`action-states.txt` changes as designed:

- the `restore` column is replaced by `open`, `saveas` and `reset`;
- the `config` column becomes `document`, which is `none`, `same` or `other`;
- Read needs only an attached IDLE tag;
- Start also needs a document of the tag's type;
- Open is always available, and Save, Save As and Reset need a document.

All 34 rows were checked against these rules.

## 2026-10-23: the Configuration tab's buttons become menu actions

Change: step 1 of
[the editor, menus and start checks proposal](../design/proposals/editor-menus-and-start-checks.md).
The save, restore, read and start buttons, and their "Configuration File" and
"Configure Tag" group boxes, leave the Configuration tab. The commands are now
actions in the File and Tag menus, on the tab's popup menu, and (Start) on the
status strip, none of which is part of `ConfigTab`.

Every case's `ui` loses the two group lines and the four button lines. Nothing
was added, and no other key changed in any case. When each command is
available is now checked separately, by `qtmonitor --check-action-states`
against `action-states.txt`, which matches the old buttons' rules row for row.

## 2026-10-23: one page of fixed panels

Change: the fixed panels ([decision 0032](../../../../docs/decisions/0032-host-config-shape-and-fixed-panels.md)). The Schedule and Sensors sub-tabs, the four
modules and the visibility table are replaced by fixed panels selected by tag
type. The probe now runs each case once, since there are no sub-tabs to
select.

Compared with the previous goldens, nothing a tag receives changed, and nothing
on screen changed, except:

| Cases | Difference | Reason |
| --- | --- | --- |
| `hibernate.count=3` (7, one per tag type with hibernation) | `sent` and `ui` lose the third hibernation interval | The panel shows at most two rows, the firmware maximum (`Config.hibernate max_count:2`). Every firmware reports two, and none accepts three. |
| 130 `restore:` cases | `ui` loses one set of hibernation rows | A probe artefact in the old goldens, not real screen content. The old `Schedule` removed the rows it replaced with `deleteLater()`, which takes effect only when control returns to the event loop, so the probe saw both sets. In each case, the old fingerprint is exactly the new one plus the base configuration's rows. |
| `bittag_log=BITTAG_UNSPECIFIED`, `restore:zero:bittag_log` | `dialogs` lists the warning once, not twice | The probe now runs each case once. |
| every case | `layout` lists the shown panels instead of sub-tabs | The layout changed, as intended. |

`qtmonitor --check-config-panels` reports that the panels agree with every
tag's default configuration.

## 2026-10-23: Restore copies only what the tag supports and the file states

Change: `ConfigShape` and the shared merge ([decision 0032](../../../../docs/decisions/0032-host-config-shape-and-fixed-panels.md)). Restore's hand-written `mergeRestoredConfig()`
is replaced by `ConfigShape::forFile()` and `ConfigShape::merge()` in tagcore.
The visibility table now comes from `ConfigShape` instead of the build-time
generator (`config_shape_check` confirms it is identical for every tag type).

68 cases changed, all of them `restore:` cases. No base, field, schedule or
ADXL-combination case changed. They fall into three groups:

| Cases | Before | Now |
| --- | --- | --- |
| `restore:only:*` (24): the file states one field | A stated `adxl362` or `active_interval` field replaced the whole message, wiping its other fields. A one-field file onto a BitTagNG switched its sensor type to ADXL362 and its inactivity count to the widget minimum; a start-only file set the end time to 0 and switched the schedule to "run until". | Only the stated field changes. |
| `restore:zero:*` (26): the file states a zero for its own tag type | Zero scalars, unset enums and the zero interval end were skipped; a zero inside `adxl362` wiped the message as above. | The stated value is copied. A BitTag file stating `BITTAG_UNSPECIFIED` now reaches the tag, which raises the existing "missing log specification" warning (the one `dialogs` change). |
| `restore:foreign:*` (18): another tag type's full file | `adxl362` was copied whole: a BitTag file put its range, rate and filter on a BitTagNG and changed its sensor type to ADXL362; a BitTagNG file zeroed a BitPresTag's inactive threshold (clamped to 0.03 g). | The file speaks only for its own tag type's fields, and only those the target also has are copied. Save's zero padding for fields the file's type lacks is ignored. |

Two of the foreign cases (`bittag.json` from BitPresTag and BitTag LE) change
the fingerprint only. Before, the range buttons stayed on 4 g while the
threshold spin boxes were sized for 2 g, because the copied message had no
range. Now both reflect 4 g. What is sent is unchanged.

## 2026-10-23: what Start sends no longer depends on the selected sub-tab

Change: `Adxl362Config::GetConfig()` tested sample rate, range, filter and
inactive threshold with `isVisible()`. It now tests `!isHidden()`, which is
true whenever `SetConfig()` showed the control for this tag, whichever sub-tab
is on screen ([decision 0032](../../../../docs/decisions/0032-host-config-shape-and-fixed-panels.md)).

Differences from the previous goldens, all in `sent_with_schedule_tab`: that
key disappears from the 69 cases listed under "goldens created" below
(`bittag.json` 39, `bittag_le.json` 15, `bitprestag.json` 15). In each of them,
what is sent now equals the previous Sensors-tab `sent`, which is unchanged. No
other key changed in any case.

## 2026-10-23: goldens created

Reference build: qtmonitor at the commit that added the probe, the first step
of the work recorded in [decision 0032](../../../../docs/decisions/0032-host-config-shape-and-fixed-panels.md).
Its configuration code is today's, apart from two changes that do not alter
behaviour: the schedule reads the clock through `ConfigClock`, and Restore's
parse-and-merge moved into `ConfigTab::RestoreFromJsonText()`.

The goldens record one known defect, fixed in the entry above.

**What Start sends depends on the selected sub-tab, in 69 cases.**
`Adxl362Config::GetConfig()` writes sample rate, range, filter and the inactive
threshold only when their widget `isVisible()`. That is false whenever the
Sensors sub-tab is not selected. With the Schedule sub-tab selected, the tag's
original values are sent instead of the widgets' values. The cases that show
it are those where loading changes a value:

- a value the widget clamps, such as a BitPresTag inactive threshold of 6 g,
  sent as 2 g from the Sensors tab and 6 g from the Schedule tab;
- an enum the widget cannot show, such as a BitTag sample rate of 99, sent as
  12.5 Hz from the Sensors tab and 99 from the Schedule tab.

| Case file | Cases with `sent_with_schedule_tab` |
| --- | --- |
| `bittag.json` | 39 |
| `bittag_le.json` | 15 |
| `bitprestag.json` | 15 |

For a user, the same path means that an edit to the accelerometer's sample
rate, range, filter or inactive threshold is silently discarded if the Schedule
sub-tab is selected when Start is pressed. The active threshold and inactivity
time are written unconditionally and are not affected.
