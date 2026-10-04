---
type: worklist
status: current
summary: Open CompassTag power and monitor items left by the 2026-09 Standby-after-attach work.
---

# CompassTag TODO

- **Move the calibration region to the end of flash, as IMUTag does.
  Scheduled for the next firmware release (agreed 2026-10-04).** On the L432 the linker places it as
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

  Two things learned doing this on 2026-10-04, on a unit whose calibration was
  already at the address `fw-v0.5` uses:
  - **A byte-level backup is not a backup of the calibration's meaning.**
    Dumping the page before a mass erase and writing the same bytes back gave a
    byte-identical readback and calibration that did not work. Reprovisioning
    with `tag-cal`/`qtcalibrate` did.
  - **Mass erase costs a power cycle**, because `FLASH_SR.PEMPTY` is sticky and
    sends every reset to the ROM bootloader until power-on or an option-byte
    load. That makes "mass erase and reprovision" — the safe answer for a major
    upgrade that moves these regions — need physical access to the tag. Pinning
    the region removes the reason to mass erase in the first place, which is
    the stronger argument for doing it. See
    [the release procedure](../../../../docs/release/release-procedure.md).

  **Doing it orphans every board already calibrated**, which is the same
  hazard stated from the other side: pinning the region moves it, so the
  constants an existing unit holds end up at an address the new firmware does
  not read. Two units are known to be affected — the production
  `203633324B425006004A005D`, and `203633324B4250060022005E`, recalibrated by
  hand on 2026-10-04 with its constants at `0x0800b000`. The change therefore
  **Decided 2026-10-04: erase and recalibrate, no migration.** Existing units
  get `flash_release.py --erase`, which erases and programs in one invocation
  and so never leaves the reset-with-empty-flash window that latches
  `FLASH_SR.PEMPTY`, followed by a recalibration. For two boards that is
  cheaper than a migration path that has to know where the old region was, and
  it leaves no code behind to maintain.

  Whichever is chosen, the release notes must say that the image relocates
  calibration, because a tag that silently reads an unwritten page looks like
  a calibration fault rather than an upgrade step.
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
