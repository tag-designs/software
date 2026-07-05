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
write generated board files into the CMake build tree.

## Active Board Consumers

These are the board targets used by active tag and base firmware targets.

| Board directory | Board target | Generated board include | Generator | Firmware consumers |
| --- | --- | --- | --- | --- |
| `BitPresTagv1` | `board-bitprestag` | `BitPresTagv1/board.mk` | `generate_configured_board_files()` | Tags: `BitPresTag`, `BitPresTagMX25R` |
| `BitTagv6` | `board-bittag-v6` | `BitTagv6/board.mk` | `generate_configured_board_files()` | Tags: `BitTag`, `BitTag-legacy` |
| `CompassTagv1` | `board-compasstag` | `CompassTagv1/board.mk` | `generate_configured_board_files()` | Tags: `CompassTag`, `CompassTagAT25`, `CompassTagAT25Breakout` |
| `IMUTagv1` | `board-imutag-breakout` | `IMUTagv1/board.mk` | `generate_configured_board_files()` | Tags: `IMUTagBreakout` |
| `IMUTagU375` | `board-imutag-u375` | `IMUTagU375/board.mk` | `generate_configured_board_files()` | Tags: `IMUTagU375` |
| `PresTagv3` | `board-prestag` | `PresTagv3/board.mk` | `generate_configured_board_files()` | Tags: `PresTag` |
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
| `BitTagNG` | `board_bittagng` | `generate_board_files()` | Historical/experimental BitTagNG board description. |
| `TagSteval` | `board-steval` | `generate_board_files()` | Used by archived or prototype STEVAL-based tag firmware. |
| `bittag-base-jlcpcb-v2` | `board-bittag-base-jlcpcb-v2` | `generate_board_files()` | Older BitTag base board generation path. |

`archive/` contains retired or reference board descriptions and is not part of
the normal active firmware build.

## Updating Board Names

Board files should expose the physical signal names that firmware code uses.
When a tag or base needs a `LINE_xxx` name, prefer fixing the corresponding
board customization or board template input instead of adding aliases in a
tag-local `custom.h`.

For generated-configured boards, update `cfg/board-customizations.json`. The
next build of the board target regenerates:

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
