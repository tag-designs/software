---
type: investigation
status: closed
summary: The 2026-06 SWD bitbang optimization of the F042 bridge on tag-breakout-base-l432-v1: about 23% faster downloads, and why ll_swd.c stays at the normal optimization level.
---

# SWD bitbang optimization on tag-breakout-base-l432-v1 (2026-06)

Closed: the optimization shipped, and the `-O3` experiment was rejected.
Extracted verbatim from [base design notes](../../README.md); ideas for a future
base remain there.

The `tag-breakout-base-l432-v1` base uses an STM32F042 as the SWD bridge MCU
and is already running at the F042 maximum 48 MHz SYSCLK/HCLK. The 2026-06 SWD
bitbang optimization pass replaced hot-path PAL line calls with lower-level
GPIO helpers, cached direction changes, and added fixed-width unrolled shift
helpers. Measured download speed improved by about 23%.

An experiment to compile only `tag-breakout-base-l432-v1/src/ll_swd.c` at
`-O3` was a net performance loss and increased flash size, so keep that file at
the normal target optimization level.
