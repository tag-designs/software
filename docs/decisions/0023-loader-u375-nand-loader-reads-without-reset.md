---
type: decision
status: accepted
summary: The GD5F2GM7RE SPI-NAND loader for STM32U375 never resets the NAND or writes its lock register, allows only a restored ECC_EN toggle, leaves the clock near reset, drives FLASH_PWR itself, and leaves blank-block skipping to the host.
---

# NNNN. Loaders: the U375 SPI-NAND loader reads without reset or lasting writes

Date: 2026-10-02

The five decisions agreed for `GD5F2GM7RE_IMUTagNandv2`, the first loader for
STM32U375 and for SPI NAND. All five were built as recorded below, with three
amendments made during the build (listed under Consequences). Cut verbatim from
the [U375 SPI-NAND Loader Plan](../../embedded/loaders/design/proposals/u375-nand-loader-plan.md), which keeps the step-by-step bench
record.

## Decision

1. **Never send `FF` (reset), and never write the block-lock register `A0`.**
   - Reset destroys the status bits of the part's last operation, and the lock
     bits as found (`0x38` from power-up) are evidence.
   - A read needs neither. The firmware's `gd5fProbe()` does both on every
     boot (`common/storage/src/gd5f.c:280-308`), so the loader cannot reuse
     it.
2. **Allow one volatile feature write, restored before exit:** `SET_FEATURE B0`
   to clear `ECC_EN` for raw reads, and to set it again for ECC reads.
   - `B0` is volatile (back to its default at power-on) and is not NAND
     content.
   - The loader records `A0`, `B0` and `C0` as found in `detail[]` before
     touching anything, and puts `B0` back as found before `Serve()` exits.
   - The alternative, ECC reads only, gives up the raw pages that Field Data
     Extraction asks for. *Recommendation: allow it.*
3. **Leave the clock at its reset value.**
   - The reset clock is MSIS at 6 MHz (ChibiOS `hal_lld.c:336`, to be checked
     against RM0487), voltage range 2, with 1 wait state.
   - SPI1 on its reset kernel clock (PCLK2) at /2 gives about 3 MHz, about
     6 ms per 2176-byte page. SWD then needs about 11 ms to fetch the page at
     the 200 KB/s seen on the U375, so SPI is not the bottleneck.
   - This avoids touching `PWR_VOSR`, the booster or `FLASH_ACR`. ChibiOS's
     clock init is ruled out anyway: on U3 it also calls `bd_reset()`
     (`hal_lld.c:771`) and sets `DBP`.
   - The DWT delay code needs the real core clock. Calibrate it from the reset
     value, and make every budget an iteration bound with a wide margin.
     *Recommendation: no clock change.*
4. **Power and neighbours on the bus:**
   - **NAND rail.** Drive `FLASH_PWR` (PA8) high, wait tVSL plus margin
     (5 ms), then send `AB` (release from deep power-down, which the tag
     enters in Standby) and wait tRES1 (30 µs).
     - The bench IMUTagNandBmp581 is a breakout board with a NAND load switch
       on PA8. The final tag has no switch.
     - The firmware drives PA8 high on every IMUTagNandv2 board (`board.h`:
       output, ODR high), so the loader doing the same is safe on both.
     - After the attach's reset PA8 is undriven. On the breakout the NAND then
       ran on residual charge and parasitic power through its IO pins. It
       answered for a few tens of milliseconds and then dropped off the bus:
       status read `FF`, then `00`, and a cache read in progress came back as
       `FF` from the point of the outage, silently.
     - This was found in step 2 (see there). An earlier draft of this plan
       left PA8 alone, wrongly.
   - **LSM6DSV16X.** It shares the NAND's SCK, MISO and MOSI nets
     (PA5-PA7) and is always powered, so the loader drives its CS (PB1)
     high before any SPI traffic, and leaves every other LSM6DSV pin alone.
   - **BMP581.** It is on SPI1's other pin set and is not touched.
5. **Read only the blocks that hold data, found by blank detection in the
   host library.**
   - A block's pages are written in order from page 0. If the first page of a
     block is blank, the rest of the block is blank for this tag and is
     skipped.
   - The decision belongs to the host download library, not the loader. That
     keeps the loader generic: it reads pages, with no knowledge of any tag's
     write pattern. A tag that writes differently changes only host code.
   - The host fetches page 0 of each block raw with `READ_PAGE` and tests all
     2176 bytes, spare included, for `FF`.
     - Cost per block: about 6 ms of SPI at 3 MHz plus about 11 ms of SWD
       transfer, so about 35 s for all 2048 blocks. That is small against
       about 23 minutes for a full pass, and less again for a tag that used
       only a few blocks.
   - A factory bad block is non-blank through its mark (page 0's spare byte 0
     is not `FF`). The host tells it from a used block by that byte.
   - The host then reads each non-blank block, raw and through ECC.
   - Blank detection does not depend on internal flash. The captured
     checkpoints and map remain the authority for which physical pages make
     up the log; the scan decides only what to fetch.
   - A full raw pass of every page stays available as an option, for a part
     whose contents are in doubt.

## Consequences

Amendments made while building, each recorded in the plan's work log:

- **Decision 3, clock.** The reset clock could not be established from
  documentation: ChibiOS assumes MSIRC1/4 (6 MHz), but the captured reset
  registers read MSIRC1/2 (12 MHz). `loader_clock_u3.c` therefore pins MSIS at
  MSIRC1/2 = 12 MHz through `RCC_ICSCR1` (MSIRGSEL = 1), within voltage range 2
  at the reset's 1 wait state. PWR, the booster, `FLASH_ACR` and the backup
  domain stay untouched.
- **Decision 4, NAND power.** An earlier draft left PA8 alone. The bench
  breakout's NAND then ran on residual and parasitic power and silently dropped
  off the bus mid-read, so the loader drives `FLASH_PWR` (PA8) high, as the
  firmware does.
- **Decision 5, blank detection** moved from the loader to the host download
  library, keeping the loader generic. The skip rule reads only page 0, while
  the firmware also looks for a factory mark on page 1; a block marked only on
  page 1 is skipped unreported, which loses nothing since it holds no data.
- Every page read is followed by a status read, and an all-ones reply (the
  part not driving MISO) is retried rather than returned.
