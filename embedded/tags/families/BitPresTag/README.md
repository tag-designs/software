# BitPresTag Family

This family contains the shared application code for the BitPresTag pressure
and activity tags.

Current variants:

- `BitPresTag`: AT25XE external flash.
- `BitPresTagMX25R`: MX25R external flash.

The variants are intended to differ only in the external flash module selected
from their `project.mk` files. Shared headers, state code, data logging, tests,
and ChibiOS configuration live here. Variant directories keep `custom.h` and
the small build wrappers so a flash-specific firmware string or board constant
can still be adjusted without copying application code.

Power and bus control now come from the common `tag_core` module. The
BitPresTag variants set `SWAP_I2C` and `ACCEL_WAKEUP_SOURCE` in `custom.h` to
describe the board-specific RTC line swap and wakeup line used by the common
power code.

If a variant needs to experiment with a ChibiOS setting during bring-up, add the
same-named file under that variant's local `cfg/` directory. The common makefile
searches local `cfg/` before this family `cfg/`, so local overrides still work
without copying the normal configuration into every variant.

The larger descriptor-based power cleanup remains future work, especially for
the USART-style LPS pressure sensor bus.
