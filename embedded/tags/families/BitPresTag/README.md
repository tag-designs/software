# BitPresTag Family

This family contains the shared application code for the BitPresTag pressure
and activity tags.

Current variants:

- `BitPresTag`: AT25XE external flash.
- `BitPresTagMX25R`: MX25R external flash.

The variants are intended to differ only in the external flash module selected
from their `project.mk` files. Shared headers, state code, data logging, tests,
and power/bus control live here. Variant directories keep `custom.h`, `cfg/`,
and the small build wrappers so a flash-specific firmware string or board
constant can still be adjusted without copying application code.

The family currently keeps the historical BitPresTag power implementation. It
does mark SPI1 and USART2 active/inactive for the common Stop2 suspend/resume
path, but the larger descriptor-based power cleanup remains future work.
