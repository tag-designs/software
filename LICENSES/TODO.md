---
type: worklist
status: current
summary: Remaining licensing follow-ups from the 2026-10-06 inventory.
---

# Licensing Worklist

Follow-ups from the third-party inventory of 2026-10-06; see
[README.md](README.md). Two components ship with a known licensing gap, which
their notice files state together with the one program each affects:
MotionCal's `quality.c` and `magcal.h` in qtcalibrate
(`host/third-party/motioncal-quality.txt`), and SOLPOS in btviz
(`host/third-party/nrel-solpos.txt`).

## Worth reducing

- **Project source files carry no license header.** The root `LICENSE` covers
  them, but an `SPDX-License-Identifier: MIT` line in each file would make
  that explicit to anyone who copies a single file.
