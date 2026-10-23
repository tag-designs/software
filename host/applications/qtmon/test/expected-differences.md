---
type: results
status: current
summary: Every accepted change to the qtmonitor configuration-probe goldens, newest first, with the commit, the cases affected and the reason.
---

# Expected Differences in the Configuration Probe

Each entry records a change to `golden/` that a code change was meant to make:
which cases changed, how, and why. A difference that `compare.py` reports
without an entry here is a defect. See [README.md](README.md).

## 2026-10-23: goldens created

Reference build: qtmonitor at the commit that added the probe (rollout step 0
of [Configuration Shape and Panels](../../../../docs/shared/design/proposals/config-shape-and-panels.md)).
Its configuration code is today's, apart from two changes that do not alter
behaviour: the schedule reads the clock through `ConfigClock`, and Restore's
parse-and-merge moved into `ConfigTab::RestoreFromJsonText()`.

The goldens record one known defect, which rollout step 1 will fix.

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
