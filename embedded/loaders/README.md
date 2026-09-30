# External Flash Loaders

Small images that read, and optionally erase and program, a tag's external
flash **without touching internal flash**. The programmer downloads a loader
into the tag's SRAM and calls its entry points over SWD. The tag's firmware, its
persistent configuration and its marker log stay exactly as they were, which is
the point: a returned tag's internal flash is evidence, and a recovery firmware
would overwrite it. The case for this approach is in
[Field Data Extraction](../../design/field-data-extraction.md).

The images follow STM32CubeProgrammer's external-loader (`.stldr`) contract, so
`STM32_Programmer_CLI -el` can drive them today. A host tool built on `tagcore`
is planned to replace CubeProgrammer as the driver; it will use the same images.

Runtime rules, the entry-point contract and what the bench established are in
[Loader Runtime Design](design/loader-runtime.md). Read it before changing
anything in `common/`.

## Layout

```text
loaders/
  CMakeLists.txt            add_subdirectory per loader
  common/
    make.mk                 build rules: ChibiOS headers + PAL, os-less OSAL, no crt0
    STM32L432-loader.ld     link map: StorageInfo at 0, image in SRAM1 from 0x20000004
    cfg/stm32l4/            halconf.h (PAL only), mcuconf.h (HSI16, STM32_NO_INIT), osalconf.h
    inc/loader.h            clock, delay, SPI, and the board hooks a target supplies
    inc/loader_flash.h      the part-driver interface
    inc/dev_inf.h           CubeProgrammer's StorageInfo layout
    src/loader_entry.c      Init / Read / Write / SectorErase / MassErase
    src/loader_clock.c      HSI16 by hand, never the backup domain
    src/loader_delay.c      DWT busy-wait delays
    src/loader_spi.c        polled, bounded SPI master (STM32 SPIv2)
    src/at25xe_loader.c     AT25XE part driver
  AT25XE_PresTagv3/         one loader: one board + one part
    CMakeLists.txt          two images, RO and RW
    Makefile, project.mk    like a tag target
    src/board_loader.c      pins and board bring-up
    src/dev_inf.c           StorageInfo: name, size, sector map
```

A loader target is laid out like a tag target: `Makefile` includes
`../common/make.mk`, `project.mk` names the board, MCU config and sources, and a
target's `./cfg`, `./inc` and `./src` override same-named files in `common/`.

## Naming: one loader per board and part

A loader is bound to a board -- its pins, its flash part, its MCU and SRAM map
-- not to a tag. PresTag and PresTagRaw both run on PresTagv3, so one loader
serves both. Names follow ST's `<MEMORY>_<BOARD>` convention:

| Image | Contents |
| --- | --- |
| `AT25XE_PresTagv3.stldr` | Read-only. Forensic use; the only image the recovery procedure uses. |
| `AT25XE_PresTagv3-RW.stldr` | Erase and program, each verified by read-back. Rescue and bench testing. |

The two are one source directory built twice. The read-only image is built with
`LOADER_ALLOW_WRITE=0` and does not contain the erase or program code at all; it
refuses both. The device name in `StorageInfo` carries the variant and the
commit, e.g. `AT25XE_PresTagv3 RO 419d047e`, so CubeProgrammer shows which image
is loaded.

## Building

```sh
cmake --build <build-dir> --target AT25XE_PresTagv3 AT25XE_PresTagv3-RW
```

`add_embedded_loader()` in `embedded/CMakeLists.txt` runs make, writes a build
manifest beside the image, and copies the ELF to `.stldr`. Outputs land in
`<build-dir>/embedded/loaders/<dir>/<image>/build/`. Loaders marked `DISTRIBUTE`
are installed to `share/<package>/loaders/<image>/` and are built by
`distributed_firmware`, so a tag's loader ships from the same archive as its
firmware.

## Using a loader with STM32CubeProgrammer

Capture the tag's state **first**. The loader overwrites the start of SRAM, and
that SRAM is evidence. See `embedded/tools/tag_capture_state.py`, and note its
limitation on STM32L432 under [Open issues](design/loader-runtime.md#open-issues).

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

For **SPI NAND** (GD5F), read raw pages including the spare area, and leave
bad-block handling and ECC to the host, for the reasons in Field Data
Extraction. The page-read-to-cache sequence fits the same `loaderFlashRead`
interface.

**New MCU** (STM32U375): needs its own `cfg/stm32u3/`, a
`make-u375.mk`-style variant of `make.mk` (different startup and platform
makefiles), a linker script for its SRAM map, and a second path in
`loader_spi.c` for the U3's SPI peripheral, which is the `SPI_TXDR` design
already handled by `tags/common/core/src/spi_bus_polled.inc`. Check the clock
switch against that part's reference manual; `loader_clock.c` is L4-specific.

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
   backup domain; see the open issue on `resetCause`.
