# AGENTS.md

This file is for agents working under `embedded/`. It summarizes firmware layer
ownership, ChibiOS usage, and build/documentation expectations.

## Directory Ownership

- `boards/`: physical hardware descriptions, pin/signal names, and generated
  ChibiOS board files.
- `bases/`: firmware for base/programmer boards.
- `proto-c/`: nanopb C bindings, nanopb options, and tag default configuration
  data generated from JSON.
- `tags/`: tag firmware applications.
- `tags/common/`: shared tag firmware support code and drivers.
- `tags/common/modules/`: ChibiOS makefile fragments that group shared tag
  sources into named modules used by each tag's `project.mk`.
- `design/`: developer-facing architecture and layout documentation.
- Any `archive/` directory contains retired or reference firmware. Ignore it
  for normal searches, refactors, builds, and reviews unless the task
  explicitly calls it out.

## Layer Relationships

Firmware targets usually combine:

- one board target from `boards/`;
- for tags, one nanopb protocol target from `proto-c/`;
- tag/base application code;
- shared code from `tags/common` where appropriate;
- ChibiOS sources from the repository `ChibiOS` submodule.

Fix bugs at the layer that owns them:

- pin names, GPIO lines, and board constants: `boards/`;
- default protobuf configuration and nanopb sizing/allocation: `proto-c/`;
- runtime tag behavior and sensor logging: `tags/`;
- base/programmer USB/SWD/programming behavior: `bases/`;
- shared tag drivers/helpers: `tags/common/`.

## Tag Build Manifests

Each active tag target has a `project.mk` that should separate shared modules
from tag-local application files:

```make
TAG_MODULES += \
       protocol_nanopb \
       tag_core \
       monitor \
       rtc_rv3028

include ../common/modules/modules.mk

ALLCSRC += \
       config.c \
       datalog.c \
       state_run.c
```

Prefer adding shared sources to a narrow module under `tags/common/modules/`
instead of adding many individual common filenames to each tag. Keep genuinely
tag-specific code listed directly in the tag's `project.mk`.

## ChibiOS

This repository uses the top-level `ChibiOS` submodule. Do not assume a
system-installed ChibiOS tree, and do not edit `ChibiOS/` as project source.
If the submodule is missing, initialize it rather than working around it:

```sh
git submodule update --init --recursive
```

## proto-c Notes

Each tag protocol directory typically contains:

- `default-config.json`: tag-specific default protobuf configuration.
- `tag.override.options`: nanopb overrides for `tag.proto`.
- `tagdata.override.options`: nanopb overrides for `tagdata.proto`.
- `CMakeLists.txt`: `add_nanopb_target(<name>)`.

Shared nanopb defaults live in `proto-c/default-options/`. Tag-specific
override files narrow those defaults for a particular firmware image.

Changing `proto/tag.proto` or `proto/tagdata.proto` can affect host code,
embedded nanopb generation, default config JSON, and stored/logged data. Scope
those changes deliberately.

## Build And Source Lists

Prefer building the affected target rather than all firmware:

```sh
cmake --build <build-dir> --target PresTag
cmake --build <build-dir> --target BitTag
cmake --build <build-dir> --target CompassTagAT25Breakout
cmake --build <build-dir> --target bittag-base-v7
```

If CMake source lists change, update:

- `tags/BUILD_SOURCES.md` for tag targets.
- `bases/BUILD_SOURCES.md` for base targets.

Generated board files, nanopb output, and firmware artifacts belong in the
CMake build tree, not in source directories.

## Documentation

- Keep `embedded/design/build-orientation.md` aligned with major layout or ownership
  changes.
- Keep target-specific design notes near the target under a `design/` subfolder, for example
  `tags/<target>/design/notes.md`.
