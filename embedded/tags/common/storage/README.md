# External Storage

`storage` owns the common external-flash API and chip-specific external-memory
drivers used by logging tags.

## Public Shape

The shared runtime and tag-local datalog code call the legacy `Ex*` API from
`external_flash.h`, for example:

- `ExFlashPwrUp()` / `ExFlashPwrDown()`
- `ExCheckID()`
- `ExFlashWrite()`
- `ExFlashRead()`
- `ExFlashSectorErase()`
- `ExSectorSize()` / `ExSectorCount()`

The selected storage module chooses the chip implementation:

- `flash_at25xe` compiles `src/at25xe.c`
- `flash_mx25l` compiles `src/mx25l.c`
- `flash_mx25r` compiles `src/mx25r.c`

`external_flash_test.c` provides the shared monitor self-test hook.

## Current Architecture

Storage drivers still mix three concerns:

- chip command formats and polling rules;
- SPI transaction framing and optimized polled byte transfers;
- assumptions about the tag's flash bus and chip-select line.

That is older than the sensor descriptor model. It works, but it is harder to
maintain than the newer split where tag/family code owns the board descriptor
and the chip driver owns only chip commands.

## Planned Cleanup

Move toward a small external-flash device descriptor that carries the SPI bus,
chip-select framing, and optimized read/write helpers. Keep each chip's command
format in its own driver. The cleanup should preserve the current `Ex*` API
until datalog users are migrated.
