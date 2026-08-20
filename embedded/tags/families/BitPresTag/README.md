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
BitPresTag variants keep any RTC line-swap override in `custom.h`; the shared
family `devices.c` owns the accelerometer wake-source selection and standby pin
policy.

## Configuration Semantics

BitPresTag keeps the ADXL362 in wake-mode operation for activity detection.
The host configuration therefore exposes only the active threshold, inactive
threshold, and inactivity count. The stored ADXL362 range, sample rate, and
anti-alias filter are fixed by firmware.

The protobuf field `Config.adxl362.inactive_sec` is historical for this tag
family. BitPresTag interprets it as an inactivity sample count at the
accelerometer wake-mode rate, not as seconds.

If a variant needs to experiment with a ChibiOS setting during bring-up, add the
same-named file under that variant's local `cfg/` directory. The common makefile
searches local `cfg/` before this family `cfg/`, so local overrides still work
without copying the normal configuration into every variant.

The larger descriptor-based power cleanup remains future work, especially for
the USART-style LPS pressure sensor bus.
