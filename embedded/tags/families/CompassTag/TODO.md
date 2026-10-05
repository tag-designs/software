---
type: worklist
status: current
summary: Open CompassTag power and monitor items left by the 2026-09 Standby-after-attach work.
---

# CompassTag TODO

- ~~**Move the calibration region to the end of flash, as IMUTag does.**~~
  **Rejected 2026-10-05, after it was built and measured.** It was scheduled on
  2026-10-04 and implemented the next day: `STM32L432xC.ld` was given the U375
  arrangement -- `.calibration` and the NAND map pinned against the top of
  flash, `.persistent` pinned from a 64 KB code ceiling, six `ASSERT`s -- and
  all four L432 targets linked with identical regions (calibration
  `0x0803f000`, persistent `0x08010000..0x0803f000`). It was then dropped
  without being flashed, for two reasons that only appeared once the cost was
  measured:

  - **The upgrade path is a full erase**, which is the hazard the pinning
    defends against. Field tags are programmed once and are not upgraded in
    place, so the benefit is close to theoretical.
  - **The cost falls on all four L432 targets to protect one.** Today an empty
    `.calibration` collapses -- on BitTag, PresTag and UIUCTag
    `cal_start == cal_end == nand_map == persist_start`, so only CompassTag
    reserves a page. Pinning charges the other three a page each for a region
    they never use, and the fixed code ceiling costs internal log capacity
    outright:

    | target | floating | pinned | change |
    | --- | ---: | ---: | ---: |
    | BitTag | 222 KB | 188 KB | **-15.3%** |
    | PresTag | 218 KB | 188 KB | -13.8% |
    | CompassTagAT25 | 210 KB | 188 KB | -10.5% |
    | UIUCTag | 210 KB | 188 KB | -10.5% |

    BitTag has no external flash, so its 15.3% is the whole deployment budget.

  **Standing policy instead: upgrade a provisioned board with
  `flash_release.py --erase` and reprovision it.** That erases and programs in
  one invocation, so it never leaves the reset-with-empty-flash window that
  latches `FLASH_SR.PEMPTY`, and recalibration costs less than the flash. Two
  findings from the 2026-10-04 attempt stand and are the reason the policy is
  stated rather than assumed:

  - **A byte-level backup is not a backup of the calibration's meaning.**
    Dumping the page before a mass erase and writing the same bytes back gave
    a byte-identical readback and calibration that did not work.
    Reprovisioning with `tag-cal`/`qtcalibrate` did.
  - **A calibrated unit must not be reflashed without comparing
    `__calibration_start__` between the two images.** The region moves when the
    image grows: at `4160d1e` it was `0x0800a800`, in `fw-v0.5` `0x0800b000`.
    When it moves, erase and recalibrate.

  Release notes for any image that moves the region must say so, because a tag
  reading an unwritten page looks like a calibration fault rather than an
  upgrade step.
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
