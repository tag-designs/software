# Embedded Bases

`embedded/bases` contains firmware targets for base and programmer boards.
These targets use generated board files from `embedded/boards`, shared base
support from `embedded/bases/common`, and ChibiOS sources from the repository
`ChibiOS` submodule. Base targets pass `0` as the protocol target because they
do not compile tag nanopb protocol sources.

## File Organization

```text
embedded/bases/
  CMakeLists.txt              Active base target list
  BUILD_SOURCES.md            Source inventory from successful builds
  notes.md                    Older base-board notes
  common/
    inc/                      Shared USB, SWD, ST-Link, and app headers
    src/                      Shared USB, SWD, ST-Link, and ADC sources
    make.mk                   Shared ChibiOS make scaffold for STM32F0/L4 bases
    make-c071.mk              Shared ChibiOS make scaffold for STM32C071 bases
  <base-target>/
    CMakeLists.txt            Firmware target and board dependency
    Makefile                  Includes a shared or target-local make scaffold
    project.mk                Board include and local/shared source list
    cfg/                      ChibiOS configuration overrides
    src/                      Base-local application or SWD implementation
```

Each active base target follows this CMake shape:

```cmake
add_embedded_target(<base-target> 0)
add_dependencies(<base-target> <board-target>)
```

The corresponding `project.mk` includes the generated board make fragment:

```make
include $(BOARDDIR)/<BOARD_TYPE>/board.mk
```

Generated firmware artifacts are written under the CMake build tree, not the
source tree.

## Active Bases

| Base target | Board target | Board include |
| --- | --- | --- |
| `bittag-base-jlcpcb-v3` | `board-bittag-base-jlcpcb-v3` | `bittag-base-jlcpcb-v3/board.mk` |
| `bittag-base-v7` | `board-bittag-base-v7` | `BITTAG_BASE_V7/board.mk` |
| `tag-base-c071` | `board-tag-base-c071v1` | `ST_NUCLEO64_C071RB/board.mk` |
| `tag-breakout-base-jlcpcb32-v1` | `board-tag-breakout-base-jlcpcb32-v1` | `tag-breakout-base-jlcpcb32-v1/board.mk` |
| `tag-breakout-base-l432-v1` | `board-tag-breakout-base-l432-v1` | `Tag_Breakout_Base_L432_V1/board.mk` |
| `tag-breakout-base-l432-u375-1v8` | `board-tag-breakout-base-l432-u375-1v8` | `Tag_Breakout_Base_L432_U375_1V8/board.mk` |

## Base Descriptions

Use these sections as the place to describe what each base is for, which
hardware revision it supports, and any programming or debug caveats.

### `bittag-base-jlcpcb-v3`

Description: TODO.

Hardware notes: TODO. Older notes identify this as a 32-pin LQFP processor
package path intended for Tagbase v6/v7 hardware.

Build target:

```sh
cmake --build <build-dir> --target bittag-base-jlcpcb-v3
```

Board dependency: `board-bittag-base-jlcpcb-v3`

Board include: `$(BOARDDIR)/bittag-base-jlcpcb-v3/board.mk`

Local files:

- `src/main.c`
- `cfg/chconf.h`
- `cfg/halconf.h`
- `cfg/mcuconf.h`

Shared sources from `common`: `usbcfg.c`, `ll_swd.c`, `stlink.c`,
`stm32adc.c`.

Notes: TODO.

### `bittag-base-v7`

Description: TODO.

Hardware notes: TODO.

Build target:

```sh
cmake --build <build-dir> --target bittag-base-v7
```

Board dependency: `board-bittag-base-v7`

Board include: `$(BOARDDIR)/BITTAG_BASE_V7/board.mk`

Local files:

- `src/main.c`
- `cfg/chconf.h`
- `cfg/halconf.h`
- `cfg/mcuconf.h`

Shared sources from `common`: `usbcfg.c`, `ll_swd.c`, `stlink.c`,
`stm32adc.c`.

Notes: TODO.

### `tag-base-c071`

Description: TODO.

Hardware notes: TODO.

Build target:

```sh
cmake --build <build-dir> --target tag-base-c071
```

Board dependency: `board-tag-base-c071v1`

Board include: `$(BOARDDIR)/ST_NUCLEO64_C071RB/board.mk`

Local files:

- `src/main.c`
- `src/ll_swd.c`
- `src/ll_swd_spi.c`
- `src/stlink-local.c`
- `STM32C071xB.ld`
- `cfg/chconf.h`
- `cfg/halconf.h`
- `cfg/mcuconf.h`

Shared sources from `common`: `usbcfg.c`, `stlink.c`, `stm32adc.c`.

Notes: TODO.

### `tag-breakout-base-jlcpcb32-v1`

Description: TODO.

Hardware notes: TODO.

Build target:

```sh
cmake --build <build-dir> --target tag-breakout-base-jlcpcb32-v1
```

Board dependency: `board-tag-breakout-base-jlcpcb32-v1`

Board include: `$(BOARDDIR)/tag-breakout-base-jlcpcb32-v1/board.mk`

Local files:

- `src/main.c`
- `cfg/chconf.h`
- `cfg/halconf.h`
- `cfg/mcuconf.h`

Shared sources from `common`: `usbcfg.c`, `ll_swd.c`, `stlink.c`,
`stm32adc.c`.

Notes: TODO.

### `tag-breakout-base-l432-v1`

Description: TODO.

Hardware notes: TODO.

Build target:

```sh
cmake --build <build-dir> --target tag-breakout-base-l432-v1
```

Board dependency: `board-tag-breakout-base-l432-v1`

Board include: `$(BOARDDIR)/Tag_Breakout_Base_L432_V1/board.mk`

Local files:

- `src/main.c`
- `src/ll_swd.c`
- `src/ll_swd_spi.c`
- `Note.md`
- `cfg/chconf.h`
- `cfg/halconf.h`
- `cfg/mcuconf.h`

Shared sources from `common`: `usbcfg.c`, `stlink.c`, `stm32adc.c`.

Notes: TODO.

### `tag-breakout-base-l432-u375-1v8`

Description: STLink-compatible breakout base for an STM32U375 target with
1.8 V analog buffer enable and explicit SWDIO direction control.

Hardware notes: The STM32L432 base MCU runs from an 80 MHz HSI16-derived PLL.
USB uses HSI48 with CRS sync. `EN1V8` is a static high board output,
`TGT_RESET` is active-high, and local bitbang SWD drives `SWDIO_DIR` high for
transmit and low for receive.

Build target:

```sh
cmake --build <build-dir> --target tag-breakout-base-l432-u375-1v8
```

Board dependency: `board-tag-breakout-base-l432-u375-1v8`

Board include: `$(BOARDDIR)/Tag_Breakout_Base_L432_U375_1V8/board.mk`

Local files:

- `src/main.c`
- `src/ll_swd.c`
- `make-l432.mk`
- `cfg/chconf.h`
- `cfg/halconf.h`
- `cfg/mcuconf.h`

Shared sources from `common`: `usbcfg.c`, `stlink.c`.

Notes: Initial bringup uses bitbang SWD. SPI-capable SWD can be added after
the target attach path is proven.

## Maintenance Notes

- Update this README when adding, removing, or renaming a base target.
- Update `BUILD_SOURCES.md` when CMake source lists or base build membership
  changes.
- Keep board pin and signal changes in `embedded/boards`; keep base USB, SWD,
  programming, and command behavior here.
