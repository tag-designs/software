---
type: proposal
status: proposed
summary: Two Rust command-line tools that remove STM32CubeProgrammer from the build and the bench tools - tag-flash, which programs, erases and reads tags through a base on the tagprobe library Tag Programmer already uses, and base-flash, which programs bases through the STM32 ROM bootloader's DfuSe interface with dfu-core and dfu-nusb.
---

# Programming Without STM32CubeProgrammer

STM32CubeProgrammer can leave the build and the bench with two small Rust
tools:

- **`tag-flash`** programs, erases and reads a **tag** through a base. It is a
  command line over [`tagprobe`](../../../libraries/tagprobe-rs/README.md),
  which Tag Programmer already uses for the same jobs, so the programming code
  exists and has been run on hardware.
- **`base-flash`** programs a **base** through its MCU's ROM bootloader over
  USB DFU. It is new code, small, on `dfu-core` and `dfu-nusb` (MIT OR
  Apache-2.0). Its one subtlety is ST's DfuSe dialect, below.

The CMake `-download`, `-erase` and `-dfu` targets then call these tools, with
STM32CubeProgrammer as the fallback while they are proven. Where cargo is
installed the build compiles them (the `flash_tools` target); without it they
are found on the path, as STM32_Programmer_CLI is, and failing that the
targets use STM32_Programmer_CLI.

## What uses STM32CubeProgrammer today

| User | Call | Replaced by |
| --- | --- | --- |
| `<Tag>-download` (`embedded/CMakeLists.txt`) | `-d <elf> -g 0x08000000` through `stm32_programmer_select.py` | `tag-flash program` |
| `<Tag>-erase` | `-e all` | `tag-flash erase` |
| `<Base>-dfu` | `-c port=usb1 -d <elf>` | `base-flash` |
| `embedded/tools/flash_release.py` | program, partial or mass erase, PEMPTY check | `tag-flash program --erase ...` (the script keeps its manifest and record work) |
| `embedded/tools/stm32_programmer_select.py` | chooses an ST-LINK by serial or index | `--base` on each tool; the script is retired |
| `embedded/tools/decode_scratchpad.py` | `-u 0x2003E000 8192` | `tag-flash read` |
| `embedded/tools/tag_capture_state.py` | register and memory reads | already replaced by `tag-capture`; retire |
| `embedded/loaders/` | build `.stldr` external loaders | nothing to replace: `tag-xflash` runs them itself. The format stays ST's, so they still work in CubeProgrammer. |

## `tag-flash`

```text
tag-flash [--base <serial>] info
tag-flash [--base <serial>] program <elf> [--erase image|all|pages:<first>-<last>|keep-calibration]
tag-flash [--base <serial>] erase
tag-flash [--base <serial>] read <address> <length> -o <file>
tag-flash list
```

Each command is one `tagprobe` call:

| Command | `tagprobe` | Notes |
| --- | --- | --- |
| `list` | `list()`, `identify()` | Bases on USB, with serial and identity string. |
| `info` | `connect_check()` | Device ID, part name, target voltage. |
| `program` | `chip_for()`, then `program()` | Erase, program and verify in one session, then clear `FLASH_SR.PEMPTY` on an L4 and start the image from its vector table: the one-session rule `flash_release.py` relies on, and the `-g 0x08000000` the download targets use. |
| `erase` | `program()` with `Erase::All` and no image, or a new `erase()` | A mass erase leaves PEMPTY set; `tag-flash erase` says so and does not start anything. |
| `read` | `read_memory()` | Under reset: post-mortem only, like every attach. |

- **Base selection.** `--base` takes the base's USB serial. With one base
  connected it may be omitted; with several it is required, and the error lists
  them. This replaces `stm32_programmer_select.py`'s selectors, whose default
  `pid:0483:3748` does not match the bench bases anyway.
- **Driver.** `Driver::for_identity(identify(..))`, as Tag Programmer does, so
  field bases whose firmware predates the identify command use the FieldSafe
  driver.
- **`--erase keep-calibration`** moves `calibration_keep_erase()` from
  `flash_release.py` into the tool: it reads `__calibration_start__` and
  `__tag_flash_page_size__` from the image and erases the pages below it, as
  `Erase::Range`. Same refusals as the script (no pinned calibration, not a
  2 KB-page image).
- **Exit status** 0 on success, 1 on failure, 2 on bad options (clap), as the
  other Rust tools.

## `base-flash`

```text
base-flash [--serial <dfu serial>] [--wait <seconds>] <elf>
base-flash list
```

It writes the base image's flash contents to internal flash through the ROM
bootloader and leaves DFU mode, so the base starts the new firmware. It is the
equivalent of

```text
dfu-util -a 0 -i 0 -s 0x08000000:leave -D base.bin
```

### How STM32 DFU differs from plain DFU

The ROM bootloader on the base MCUs (STM32F042, STM32L432) speaks
**DfuSe**, ST's extension of DFU 1.1, announced as bcdDFU `0x011A`. Plain DFU
writes a stream to an implied location; DfuSe adds commands carried in
DFU_DNLOAD block 0:

| Command | Byte | Use |
| --- | --- | --- |
| Set Address Pointer | `0x21` | Where the following blocks are written, and where *leave* jumps. |
| Erase | `0x41` | Erase the page containing an address (or mass erase with no address). |

Data blocks start at `wBlockNum` 2 and land at *pointer + (block - 2) x
transfer size*. The memory map comes from the alternate setting's string,
for example `@Internal Flash  /0x08000000/32*001Kg`: start address, then
page counts, sizes and attributes.

`dfu-core` handles this: given the DfuSe version it parses that string, erases
the pages the image covers (and no others), sets the address once, and writes
the blocks. Against the `dfu-util` line above:

| `dfu-util` | `base-flash` |
| --- | --- |
| `-i 0` | Interface 0, the bootloader's only one. |
| `-a 0` | Alternate setting 0, **checked** to be `@Internal Flash` at `0x08000000` before anything is written. Alternate setting 1 is option bytes, which this tool never writes. |
| `-s 0x08000000` | `override_address(0x08000000)`, taken from the ELF, not typed. |
| `:leave` | After the last block `dfu-core` sends the zero-length DFU_DNLOAD; the bootloader's GET_STATUS then manifests and jumps to the address pointer, which is still the start address because `dfu-core` set it once. The bootloader is not manifestation tolerant and sets willDetach, so the device leaves the bus itself. **To be confirmed on each base MCU** (below). |
| `-D base.bin` | The ELF's loadable segments in flash, flattened into one image from the lowest address with gaps filled with `0xFF` (what `objcopy -O binary` makes). A segment outside the alternate setting's memory map is refused, not dropped. |

### Selection, waiting and checking

- The bootloader enumerates as `0483:df11`. Its serial number is not the
  base's ST-LINK serial, so `--serial` takes the DFU serial that `base-flash
  list` prints. With one DFU device it may be omitted.
- **Entering DFU is done by hand**: the base is plugged into USB with its
  button held, which holds BOOT0 so the MCU starts its ROM bootloader. The base
  firmware has no jump-to-bootloader command. `--wait` polls for the device so
  the command can be started first, and the tool says to plug the base in with
  the button held while it waits.
- **Checking the result.** `dfu-core` has no upload (read-back). The check is
  that the base comes back as an ST-LINK (`0483:3748`) within a few seconds and
  that its identity string names the commit of the image just written: the
  base's own firmware reports `fw=<commit>;dirty=<0|1>`. A DfuSe upload
  read-back can be added later in about fifty lines if that is not enough.
- **No async runtime.** `dfu-nusb` offers a synchronous `DfuSync`; only opening
  the interface is async, and a one-line `block_on` covers it.

## Where the code goes

- **`tagprobe-rs`** gains a `dfu` module: listing DfuSe devices, the alternate
  setting check, the ELF flattening and the download. It is the project's
  programming library, and Tag Programmer could later offer *Update base
  firmware* from the same code.
- **`host/commandline-rs`** gains the two binaries. They are new tools with no
  C++ counterpart, so they do not change the decision that the shipped
  command-line tools stay C++.
- **Packaging.** `cmake/RustFlashTools.cmake` defines one cargo build of the
  two tools (release profile, `--locked`) per build tree, the `flash_tools`
  target. `host/commandline-rs/CMakeLists.txt`, added with the Rust apps under
  `BUILD_RUST_APPS`, installs them with the host tools (`tag_tools/bin`;
  beside the other tools on Windows) and signs them on macOS, so a bench
  machine gets them from the release package. Developers can also `cargo
  install --path host/commandline-rs --bin tag-flash --bin base-flash`.
- **`embedded/CMakeLists.txt`** uses the same `flash_tools` build, or without
  cargo `find_program`, and prefers the tools; `EMBEDDED_PROGRAMMER`
  (`AUTO`, `TAG_FLASH`, `CUBEPROGRAMMER`) can choose outright. A cache
  variable `TAG_FLASH_BASE` names a base by serial, as `STM32_PROGRAMMER_PROBE`
  does for the fallback.
- **Linux udev rule.** `70-ultralight-tags.rules` now grants `0483:df11`, so
  `base-flash` needs no root.
- **`BUILD_SOURCES.md`, `docs/build/toolchain-inventory.md` and
  `docs/bench/`** name the new tools where they now name STM32CubeProgrammer.

## Progress

- `tag-flash` ran on 2026-10-30 against a BitTag V6 (UUID
  `2039354D30315012005E0068`, STM32L432KC, 3.22 V from the base) on that
  `bittag-base-v7`, with the fw-v0.6 BitTag image the tag was already running
  (`56e5e6a0`, verified against its manifest):
  - `read` of the image's 33,996 bytes was identical to the release `.bin`;
  - `program` (image pages) took 2.5 s, and the tag then ran its image, IDLE,
    self-tests `ALL_PASSED`;
  - 14 mass-erase cycles (`program --erase plan`, which is a mass erase for
    this pre-layout image) and 4 separate `erase`-then-`program` cycles all
    left the tag running its image; `FLASH_SR` read 0 (PEMPTY clear). Bench
    check 2 is done for this BitTag.
  - A tool attaching the moment `tag-flash` let the base go could fail its
    first debug read (SWD status `0x81`): 3 of 5 back-to-back `tag-info` runs
    did, and the tag answered at the next attach. `tag-flash` now waits 1 s
    before exiting after any command that attaches; 4 of 4 then passed.
  - The connect check cannot read the L4's flash size register, because
    system memory does not answer while the core is held in reset, so
    `tagprobe::flash_bytes()` reads it in a probe-rs session (one more
    attach). It read 256 KB.
- `base-flash` is built, and on 2026-10-30 wrote a `bittag-base-v7`
  (STM32F042, serial `8009001A5556431520303537`) its own image (`be8f9062`)
  over DFU: the bootloader's string parsed, the 19,548-byte image was written,
  *leave* started it without a replug, and the base came back as an ST-LINK
  reporting the image's identity. The same day it wrote a
  `tag-breakout-base-l432-u375-1v8` (STM32L432, serial
  `004D0059543050162030324E`) main 5017632d (22,908 bytes), with the same
  result. Bench check 4 is done for both base MCUs. (`tag-base-c071` was an
  abandoned development base and is not a target.)
- `tag-flash` programmed the IMUTagNandBmp581 breakout (STM32U375RG, UUID
  `00303143433650090059002E`) on that base three times on 2026-10-30 with the
  image it already carried (main 7cef2ba9): 3.4 s each, the tag running its
  image (IDLE) after each, flash read back identical. Bench check 1 is done.
  The attempts are logged in
  `captures/2026-10-30-imutag-tag-flash/flash-log.md`.
- Steps 2 and 4 were done on 2026-10-30, with packaging: the CMake
  `-download`, `-erase` and `-dfu` targets run `tag-flash program`,
  `tag-flash erase` and `base-flash program --wait 60`; `flash_release.py`
  programs with `tag-flash` (`--erase image|all`, and `plan` for
  `--keep-calibration` after the script's own checks) and falls back to
  STM32_Programmer_CLI; `decode_scratchpad.py --read` reads the scratchpad
  with `tag-flash read`. The targets were checked by configuring a build tree
  each way and building `flash_tools` (`--locked`); the host install was
  checked by installing and ad-hoc signing the two tools. The same day, on
  the IMUTagNandBmp581 breakout and its `tag-breakout-base-l432-u375-1v8`,
  each path programmed a board from a build at 33dfce16:
  `IMUTagNandBmp581-download` (11.6 s, tag IDLE on the new image),
  `flash_release.py` (manifest checked, JSON record written, tag IDLE), and
  `tag-breakout-base-l432-u375-1v8-dfu` (base found in DFU mode, written,
  confirmed reporting `fw=33dfce16`; the tag then answered through it).
- `nusb`'s futures need a Tokio reactor when awaited (its `tokio` feature is
  on through `dfu-nusb`), so `tagprobe::dfu` opens the device on a
  single-threaded Tokio runtime. The download itself is synchronous.

## Bench checks before CubeProgrammer is dropped

Following the flashing protocol in `AGENTS.md` (supply voltage, UUID, erase by
the firmware that wrote the log, flash, self-test):

1. **A U375 program cycle** through `tag-flash program`. `tagprobe` has only
   run Retest on the IMUTag; programming a U375 has not been run.
2. **The PEMPTY rule** on an L432 tag: `program` after `erase` and after
   `--erase all`, several times each, the tag booting its image every time
   (the failure the download script was chosen to avoid boots the ROM
   bootloader 3 times in 4).
3. **`--erase keep-calibration`** on a CompassTag: calibration survives,
   configuration and log do not.
4. **`base-flash` on each base MCU** (F042, L432): the image starts after
   *leave* without a power cycle, and the identity string names the new commit.
5. **Windows and Linux** for `base-flash` (driver and udev, below).

## Open

- **Windows driver for `0483:df11`.** `nusb` needs WinUSB bound to the
  bootloader. WinUSB itself ships with Windows 11; that it binds to the ST
  bootloader on a machine that has never had STM32CubeProgrammer installed is
  still to be seen on hardware (bench check 5).
- **Whether *leave* starts the image on every bootloader version.** It does on
  the F042 (above). Some ST bootloaders need the Set Address Pointer sent again before the zero-length
  download, as `dfu-util` does for `:leave`. If one does, `base-flash` sends it
  itself rather than relying on `dfu-core`'s pointer.
- **Whether `flash_release.py` should become a `tag-flash release <dir>`
  command**, folding in the manifest check and the CSV record that Tag
  Programmer already has in Rust. The first step keeps the script and only
  changes the program it calls.

## Steps

1. `tag-flash` with `list`, `info`, `program`, `erase`, `read`, on the
   existing `tagprobe` calls; bench checks 1-3.
2. The CMake download and erase targets prefer `tag-flash`;
   `flash_release.py` and `decode_scratchpad.py` call it.
3. `tagprobe::dfu` and `base-flash`; the udev rule; bench checks 4-5.
4. The CMake `-dfu` targets prefer `base-flash`.
5. Retire `stm32_programmer_select.py` and `tag_capture_state.py`; drop the
   STM32_Programmer_CLI fallback, leaving STM32CubeProgrammer only as Tag
   Programmer's Qt build's dependency until that build is removed.
