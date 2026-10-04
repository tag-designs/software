---
type: readme
status: current
summary: How to build and run the CompassTag datalog host simulation that checks the resume-cursor, partial-page and mid-block halt fixes.
---

# CompassTag Datalog Simulation

`datalog_sim.c` compiles the real `../src/state_run.c` and `../src/datalog.c`
for the host, against the minimal stubs in `stub/`. It drives `Running()` over a
synthetic 30 s ticker into a fake external NOR and a fake internal header array,
then downloads every page through the real `data_logAck()`.

It checks two fixes from the `firmware-fix` branch
(see [next-release-todo.md](../../../design/next-release-todo.md)). Both are
slow to reproduce on a tag:

- **A1, resume cursor.** After a reset, `restoreLog()` followed by
  `Running(T_INIT)` must put the external cursor at
  `pages * DATALOG_PAGE_WORDS`. That is the start of the next page, counted in
  16-bit words (190 per page). fw-v0.0.3 used `pages * 30`, which pointed back
  into page 0. The fake NOR asserts on programming a byte that is not erased, so
  that bug aborts the run. A second resume runs straight into
  `Running(T_INIT)` from a stale cursor, as on the hibernation path.
- **A3, last partial page.** Flash size is not a multiple of the 380-byte page,
  so the end of the part cuts the final page short. When the flash fills, that
  page must still be downloaded. It must hold exactly the blocks that were
  completed: 18 samples on 4 MiB (AT25XE321D) and 6 on 8 MiB (MX25R6435F). The
  fake NOR also asserts on any read past the end of the part.

It also checks A7. A 60 s halt mid-block must start a new page whose header
places the next sample at its own time, dropping only the unfinished block.

Every sample carries its sequence number in `ax`/`ay`, so the download is
checked sample by sample against what was written.

## Build and run

```sh
cd embedded/tags/families/CompassTag
cc -std=c11 -Wall -Wextra -Wno-pointer-to-int-cast -O1 \
   -o /tmp/compasstag_datalog_sim test/datalog_sim.c -Itest/stub -Iinc
/tmp/compasstag_datalog_sim
```

The run should print `COMPASSTAG DATALOG SIM: all assertions passed`. Any
failure aborts on the assertion that describes it. The run takes well under a
second, including filling 8 MiB.

`-Wno-pointer-to-int-cast` is needed because `datalog.c` compares flash
addresses as `uint32_t`, which is exact on the 32-bit tag. On a 64-bit host the
harness asserts that its header array does not straddle a 4 GiB boundary, which
is the only condition under which that truncation would matter.

Built against the sources from before each fix, the harness fails as intended:

- before A1: `restoreLog cursor not in words`;
- before A3: `final partial page not served`.

## What it does not cover

Sensors, buses, the RTC, power and real timing are all stubbed. The FINISHED
marker path of `restoreLog()` is not exercised. The internal header region is
sized so that external flash fills first. On a real tag, check that the
persistent region holds a header for every page the external flash can hold.

## Stubs

`stub/` holds hand-written stand-ins for the headers the two sources include.
Among them are the family's `BackupState`, the `CompassTagLog` Ack (mirroring
`embedded/proto-c/compasstag-proto-c`), the storage and internal-flash calls,
and the clock registers `fast_msi()` touches. The real `inc/datalog.h` and
`inc/sensors.h` are used, so the page layout under test is the firmware's own.
If a source starts using something new, the simulation fails to compile until
the stub is extended, which is the intended signal.
