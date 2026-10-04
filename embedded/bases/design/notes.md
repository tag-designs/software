---
type: design
status: current
summary: Brief base-board history plus SWD bitbang optimization results on the F042 breakout base and ideas for a hybrid SPI backend.
---

* bittag-base-jlcpcb-v3
  * tagbase v6,v7 (not yet fabbed)
  * 32 lqfp processor package
* bittag-base-jlcpcb-v2
  * tagbase v5c
  * 28 plcc processor package

## SWD Bitbang Optimization Notes

History: the 2026-06 optimization pass and the `-O3` experiment are in [the investigation](investigations/2026-06-swd-bitbang-optimization.md).

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
