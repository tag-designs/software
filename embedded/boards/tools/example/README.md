# STM32L432 standalone board-generation example

This directory is a small standalone CMake project that uses the board
generation tools in the parent directory. It defines a minimal STM32L432 board
with five pins and generates:

- `board.c`
- `board.h`
- `board.mk`
- `board_standby.h`

Configure it with explicit paths to ChibiOS and `fmpp`:

```sh
cmake -S /path/to/example -B build-board-example \
  -DCHIBIOS_DIR=/path/to/ChibiOS \
  -DFMPP_EXECUTABLE=/path/to/fmpp
```

Then build the generated board files:

```sh
cmake --build build-board-example --target generate-stm32l432-example-board
```

If `CHIBIOS_DIR` or `FMPP_EXECUTABLE` is missing or invalid, CMake stops during
configuration with the exact option to set. `CHIBIOS_DIR` and
`FMPP_EXECUTABLE` may also be supplied as environment variables; `fmpp` can be
found automatically if it is already on `PATH`.

Generated files are written under:

```text
build-board-example/boards/STM32L432_Standalone_Example/
```
