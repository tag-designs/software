* bittag-base-jlcpcb-v3
  * tagbase v6,v7 (not yet fabbed)
  * 32 lqfp processor package
* bittag-base-jlcpcb-v2
  * tagbase v5c
  * 28 plcc processor package

## SWD Bitbang Optimization Notes

The `tag-breakout-base-l432-v1` base uses an STM32F042 as the SWD bridge MCU
and is already running at the F042 maximum 48 MHz SYSCLK/HCLK. The 2026-06 SWD
bitbang optimization pass replaced hot-path PAL line calls with lower-level
GPIO helpers, cached direction changes, and added fixed-width unrolled shift
helpers. Measured download speed improved by about 23%.

An experiment to compile only `tag-breakout-base-l432-v1/src/ll_swd.c` at
`-O3` was a net performance loss and increased flash size, so keep that file at
the normal target optimization level.

For a future base, the likely next SWD transport experiment is a hybrid backend:
use SPI only for byte-aligned shift-out phases, and keep GPIO bitbang for the
odd-sized shift-in phases. The current shift-out widths in the optimized
`tag-breakout-base-l432-v1/src/ll_swd.c` are all multiples of 8 bits
(`8`, `16`, `24`, and `32`), while the odd-sized operations are shift-in
(`5`, `9`, and `24` bits). The `tag-base-c071/src/ll_swd_spi.c`
implementation is the reference for this hybrid approach: SPI handles
`SW_ShiftOutBytes()`, while reads switch back to GPIO bitbang.

If a future base uses a faster MCU with enough SRAM headroom, also consider
placing only the hottest SWD bitbang helpers in RAM. The F042 base is not a good
candidate because the successful build already uses essentially all available
SRAM.
