---
type: worklist
status: current
summary: Open work on the external flash loaders -- the L432 state-capture script, watchdog refresh in the ST entry points, and untested MassErase.
---

# Loaders Worklist

Delete an item when it is done. Runtime rules are in
[Loader Runtime Design](design/loader-runtime.md).

- **The ST entry points do not refresh the watchdog.** `Serve()` does: it
  writes `IWDG_KR = 0xAAAA` while it waits for commands. The ST entry points do
  not. A tag whose option bytes select the hardware watchdog
  (`FLASH_OPTR.IWDG_SW` = 0) has it running from reset, and the debug freeze
  covers only a halted core. A long run of loader code would then be cut off by
  a reset, a mass erase above all. The bench PresTag uses the software watchdog,
  so it is unaffected. The fix is an `IWDG_KR = 0xAAAA` write in the poll loops,
  which has no effect when the watchdog is not running; see
  [SWD Capture and Recovery Library](../../host/libraries/tagcore-rs/design/swd-recovery.md#mcu-reference).
- **`MassErase` is untested.** It has not been established whether `-e all`
  with a loader loaded also erases internal flash, and it was not tried on a
  tag with firmware worth keeping. Sector erase is tested, through the
  programmer's erase-before-write.
