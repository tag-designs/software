---
type: worklist
status: current
summary: Remaining licensing follow-ups from the 2026-10-06 inventory.
---

# Licensing Worklist

Follow-ups from the third-party inventory of 2026-10-06; see
[README.md](README.md). Two components ship with a known licensing gap, which
their notice files state together with the one program each affects:
MotionCal's `magcal.h` in qtcalibrate
(`host/third-party/motioncal-quality.txt`), and SOLPOS in btviz
(`host/third-party/nrel-solpos.txt`).

`quality.c` was the other half of the first of those and is gone as of
2026-10-10, replaced by owned arithmetic. What is left is a header declaring
the Freescale solver that `magcal.c` and `matrix.c` implement, both of them
BSD-3-Clause; writing our own declarations from those two would close the gap
entirely.

## Worth reducing

- **Project source files carry no license header.** The root `LICENSE` covers
  them, but an `SPDX-License-Identifier: MIT` line in each file would make
  that explicit to anyone who copies a single file.
