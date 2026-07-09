# IMUTagU375 Bring-Up Closeout

Date: 2026-07-09

Status: parked. This branch preserves the STM32U375 bring-up work, but the
U375 is not recommended as the next IMUTag processor without a fresh low-power
bring-up plan. The L432 path remains the stable baseline.

## Summary

The IMUTagU375 bring-up reached the point where the firmware could build,
qtmonitor could attach, the RV3028 RTC could be configured, self-tests could
report bounded failures for missing sensors, and basic logging diagnostics could
run. The remaining problems were concentrated around STM32U3 reset, backup
state, debug attach, Stop1, and detached data collection behavior. Those
problems required too many shared runtime and monitor changes relative to the
benefit of switching from the L432.

This branch should be treated as an investigation snapshot, not a merge-ready
processor port.

## Scope Tried

- Split shared IMUTag code into `embedded/tags/families/IMUTag` and made the
  existing IMUTagBreakout consume that family code.
- Added the `IMUTagU375` tag target, `IMUTagU375` board, STM32U375 linker
  script, and STM32U3 tag makefile support.
- Added STM32U375 ChibiOS configuration using the same MSI clock strategy as
  IMUTagBreakout.
- Added a local STM32U3 RTC LLD override for the ChibiOS RTCv3 issues found
  during bring-up. Details are in
  [`chibios-stm32u3-rtc-report.md`](chibios-stm32u3-rtc-report.md).
- Brought up the RV3028 path on the U375 board, including the PB7/PB6
  software-I2C binding, RTC APB clock handling, LSE bypass behavior, and a
  slower RTC bit-bang delay. The current IMUTagU375 manifest sets
  `TAG_RTC_I2C_DELAY_CYCLES=16U`.
- Added STM32U3 internal flash support paths and backup-register portability
  for RTC/TAMP backup register layout differences.
- Added persistent-state initialization on cold boot so zero-filled backup
  state does not leave the tag in `STATE_UNSPECIFIED`.
- Added device-specific self-test request/result names for the actual IMUTag
  parts, including `RUN_LSM6DSV16X`, `RUN_AK09940A`, `RUN_LPS22HH`,
  `RUN_RV3028`, `RUN_MX25L`, and preliminary `RUN_MX25U12843`.
- Bounded failing SPI/storage/sensor operations so missing hardware reports a
  test failure instead of hanging indefinitely.
- Added a preliminary MX25U12843 external flash driver for the 1.8 V Macronix
  flash used by the U375 hardware.
- Added retained run diagnostics to help distinguish detached execution from
  monitor attach/reset recovery.

## Useful Findings

- The STM32U375 backup registers are exposed through the TAMP layout, so code
  that directly assumes `RTC->BKP0R` is not portable across the L432 and U375.
- The STM32U3 RTC APB clock gate and RTCv3 date decode needed local handling.
  The ChibiOS RTC report records the evidence and suggested upstream fixes.
- The RV3028 `CLKOUT` check in the project driver was wrong: the expected mask
  uses `0xC0 | RV3028_CLKOUT_VAL`, not `0xC0 & RV3028_CLKOUT_VAL`.
- The U375 board relies on internal pullups for the RV3028 software-I2C lines,
  so the L432-era single-NOP software-I2C delay was too aggressive.
- Monitor attach on STM32U3 is more sensitive to debug state cleanup than the
  existing L432 path. Some host-side monitor changes, especially releasing halt
  and clearing monitor request bits during detach, may still be useful.
- Programming/option-byte state mattered during bring-up. Several early
  `STATE_UNSPECIFIED` and monitor attach failures were not pure runtime bugs.

## Current Workarounds And Diagnostics

- `IMUTagU375/inc/custom.h` currently disables Stop1 with `USE_STOP1 0`.
  This is only a bring-up workaround. It keeps RUNNING collection awake because
  detached Stop1 collection was not reliable.
- `TAG_RETAINED_RUN_DIAGNOSTICS` is enabled for IMUTagU375. It exposes retained
  heartbeat, state, page, external-block, and header-write diagnostics through
  monitor status/debug messages. This is useful for investigation, but it is
  not part of the normal IMUTag runtime contract.
- The state machine contains monitor-reset recovery logic that tries to prefer
  live backup cursors over flash state markers when a debugger reset occurs.
  That logic remains speculative.
- The MX25U12843 driver is preliminary. Its command set follows the existing
  Macronix-style storage drivers, but it needs real erase, write, readback,
  download, sleep, and attach/detach validation on assembled hardware.

## Unresolved Blockers

- Detached RUNNING collection did not behave reliably when Stop1 was enabled.
  The counter could stop during the detached period, and later experiments made
  monitor reattach abort collection.
- Reattach/reset recovery became entangled with retained backup state,
  persistent flash state markers, and live data-log cursors. The current branch
  contains diagnostics and partial recovery logic, but not a proven design.
- The U375 low-power path needs a clean restart from first principles: wake
  source setup, EXTI constants, Stop1 entry/exit, ChibiOS clock restore, debug
  attach interactions, and device reinitialization should be validated one at a
  time against the L432 behavior.
- Internal-flash header writes and retained data-log cursors were instrumented,
  but the final detach/reattach logging behavior was not proven.
- The U375 port forced changes into shared monitor, power, state machine,
  storage, RTC, and flash code. That is a large regression surface for a
  processor swap.

## Recommended Disposition

- Do not merge this branch wholesale into `main`.
- Preserve the branch as `imutag-u375-bringup` for reference.
- Cherry-pick only well-isolated fixes after review and testing on existing
  L432 tags.
- Likely candidates for separate extraction:
  - ChibiOS STM32U3 RTC findings and local report.
  - RV3028 `CLKOUT` validation fix.
  - Bounded SPI/storage/sensor test failures.
  - Device-specific self-test protobuf names.
  - Documentation for tag family/module layout.
  - Possibly the host monitor detach cleanup, after verifying it does not
    affect existing L432 monitor workflows.
- Treat all Stop1, monitor-reset recovery, retained RUNNING diagnostics, and
  MX25U12843 work as experimental until validated independently.
