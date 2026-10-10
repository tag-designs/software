---
type: worklist
status: current
summary: Open tag firmware work -- defects found by reading code, low-power and recovery questions, NAND bring-up, the next release's remaining items and their host counterparts, and cleanup.
---

# Tag Firmware TODO

## Before the next release (opened 2026-10-04/05)

Background:
[the ADC and power-floor investigation](design/investigations/2026-10-adc-and-the-power-floor.md).

- ~~**Requalify all five distributed targets.**~~ **Done 2026-10-08: all five
  PASS against the `fw-v0.6` release image**, each measured from the published
  image rather than a local rebuild, each with its session logs attached to the
  release.

  | Target | Result | Supply | Idle | Running |
  | --- | --- | ---: | ---: | ---: |
  | `IMUTagNandBmp581` | **PASS** 10-07, storms included ([results](families/IMUTag/design/power.md)) | 3.693 V | 6.43 µA | 665.41 µA @ 400 Hz |
  | `CompassTagAT25` | **PASS** 10-06 ([results](families/CompassTag/design/power-results.md)) | 2.496 V | 0.21 µA | 1.95 / 1.9478 µA @ 30 s |
  | `UIUCTag` | **PASS** 10-08 ([results](UIUCTag/design/power-results.md)) | 2.496 V | 0.1572 µA | 0.5620 µA @ 300 s |
  | `BitTag` | **PASS** 10-07, both log formats ([results](BitTag/design/power-results.md)) | 2.496 V | 0.1185 µA | 0.4941 µA default |
  | `PresTag` | **PASS** 10-07, first ever ([results](families/PresTag/design/power-results.md)) | 2.496 V | 0.1118 µA | 0.3894 / 0.3908 µA @ 60 s |

  **No power regression.** Running current moved +0.5% on IMUTagNandBmp581,
  −0.5% on CompassTagAT25 and −0.14% on BitTag against their previous qualified
  figures, on the same boards and supplies. IMUTag is the one that matters: it
  draws three orders of magnitude more than the rest, so it alone sets whether
  a deployment makes its battery life, and it is the part where image layout has
  moved idle current by 150x before. Its firmware contribution is isolated
  because the outgoing image was measured on the same board in the same session
  before flashing.

  **What the next requalification needs to know**, learned doing this one:

  - **Pass the bounds for every target but IMUTag.** `tag_release_check.py`
    defaults to a 100 µA sleep threshold, which is also the floor the run must
    clear, so a sub-100 µA run is failed as "not collecting". `630ce14e` lets
    the bounds through; the defaults stay because IMUTag needs them.
  - **The harness cannot drive BitTag, PresTag or UIUCTag**, aborting on
    `Monitor attach failed: initial DEMCR read failed`. Those were qualified by
    running the phases by hand. **That is the harness aborting, not the tag**:
    no run in any session reached `ABORTED`.
  - **The harness checks neither calibration nor `tag-test`.** Close both by
    hand. Only CompassTag carries calibration, but no target's qualification
    includes a self-test unless one is run.
  - **Measure the outgoing image before flashing.** One 120 s window separates
    the firmware's contribution from the board's, and without it IMUTag would
    have looked like a 16% idle regression that belonged to the board.

- ~~**Find what lowered PresTag's resting floor.**~~ **Closed 2026-10-05: the
  firmware did not lower it.** The September build `890a11b` was rebuilt and
  flashed to PresTag board B, which measured **0.1267 µA** idle against
  **0.1341 µA** for the same board on `d41b5357` -- a 5.8% spread, with the
  dearer figure on the *current* firmware. One board, two builds, no
  firmware effect. The bisection of `890a11b..d41b5357` is cancelled.

  What remains is that September's **0.2810 µA** is unreproducible on either
  board under any build tried. It was anomalous when it was taken: BitTag
  carries strictly more parts in sleep -- an ADXL362 in shutdown on top of
  PresTag's RV-3028 and L432 -- and reads **0.1169 µA**, so PresTag could not
  legitimately rest at 2.4x BitTag. The remaining explanations are board
  condition (surface leakage from flux residue, which is humidity- and
  temperature-dependent; the session was taken on a hot day) and the session
  itself. Neither is now worth chasing: **treat 0.2810 µA as suspect data, not
  as a superseded result.** The lesson is the recording checklist, which now
  demands a board UUID -- the September entry has none, so it cannot be
  attributed to a board even in principle.
- **Verify the stored-configuration write**, at least part 1 of
  [the proposal](design/proposals/stored-config-write-is-unchecked.md) --
  verify and report, which is safe on a shared page. A qualification taken
  against an unverified config write can report a pass for a tag running a
  configuration nobody chose, which is worse than a failure because it looks
  fine.
- **Explain the 11% gap** between PresTag's fitted `I_rest` (0.1122 µA) and its
  measured `IDLE` (0.1261 µA). September had the two within 1.1%.
- **Test `flash_release.py`'s PEMPTY clearing on hardware.** The conditional
  clear has only ever reported "already clear"; the branch that writes the
  register is unexercised.

Open work only. Delete an item when it is done; put what was learned in an
investigation or decision record. Items marked *found by reading code* have
not been reproduced on a tag.

## Defects

- **The state-marker log stops silently when full.** `recordState()`
  (`common/core/src/persistent.c`) stops writing once `sEpoch[]` is full: 24
  entries on L4, 20 on U3. The only notice is a scratchpad `ESLF` record, and
  field builds have no scratchpad. So a tag that filled its marker log and then
  failed looks the same as one that simply stopped transitioning (Field Data
  Extraction, Gap 2). Fix: reserve the final slot for an overflow marker, or
  keep a dropped-transition count that a later write folds in. Verify: drive
  more than `sEPOCH_SIZE` transitions on the bench and confirm the log shows
  the overflow. *Found by reading code.*
- **Resets and faults leave no marker.** A hard fault, a watchdog reset or a
  brown-out leaves nothing in the flash record, and the tag simply reappears
  in an earlier state. Fix: latch the reset cause from `RCC_CSR` at boot, and
  record a marker only when it is not an ordinary power-on or a monitor attach,
  so attach storms do not burn the log. Weigh the flash write against
  endurance on a 12 mAh cell first; this is an open question in Field Data
  Extraction.
- **`gd5fSectorErase()` reports success without erasing** on three paths:
  logical block out of range, mapping failure, and physical block out of range.
  A caller cannot tell "erased" from "silently skipped". *Found by reading
  code.*
- **`gd5fRead()` does not invalidate `gd5f_cache_active`.** Only
  `gd5fProgramCacheLoad()` and `gd5fProgramExecuteCache()` clear the flag. A
  read can therefore load a different page into the device cache register while
  the flag still claims the programmed page is resident. *Found by reading
  code.*
- **Unbounded hardware waits** in the IMUTagNandBmp581 RTC LLD
  (`IMUTagNandBmp581/src/hal_rtc_lld.c`): the `ALRAWF`, `ALRBWF` and `WUTWF`
  waits, and the `do`/`while` inside a critical zone. Any of them hangs the tag
  if the bit never sets. *Found by reading code.*
- **Asymmetric wakeup-timer disable.** `rtcSTM32SetPeriodicWakeup()` waits for
  `WUTWF` when arming but not when disarming. It was exonerated as a cause of
  an idle fault, because compiling the wakeup timer out changed nothing, but
  the asymmetry is still there. The IMUTag family never calls `enableTicker()`,
  so it never arms the timer.
- **BitTagNG writes stray RAM into flash.** Its 28-byte `t_storedconfig` is an
  odd number of words, and on STM32L4 `FLASH_Program_Array()` programs one word
  past the struct for an odd word count. The extra word lands in alignment
  padding. *Found by reading code.*
- **CompassTag and UIUCTag reattach paths are unchecked.** After the
  2026-10-02 rule that only true failures abort (see
  [the investigation](design/investigations/2026-10-reset-during-running-aborts.md)),
  PresTag needed `Running(T_CONT, POWERFAIL)` to re-arm its sample ticker.
  CompassTag and UIUCTag were not re-tested and may need the same re-arm.
- **The `debug_log` module prevents low-power entry on STM32U375** (1.71 mA).
  This is a fault in the module itself, separate from the flash-flag and layout
  faults; see the warning in `IMUTagNandBmp581/project.mk`.
- **IMUTagNandBmp581 RTC sync.** `tag-start --set-rtc` intermittently failed
  with "RTC sync failed while writing tag clock", and boots often reported
  `rtcInitializedAtBoot` and `clockTrusted` false. The I2C bus clear
  ([0005](../../docs/decisions/0005-i2c-clear-a-stuck-bus-at-session-start-boot-and-standby.md))
  removed the stuck-bus cause. A single failure in 30 was still seen on
  2026-09-07 ([0007](../../docs/decisions/0007-u375-run-sleep-is-stop-2.md)).
  Confirm whether a residual RV-3028 write fault remains.

## STM32U375 low power

- **Open question: which monitor predicate guards the terminal sleep.**
  `tagPowerEnterStop3()` refuses only while `monitorIsAttached()`. The retired
  Standby path, the idle hook and the RUNNING wait use the broader
  `isMonitorEnabled()`, which on U3 also covers `monitorAttachGraceActive()`.
  During an attach grace the tag can therefore enter Stop 3 but not returned
  STOP. Decide whether that is intended
  ([STM32U375 Low Power](common/core/design/u375-low-power.md#monitor-guards)).
- **Move the flash error-flag clear to where errors occur.**
  `tagPowerClearFlashErrorFlags()` runs before every terminal sleep and clears
  any latched error without reporting it. Clear and report the flags at the
  datalog flash operation that latched them, where the error can be attributed
  to an access. Measure idle before and after: this touches the power path.
- **Scratchpad retention through Stop 3 is unverified.** `tagScratchRetain()`
  (`PWR_CR1_RRSB3`) is called only from `tagPowerEnterStandby()`, which is
  unused. Check whether the scratchpad survives the Stop 3 terminal sleep and
  the reset that follows it, and whether the retention bit is needed there.
- **Dead code in `pwr-u375.c`.** `tagPowerRestoreClocksAfterStop3()`,
  `tagPowerRestoreFlashAfterStop3()` and `tagPowerPostStop3WakeEventI()` are
  never called, and `TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS` is defined but never
  read. Remove them or document them as unused. This changes the image, so
  measure idle and run current after the change.
- **Standby bias for interrupt lines.** On both IMUTagNand boards `WKUP1`,
  `LSM_TRG`, `LPS_DRDY` and `BMM_INT` (and `LMS_TRIG_2` on v1) are left
  unbiased "while measuring whether interrupt pulldowns fight latched
  active-high outputs". The outcome was not recorded. Decide the bias and
  update `embedded/boards/IMUTagNandv*/cfg/board-customizations.json` and the
  boards' `standby-pins.md`.
- **LPTIM-backed system timer.** Not built; see
  [the proposal](common/core/design/lptim-system-timer.md).
- **Validate `stopMilliseconds()` on L432 hardware**, if not already done:
  scope or log 1, 2, 10 and 100 ms delays and confirm none returns early, and
  confirm that monitor-attached behaviour uses `chThdSleepMilliseconds()`.

## Restart recovery

- **The monitor-attach recovery branch adopts retained state without
  cross-checking the marker log.** Nothing depends on that now, but if
  `pState->state` is ever wiped or corrupted, it will again outrank durable
  flash evidence.
- **Acquisition-phase sentinels** are not implemented. Add retained sentinels
  around sensor read, header write, external data write and cursor commit, so
  recovery can decide whether the final pre-reset page is complete, abandoned
  or should be hidden. Download timing would then combine `vddHeader` anchors,
  state markers for discontinuities, and the sentinel state.
- **Consider a full erase sweep** when a run did not finish with clearly
  recoverable boundaries.
- **Exercise checked flash reads on hardware.** Reset a tag repeatedly during
  internal header writes, and verify that recovery and download stop at the
  last checked-readable header instead of entering the exception path. If an
  ECC-faulted double-word can be produced on a bench unit, check that a
  guarded read returns an ECC error and an unguarded read still takes the
  ordinary exception path.
- **Host erase-progress fallback.** Once deployed firmware reliably reports
  `Status.erase_sectors_total_plus_one`, remove the IMUTag page-size fallback
  (`kImuDataLogPageBytes`) from `qtmon` and `qtprogram`, so they no longer
  need to know `DATALOG_SAMPLES * sizeof(t_DataLog)`.

## IMUTagNand and IMUTagNandBmp581 NAND bring-up

- Investigate the GD5F2GM7RE `B9h` extreme-low-power command, and the wake
  sequence and timing needed before the shared NAND driver can use it
  (IMUTagNandBmp581).
- Exercise the NAND erase path with a provisioned map present.
- Confirm the tag refuses to start collection when the NAND map is absent.
- Test page-cache write behaviour across NAND page boundaries.
- Verify that normal reprogramming and erase flows preserve calibration, the
  NAND map, and the dedicated configuration pages.
- Test behaviour with simulated or forced factory bad-block markers.
- Handle ECC status on NAND page reads.
- Provide a "bad page" marker or equivalent read result, so host download can
  skip unreadable pages instead of failing the whole download.
- Decide where bad-page observations live after runtime reads: a transient RAM
  table, the debug log only, a persisted table, or appended download metadata.
- Add a way to inspect bad-page information during download, by dumping the
  table or through a monitor or debug command.
- Confirm downloader behaviour when sparse pages are skipped: sample count,
  timestamp continuity, and log metadata.
- Keep first-boot map provisioning read-only with respect to NAND factory
  markers.
- Confirm that logical-to-physical map validation rejects erased, unsorted or
  out-of-range entries.
- Confirm that erase always checks physical bad-block markers before issuing a
  NAND block erase.
- Decide whether runtime program or erase failures retire blocks, report an
  error only, or both.
- Keep qtmonitor line-buffering behaviour under longer debug streams.
- Consider quieting duplicate NAND ID logs once bring-up stabilizes.
- Add host-side checks that distinguish an absent map, a bad NAND ID, an
  unreadable page, and erased or no-data cases.

## Next release

The release after `fw-v0.0.3`. The fixes already landed, and how each was
verified, are in
[the next-release investigation](design/investigations/2026-10-next-release-fixes-verified.md).

### Remaining measurements for landed fixes

- PresTag run and idle current against its flash-log entry 0
  (`captures/2026-10-02-prestag-d1/flash-log.md`), for the mid-page halt fix.
- CompassTag run and idle current against its entry 0
  (`captures/2026-10-02-compasstag-d1`), for the same fix.
- IMUTagNandBmp581 currents for the external-reset-is-a-reattach image
  (entry 2 of `captures/2026-10-02-imutag-nand-bmp581/flash-log.md`).

### Offline reconstruction layout

Specified in
[Offline Log Reconstruction, Decisions and plan](../../docs/investigations/2026-10-offline-log-reconstruction.md#decisions-and-plan)
and the records
[0021](../../docs/decisions/0021-offline-rebuild-tag-identity-record.md),
[0020](../../docs/decisions/0020-offline-rebuild-session-facts-in-stored-config.md)
and [0017](../../docs/decisions/0017-offline-rebuild-build-time-layout-descriptor.md).

- **Identity record (B1):** add the compiler version. The build-options digest
  does not cover the toolchain: an image built on another machine had the same
  digest as a local build of the same source, but was 460 bytes smaller, with
  every code address shifted. Also add the GD5F logical block count.
- **B1 on hardware:** families other than PresTag, CompassTagAT25, UIUCTag and
  IMUTagNandBmp581 are not yet checked; BitTag remains.
- **Session facts (B2), still to do:** the effective sample settings as values
  (sample period, ODR, ranges), and optionally the nanopb-encoded `Config`
  beside the raw struct, which removes the per-family `readConfig()` mapping
  from the offline path. Give new members fixed-width integer types (short-enum
  ABI), and check the larger struct still fits its page or region, especially
  on L4, where `sconfig` shares `.persistent`.
- **Build-time layout descriptor (B3).** Not built.
  - Compile a small C program for each target, with that target's flags and
    `-fshort-enums`. It emits a JSON descriptor: `sizeof` and member offsets
    for every stored struct, the same constants as B1, and the symbol
    addresses read from the linked ELF.
  - Install the descriptor beside the ELF and record its hash in the build
    manifest.
  - Copy the `.proto` files, or a `FileDescriptorSet`, into the package.
  - Generate B1's record and the descriptor from the same definitions.
  - Verify: for each fw-v0.0.3 target the descriptor agrees with the struct
    sizes and offsets established by hand in Offline Log Reconstruction, and a
    reproducible build regenerates it byte-identically.

### Release qualification

Every item above changes a tag image, so this release needs measurement, not
argument. The procedure is
[the release procedure](../../docs/release/release-procedure.md).

- **The current checks are the gate for this release.** For each shipped
  target, measure idle, running and finished current from a clean build of the
  release commit, and record the numbers as the next release's baseline.
  IMUTagNandBmp581 is still a prototype, with boards out for fab, so it is not
  gated on `tag_release_check.py` for this release. Run that check when the
  target ships.
- **Qualify the never-qualified targets first, then redo IMUTagNandBmp581**
  (agreed 2026-10-03). PresTag, CompassTagAT25 and UIUCTag have no power
  qualification. BitTag's first was committed in 009bc134 against
  [its plan](BitTag/design/power-test-plan.md). IMUTagNandBmp581's
  `fw-v0.5`-era measurement was of a locally built image and needs redoing
  against the release.
- **Each qualification updates the release notes**, per *Publishing the
  qualification* in the release procedure: correct the "not bench-tested"
  sentence for each target as it is cleared.
- **Functional checks beyond the gate:** the CompassTag resume test, the
  full-flash download test, and a `tag-capture` of each target to confirm the
  identity record and session facts read back from the capture.
- **Make `tag_release_check.py` cover every tag.** Today it is a full gate only
  for IMUTagNandBmp581:
  - `power-configs/` holds configs only for IMUTag and BitTag;
  - `--run-max-ua 760` is sized for IMUTag at 400 Hz from 3.7 V;
  - the sample-count check reads only IMUTag's `lsm6.odr`;
  - the life-cycle run is a fixed 60 s.

  For other tags the life-cycle step is not meaningful. Its run bound is too
  high for PresTag and BitTag to catch anything. CompassTag and UIUCTag
  probably produce no downloadable row in 60 s, so they would fail on an
  empty table. The fix needs:
  - a config per tag (PresTag, CompassTag and UIUCTag have none);
  - per-target defaults, chosen by `--target`, for the run-current bound,
    taken from a known-good measurement;
  - a run long enough to fill a downloadable page;
  - a per-tag sample-count check.
- Tag the release and publish the package.

### Host counterparts

These ship separately but are needed for the firmware changes to be useful.

- **`tag-start --start-timeout` does not wait.** Its help says "seconds to wait
  for the tag to leave IDLE after the start is accepted", but the poll loop in
  `host/commandline/tag-start.cc` breaks on the **first** failed status read:

  ```c
  read_ok = tag.GetStatus(status);
  if (!read_ok) break;          /* gives up; the remaining tries are not used */
  ```

  A tag that sleeps straight after a start -- the normal case -- drops the
  debug link, so the first read fails and `--start-timeout 90` waits about zero
  seconds. The flag is inoperative in exactly the situation it was added for,
  and the tool reports `State: not confirmed (last read: IDLE)`.

  **At minimum the help text is wrong.** Whether the loop should instead keep
  trying for the remaining timeout depends on something untested: whether the
  monitor link recovers when the tag next wakes, without a re-attach. If it
  does not -- hotplug does not work on this rig -- the current behaviour is
  right and only the documentation needs fixing. Test that before changing the
  loop. `tag-stop` has the same shape and exits 1 on a failed read.

  This matters beyond tidiness: **there is currently no command-line way to
  confirm a tag reached RUNNING.** Re-attaching to check resets the run, and
  holding the link keeps `isMonitorEnabled()` true so the tag never sleeps.
  That gap is what makes a UIUCTag run hard to qualify, because a download with
  no samples cannot be distinguished from a run that never started.

- **D1, offline rebuild:** decoders exist for all five distributed targets
  ([0018](../../docs/decisions/0018-offline-rebuild-capture-backed-source.md)).
  Still to do: the fw-v0.0.3 layouts, and the GD5F logical block count in the
  identity record.
- **D2:** in `tagcore/recovery`, read the identity record at the per-MCU address
  (add it to `swdmcu`). Check its magic, size and end entry (the record has no
  CRC), and use it to choose the loader and decoder. Without the record, fall
  back to hash or string identification.
- **D3, capture additions:** the EEOffset over I2C for tags already deployed;
  NAND pages read through on-die ECC, with their status; internal-flash ECC
  faults.
- **D4:** `host/libraries/tagcore/legacy_bittag_rescue.cc` should take
  `vddHeader` from the ELF instead of the hard-coded `0x08007a68`, and accept
  `BITTAG_LE`.
- **D5:** decoders should read CompassTag `temp10` as signed for fw-v0.0.3
  offline decoding, and read the new `sconfig` members by layout version.

## Cleanup

- **Move sensor orchestration out of `state_run.c`.** Some older tags still
  configure sensors directly from `state_run.c`, which crowds the acquisition
  state with device setup. The CompassTag family started a family-owned
  `sensors.c`; the name is historical and means "how this family uses its
  sensors", not a reusable driver. When touching an older `state_run.c`, move
  sensor configuration, orientation transforms, calibration handling and
  sensor wake/sleep flow into a tag- or family-owned orchestration file with a
  clearer name. Reusable drivers stay under `common/sensors`.
- **RTC descriptors.** The default RV3028 binding is still a weak board default
  in common code. Tags with non-default RTC wiring should move the complete
  `TagRtcDevice` descriptor into tag or family board support, so that one
  place owns the register bus and the board power callbacks. That also removes
  the remaining global `rtcOn()`/`rtcOff()` glue.
- **I2C backends:** build one mixed development target that puts the software
  and hardware backends on different buses.
- **BMM350 self-test enum.** The monitor test table maps the BMM350 to the
  legacy `RUN_MMC5633` request until the protobuf test enum gains a
  BMM350-specific request and result.
