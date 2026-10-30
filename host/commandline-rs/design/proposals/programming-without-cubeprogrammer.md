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
STM32CubeProgrammer as the fallback while they are proven. Nothing in the
firmware build compiles Rust: the tools are found on the path, as
STM32_Programmer_CLI is now.

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

The ROM bootloader on the base MCUs (STM32F042, STM32C071, STM32L432) speaks
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
- **Packaging.** The two tools are built by CMake the way the Rust apps are
  (cargo, release profile) and installed with the host tools, so a bench
  machine gets them from the release package. Developers can also `cargo
  install --path host/commandline-rs --bin tag-flash --bin base-flash`.
- **`embedded/CMakeLists.txt`** looks for `tag-flash` and `base-flash` with
  `find_program`, prefers them, and falls back to STM32_Programmer_CLI while
  both exist. A cache variable `TAG_FLASH_BASE` replaces
  `STM32_PROGRAMMER_PROBE`.
- **Linux udev rule.** `70-ultralight-tags.rules` now grants `0483:df11`, so
  `base-flash` needs no root.
- **`BUILD_SOURCES.md`, `docs/build/toolchain-inventory.md` and
  `docs/bench/`** name the new tools where they now name STM32CubeProgrammer.

## Progress

- `tag-flash` is built; not yet run against a tag.
- `base-flash` is built, and on 2026-10-30 wrote a `bittag-base-v7`
  (STM32F042, serial `8009001A5556431520303537`) its own image (`be8f9062`)
  over DFU: the bootloader's string parsed, the 19,548-byte image was written,
  *leave* started it without a replug, and the base came back as an ST-LINK
  reporting the image's identity. Bench check 4 is done for the F042; the C071
  and L432 bases remain.
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
4. **`base-flash` on each base MCU** (F042, C071, L432): the image starts after
   *leave* without a power cycle, and the identity string names the new commit.
5. **Windows and Linux** for `base-flash` (driver and udev, below).

## Open

- **Windows driver for `0483:df11`.** `nusb` needs WinUSB bound to the
  bootloader. STM32CubeProgrammer installs a driver for it; whether that is
  WinUSB, and what a machine without CubeProgrammer needs (Zadig, or an INF we
  ship), is to be checked.
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
