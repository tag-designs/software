# PresTag Power Testing — Live Status

**Overwritten by whichever session is driving the rig. Read this first on
connect; do not accumulate history here — that belongs in
[`power-test-results.md`](power-test-results.md).**

Updated: **2026-09-09 ~18:30**  ·  Branch `prestag-power-test-plan` at `458982d`

## Current objective

Re-run the functional tests against the RTC Alarm A stop-delay change to
confirm nothing else broke, and collect a **measured** power model at 10 s,
60 s and 90 s — not extrapolated from one point. Then merge the branch to main.

## State right now

| | |
| --- | --- |
| worktree | `458982d` plus an uncommitted `PresTagRaw/custom.h` alignment |
| tag firmware | `458982d` PresTag — PA2 analog, RTC Alarm A ticker |
| tag state | RUNNING at 90 s, mid-measurement |
| instrument | `joulescope_server.py` **running**, holding the JS320 |
| in flight | 90 s sweep point, 5400 s block (restarted 18:30) |

## Done

- Stop 2 works. Two faults, both fixed: PA2/INT1 was a floating input (~130 µA
  in Stop 2 only), and the LPTIM re-arm cost 6.3-7.1 ms of Run current per delay.
- Phase C and D re-run clean on the Alarm A build; hibernation identical to before.
- Sweep, measured over full blocks: **IDLE 0.2810 µA**, **10 s 1.8099 µA**,
  **60 s 0.5406 µA**. `Q_cycle` from the two run points agrees to 2%
  (15.29 vs 15.58 µC), so the linear model holds across a 6x span.

## Outstanding

- 90 s sweep point, then a three-point fit with residuals.
- **PresTagRaw**: its `custom.h` silently inherited `STANDBY` for every state and
  the LPS27 driver defaults (10 ms power-up, up to six 15 ms polls) where PresTag
  uses `SHUTDOWN` and 5/5/1. Now aligned in the worktree, **built but never run**.
  Next: flash it, measure idle and a run, and check the download for sanity --
  it writes the same Pressure/Temperature/Voltage tables, so
  `prestag_check_download.py` works unchanged.
- **C6** (C3/C4 at the 90 s default) and **T4** (brownout recovery, the real
  regression test for the section 1.7 cursor fix) have never been run.
- **F3**: `writeStoredConfig()` ignores the result of `FLASH_Program_Array()` and
  `erasePersistent()` never checks its erase, so a stale `sconfig` can survive a
  reset-and-start. Open.

## What to watch for

**A killed session leaves the tag unpowered.** The clean release only runs on a
normal exit; a hard kill skips it and the DUT loses its supply through the open
sense path, so the tag resets and any run in progress is gone. Starting the
server restores power. After any crash, assume the run died and check.

**The instrument.** One server, started once, left running. Stop/start cycling
wedges the JS320 — it keeps enumerating, nothing holds its USB handle, and
`jsdrv_open` times out until it is physically replugged. If you need the UI,
stop the server and leave it stopped. Releasing now leaves the DUT powered and
says so (`released, DUT left powered (range mode 4)`); an unpowered DUT shows
downstream as `Unable to get core ID`, which does not look like a power fault.

**Flashing.** `mode=UR` is required — every rest state is Shutdown with the
debug port down, and a hotplug connect fails with `Unable to get core ID` at a
healthy target voltage. Follow **any** erase with `-g 0x08000000`; an erase
leaves the part halted under reset and it boots the ROM bootloader at 14 mA
(`PC = 0x1FFF….`). Recovery is that same `-g` line.

**First attach usually fails.** Three distinct errors seen: `initial DEMCR read
failed`, `couldn't fetch monitor buffer length`, `empty or invalid
acknowledgement`. Always retry; never treat one failure as a verdict.

**Verify the period from the data, not the host's echo.** `tag-start` prints the
requested configuration, not the programmed one (see F3).

**Do not poll state during a run.** Every attach connects under reset. A C4 run
captured 3 samples instead of ~30 because it was polled eight times; read the
epochs back from the download instead.

**Measurement windows** must be a whole number of 60-sample blocks —
`60 × period` seconds. The starting phase does not matter, only the duration.
