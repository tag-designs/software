---
type: worklist
status: current
summary: Open tag firmware work -- defects found by reading code, low-power and recovery questions, NAND bring-up, the next release's remaining items and their host counterparts, and cleanup.
---

# Tag Firmware TODO

## Release follow-ups

Background:
[the ADC and power-floor investigation](design/investigations/2026-10-adc-and-the-power-floor.md).

**Firmware changes go in as one batch (decided 2026-11-01).** Most items in
this worklist change the image of most tags, and every changed image needs
requalifying. Collect them -- including the TIM2 prescaler port, the stored
configuration's own page, the state-marker overflow and reset markers, PresTag
F2/F3 and the L4 monitor's remaining checks -- and qualify once, rather than
landing and measuring them one at a time. Host tool fixes are not held back.

- **Re-arm the run's wake source on a POWERFAIL resume in CompassTag, BitTag
  and the BitPresTag family.** On an L432 tag, an attach or external reset
  that lands while the tag is awake boots as `resetPower`. Only a reset from
  Standby is classified otherwise. The plain `deviceInit()` power-init path
  then turns every alarm and the ticker off, and recovery resumes with
  `Running(T_CONT, POWERFAIL)`.
  - **Fixed so far:** PresTag (`a406eda7`, ticker) and UIUCTag (2026-11-03,
    minute alarm). In both, `T_CONT` re-arms on POWERFAIL.
  - **Still missing:** `families/CompassTag/src/state_run.c` (ticker),
    `BitTag/src/bt_state_run.c` (minute alarm) and
    `families/BitPresTag/src/state_run.c` (ticker). They re-arm nothing, so an
    attach at the wrong moment leaves the run RUNNING and collecting nothing.
  - **Evidence and test:** on UIUCTag this was measured and confirmed with
    `RTC_CR` captures; see `UIUCTag/design/power-results.md`, 2026-11-03. Any
    attach inside the ~1.5 s after `tag-start` reproduces it, as the release
    check's first `tag-info` does once the start reply is synchronous. Verify
    each fix with an attach straight after start, then 420 s of trace and a
    download.
  - **The general alternative:** skip the alarm and ticker teardown in the
    L432 `deviceInit()` on a reattach, as `TAG_MONITOR_RESET_RECOVERY` does on
    U3. That is broader and touches every L4 image's boot path.

- **Give the stored configuration its own page (part 2).** Part 1 of
  [the proposal](design/proposals/stored-config-write-is-unchecked.md) landed
  in `dee8654d` and shipped in `fw-v0.6`: every target reads the write back with
  `storedConfigVerify()` and aborts the start on a mismatch. That makes a
  refused write loud; it does not make it succeed, because `sconfig` shares its
  page and cannot be erased first. Part 2 places it on its own page and erases
  before programming, as IMUTag does. It is a linker change per target and needs
  measuring afterwards.
- **Explain the 11% gap** between PresTag's fitted `I_rest` (0.1122 µA) and its
  measured `IDLE` (0.1261 µA). September had the two within 1.1%.
- **Test `flash_release.py`'s PEMPTY clearing on hardware.** The conditional
  clear has only ever reported "already clear"; the branch that writes the
  register is unexercised.

Open work only. Delete an item when it is done; put what was learned in an
investigation or decision record. Items marked *found by reading code* have
not been reproduced on a tag.

## Defects

- **On an L4 tag, the first request after an attach waits for an erase to
  finish.** Seen on CompassTagAT25 (2026-11-03, 2.50 V, branch
  `sync-monitor-commands`): attach to a tag in sRESET, and a stop or erase is
  answered only after the sweep ends, 6.6 s later, although within a session
  the sweep yields to a request after one sector. Status reads are answered at
  once, from the cached status. On IMUTag (U3) the same request was answered
  in 0.23 s. The reply is still correct (PERM, then IDLE), so an erase sent in
  a new session to an erasing L4 tag never continues it; it finds the erase
  done. Not yet explained; the sweep's event check, the L4 attach sequence and
  the boot's first pass are the places to look.

- **The L4 DebugMonitor handler can clear the MON_REQ of a request still
  pending.** Step 1 of
  [l4-monitor-robustness.md](common/core/design/proposals/l4-monitor-robustness.md)
  is on branch `bittag-prestag-fixes`: a request that arrives while one is in
  flight leaves `MON_REQ` alone, and a plain `get_status` while the main thread
  is in its guarded region is answered from cached state in the handler.
  On 2026-11-01 BitTag and PresTag passed `tag-test` with resting current
  unchanged, and a PresTag Tag Monitor session stayed live through Start, Stop,
  Erase and Synchronize Clock. On 2026-11-02 the UIUCTag breakout (main
  `12af820d`, on
  `tag-breakout-base-l432-v1` (fw `be8f9062`, a dirty build of 2026-10-28)) passed `tag-test` six times out of
  six with no 0x81 burst, and Tag Monitor's `session_check` passed every step
  with a clean log; resting current 0.1507 µA against 0.1521 µA before
  ([UIUCTag power results](UIUCTag/design/power-results.md)). Still to verify,
  per the proposal: `tag-test` on CompassTag. Step 2 (the mailbox) waits on
  whether bursts remain; none have been seen since.
  Observed 2026-10-27 on a UIUCTag breakout (fw `7c508f46` plus the
  session-clock fix), reading `common/core/src/handlersL4.c` and `monitor.c`
  after bursts of base 0x81 errors during Synchronize Clock and Self-Test:
  - **The handler.** `DebugMon_Handler` latches a PROTOBUF request
    (`monitor_pending`, DCRDR = 0) and leaves MON_REQ set until the main
    thread runs it in `monitorServicePending()` at a cooperative point. A
    second PROTOBUF that arrives while one is pending takes the `else`
    branch, `monitorClearRequest()`, and clears the pending request's
    MON_REQ. The host then takes that request as done and reads a reply
    nobody wrote, and `monitorServicePending()` later finds `monitor_pending`
    with MON_REQ clear and drops the request. The clear is right when no
    session is open; while a request is pending it should leave MON_REQ
    alone.
  - **Why a request stays pending.** Synchronize Clock runs
    `SetTimeUnixSec()` inside `proto_eval()` in the main thread: it sets the
    internal RTC, re-initialises it (`tagRtcInit()`), applies the clock
    correction, and writes the RV3028 over I2C, all while the host waits for
    MON_REQ to clear. Self-Test's tests run in the main thread straight after
    the request is accepted.
  - **The 0x81s themselves.** `tag-test` shows the host's DEMCR reads failing
    at the SWD level (base 0x81) for most of the time the main thread spends
    on those operations: 172 failed reads inside one call's wait, about 330 in
    the run. The handler race does not explain an access failing outright.
    **Explained and fixed (2026-10-27):** `DBGMCU_CR.DBG_SLEEP` was clear,
    so the idle thread's Sleep-mode WFI stopped HCLK and held off every SWD
    access while the main thread waited on I2C or a test delay, past the
    base's 25 WAIT retries. The host now sets `DBG_SLEEP` after
    `MONITORSTART` (tagcore, both libraries); see
    `embedded/bases/design/investigations/2026-10-0x81-dbg-sleep.md`. The
    handler race above is separate and still open.
  - **An empty reply during a self-test.** With the host change below in
    place, the Rust `tag-test` (ALL_PASSED) still logged one `GetStatus
    failed: Tag returned an empty or invalid acknowledgement`: a call that
    completed with DCRDR = 0. Possible causes, not yet told apart:
    - the handler race above;
    - a DEMCR read inside a 0x81 burst that came back without MON_REQ;
    - `monitorServicePending()` answering 0 because `monitor_enabled` was
      false.
  - **Host side, done:** tagcore's `TagMonitor::Rpc()` now waits for an L4
    tag's MON_REQ and MON_PEND to clear before writing the request buffer
    (`WaitL4MonitorIdle()`, and `wait_l4_idle()` in tagcore-rs),
    so a request a failed call left pending is no longer overwritten; qtmonitor pauses its polls for 1.5 s after
    Synchronize Clock and Self-Test.
- **The download clock switch's TIM2 prescaler writes do nothing** in the
  BitPresTag, UIUCTag and CompassTag `datalog.c`. `fast_msi()`/`slow_msi()`
  raise MSI 12x around one page of `data_logAck()` and write
  `STM32_ST_TIM->PSC` to compensate. The prescaler is buffered until an update
  event, which ChibiOS's free-running 32-bit TIM2 does not reach, so the tick
  runs 12x fast for each page: every RTOS sleep in that window, the external
  flash's wake delay included, is cut to a twelfth. The same pattern in the
  monitor session made the 60 s watchdog fire after 5 s on UIUCTag (removed
  2026-10-26, see `docs/shared/monitor-interface.md`). PresTag is fixed on
  branch `bittag-prestag-fixes` by `st_load_prescaler()` in
  `families/PresTag/src/datalog.c`, which forces the update and restores the
  counter, and a PresTag download was identical to the previous image's on
  hardware (2026-11-01, [results](families/PresTag/design/power-results.md)).
  Port it to the other three.
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

- **Clean up the reset recovery code in `StateMachine()`** (user request,
  2026-11-02). The recovery branch at the top of `StateMachine()`
  (`common/core/src/state_machine.c`) has grown one bench fault at a time and
  is now most of the function. It has:
  - two parallel recovery paths, selected by `TAG_MONITOR_RESET_RECOVERY`;
  - four overlapping ways of deciding why the tag reset: `reattachReset()`,
    `externalResetAtBoot`, `backupStateValidAtBoot` and
    `monitor_reset_recovery`;
  - trace bookkeeping (`TAG_RECOVERY_TRACE`, about 50 references in the
    branch) and retained run diagnostics woven through the decisions.

  Reduce it to one path with one reset classification, and move tracing out
  of the decision logic. Write down the cases the path must handle first:
  standby wake, attach reset, field power loss, brownout, exception. Then
  check each against the code rather than preserving every branch. UIUCTag
  behaves differently from BitTag after an attach, and nothing in its
  `restoreLog()` explains why. The synchronous
  [monitor commands](common/core/design/monitor-commands.md) removed one reason
  for the complexity: the main loop now serves no request before the recovery
  pass, so a request can no longer race it.
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

## Offline reconstruction and release tooling

The fixes landed after `fw-v0.0.3`, and how each was verified, are in
[the next-release investigation](design/investigations/2026-10-next-release-fixes-verified.md).
All five distributed targets passed qualification against the `fw-v0.6`
image on 2026-10-06/08; each target's results log has the figures.

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

- **Functional checks beyond the gate:** the CompassTag resume test, the
  full-flash download test, and a `tag-capture` of each target to confirm the
  identity record and session facts read back from the capture.
- **Make `tag_release_check.py` cover every tag.** It has profiles for
  IMUTag, BitTag, PresTag and UIUCTag: config, sleep bound, run floor and
  bound, run, rest and idle windows, settle and storms, from each target's
  qualified figures, and a sample-count check for each through
  `config_odr_hz()`. BitTag, PresTag (2026-11-01) and UIUCTag (2026-11-02)
  passed end to end with no hand steps
  ([BitTag](BitTag/design/power-results.md),
  [PresTag](families/PresTag/design/power-results.md),
  [UIUCTag](UIUCTag/design/power-results.md)). Still to do:
  - BitTag's run bound rests on one board; a second ran 13% lower, inside it;
  - CompassTag: a config, a profile and a rate;
  - it checks neither calibration nor `tag-test`, so both are closed by hand.

### Host counterparts

These ship separately but are needed for the firmware changes to be useful.

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
- **D5:** decoders should read CompassTag `temp10` as signed for fw-v0.0.3
  offline decoding, and read the new `sconfig` members by layout version.

## Cleanup

- **The start-time comment in `Configured()` describes only IMUTag**
  (`common/core/src/state_machine.c`, the `TAG_CONFIGURED_IMMEDIATE_START`
  block). It says `config_tmp.start = (start_delay - 1) * 60 + timestamp`, and
  argues from that that the immediate-start test is a relative comparison
  against the same clock and so need not depend on `clockTrusted`. Only IMUTag
  (`families/IMUTag/src/config.c`) computes the start that way. BitTag,
  BitTagNG, PresTag, BitPresTag, CompassTag and UIUCTag all take the absolute
  `config->active_interval.start_epoch`. For them the start is a wall-clock
  time, and the test does depend on the clock being right. Correct the comment,
  and decide whether the `clockTrusted` reasoning still holds for
  absolute-start tags. Related host point: `tag-start --start-now` only zeroes
  `start_delay`, so it changes nothing on those tags; they start at once only
  because their configs set `start_epoch` to 0. Found 2026-11-03, when a
  BitTag "delayed start" built on `start_delay` started immediately.
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
