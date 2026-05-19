# BitPresTag Family

This family contains the shared application code for the BitPresTag pressure
and activity tags.

Current variants:

- `BitPresTag`: AT25XE external flash.
- `BitPresTagMX25R`: MX25R external flash.

The variants are intended to differ only in the external flash module selected
from their `project.mk` files. Shared headers, state code, data logging, tests,
power/bus control, and ChibiOS configuration live here. Variant directories keep
`custom.h` and the small build wrappers so a flash-specific firmware string or
board constant can still be adjusted without copying application code.

If a variant needs to experiment with a ChibiOS setting during bring-up, add the
same-named file under that variant's local `cfg/` directory. The common makefile
searches local `cfg/` before this family `cfg/`, so local overrides still work
without copying the normal configuration into every variant.

The family currently keeps the historical BitPresTag power implementation. It
does mark SPI1 and USART2 active/inactive for the common Stop2 suspend/resume
path, but the larger descriptor-based power cleanup remains future work.
