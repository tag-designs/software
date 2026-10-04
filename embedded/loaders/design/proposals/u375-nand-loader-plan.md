---
type: proposal
status: historical
summary: Plan, now built, for the GD5F2GM7RE SPI-NAND loader on STM32U375: raw and ECC page reads, NAND power, shared SPI bus and work order.
---

# Plan: an STM32U375 SPI-NAND Loader (GD5F2GM7RE_IMUTagNandv2)

Status: **plan accepted**, 2026-10-02. All five decisions below were agreed
with the recommendations. Decision 5 moved blank detection from the loader to
the host library. Decision 4's NAND power was corrected in step 2: the loader drives FLASH_PWR, as the firmware does.
All five steps are built and checked (2026-10-02); see each step.

This plan covers reading an IMUTagNandBmp581's external flash (GigaDevice
GD5F2GM7RE SPI NAND, 256 MiB) over SWD without its firmware. The tag's identity
record already names the loader `GD5F2GM7RE_IMUTagNandv2`. It is the first
loader for a new MCU (STM32U375) and the first for a new memory type (SPI
NAND), so it is planned before it is built.

Background:
- [Loader Runtime Design](../loader-runtime.md): the contract and the runtime
  rules, all of which still apply.
- [Field Data Extraction](../../../tags/design/proposals/field-data-extraction.md): why NAND is
  read raw and through ECC.
- [Offline Log Reconstruction](../../../../docs/investigations/2026-10-offline-log-reconstruction.md):
  how IMUTag pages map to physical blocks.

Facts are cited from the source. The datasheet used is
`hardware/BoardDesigns/libraries/datasheets/DS_00819_GD5F2GM7RE_Rev1_3-3435814.pdf`
in the sibling repository.

## What the loader must read

- **Raw pages including the spare area.** 2048 data bytes plus 128 spare bytes
  (columns 0-2175) per page; 64 pages per 128 KiB block; 2048 physical
  blocks. The factory bad-block mark is spare byte 0 of page 0 (and 1) of a
  block. With on-die ECC enabled, spare bytes 0x840-0x87F hold ECC parity,
  readable but not writable.
- **The ECC verdict per page, as the firmware sees it.** The firmware reads
  with ECC on and keeps a page only if ECC reports no error or corrected
  (`families/IMUTag/src/datalog.c`). The on-die algorithm is undocumented, so
  a raw page cannot be corrected on the host. Field Data Extraction therefore
  asks for each used page **both raw and through ECC**, with the ECC status
  (`C0` bits 5:4, and the extended `F0` bits the firmware never reads).
- **Physical pages, not logical ones.** Bad-block remapping is the host's job:
  - `gd5fLogicalBlockMap` (`uint16[2048]` at `0x080FF000`) and the 16-byte
    checkpoints at `0x08021280` are both in internal flash;
  - `tag-capture` already captures them, and the identity record locates
    them.

## Decisions (agreed)

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

## Work, in order

Each step ends with a check on the bench IMUTagNandBmp581. Flashes of tag
firmware, if any, go in its flash log
(`captures/2026-10-02-imutag-nand-bmp581/flash-log.md`).

1. **U3 loader build.**
   - `loaders/common/make-u375.mk`, mirroring `make.mk`: the
     `startup_stm32u3xx.mk` include paths only, the STM32U3xx
     `platform.mk` with only `hal_pal_lld.c` compiled, the os-less OSAL,
     `MCU = cortex-m33` and `USE_FPU = no`.
   - `cfg/stm32u3/` with PAL-only `halconf.h` and an `mcuconf.h` with
     `STM32_NO_INIT TRUE`.
   - `STM32U375-loader.ld`: `StorageInfo` at 0, the image in SRAM1 from
     `0x20000004`.

   The first 64 bytes of SRAM1 are the firmware's monitor mailbox. The
   loader overwrites them only while the core is halted, and the firmware
   clears them at boot; confirm that.

   *Check:* the RV3028 probe, ported first as `RV3028_IMUTagNandv2`,
   returns this tag's factory EEOffset. Read-only, as before.

   **Done 2026-10-02.**
   - Built: `common/make-u375.mk`, `common/cfg/stm32u3/`,
     `common/STM32U375-loader.ld`, `common/src/loader_clock_u3.c` and
     `RV3028_IMUTagNandv2/`.
   - The board names its I2C pins `LINE_SDA` (PB7) and `LINE_SCL` (PB6),
     aliased by the target's `project.mk`.
   - The image references only RCC, GPIOB 6/7, DWT and DEMCR.
   - On the bench IMUTagNandBmp581 it read all 64 registers:
     - CLKOUT `C0`;
     - EEOffset `00`/`10` = 0 steps, agreeing with the stored session facts
       and `tag-info`;
     - the time as set by the firmware.

   **One departure from decision 3.**
   - The reset clock could not be established from documentation. ChibiOS
     assumes MSIRC1/4 (6 MHz); the captured reset registers read MSIRC1/2
     (12 MHz).
   - So `loader_clock_u3.c` pins MSIS at MSIRC1/2 = 12 MHz through
     `RCC_ICSCR1` (MSIRGSEL = 1). That is a value the part may already be
     running at, and it stays within voltage range 2 at the reset's 1 wait
     state.
   - Still untouched: PWR, the booster, `FLASH_ACR` and the backup domain.
2. **U3 SPI and delay.**
   - `loader_spi_u3.c`, a bounded polled master following
     `common/core/src/spi_bus_polled.inc:57-68, 187-207`: `CFG1` 8-bit, and
     `MBR` chosen for the reset clock; `CFG2` `MASTER|SSOE`; `CR1`
     `MASRX|SPE`; then per byte `CSTART`, `TXP`/`TXDR`, `RXP`/`RXDR`,
     `CSUSP`.
   - DWT delays calibrated for the reset clock.

   *Check:* `READ_ID` (`9F 00`) returns `C8 82`, and `GET_FEATURE` of `A0`,
   `B0` and `C0` returns plausible values.
3. **GD5F part driver, read-only.**
   - `common/inc/gd5f_commands.h`: move the opcodes, feature addresses and
     timing from `gd5f.c:19-64` into a dependency-free header that the
     firmware driver also includes.
   - Prove the firmware unchanged by comparing the `.list` of every target
     that builds `gd5f.c`, at the same commit, as the README requires.
   - `common/src/gd5f_loader.c`: probe without reset, wake, page read to
     cache (`13`) with an OIP poll bounded at tRD_ECC max (120 µs) times a
     margin, read from cache (`03`) of 2176 bytes, and status `C0`/`F0`.
   - Writes are limited to `B0` (decision 2). There is no program or erase
     code in this image at all.

   *Check:* page 0 of block 0 read raw matches the bad-block mark convention,
   and a used page read through ECC reports ECC OK.
4. **`Serve()` for NAND.** `loader_service.h` version 2 adds one command:
   `READ_PAGE(page, mode)`. It returns 2176 bytes, plus the `C0`/`F0` status
   in `detail[]`. Page selection is the host's (decision 5).
   The AT25 loaders keep version 1, and the host accepts either version.
   *Check:* the host's blank scan, on a tag with a short run, finds exactly
   the blocks the checkpoints name, plus any factory-marked blocks. A `tag-xflash` NAND mode
   then reads those blocks raw and through ECC, and the ECC-mode data bytes
   match a normal `tag-dwnld` of the same run.
5. **Capture integration** (swd-recovery step 5): external flash in
   `tag-capture`, with the loader named by the identity record. For NAND,
   write one file of raw pages, one of ECC pages and a per-page status table.
   *Check:* the offline shim (next-release-todo D1) rebuilds the run's SQLite
   file from the capture alone.

   **Step 2 done 2026-10-02.**
   - Built: `common/src/loader_spi_u3.c`; `common/src/gd5f_loader.c`,
     read-only, with no reset and no feature writes; and the
     `GD5F2GM7RE_IMUTagNandv2` target.
   - The GD5F opcodes moved to `tags/common/storage/inc/gd5f_commands.h`. The
     `.list` of IMUTagNandBmp581 and IMUTagNand is identical before and after.
   - `NandInfo` read ID `C8 82`, `A0` `00`, `B0` `10` (ECC on), `C0` `00` and
     `F0` `00`.
   - Through the ST entry points and `Serve()`, four reads of the first 64
     pages, 5 s apart, were byte-identical. They held 52 non-blank pages,
     matching the 52 pages a monitor download of the same run returned.
   - The core clock was checked against the host: a 2000 ms delay took
     2.018 s.
   - Every page read is now followed by a status read. A reply of all ones
     means the part was not driving MISO, so the page is retried and never
     returned silently. That is cheap insurance against the failure that
     the missing PA8 drive caused.

   **Steps 3 and 4 done 2026-10-02.**
   - `gd5f_loader.c` gained `loaderFlashReadPage()`: 2176 bytes, raw or
     through ECC, with C0h and F0h after the read, and the no-reply check.
     It also gained `loaderFlashRestore()`.
   - B0h writes go only through `gd5fSetEcc()`, which changes ECC_EN alone
     and holds the reserved bits low. It refuses if OTP_EN or OTP_PRT was
     found set: OTP_PRT is non-volatile.
   - `Serve()` restores B0h as found before it returns.
   - `loader_service.h` version 2 adds `LOADER_CMD_READ_PAGE` and widens
     `detail[]`: page bytes, and A0/B0/C0/F0 as found. All loaders are
     version 2; the host accepts 1 or 2.
   - `ExternalFlash::ReadPage()` is the host side.
   - `tag-xflash nand -o DIR` applies decision 5: page 0 of each block is
     read raw, a blank block is skipped, and every other block is read raw
     and through ECC. It writes `raw.bin`, `ecc.bin`, `pages.csv` and
     `summary.txt`.

   Checked on the bench IMUTagNandBmp581:
   - The whole part (2048 blocks) took 74 s: 1 block read (64 pages, every
     ECC verdict `ok`), 2047 blank, none factory-marked on page 0.
   - The ECC-mode data was byte-identical to the step-2 linear dump.
   - Raw data equalled the ECC data, so there were no bit errors, and the
     raw spare showed the on-die parity at 0x840.
   - B0h read `10` after the session, as found.

   One limit: the firmware looks for the factory mark on pages 0 and 1, but
   the skip rule reads only page 0. A block marked only on page 1 is skipped
   unreported. It holds no data, so nothing is lost.

   **Step 5 done 2026-10-02.**
   - `recovery/identityrecord.*` parses the identity record from the
     captured internal flash, at the new `McuMap::identity_offset`: 0x1A0 on
     L432, 0x240 on U375.
   - `recovery/externalcapture.*` holds the shared capture (linear or
     paged), used by both `tag-capture` and `tag-xflash nand`.
   - `tag-capture` now ends with the external flash, through the loader the
     record names, searched for under `--loader-dir`, `$TAG_LOADER_DIR` and
     `build-host/embedded/loaders`; `--loader` overrides it. It checks the
     JEDEC ID against the record (manufacturer and first device byte).
   - The manifest gains `identity` and `external_flash` sections: loader and
     its SHA-256, Serve version, registers as found, selection rule, block
     counts, and every file's SHA-256.

   On the bench IMUTagNandBmp581:
   - One `tag-capture` found `GD5F2GM7RE_IMUTagNandv2` by name and scanned
     the NAND in 73.7 s: 1 block read, 2047 blank, no uncorrectable pages.
   - `external_ecc.bin` was byte-identical to the step-4 `tag-xflash nand`
     run.
   - On this breakout the registers "as found" are the NAND's power-on
     values (A0 `38`, F0 `08`). The attach's reset leaves PA8 undriven, so
     the switched NAND loses power until the loader drives PA8 again. On the
     final tag, which has no switch, they are the real state.

   The linear path, through the AT25 loaders, is the same code as
   `tag-xflash dump` but has not yet been run through `tag-capture` on a
   PresTag.

## Risks

- **The reset clock.** If MSIS after reset is not 6 MHz on this part, delays
  run fast or slow. Read `RCC_ICSCR1` and `RCC_CFGR1` in step 1, and size
  every budget so that a factor of 4 either way still works.
- **The capture exit aborts a running IMUTag** (next-release-todo A6). This
  plan does not change that. Stop a run before capturing on the bench.
- **The firmware's ECC reading has a gap.** `ECCS` = `11b` ("8 bits
  corrected") is treated as OK only by accident (`gd5f.c:341-345`). It is
  worth fixing in the firmware separately; the loader reports the raw bits.
