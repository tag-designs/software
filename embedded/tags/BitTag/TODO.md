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
- **Done 2026-10-05: sample VDD and temperature at the write site.**
  `adcVDD()` ran on every RTC alarm wake and fed an exponential average, but
  `pState->vdd100` and `temp10` are only consumed when a record is written, and
  `recordState()` takes its own reading for state markers. The wake is always
  once a minute and the record period is not, so at the firmware default of
  `BITSPERFIVEMIN` that was 35 conversions per stored value. The call now sits
  immediately before `writeDataLog()`.

  The average went with it. `git log -S` puts it in the initial commit
  alongside the two `adcVDD()` defects fixed in `54135465`, so it was most
  likely smoothing a reading that would not sit still; with those fixed,
  consecutive raw samples agree to about 10 mV and 1.4 C.

  **No battery-life claim attaches to this.** The cost of an `adcVDD()` call
  was measured at tens of microseconds of active time, not the millisecond
  first estimated -- the settling delay is a thread sleep, so the MCU idles
  through it. The saving at `BITSPERFIVEMIN` is therefore below what a 1200 s
  window resolves. It was done because 34 of 35 conversions were redundant and
  the filter was compensating for a fixed bug, not to save current.
