---
type: worklist
status: current
summary: Open BitTag power-qualification work -- tool support, unmeasured phases, and a release-check bound.
---

# BitTag TODO

- **Give `tag_lifecycle_check.py` an attach retry.** The first attach to a
  sleeping BitTag fails and wakes the part, so the tool dies at
  `[1/5] reset to idle` and Phase A of the
  [power test plan](design/power-test-plan.md) has to be run by hand.
- **Run Phase B2 (format independence, `bittag-default.json`) and Phase D
  (activity sensitivity).** Neither was run in the first qualification.
- **Set a run-current bound and add BitTag to `tag_release_check.py`** once a
  second board or session agrees with the first. About 0.584 uA (1.15x the
  measured 0.5082 uA) would match IMUTag's margin. Release-check coverage for
  every tag is tracked in
  [firmware TODO](../TODO.md).
- **Sweep current against cell voltage**, from a fresh cell down to the 2.00 V
  firmware floor. BitTag has no regulator, so nothing measured at 2.5 V
  transfers to another voltage.
- **Sample VDD and temperature when a record is written, not on every wake.**
  `bt_state_run.c` calls `adcVDD()` on every RTC alarm wake and folds the
  result into an exponential average, but `pState->vdd100` and
  `pState->temp10` are only read in `datalog.c` when a record is written --
  `dlog.vdd100`, `dlog.temp10`, and the `< 200` low-battery test at the same
  moment. State markers do not depend on it either: `recordState()` takes its
  own reading through `tagStatusMeasure()`.

  The waste is set by the log format, because the wake is always once a
  minute while the record period is not:

  | Format | Record every | Wakes per record |
  | --- | ---: | ---: |
  | `BITTAG_BITPERSEC` | 60 s | 1 |
  | `BITTAG_BITSPERMIN` | 600 s | 10 |
  | `BITTAG_BITSPERFOURMIN` | 1920 s | 32 |
  | `BITTAG_BITSPERFIVEMIN` *(firmware default)* | 2100 s | 35 |

  It matters more now than it did: the `adcVDD()` fix of 2026-10-04 added a
  1 ms settling delay and moved both internal channels to 640.5 cycles, so
  each call costs roughly 1 ms of active current. Once a minute that is about
  **+0.03 µA average**, against a measured run current of 0.508 µA -- about
  6%. Moving the call to the write path removes almost all of it at the
  default format and changes nothing at `BITPERSEC`, which is what the
  2026-10-03 qualification used.

  Keep the exponential average and feed it at write time rather than deleting
  it: the smoothing then runs over records instead of minutes, which is still
  smoothed and is the cadence the value is actually stored at. Note this does
  change what gets logged, so it wants a before-and-after comparison on the
  same board rather than being slipped in.
