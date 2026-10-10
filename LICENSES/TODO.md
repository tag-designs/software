---
type: worklist
status: current
summary: Remaining licensing follow-ups from the 2026-10-06 inventory.
---

# Licensing Worklist

Follow-ups from the third-party inventory of 2026-10-06; see
[README.md](README.md). One component ships with a known licensing gap, which
its notice file states together with the one program it affects: SOLPOS in
btviz (`host/third-party/nrel-solpos.txt`).

The MotionCal gap in qtcalibrate closed on 2026-10-10. `quality.c` went first,
replaced by owned arithmetic in `host/libraries/sensoranalysis`; then
`magcal.h`, rewritten from the Freescale solver it declares. `magcal.c` and
`matrix.c` stay, and those are BSD-3-Clause with a notice of their own
(`host/third-party/freescale-magcal.txt`).

## Worth reducing

- **Project source files carry no license header.** The root `LICENSE` covers
  them, but an `SPDX-License-Identifier: MIT` line in each file would make
  that explicit to anyone who copies a single file.
