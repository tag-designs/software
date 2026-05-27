# Board configuration tools

These tools support a generated-board workflow for ChibiOS boards. Instead of
copying `board.chcfg`, `board.c.ftl`, `board.h.ftl`, `board.mk.ftl`, and
`board.fmpp` into every board directory, a board can provide:

- the ChibiOS processor template family, such as `stm32l4xx`;
- the generated board directory name used by firmware makefiles;
- one JSON file containing board-specific XML and pin customizations.

CMake then generates `board.chcfg` in the build tree and runs `fmpp` against
the matching ChibiOS templates to produce `board.c`, `board.h`, and `board.mk`.
It also emits `board_standby.h`, which contains the board-level STM32 standby
pull masks used by tag firmware.

## CMake helper

Use `generate_configured_board_files()` from `embedded/boards/CMakeLists.txt`
for generated boards:

```cmake
generate_configured_board_files(board-bitprestag
  PROCESSOR stm32l4xx
  BOARD_TYPE BitPresTagv1
  CUSTOMIZATIONS cfg/board-customizations.json)
```

Arguments:

- `PROCESSOR`: ChibiOS processor template family. For example, `stm32l4xx`
  uses `ChibiOS/tools/ftl/xml/stm32l4board.xml` and
  `ChibiOS/tools/ftl/processors/boards/stm32l4xx/templates`.
- `BOARD_TYPE`: build-tree board directory and firmware include name. Firmware
  should include `$(BOARDDIR)/<BOARD_TYPE>/board.mk`. The JSON `board_id`
  should normally match `BOARD_TYPE`, because the stock ChibiOS `board.mk.ftl`
  uses `board_id` when emitting `BOARDSRC` and `BOARDINC`.
- `CUSTOMIZATIONS`: JSON file relative to the board source directory.

Generated board files are written beside the other board outputs under
`<build>/embedded/boards/<BOARD_TYPE>/`. This matches the `BOARDDIR` value
passed into embedded firmware builds.

## `generate_board_chcfg.py`

Generate a `board.chcfg` from an existing template plus a JSON file containing
board and pin customizations.

Example:

```sh
python3 embedded/boards/tools/generate_board_chcfg.py \
  --template embedded/boards/BitPresTagv1/cfg/board.chcfg \
  --customizations my-board-pins.json \
  --output embedded/boards/MyBoard/cfg/board.chcfg \
  --standby-header embedded/boards/MyBoard/cfg/board_standby.h
```

The JSON file can include board metadata and either a compact pin map:

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

When starting from one of ChibiOS' stock processor XML files, the same JSON can
also customize non-pin XML:

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

The board CMake helper uses this form to generate a `board.chcfg` from a
processor family such as `stm32l4xx`, then runs `fmpp` against the matching
ChibiOS templates to produce `board.c`, `board.h`, and `board.mk`.

or grouped pins by port:

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

A list form is also accepted for generated data:

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
