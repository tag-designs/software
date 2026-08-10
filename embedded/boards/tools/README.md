# Board configuration tools

These tools support a generated-board workflow for ChibiOS boards. Instead of
copying `board.chcfg`, `board.c.ftl`, `board.h.ftl`, `board.mk.ftl`, and
`board.fmpp` into every board directory, a board can provide:

- one JSON file containing board-specific XML and pin customizations;
- the ChibiOS processor template family, such as `stm32l4xx`;
- the generated board directory name used by firmware makefiles.

`generate_board_chcfg.py` patches a ChibiOS board XML template with the JSON
customizations and writes `board.chcfg`. It can also emit `board_standby.h`,
which contains project-level STM32 standby pull masks for firmware that wants
them. The script imports only the Python standard library and does not depend
on CMake or on a particular source tree layout.

The Python script does not produce `board.c`, `board.h`, or `board.mk` by
itself. Run `fmpp` afterwards against ChibiOS' matching board templates to
generate the complete board directory.

## Input Files

The standalone workflow has one project-owned input and two ChibiOS inputs.

### Customization JSON

The project-owned input is the JSON customization file passed with
`--customizations`. It describes the board identity, ChibiOS XML edits, and pin
attribute changes.

When generating from a stock ChibiOS processor template, the JSON should
normally set:

- `board_id` to the generated board directory name used by firmware makefiles;
- `elements.configuration_settings/board_files_path` to `$(BOARDDIR)`, so the
  generated `board.mk` stays relocatable;
- processor-specific metadata required by the ChibiOS XML template, such as
  `subtype` and clock attributes;
- `pins`, either as a compact location map, grouped-by-port map, or list.

`board_name` is human-readable metadata. `board_id` is also used by ChibiOS'
stock `board.mk.ftl` when emitting `BOARDSRC` and `BOARDINC`, so it should
match the output `BOARD_TYPE` directory unless the consuming firmware makefiles
intentionally use a different layout.

A compact JSON file can name pins by location:

```json
{
  "board_name": "My Board",
  "board_id": "MyBoard",
  "pins": {
    "PA5": {
      "ID": "ACCEL_SCK",
      "Mode": "Alternate",
      "Alternate": "5",
      "Standby": "PULLDOWN"
    },
    "GPIOB.pin6": {
      "ID": "RTC_SDA",
      "Type": "OpenDrain",
      "Resistor": "PullUp",
      "Mode": "Alternate",
      "Alternate": "4"
    }
  }
}
```

When starting from a stock ChibiOS processor XML file, the same JSON can also
customize non-pin XML:

```json
{
  "board_name": "My Board",
  "board_id": "MyBoard",
  "remove_elements": ["ethernet_phy"],
  "elements": {
    "subtype": "STM32L432xx",
    "configuration_settings/board_files_path": "$(BOARDDIR)"
  },
  "attributes": {
    "clocks": {
      "HSEFrequency": "0",
      "LSEFrequency": "32768"
    }
  },
  "pins": {
    "PA5": {
      "ID": "ACCEL_SCK",
      "Mode": "Alternate",
      "Alternate": "5"
    }
  }
}
```

Pins can also be grouped by port:

```json
{
  "pins": {
    "GPIOA": {
      "pin5": {
        "ID": "ACCEL_SCK",
        "Mode": "Alternate",
        "Alternate": "5"
      }
    }
  }
}
```

A list form is accepted for generated data:

```json
{
  "pins": [
    {
      "port": "GPIOA",
      "pin": 5,
      "ID": "ACCEL_SCK",
      "Mode": "Alternate",
      "Alternate": "5"
    }
  ]
}
```

Pin attributes are matched case-insensitively against the attributes already
present on the template pin, so misspelled fields fail instead of silently
creating invalid configuration. Use `--allow-new-attributes` only when a new
ChibiOS board schema attribute is intentionally being introduced.

`Standby` is project metadata, not a ChibiOS XML attribute. It may be placed in
a pin definition after `PinLock` and accepts `FLOAT`, `PULLUP`, or `PULLDOWN`;
omitting it is the same as `FLOAT`. The generator strips `Standby` from
`board.chcfg` and uses it only for `board_standby.h`. Pins with non-floating
standby state must have a non-empty `ID`, because the generated masks use the
human-readable `LINE_<ID>` names from `board.h`.

The script validates requested XML paths and pin names with ElementTree, then
patches the original XML text. This keeps generated `board.chcfg` files close
to the source template formatting, which makes generated-output diffs easier to
review.

### ChibiOS Templates

The first ChibiOS input is the processor-family XML template passed to
`generate_board_chcfg.py --template`. The second is the matching board
FreeMarker template directory passed to `fmpp` as `sourceRoot`. Their names are
related but not identical:

| Processor family | XML template | FTL template directory |
| --- | --- | --- |
| `stm32l4xx` | `tools/ftl/xml/stm32l4board.xml` | `tools/ftl/processors/boards/stm32l4xx/templates` |
| `stm32u3xx` | `tools/ftl/xml/stm32u3board.xml` | `tools/ftl/processors/boards/stm32u3xx/templates` |
| `stm32c0xx` | `tools/ftl/xml/stm32c0board.xml` | `tools/ftl/processors/boards/stm32c0xx/templates` |
| `stm32f0xx` | `tools/ftl/xml/stm32f0board.xml` | `tools/ftl/processors/boards/stm32f0xx/templates` |

## Standalone Workflow

Copy `generate_board_chcfg.py` into any project that can provide the inputs
above. `board.fmpp.in` is optional, but it shows the minimal `fmpp` config
shape needed to turn the generated `board.chcfg` into `board.c`, `board.h`, and
`board.mk`.

The workflow has two phases:

1. generate the `fmpp` inputs: `board.chcfg`, optional `board_standby.h`, and a
   small `board.fmpp` config;
2. run `fmpp` to generate the board directory consumed by firmware makefiles.

### Generate `fmpp` Inputs

Example using generic paths:

```sh
CHIBIOS_DIR=/path/to/ChibiOS
BOARD_JSON=/path/to/MyBoard/board-customizations.json
OUT_DIR=/path/to/generated-boards
BOARD_TYPE=MyBoard
PROCESSOR=stm32l4xx
XML_TEMPLATE=$CHIBIOS_DIR/tools/ftl/xml/stm32l4board.xml
FTL_TEMPLATE_DIR=$CHIBIOS_DIR/tools/ftl/processors/boards/$PROCESSOR/templates

mkdir -p "$OUT_DIR/cfg" "$OUT_DIR/$BOARD_TYPE"

python3 /path/to/generate_board_chcfg.py \
  --template "$XML_TEMPLATE" \
  --customizations "$BOARD_JSON" \
  --output "$OUT_DIR/cfg/board.chcfg" \
  --standby-header "$OUT_DIR/$BOARD_TYPE/board_standby.h"
```

Then create an `fmpp` configuration that points at the ChibiOS board templates,
the generated `board.chcfg`, and the output board directory:

```sh
cat > "$OUT_DIR/cfg/board.fmpp" <<EOF
sourceRoot: $FTL_TEMPLATE_DIR
outputRoot: $OUT_DIR/$BOARD_TYPE
dataRoot: $OUT_DIR/cfg

data : {
  doc1:xml (
    board.chcfg
    {
    }
  )
}
EOF
```

### Run `fmpp`

Run `fmpp` with ChibiOS' shared FreeMarker library path:

```sh
fmpp --freemarker-links "{lib : $CHIBIOS_DIR/tools/ftl/libs}" \
  -C "$OUT_DIR/cfg/board.fmpp" \
  -v \
  -O "$OUT_DIR/$BOARD_TYPE"
```

The result is a complete generated board directory:

```text
/path/to/generated-boards/MyBoard/board.c
/path/to/generated-boards/MyBoard/board.h
/path/to/generated-boards/MyBoard/board.mk
/path/to/generated-boards/MyBoard/board_standby.h
```

The generated `board.mk` normally expects firmware builds to set `BOARDDIR` to
the parent of `BOARD_TYPE`; for the example above that parent is
`/path/to/generated-boards`.

Firmware makefiles consume that directory through `BOARDDIR`. For example, a
firmware `project.mk` containing `include $(BOARDDIR)/MyBoard/board.mk` can be
built manually only if the surrounding firmware make scaffold receives the
variables it expects. Typical ChibiOS projects need at least `BOARDDIR` and
`CHIBIOS`; larger projects may also require build, dependency, project-name, or
generated-code paths such as `BUILDDIR`, `DEPDIR`, `PROJECT`, `SOURCEDIR`, and
protobuf or nanopb include directories.

## CMake Integration Pattern

A CMake project can wrap the standalone tool with a custom target. The wrapper
needs to derive the ChibiOS XML template, configure an `fmpp` file, and run the
two generation commands.

The example below uses `BOARD_TOOLS_DIR` for the directory containing
`generate_board_chcfg.py` and `board.fmpp.in`. It assumes the helper is called
from a board source directory containing `cfg/board-customizations.json`.

```cmake
set(BOARD_TOOLS_DIR
    ""
    CACHE PATH "Directory containing generate_board_chcfg.py and board.fmpp.in")
set(CHIBIOS_DIR "" CACHE PATH "ChibiOS source directory")

find_program(PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
find_program(FMPP_EXECUTABLE NAMES fmpp fmpp.bat REQUIRED)

if(NOT EXISTS "${CHIBIOS_DIR}/os")
  message(FATAL_ERROR "Set CHIBIOS_DIR to a ChibiOS source tree")
endif()
if(NOT EXISTS "${BOARD_TOOLS_DIR}/generate_board_chcfg.py")
  message(FATAL_ERROR "Set BOARD_TOOLS_DIR to the board configuration tools directory")
endif()
if(NOT EXISTS "${BOARD_TOOLS_DIR}/board.fmpp.in")
  message(FATAL_ERROR "BOARD_TOOLS_DIR must contain board.fmpp.in")
endif()

function(generate_configured_board_files target_name)
  set(one_value_args PROCESSOR BOARD_TYPE CUSTOMIZATIONS OUTPUT_PARENT)
  cmake_parse_arguments(GCB "" "${one_value_args}" "" ${ARGN})

  if(NOT GCB_PROCESSOR)
    message(FATAL_ERROR "generate_configured_board_files requires PROCESSOR")
  endif()
  if(NOT GCB_BOARD_TYPE)
    message(FATAL_ERROR "generate_configured_board_files requires BOARD_TYPE")
  endif()
  if(NOT GCB_CUSTOMIZATIONS)
    message(FATAL_ERROR "generate_configured_board_files requires CUSTOMIZATIONS")
  endif()

  if(NOT GCB_OUTPUT_PARENT)
    set(GCB_OUTPUT_PARENT "${CMAKE_CURRENT_BINARY_DIR}/boards")
  endif()

  string(TOLOWER "${GCB_PROCESSOR}" processor)
  string(REGEX REPLACE "xx$" "" processor_xml "${processor}")

  set(template_xml
      "${CHIBIOS_DIR}/tools/ftl/xml/${processor_xml}board.xml")
  set(template_dir
      "${CHIBIOS_DIR}/tools/ftl/processors/boards/${processor}/templates")
  set(customizations
      "${CMAKE_CURRENT_SOURCE_DIR}/${GCB_CUSTOMIZATIONS}")

  set(generated_cfg_dir "${CMAKE_CURRENT_BINARY_DIR}/${GCB_BOARD_TYPE}-cfg")
  set(generated_chcfg "${generated_cfg_dir}/board.chcfg")
  set(generated_fmpp "${generated_cfg_dir}/board.fmpp")
  set(board_output_dir "${GCB_OUTPUT_PARENT}/${GCB_BOARD_TYPE}")
  set(standby_header "${board_output_dir}/board_standby.h")

  if(NOT EXISTS "${template_xml}")
    message(FATAL_ERROR "Missing ChibiOS board XML template: ${template_xml}")
  endif()
  if(NOT EXISTS "${template_dir}/board.c.ftl")
    message(FATAL_ERROR "Missing ChibiOS board template directory: ${template_dir}")
  endif()

  file(MAKE_DIRECTORY "${generated_cfg_dir}")

  set(BOARD_FMPP_SOURCE_ROOT "${template_dir}")
  set(BOARD_FMPP_OUTPUT_ROOT "${board_output_dir}")
  set(BOARD_FMPP_DATA_ROOT "${generated_cfg_dir}")
  configure_file(
    "${BOARD_TOOLS_DIR}/board.fmpp.in"
    "${generated_fmpp}"
    @ONLY)

  add_custom_command(
    OUTPUT "${generated_chcfg}" "${standby_header}"
    COMMAND "${PYTHON_EXECUTABLE}"
            "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
            --template "${template_xml}"
            --customizations "${customizations}"
            --output "${generated_chcfg}"
            --standby-header "${standby_header}"
    DEPENDS
      "${BOARD_TOOLS_DIR}/generate_board_chcfg.py"
      "${customizations}"
      "${template_xml}"
    VERBATIM)

  set(board_files
    "${board_output_dir}/board.c"
    "${board_output_dir}/board.h"
    "${board_output_dir}/board.mk")

  add_custom_command(
    OUTPUT ${board_files}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${board_output_dir}"
    COMMAND "${FMPP_EXECUTABLE}"
            --freemarker-links "{lib : ${CHIBIOS_DIR}/tools/ftl/libs}"
            -C "${generated_fmpp}"
            -v
            -O "${board_output_dir}"
    DEPENDS
      "${generated_chcfg}"
      "${standby_header}"
      "${generated_fmpp}"
      "${template_dir}/board.c.ftl"
      "${template_dir}/board.h.ftl"
      "${template_dir}/board.mk.ftl"
    VERBATIM)

  add_custom_target("${target_name}"
    DEPENDS ${board_files} "${standby_header}")
endfunction()
```

Use the helper from a board directory like this:

```cmake
generate_configured_board_files(board-myboard
  PROCESSOR stm32l4xx
  BOARD_TYPE MyBoard
  CUSTOMIZATIONS cfg/board-customizations.json)
```

Configure and build it with the paths for your checkout:

```sh
cmake -S . -B build \
  -DCHIBIOS_DIR=/path/to/ChibiOS \
  -DBOARD_TOOLS_DIR=/path/to/board-tools

cmake --build build --target board-myboard
```

Arguments:

- `PROCESSOR`: ChibiOS processor template family. For example, `stm32l4xx`
  uses `<ChibiOS>/tools/ftl/xml/stm32l4board.xml` and
  `<ChibiOS>/tools/ftl/processors/boards/stm32l4xx/templates`. A wrapper can
  derive the XML name by lowercasing `PROCESSOR` and dropping a trailing `xx`;
  for example `stm32u3xx` becomes `stm32u3board.xml`.
- `BOARD_TYPE`: build-tree board directory and firmware include name. Firmware
  should include `$(BOARDDIR)/<BOARD_TYPE>/board.mk`. This should align with
  the customization JSON's `board_id`, as described in Input Files.
- `CUSTOMIZATIONS`: JSON file relative to the board source directory.
- `OUTPUT_PARENT`: optional parent directory for generated board directories.
  Firmware makefiles should receive this value as `BOARDDIR`.

A generated board target should produce:

```text
<build>/boards/<BOARD_TYPE>/board.c
<build>/boards/<BOARD_TYPE>/board.h
<build>/boards/<BOARD_TYPE>/board.mk
<build>/boards/<BOARD_TYPE>/board_standby.h
```

Firmware targets should depend on the board target, set `BOARDDIR` to the
parent output directory, and include `$(BOARDDIR)/<BOARD_TYPE>/board.mk`.

For example:

```cmake
add_dependencies(MyFirmware board-myboard)
```

The firmware build command or wrapper would then pass something equivalent to:

```make
BOARDDIR=<build>/boards
```

## This Repository

This final section documents the local integration in this source tree. It can
be deleted or replaced when copying the tools into another project.

In this repository, CMake supplies the script's `--template` path from
`CHIBIOS_DIR` inside `generate_configured_board_files()` in
`embedded/boards/CMakeLists.txt`. `embedded/CMakeLists.txt` resolves
`CHIBIOS_DIR` in this order:

1. the repository `ChibiOS/` submodule, when `ChibiOS/os` exists;
2. the `CHIBIOS_DIR` environment variable, when it points at a ChibiOS source
   tree;
3. the CMake cache value passed with `-DCHIBIOS_DIR=/path/to/ChibiOS`.

The usual reproducible setup is:

```sh
git submodule update --init --recursive ChibiOS
cmake -S . -B build-embedded \
  -DBUILD_HOST=OFF \
  -DBUILD_QT_APPS=OFF \
  -DBUILD_EMBEDDED=ON
```

For an out-of-tree ChibiOS checkout, configure with:

```sh
cmake -S . -B build-embedded \
  -DBUILD_HOST=OFF \
  -DBUILD_QT_APPS=OFF \
  -DBUILD_EMBEDDED=ON \
  -DCHIBIOS_DIR=/path/to/ChibiOS
```

CMake prints the resolved path as `CHIBIOS_DIR is ...` during configuration.
The embedded configure step also requires `arm-none-eabi-gcc`, `make`, Python,
and `fmpp` on `PATH`. Because the top-level embedded configuration also adds
`embedded/proto-c`, configure can require Protobuf and nanopb even when the
target you intend to build is only a board-generation target.

The configure command above assumes only CMake cache variables. It does not
require users to predefine Make variables such as `CHIBIOS`, `BOARDDIR`,
`BUILDDIR`, or `PROJECT`; `add_embedded_target()` supplies those later when a
tag or base firmware target invokes `make`.

Build a generated board directly when checking the board files only:

```sh
cmake --build build-embedded --target board-bitprestag
```

That produces:

```text
build-embedded/embedded/boards/BitPresTagv1/board.c
build-embedded/embedded/boards/BitPresTagv1/board.h
build-embedded/embedded/boards/BitPresTagv1/board.mk
build-embedded/embedded/boards/BitPresTagv1/board_standby.h
```

To build firmware that consumes the generated board, build the tag or base
target instead:

```sh
cmake --build build-embedded --target BitPresTag
```

The firmware target's `CMakeLists.txt` should depend on the board target, for
example `add_dependencies(BitPresTag board-bitprestag)`, and its `project.mk`
should include the generated fragment:

```make
include $(BOARDDIR)/BitPresTagv1/board.mk
```
