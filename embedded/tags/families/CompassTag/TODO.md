---
type: worklist
status: current
summary: Open CompassTag power and monitor items left by the 2026-09 Standby-after-attach work.
---

# CompassTag TODO

- ~~**Measure plain `CompassTag` (MX25R) after an attach.**~~ **Dropped
  2026-10-05: the MX25R path is abandoned.** It reproduced the
  Standby-after-attach fault on its own board and was never measured after the
  fix; `CompassTagAT25` was, on production hardware
  ([results](design/power-results.md), 2026-09-24). They share `pwr-l432.c`
  with no override, so the fix applies to the MX25R image by construction --
  which was never a substitute for a measurement, and now does not need to be.
  **No MX25R measurement is owed.** The `CompassTag` target still builds; it
  is simply not a qualification target, and nothing should block on it.
- **Re-confirm the never-attached cold baseline after the fix.** Remove all
  power, including any cell, and measure before a probe ever touches the
  board. The 376 nA figure is the pre-fix cold measurement.
- **The ABORTED-on-first-boot fix (`379e3f1`) has not been recorded as verified
  on hardware.**
- **`isMonitorEnabled()`'s `MONCONNECTED` latch.** STM32L4 has no
  `TAG_MONITOR_MAILBOX`, and the latch bug noted in `handlers.c` is real. It
  was ruled out as the cause of the Standby fault, because a clean
  `MONITORSTOP` measurably did not fix the stuck current, but it is still open.
- **Host `TagMonitor::Call()` `MONITORSTOP` path.** It reports success after a
  blind 5 ms sleep instead of polling for completion, unlike every other
  operation. Changing it needs a validation cycle across every tag.
