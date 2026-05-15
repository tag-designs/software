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
       +-----> bases/    Programmer/base-board firmware targets
       |
       +-----> tags/     Tag firmware targets

  proto-c/     nanopb protocol bindings and default tag configuration
       |
       +-----> tags/     Tag firmware communication structures
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
firmware. Each board subdirectory is centered around `cfg/board.chcfg`,
`cfg/board.fmpp`, and FreeMarker templates such as `board.h.ftl`,
`board.c.ftl`, and `board.mk.ftl`.

During the CMake build, `generate_board_files()` runs `fmpp` with ChibiOS
templates to produce:

- `board.h`: logical signal names, GPIO lines, and board-level constants.
- `board.c`: low-level board initialization code.
- `board.mk`: makefile fragments consumed by the ChibiOS firmware build.

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
  ChibiOS board configuration changes.
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
