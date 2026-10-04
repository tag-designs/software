---
type: readme
status: current
summary: How to build and run the PresTag datalog host simulation for both download formats, checking the last partial page and mid-page resets.
---

# PresTag Datalog Simulation

`datalog_sim.c` compiles the real `../src/state_run.c` and `../src/datalog.c`
for the host, against the minimal stubs in `stub/`. It drives `Running()` over a
synthetic sample ticker into a fake external NOR and a fake internal header
array, then downloads every page through the real `data_logAck()`.

Its subject is fix A3 from the `firmware-fix` branch
(see [next-release-todo.md](../../../design/next-release-todo.md)):

- Flash size is not a multiple of the 240-byte page, so the end of the part cuts
  the final page short. When the flash fills, that page must still be downloaded
  and must end exactly where the samples end: 16 samples on 4 MiB (AT25XE321D).
- fw-v0.0.3 served only pages that fit whole.
- The fake NOR also asserts on any read past the end of the part.

It also checks that a reset in the middle of a page resumes on a fresh page
without reprogramming written bytes, and that a halt mid-page also starts one
(A7). Samples carry no timestamps, so a page that missed samples would place
every later sample too early. Integer-second wake jitter at 1 s must not split
a page.

It is built twice, once for each download path:

- as is, for the converted `PresTagLog` download (PresTag);
- with `-DPRESTAG_RAW_LOG=1`, for the packed-block `PresTagRawLog` download
  (PresTagRaw).

Every sample carries its sequence number in its pressure and temperature words,
so the download is checked sample by sample against what was written.

## Build and run

```sh
cd embedded/tags/families/PresTag
for raw in 0 1; do
  cc -std=c11 -Wall -Wextra -Wno-pointer-to-int-cast -O1 -DPRESTAG_RAW_LOG=$raw \
     -o /tmp/prestag_datalog_sim$raw test/datalog_sim.c \
     -Itest/stub -Iinc -I../../../../include
  /tmp/prestag_datalog_sim$raw
done
```

On macOS, if `cc` fails to link against the installed SDK, use the
compiler and SDK the CMake host build uses (`CMAKE_C_COMPILER` and
`CMAKE_OSX_SYSROOT` in `build-host/CMakeCache.txt`, with `-isysroot`).

Each build should print `PRESTAG DATALOG SIM: all assertions passed`. Any
failure aborts on the assertion that describes it. Built against the
`datalog.c` from before A3, both builds fail with
`final partial page not served`.

`-Wno-pointer-to-int-cast` is needed because `datalog.c` compares flash
addresses as `uint32_t`, which is exact on the 32-bit tag. On a 64-bit host the
harness asserts that its header array does not straddle a 4 GiB boundary.

## What it does not cover

Sensors, buses, the RTC, power and real timing are all stubbed.

The conversions `lps27Pressure()` and `lps27Temperature()` are replaced by
identity functions so that samples decode to their raw words. The real
conversions are not tested here.

The internal header region is sized so that external flash fills first.

## Stubs

`stub/` holds hand-written stand-ins for the headers the two sources include.
Among them are the family's `BackupState`, the `PresTagLog` and `PresTagRawLog`
Acks (mirroring `embedded/proto-c/prestag-proto-c` and `prestagraw-proto-c`),
the storage and internal-flash calls, and the phase probes, which compile to
nothing. The real `inc/datalog.h` and `include/prestag_log_format.h` are used,
so the page layout under test is the firmware's own.
