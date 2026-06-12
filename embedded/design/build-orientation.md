# Embedded Source Layout

The embedded tree is organized around four major sections:

- `bases`: firmware images for programmer/base boards.
- `boards`: generated ChibiOS board descriptions for physical hardware.
- `proto-c`: nanopb C bindings and default configuration data for tag protocols.
- `tags`: firmware images that run on the tags themselves.

At a high level, `boards` describes hardware pins and signals, `proto-c`
describes the compact protocol structures used by tag firmware, and `bases` and
`tags` consume those descriptions to build firmware images.

## Directory Relationship

```text
embedded/
       boards/      Hardware signal and pin definitions
          |       Generates board.h, board.c, and board.mk
          |
       bases/    Programmer/base-board firmware targets
          |
        tags/     Tag firmware targets
          |
     proto-c/     nanopb protocol bindings and default tag configuration
          |
          +-----> various tags/     Tag firmware communication structures
```

The CMake build wires these relationships together. A firmware target usually
depends on one generated board target and, for tag firmware, one protocol target.
For example:

```cmake
add_embedded_target(BitTag bittag_proto)
add_dependencies(BitTag board-bittag-v6)
```

This means the `BitTag` firmware uses the `bittag_proto` nanopb target from
`proto-c` and the `board-bittag-v6` generated board files from `boards`.

## `boards`

`boards` contains the physical board descriptions used by both tag and base
firmware. The current preferred workflow is generated from a ChibiOS processor
template plus a small project JSON file:

```text
embedded/boards/<board>/
  CMakeLists.txt
  cfg/board-customizations.json
```

The board `CMakeLists.txt` calls `generate_configured_board_files()` from
`embedded/boards/CMakeLists.txt`:

```cmake
generate_configured_board_files(board-bitprestag
  PROCESSOR stm32l4xx
  BOARD_TYPE BitPresTagv1
  CUSTOMIZATIONS cfg/board-customizations.json)
```

The arguments define the board contract:

- `PROCESSOR`: ChibiOS processor-template family. For example, `stm32l4xx`
  uses the ChibiOS `stm32l4board.xml` template and the matching STM32L4
  FreeMarker board templates.
- `BOARD_TYPE`: output board directory name in the CMake build tree and the
  name firmware makefiles include as `$(BOARDDIR)/<BOARD_TYPE>/board.mk`.
  The JSON `board_id` should normally match this value because ChibiOS'
  stock `board.mk.ftl` emits `BOARDSRC` and `BOARDINC` from `board_id`.
- `CUSTOMIZATIONS`: the board-specific JSON file relative to the board source
  directory.

The generated-board build has two steps:

1. `embedded/boards/tools/generate_board_chcfg.py` starts from the ChibiOS
   processor XML template and applies `board-customizations.json` to board
   metadata, clocks, optional XML elements, and GPIO pin attributes.
2. CMake runs `fmpp` with the generated `board.chcfg` and ChibiOS'
   processor-specific `board.c.ftl`, `board.h.ftl`, and `board.mk.ftl`
   templates.

The outputs are written under the build tree, not the source tree:

```text
<build>/embedded/boards/<BOARD_TYPE>/board.h
<build>/embedded/boards/<BOARD_TYPE>/board.c
<build>/embedded/boards/<BOARD_TYPE>/board.mk
```

`board.h` defines the board-level `LINE_xxx` names consumed by tag and base
code. Keep physical signal names in the board customizations whenever possible;
avoid creating tag-level aliases in `custom.h` just to compensate for a board
name mismatch. For example, BitPresTag uses the board-generated `LINE_LPS_CS`
and `LINE_WKUP1` names directly in its family code.

`board.c` contains the low-level board initialization code, and `board.mk` is
included by firmware `project.mk` files:

```make
include $(BOARDDIR)/BitPresTagv1/board.mk
```

The firmware CMake target also declares an explicit dependency on the generated
board target:

```cmake
add_embedded_target(BitPresTag bitprestag_proto)
add_dependencies(BitPresTag board-bitprestag)
```

This dependency ensures the board files exist before the ChibiOS make-based
firmware build starts. `common/make.mk` receives `BOARDDIR` from CMake and uses
the board make fragment selected by `project.mk`.

Some older board directories still use the static-board helper
`generate_board_files()`. Those boards keep `cfg/board.chcfg`, `cfg/board.fmpp`,
and local FreeMarker templates in the source tree:

```cmake
generate_board_files(board-older-board)
```

That path still renders `board.h`, `board.c`, and `board.mk` into the build
tree, but new boards should prefer `generate_configured_board_files()` so the
repository stores only project-specific customizations while ChibiOS owns the
processor XML and template defaults.

Board directories are shared resources. They do not normally define a complete
application on their own; instead, a tag or base firmware target declares which
board target it depends on.

## `proto-c`

`proto-c` builds the nanopb C view of the shared protocol definitions. It uses
the top-level `.proto` files from `proto`, combines shared nanopb options with
tag-specific overrides, generates `.pb.c` and `.pb.h` files in the build tree,
and converts tag-specific default configuration JSON into C data.

Each protocol subdirectory provides:

- `default-config.json`: the default protobuf configuration message for that
  tag family, written as JSON.
- `tag.override.options`: nanopb overrides for `tag.proto`.
- `tagdata.override.options`: nanopb overrides for `tagdata.proto`.
- `CMakeLists.txt`: a call to `add_nanopb_target(<name>)`.

The resulting protocol target is an interface library such as `bittag_proto` or
`prestag_proto`. Tag firmware links to one of these targets so it can encode and
decode compact messages for host communication and stored tag configuration.

`default-config.json` is intentionally tag-specific. The shared protobuf
configuration message can describe features used by many tag families, but a
particular firmware image should only initialize the portions that apply to that
tag. The `config-gen` helper converts this JSON file into `default_config.c`,
which is compiled into the firmware through the generated protocol target.

Nanopb options also have two layers:

- `proto-c/default-options/*.options`: shared defaults for the whole embedded
  C protocol layer.
- `proto-c/<tag>-proto-c/*.override.options`: tag-specific overrides layered on
  top of those defaults.

At build time, CMake combines the shared default options and the tag-specific
override file into the final `.options` files used by `nanopb_generator`. This
lets most protobuf fields share one memory/layout policy while still allowing a
tag family to tighten array sizes, string sizes, field allocation, or other
nanopb details where the default would be too broad.

## `tags`

`tags` contains the firmware applications that run on tag hardware. A tag target
combines:

- tag-specific application code, usually under `inc`, `src`, and `cfg`;
- shared tag support code from `tags/common`;
- one board target from `boards`;
- one nanopb protocol target from `proto-c`;
- ChibiOS sources from the repository `ChibiOS` submodule.

The target's `CMakeLists.txt` declares both sides of that relationship. A
typical tag uses:

```cmake
add_embedded_target(PresTag prestag_proto)
add_dependencies(PresTag board-prestag)
```

Generated build products are placed in the CMake build tree, not in the source
tree. The important final artifacts are the `.elf`, `.hex`, `.bin`, `.dmp`, and
`.list` files under the tag target's build directory.

## `bases`

`bases` contains firmware applications for base or programmer boards. These
targets use the same board-generation mechanism as tags, but they generally do
not use a nanopb protocol target because they are not tag data-log firmware.

A base target typically looks like:

```cmake
add_embedded_target(bittag-base-v7 0)
add_dependencies(bittag-base-v7 board-bittag-base-v7)
```

The `0` protocol argument means the firmware target does not consume generated
nanopb sources.

## Adding or Updating Firmware

When adding a new embedded target, decide which layer is changing:

- Add or update a `boards` entry when the physical pinout, signal names, or
  ChibiOS board configuration changes. For new boards, add a
  `cfg/board-customizations.json` file and call
  `generate_configured_board_files()` from the board `CMakeLists.txt`.
- Add or update a `proto-c` entry when the tag family needs a new nanopb option
  set or default configuration.
- Add or update a `tags` entry when creating tag firmware for a tag family or
  sensor combination.
- Add or update a `bases` entry when creating firmware for a programmer or base
  board.

Most bugs should be fixed at the lowest layer that owns the behavior. Pin and
signal mistakes belong in `boards`; protocol encoding and default configuration
belong in `proto-c`; sensor behavior and runtime state belong in `tags`; base
board USB/SWD/programmer behavior belongs in `bases`.
