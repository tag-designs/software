---
type: results
status: current
summary: Every accepted change to the qtmonitor configuration-probe goldens, newest first, with the commit, the cases affected and the reason.
---

# Expected Differences in the Configuration Probe

Each entry records a change to `golden/` that a code change was meant to make:
which cases changed, how, and why. A difference that `compare.py` reports
without an entry here is a defect. See [README.md](README.md).

## 2026-10-23: Restore copies only what the tag supports and the file states

Change: stage 1 of the proposal. Restore's hand-written `mergeRestoredConfig()`
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
is on screen. Rollout step 1 of the proposal.

Differences from the previous goldens, all in `sent_with_schedule_tab`: that
key disappears from the 69 cases listed under "goldens created" below
(`bittag.json` 39, `bittag_le.json` 15, `bitprestag.json` 15). In each of them,
what is sent now equals the previous Sensors-tab `sent`, which is unchanged. No
other key changed in any case.

## 2026-10-23: goldens created

Reference build: qtmonitor at the commit that added the probe (rollout step 0
of [Configuration Shape and Panels](../../../../docs/shared/design/proposals/config-shape-and-panels.md)).
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
