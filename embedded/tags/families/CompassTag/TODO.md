---
type: worklist
status: current
summary: Open CompassTag power and monitor items left by the 2026-09 Standby-after-attach work.
---

# CompassTag TODO

- **Move the calibration region to the end of flash, as IMUTag does — next
  firmware release.** On the L432 the linker places it as
  `.calibration (NOLOAD): ALIGN(2048)` straight after the code, so it moves
  whenever the image grows, and a firmware update leaves a calibrated board's
  bytes at an address the new image does not read. It has already moved: at
  `4160d1e` it was at `0x0800a800`; in `fw-v0.5` `__calibration_start__` is
  `0x0800b000`, with the loaded image ending at `0x0800aebc` — 324 bytes of
  headroom. The STM32U375 script pins both bounds with eleven `ASSERT`s,
  with a comment saying it is done so provisioned configuration and
  calibration survive a firmware update; `STM32L432xC.ld` has one `ASSERT`,
  about `.tag_identity`. Copy the U375 arrangement: pin the region against the
  top of flash and assert that code cannot reach it. Until then, a calibrated
  unit must not be reflashed without comparing the two addresses — see
  [the power test plan](design/power-test-plan.md).
- **Measure plain `CompassTag` (MX25R) after an attach.** It reproduced the
  Standby-after-attach fault on its own board, and it has not been measured
  since the fix. `CompassTagAT25` has been, on production hardware
  ([results](design/power-results.md), 2026-09-24). They share
  `pwr-l432.c` with no override, so the fix applies by construction, but that
  is not a hardware measurement.
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
