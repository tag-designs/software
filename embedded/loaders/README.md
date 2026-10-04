---
type: readme
status: current
summary: SRAM-resident external flash loaders for STM32L432 NOR and STM32U375 NAND tags: layout, per-board naming, building, use with STM32CubeProgrammer, adding a loader and bench testing.
---

# External Flash Loaders

Small images that read, and optionally erase and program, a tag's external
flash **without touching internal flash**. The programmer downloads a loader
into the tag's SRAM and calls its entry points over SWD. The tag's firmware, its
persistent configuration and its marker log stay exactly as they were, which is
the point: a returned tag's internal flash is evidence, and a recovery firmware
would overwrite it. The case for this approach is
[decision 0022](../../docs/decisions/0022-field-extraction-sram-loader-not-recovery-firmware.md).

The images follow STM32CubeProgrammer's external-loader (`.stldr`) contract, so
`STM32_Programmer_CLI -el` can drive them. The host library in `tagcore` drives
the same images without CubeProgrammer. It uses their `Serve()` entry point,
and `tag-xflash dump` reads a whole part with it. See
[SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md).

Runtime rules, the entry-point contract and what the bench established are in
[Loader Runtime Design](design/loader-runtime.md). Read it before changing
anything in `common/`. It also covers the STM32U375 SPI-NAND loader,
`GD5F2GM7RE_IMUTagNandv2`. Open work is in [TODO.md](TODO.md).

## Layout

```text
loaders/
  CMakeLists.txt            add_subdirectory per loader
  common/
    make.mk                 build rules: ChibiOS headers + PAL, os-less OSAL, no crt0
    make-u375.mk            the same rules for STM32U375 (Cortex-M33, U3 startup and platform)
    STM32L432-loader.ld     link map: StorageInfo at 0, image in SRAM1 from 0x20000004
    STM32U375-loader.ld     the same map for STM32U375
    cfg/stm32l4/            halconf.h (PAL only), mcuconf.h (HSI16, STM32_NO_INIT), osalconf.h
    cfg/stm32u3/            the same for STM32U375
    inc/loader.h            clock, delay, SPI, and the board hooks a target supplies
    inc/loader_flash.h      the part-driver interface
    inc/dev_inf.h           CubeProgrammer's StorageInfo layout
    src/loader_entry.c      Init / Read / Write / SectorErase / MassErase, and Serve()
    src/loader_clock.c      HSI16 by hand, never the backup domain
    src/loader_clock_u3.c   STM32U375: MSIS pinned at 12 MHz, nothing else touched
    src/loader_delay.c      DWT busy-wait delays
    src/loader_spi.c        polled, bounded SPI master (STM32 SPIv2)
    src/loader_spi_u3.c     polled, bounded SPI master (STM32U3 SPI)
    src/at25xe_loader.c     AT25XE part driver
    src/gd5f_loader.c       GD5F SPI-NAND part driver, read-only, paged
    src/rv3028_probe.c      RV3028 RTC register probe
  AT25XE_PresTagv3/         one loader: one board + one part
    CMakeLists.txt          two images, RO and RW
    Makefile, project.mk    like a tag target
    src/board_loader.c      pins and board bring-up
    src/dev_inf.c           StorageInfo: name, size, sector map
  AT25XE_CompassTagv1/      the same part on the CompassTagv1 board
  AT25XE_UIUCTag/           the same part on the UIUCTag board
  GD5F2GM7RE_IMUTagNandv2/  GD5F2GM7RE SPI NAND on IMUTagNandv2 (STM32U375); read-only image only
  RV3028_PresTagv3/         not a flash loader: a read-only RTC register probe
  RV3028_UIUCTag/           the same probe, RTC lines swapped (PROBE_SWAP_I2C)
  RV3028_IMUTagNandv2/      the same probe on IMUTagNandv2 (STM32U375)
```

The probes share `common/src/rv3028_probe.c`; a probe target is a `Makefile`,
a `CMakeLists.txt` and a `project.mk` naming its board and any line mapping:
`-DPROBE_SWAP_I2C=1` for `RV3028_UIUCTag`, and
`-DLINE_RTC_SDA=LINE_SDA -DLINE_RTC_SCL=LINE_SCL` for `RV3028_IMUTagNandv2`.

The `Serve()` command block is defined in `include/loader_service.h` at the
top of the repository, because the host library uses the same definition.

A loader target is laid out like a tag target: `Makefile` includes
`../common/make.mk` (`../common/make-u375.mk` for the STM32U375 targets),
`project.mk` names the board, MCU config and sources, and a
target's `./cfg`, `./inc` and `./src` override same-named files in `common/`.

## Naming: one loader per board and part

A loader is bound to a board -- its pins, its flash part, its MCU and SRAM map
-- not to a tag. PresTag and PresTagRaw both run on PresTagv3, so one loader
serves both. Names follow ST's `<MEMORY>_<BOARD>` convention:

| Image | Contents |
| --- | --- |
| `AT25XE_PresTagv3.stldr` | Read-only. Forensic use; the only image the recovery procedure uses. |
| `AT25XE_PresTagv3-RW.stldr` | Erase and program, each verified by read-back. Rescue and bench testing. |
| `AT25XE_CompassTagv1.stldr`, `-RW` | The same pair for CompassTagAT25 and CompassTagAT25Breakout. |
| `AT25XE_UIUCTag.stldr`, `-RW` | The same pair for UIUCTag. |
| `GD5F2GM7RE_IMUTagNandv2.stldr` | Read-only, for IMUTagNandBmp581; not marked `DISTRIBUTE`, so it is not in the release (see [TODO.md](TODO.md)). Paged: the host reads it page by page through `Serve()`'s `READ_PAGE`, raw or through ECC (`tag-xflash nand`, `tag-capture`). |

The two are one source directory built twice. The read-only image is built with
`LOADER_ALLOW_WRITE=0` and does not contain the erase or program code at all; it
refuses both. The device name in `StorageInfo` carries the variant and the
commit, e.g. `AT25XE_PresTagv3 RO 419d047e`, so CubeProgrammer shows which image
is loaded.

## Building

```sh
cmake --build <build-dir> --target AT25XE_PresTagv3 AT25XE_PresTagv3-RW
```

`add_embedded_loader()` in `embedded/CMakeLists.txt` runs make and writes a
build manifest beside the image; the make scaffold (`common/make.mk` or
`common/make-u375.mk`) copies the ELF to `.stldr`. Outputs land in
`<build-dir>/embedded/loaders/<dir>/<image>/build/`. Loaders marked `DISTRIBUTE`
are installed to `share/<package>/loaders/<image>/` and are built by
`distributed_firmware`, so a tag's loader ships from the same archive as its
firmware. Only the AT25XE loaders are marked; the NAND loader and the RV3028
probes are not.

## Using a loader with STM32CubeProgrammer

Capture the tag's state **first**. The loader overwrites the start of SRAM, and
that SRAM is evidence. See `embedded/tools/tag_capture_state.py`, and note its
limitation on STM32L432 in [TODO.md](TODO.md).

```sh
LDR=$PWD/<build-dir>/embedded/loaders/AT25XE_PresTagv3/AT25XE_PresTagv3/build/AT25XE_PresTagv3.stldr
STM32_Programmer_CLI -c port=SWD mode=UR -el "$LDR" -u 0x90000000 0x400000 dump.bin
```

- **Pass `-el` an absolute path.**
- **The external flash appears at `0x90000000`.** The address is fictional, since
  the part is on plain SPI; the loader subtracts the base.
- **Check that the loader was actually used.** Add `-vb 3` and look for
  `Init flashloader...`. If CubeProgrammer rejects a loader it says nothing, and
  reads go straight to SWD address `0x90000000`, which on an STM32L432 returns
  zeros and reports success.
- **Do not erase with sector numbers** (`-e <n> <m>`) while a loader is loaded:
  whether the numbers mean external sectors or internal flash pages is not
  something to discover on a tag. Write through the RW image instead; the
  programmer erases the covering external sectors itself, and says so
  (`Erasing external memory sectors [16 32]`).

A full 4 MB read takes about 70 s.

## Adding a loader

**Same part, new board** (another AT25XE board, for example CompassTagAT25):

1. Copy `AT25XE_PresTagv3/` to `AT25XE_<Board>/` and add it to
   `CMakeLists.txt`.
2. Point `LOADER_BOARD_INC` in `project.mk` at the board's committed
   `boards/<Board>/generated/`. Only `board.h` is used.
3. Fill in `loaderFlashBus` in `src/board_loader.c` from the firmware's own
   binding (`families/<Family>/src/devices.c` or the tag's `devices.c`): SPI
   instance, CS, SCK, MISO, MOSI and the alternate function.
4. In `loaderBoardInit()`, enable the GPIO ports those lines use. If the flash
   sits behind a power switch, sequence it here, and touch nothing else. In
   particular, leave other devices' pins alone: driving a sensor's pins while its
   rail is off back-powers it.
5. Update the name string in `src/dev_inf.c`.

`AT25XE_CompassTagv1` is an example: it differs from `AT25XE_PresTagv3` by its
`LOADER_BOARD_INC`, a `board_loader.c` for PA15/PB3-PB5 with GPIOA and GPIOB
enabled, its name, and its CMake entry. Step 4 is the judgement call: the magnetometer shares SPI1 in
the firmware, but on other pins behind a switched rail, so the loader leaves
those pins alone.

**New part** (MX25R, MX25L, ...):

1. If the part's command set lives only in its firmware `.c`, move the opcodes,
   status bits and timing budgets into a dependency-free `<part>_commands.h`
   beside it, as `at25xe_commands.h` does, and have the firmware driver include
   it. Prove the firmware is unchanged by comparing the `.list` of every target
   that builds the driver, before and after, at the same commit.
2. Write `common/src/<part>_loader.c` implementing `loader_flash.h`. Probe must
   not write the part: in particular, do not reuse a firmware `wake` hook that
   clears protection. Success for erase and program is a read-back, never the
   busy bit alone.
3. Check each budget against the datasheet and note the datasheet's figures in
   the commands header.

For **SPI NAND**, follow `common/src/gd5f_loader.c`: build with
`LOADER_FLASH_PAGED=1`, implement `loaderFlashReadPage()`, return each page raw
or through ECC with its status, and leave bad-block handling, ECC decisions and
the choice of pages to the host. Its constraints are in
[Loader Runtime Design](design/loader-runtime.md#stm32u375-and-spi-nand).

**New MCU:** STM32U375 is the worked example: `cfg/stm32u3/`,
`make-u375.mk` (its startup and platform makefiles), `STM32U375-loader.ld` for
its SRAM map, `loader_clock_u3.c` and `loader_spi_u3.c`. Check the assumed reset
clock against the registers captured after reset: for the U375, ChibiOS assumes
MSIS = MSIRC1/4 (6 MHz) but the captured registers read MSIRC1/2 (12 MHz), so
`loader_clock_u3.c` selects MSIRC1/2 explicitly.

## Testing a loader on the bench

The steps that validated `AT25XE_PresTagv3`, in order. Each proves something
the one before cannot.

1. **Confirm the loader is used:** a `-vb 3` read shows `Init flashloader...`
   and a returned `R0 0x00000001`. `Init` succeeds only if the JEDEC ID matched,
   so this also proves the SPI wiring.
2. **Read twice and compare.** Identical 4 MB reads rule out a floating bus.
   A blank part reads as all `0xFF`; all `0x00` means the loader was bypassed.
3. **Write a random pattern with RW and read it back with RO.** Use an
   unaligned length. The RO read-back is independent of the programmer's own
   verify, and should show the bytes either side of the pattern still blank.
4. **Write a second pattern over the first.** This forces a real erase of
   sectors that hold data; programming can only clear bits, so a failed erase
   shows up as a mismatch.
5. **Try to write with RO.** It must fail with `failed to erase memory`, and a
   read must show the data unchanged.
6. **Restore the tag.** Write `0xFF` over the test range with RW and confirm the
   whole part reads blank. Leaving test data in external flash behind an empty
   internal log index is the inconsistent state described in
   [Loader Runtime Design](design/loader-runtime.md#rescue-erase).
7. **Compare the RTC backup registers before and after** (`RTC_BKP0R` at
   `0x40002850` on STM32L432, read under reset). The loader must not write the
   backup domain. A CubeProgrammer session itself changes `resetCause`; see
[the investigation](design/investigations/2026-10-loader-session-reset-cause.md).
