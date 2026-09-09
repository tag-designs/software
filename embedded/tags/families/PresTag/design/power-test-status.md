# PresTag Power Testing — Live Status

**Overwritten by whichever session is driving the rig. Read this first on
connect; do not accumulate history here — that belongs in
[`power-test-results.md`](power-test-results.md).**

Updated: **2026-09-09 ~16:00**  ·  Branch `prestag-power-test-plan` at `ef2f6db`

## Current objective

Re-run the functional tests against the RTC Alarm A stop-delay change to
confirm nothing else broke, and collect a **measured** power model at 10 s,
60 s and 90 s — not extrapolated from one point. Then merge the branch to main.

## State right now

| | |
| --- | --- |
| worktree | clean at `ef2f6db`, four commits ahead of `main` |
| tag firmware | `ef2f6db` — PA2 analog, RTC Alarm A ticker |
| tag state | RUNNING at a 60 s period, mid-measurement |
| instrument | `joulescope_server.py` **running**, holding the JS320 |
| in flight | 60 s sweep point, 3600 s block; then 90 s, 5400 s block |

## Done

- Stop 2 works. Two faults, both fixed: PA2/INT1 was a floating input (~130 µA
  in Stop 2 only), and the LPTIM re-arm cost 6.3–7.1 ms of Run current per delay.
- `Q_cycle` 34.06 → 26.82 (pin) → **14.49 µC** (Alarm A).
- Phase C and D re-run clean on the new build; hibernation identical to before.

## Outstanding

- 60 s and 90 s sweep points, then a three-point fit with residuals.
- **C6** (C3/C4 at the 90 s default) and **T4** (brownout recovery, the real
  regression test for the §1.7 cursor fix) have never been run.
- **PresTagRaw** is opted into the Alarm A ticker but has never been run on
  hardware.
- **F3**: `writeStoredConfig()` ignores the result of `FLASH_Program_Array()` and
  `erasePersistent()` never checks its erase, so a stale `sconfig` can survive a
  reset-and-start. Open.

## What to watch for

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
