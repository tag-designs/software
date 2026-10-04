---
type: readme
status: current
summary: Layout, CMake shape and descriptions of the base and programmer board firmware targets: MCU, board and SWD implementation of each.
---

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
  TODO.md                     Open work on the bases
  design/investigations/      Dated measurement records
  common/
    inc/                      Shared USB, SWD, ST-Link, and app headers
    src/                      Shared USB, SWD, ST-Link, and ADC sources
    make.mk                   Shared ChibiOS make scaffold for STM32F042 bases
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
source tree. Base targets are not installed or packaged:
`add_embedded_target()` installs only targets passed `DISTRIBUTE`, and no base
passes it.

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

A base is a USB SWD programmer for tags: it presents itself to the host as an
ST-LINK (`common/src/stlink.c`, over `usbcfg.c`) and drives the tag's SWD lines
from its own MCU. The CMake download targets prefer it by its USB PID,
`0483:3748` (see
[Embedded Source Layout](../design/source-layout.md#tags)). Each base differs in
MCU, board, and how it clocks SWD.

| Base target | Base MCU | Board | SWD implementation |
| --- | --- | --- | --- |
| `bittag-base-jlcpcb-v3` | STM32F042 | BitTag Base JLCPCB v3 | GPIO bitbang, `common/src/ll_swd.c` |
| `bittag-base-v7` | STM32F042x6 | BitTag Base v7 | GPIO bitbang, `common/src/ll_swd.c` |
| `tag-breakout-base-jlcpcb32-v1` | STM32F042 | Tag Breakout Base JLCPCB32 v1 | GPIO bitbang, `common/src/ll_swd.c` |
| `tag-breakout-base-l432-v1` | STM32F042x6 | Tag Breakout Base L432 v1 | optimized GPIO bitbang, local `src/ll_swd.c` |
| `tag-breakout-base-l432-u375-1v8` | STM32L432 | Tag Breakout Base L432 U375 1V8 | hybrid SPI/GPIO, local `src/ll_swd_spi.c` |
| `tag-base-c071` | STM32C071 | ST Nucleo64-C071RB | hybrid SPI/GPIO, local `src/ll_swd_spi.c` |

The STM32F042 bases build with `common/make.mk`;
`tag-breakout-base-l432-u375-1v8` has its own `make-l432.mk`, and
`tag-base-c071` uses `common/make-c071.mk`. A local file in a base's `src/`
replaces the `common/src/` file of the same name (`VPATH` lists `./src` first).
Each target's `project.mk` lists exactly what it compiles; see also
[BUILD_SOURCES.md](BUILD_SOURCES.md).

Build any of them with `cmake --build <build-dir> --target <base-target>`.

### `bittag-base-jlcpcb-v3`

For BitTag Base v6 boards (`boards/bittag-base-jlcpcb-v3/Note.txt`).
STM32F042 in a 32-pin LQFP package. Its board still uses the static
`generate_board_files()` path. The previous board revision,
`bittag-base-jlcpcb-v2` (Tagbase v5c, 28-pin PLCC package), has a board
directory but no base target and is not configured.

### `bittag-base-v7`

BitTag Base v7 on an STM32F042x6. SYSCLK is 48 MHz from the PLL fed by HSI/2
(8 MHz / 2 x 12); USB is clocked from HSI48.

### `tag-breakout-base-jlcpcb32-v1`

Tag Breakout Base JLCPCB32 v1 on an STM32F042. Its board still uses the static
`generate_board_files()` path.

### `tag-breakout-base-l432-v1`

An STM32F042x6 SWD bridge, already at the F042's 48 MHz maximum
SYSCLK/HCLK (PLL from HSI/2, USB from HSI48). Its `project.mk` adds
`-DUSEEPRINTF` to `CXXFLAGS`, which no rule in the ChibiOS make scaffold reads,
so it has no effect on these C sources. Its local `src/ll_swd.c` is the
optimized bitbang: lower-level GPIO helpers in place of PAL line calls, cached
direction changes, and fixed-width unrolled shift helpers. Keep that file at the
target's normal optimization level; `-O3` was slower and larger
([investigation](design/investigations/2026-06-swd-bitbang-optimization.md)).
The F042's SRAM is essentially full. The directory also holds an
`src/ll_swd_spi.c` that is not compiled.

### `tag-breakout-base-l432-u375-1v8`

STLink-compatible breakout base for an STM32U375 target with 1.8 V analog
buffer enable and explicit SWDIO direction control.

The STM32L432 base MCU runs from an 80 MHz HSI16-derived PLL. USB uses HSI48
with CRS sync. `EN1V8` is a static high board output, `TGT_RESET` is
active-high, and SWD drives `SWDIO_DIR` high for transmit and low for receive.

SWD is hybrid: `SW_ShiftOutBytes()` sends byte-aligned phases through SPI1, and
every other phase -- the odd-width shift-ins above all -- is GPIO bitbang.
`SWD_DELAY_COUNT` and `SWD_SPI_BR` in `project.mk` set the bitbang delay and the
SPI baud-rate divider. Local `src/usbcfg.c` replaces the common one; the local
`src/ll_swd.c` is not compiled.

`LED_GREEN` is PA3 routed to TIM2 channel 4 (`AF1`). The LED flashes only while
`stlink_open` is true. TIM2 drives a 1 kHz PWM brightness carrier, while a
ChibiOS virtual timer separately gates the channel for the visible blink. Tune
apparent brightness with `LED_GREEN_PWM_DUTY_PERCENT`; tune the visible blink
envelope with `LED_GREEN_BLINK_ON_MS` and `LED_GREEN_BLINK_OFF_MS`. This target
uses periodic SysTick timing (`CH_CFG_ST_TIMEDELTA == 0`) so TIM2 is available
to the PWM driver on STM32L432.

### `tag-base-c071`

An STM32C071 on an ST Nucleo64-C071RB development board. It links with the
ChibiOS `STM32C071xB.ld` from `$(STARTUPLD)` (`common/make-c071.mk`); the
`STM32C071xB.ld` in the target directory is not used. `project.mk` maps the tag SWD lines onto SPI pins
(`LINE_TAG_SWCLK=LINE_SPI1_SCK`, `LINE_TAG_SWDIO=LINE_SPI1_MOSI`,
`LINE_TAG_SWDIO_IN=LINE_SPI2_MOSI`). Its `src/ll_swd_spi.c` was the reference for
the hybrid approach: SPI handles `SW_ShiftOutBytes()`, and reads switch back to
GPIO bitbang. `src/stlink-local.c` is present but not compiled.

Open work on the bases is in [TODO.md](TODO.md).

## Maintenance Notes

- Update this README when adding, removing, or renaming a base target.
- Update `BUILD_SOURCES.md` when CMake source lists or base build membership
  changes.
- Keep board pin and signal changes in `embedded/boards`; keep base USB, SWD,
  programming, and command behavior here.
