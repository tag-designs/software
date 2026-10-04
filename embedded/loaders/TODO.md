---
type: worklist
status: current
summary: Open work on the external flash loaders: the L432 state-capture script, watchdog refresh in the ST entry points, untested MassErase, and NAND loader distribution.
---

# Loaders Worklist

Delete an item when it is done. Runtime rules are in
[Loader Runtime Design](design/loader-runtime.md).

- **`tag_capture_state.py` does not work on STM32L432.** Its SRAM step requests
  256 KB (the U375's size) and fails, and its backup-register step also fails.
  Both come from U375 constants in the script. For the backup registers it
  writes the U375's `RCC_APB1ENR1` at `0x40030C9C` (`RTCAPBEN` is bit 30 there).
  On the L432 that address maps to nothing; the register is at `0x40021058`,
  with `RTCAPBEN` at bit 10. The internal-flash regions capture correctly.
  Until this is fixed, read the backup registers by hand: under reset, enable
  `RCC_APB1ENR1_RTCAPBEN`, then read `0x40002850`, 128 bytes.
- **The ST entry points do not refresh the watchdog.** `Serve()` does: it
  writes `IWDG_KR = 0xAAAA` while it waits for commands. The ST entry points do
  not. A tag whose option bytes select the hardware watchdog
  (`FLASH_OPTR.IWDG_SW` = 0) has it running from reset, and the debug freeze
  covers only a halted core. A long run of loader code would then be cut off by
  a reset, a mass erase above all. The bench PresTag uses the software watchdog,
  so it is unaffected. The fix is an `IWDG_KR = 0xAAAA` write in the poll loops,
  which has no effect when the watchdog is not running; see
  [SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md#mcu-reference).
- **`MassErase` is untested.** It has not been established whether `-e all`
  with a loader loaded also erases internal flash, and it was not tried on a
  tag with firmware worth keeping. Sector erase is tested, through the
  programmer's erase-before-write.
- **Distribute the NAND loader?** `IMUTagNandBmp581` is a distributed tag, but
  `GD5F2GM7RE_IMUTagNandv2` is not marked `DISTRIBUTE`, unlike the AT25XE
  loaders, so it does not ship in the firmware release beside the tag it reads.
- **Run the linear path through `tag-capture` on a PresTag.** The AT25 loaders
  use the same code through `tag-capture` as through `tag-xflash dump`. The
  [U375 loader plan](design/proposals/u375-nand-loader-plan.md) recorded on
  2026-10-02 that this combination had not yet been run on a PresTag.
