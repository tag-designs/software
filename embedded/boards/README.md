---
type: readme
status: current
summary: How board directories generate ChibiOS board files, which firmware targets consume each board, and how to rename or check boards.
---

# Embedded Boards

`embedded/boards` contains the ChibiOS board descriptions shared by tag and
base firmware. A board directory owns the physical hardware contract: pin
names, GPIO modes, board clocks, generated `LINE_xxx` names, and the generated
`board.h`, `board.c`, and `board.mk` files consumed by firmware builds.

Most new board directories should use the JSON-customized board flow:

```cmake
generate_configured_board_files(board-example
  PROCESSOR stm32l4xx
  BOARD_TYPE ExampleBoard
  CUSTOMIZATIONS cfg/board-customizations.json)
```

Older board directories still use `generate_board_files()` with a source-tree
`cfg/board.chcfg`, `cfg/board.fmpp`, and local FreeMarker templates. Both paths
write generated board files into the CMake build tree. Boards used by a
distributed tag also commit their generated files in `<board>/generated/` and
regenerate them only when a recorded input changes (see
[Tag Firmware Build Reproducibility](../../docs/build/firmware-reproducibility.md));
otherwise the committed files are copied to
`<build>/embedded/boards/<BOARD_TYPE>/`, so firmware always reads the board
through `BOARDDIR` in the build tree.

`generate_configured_board_files()` gets ChibiOS templates from the CMake
`CHIBIOS_DIR` value. An explicit `-DCHIBIOS_DIR=/path/to/ChibiOS` wins; when
it is not set, `embedded/CMakeLists.txt` uses the repository `ChibiOS/`
submodule, then `$CHIBIOS_DIR` from the environment. See
[`tools/README.md`](tools/README.md) for the full lookup and generation flow.

## Active Board Consumers

These are the board targets used by active tag and base firmware targets.

"Committed" marks the boards behind a distributed tag, whose generated files are
in `<board>/generated/`.

| Board directory | Board target | Generated board include | Generator | Firmware consumers |
| --- | --- | --- | --- | --- |
| `BitPresTagv1` | `board-bitprestag` | `BitPresTagv1/board.mk` | `generate_configured_board_files()` | Tags: `BitPresTag`, `BitPresTagMX25R` |
| `BitTagNG` | `board_bittagng` | `BitTagNG/board.mk` | `generate_configured_board_files()` | Tags: `BitTagNG` |
| `BitTagv6` | `board-bittag-v6` | `BitTagv6/board.mk` | `generate_configured_board_files()`, committed | Tags: `BitTag`, `BitTag-legacy` |
| `CompassTagv1` | `board-compasstag` | `CompassTagv1/board.mk` | `generate_configured_board_files()`, committed | Tags: `CompassTag`, `CompassTagAT25`, `CompassTagAT25Breakout` |
| `IMUTagNandv1` | `board-imutag-nand-v1` | `IMUTagNandv1/board.mk` | `generate_configured_board_files()` | Tags: `IMUTagNand` |
| `IMUTagNandv2` | `board-imutag-nand-v2` | `IMUTagNandv2/board.mk` | `generate_configured_board_files()`, committed | Tags: `IMUTagNandBmp581` |
| `PresTagv3` | `board-prestag` | `PresTagv3/board.mk` | `generate_configured_board_files()`, committed | Tags: `PresTag`, `PresTagRaw` |
| `Stop1Test` | `board-stop1test` | none: `project.mk` adds `$(BOARDDIR)/Stop1Test` to `ALLINC` and compiles its `board.c` | `generate_configured_board_files()` | Tags: `stop1test` |
| `UIUCTag` | `board-uiuctag` | `UIUCTag/board.mk` | `generate_configured_board_files()`, committed | Tags: `UIUCTag` |
| `bittag-base-jlcpcb-v3` | `board-bittag-base-jlcpcb-v3` | `bittag-base-jlcpcb-v3/board.mk` | `generate_board_files()` | Bases: `bittag-base-jlcpcb-v3` |
| `bittag-base-v7` | `board-bittag-base-v7` | `BITTAG_BASE_V7/board.mk` | `generate_configured_board_files()` | Bases: `bittag-base-v7` |
| `tag-base-c071-v1` | `board-tag-base-c071v1` | `ST_NUCLEO64_C071RB/board.mk` | `generate_configured_board_files()` | Bases: `tag-base-c071` |
| `tag-breakout-base-jlcpcb32-v1` | `board-tag-breakout-base-jlcpcb32-v1` | `tag-breakout-base-jlcpcb32-v1/board.mk` | `generate_board_files()` | Bases: `tag-breakout-base-jlcpcb32-v1` |
| `tag-breakout-base-l432-v1` | `board-tag-breakout-base-l432-v1` | `Tag_Breakout_Base_L432_V1/board.mk` | `generate_configured_board_files()` | Bases: `tag-breakout-base-l432-v1` |
| `tag-breakout-base-l432-u375-1v8` | `board-tag-breakout-base-l432-u375-1v8` | `Tag_Breakout_Base_L432_U375_1V8/board.mk` | `generate_configured_board_files()` | Bases: `tag-breakout-base-l432-u375-1v8` |

The firmware side declares the dependency in its `CMakeLists.txt` and includes
the generated board fragment in `project.mk`:

```cmake
add_embedded_target(BitPresTag bitprestag_proto)
add_dependencies(BitPresTag board-bitprestag)
```

```make
include $(BOARDDIR)/BitPresTagv1/board.mk
```

## Board Inventory Without Active Consumers

These board directories are present in the active source tree but are not
currently used by an active tag or base target in `embedded/tags/CMakeLists.txt`
or `embedded/bases/CMakeLists.txt`.

| Board directory | Board target | Generator | Notes |
| --- | --- | --- | --- |
| `IMUTagU375` | `board-imutag-u375` | `generate_configured_board_files()` | STM32U375 IMUTag board description; built, but no active tag includes it. |
| `IMUTagv1` | `board-imutag-breakout` | `generate_configured_board_files()` | Used only by archived IMUTag variants. |
| `TagSteval` | `board-steval` | `generate_board_files()` | Used by archived or prototype STEVAL-based tag firmware. |
| `bittag-base-jlcpcb-v2` | `board-bittag-base-jlcpcb-v2` | `generate_board_files()` | Older BitTag base board (Tagbase v5c, 28-pin PLCC package); its `add_subdirectory` is commented out, so it is not configured. |

`archive/` contains retired or reference board descriptions and is not part of
the normal active firmware build.

## Board-Local Notes

- `IMUTagNandv1/standby-pins.md` documents the STM32U375 Standby pull-up and
  pull-down choices for the IMUTagNand sensor, flash, interrupt, and test
  pins.
- `IMUTagNandv2/standby-pins.md` documents the matching standby biases for the
  BMP581/GD5F2GM7RE replacement breakout. It also records why code that runs without the
  firmware, such as an SRAM loader, must drive `FLASH_PWR` (PA8) high itself.

## Updating Board Names

Board files should expose the physical signal names that firmware code uses.
When a tag or base needs a `LINE_xxx` name, prefer fixing the corresponding
board customization or board template input instead of adding aliases in a
tag-local `custom.h`.

For generated-configured boards, update `cfg/board-customizations.json`. For a
board that does not commit its files, the next build of the board target
regenerates the files below. For a committed board, whether to regenerate is
decided at configure time and editing the JSON does not trigger a reconfigure:
reconfigure (or configure with `-DREGENERATE_SOURCES=ON`), and the build then
regenerates and rewrites `<board>/generated/`, which you then commit:

```text
<build>/embedded/boards/<BOARD_TYPE>/board.h
<build>/embedded/boards/<BOARD_TYPE>/board.c
<build>/embedded/boards/<BOARD_TYPE>/board.mk
```

For static boards, update the source-tree `cfg/board.chcfg` and templates.

## Build Checks

Use the firmware target that consumes the board for focused verification:

```sh
cmake --build <build-dir> --target BitPresTag
cmake --build <build-dir> --target bittag-base-v7
```

You can also build a board target directly when checking generation only:

```sh
cmake --build <build-dir> --target board-bitprestag
```

Generated-configured board targets run the Python customization tool, then
run `fmpp` against the matching ChibiOS templates. The resulting files land in
`<build-dir>/embedded/boards/<BOARD_TYPE>/`; firmware targets consume them via
the `$(BOARDDIR)/<BOARD_TYPE>/board.mk` include in `project.mk`.
