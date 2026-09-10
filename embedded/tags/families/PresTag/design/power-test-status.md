# PresTag Power Testing — Live Status

**Overwritten by whichever session is driving the rig. Read this first on
connect; do not accumulate history here — that belongs in
[`power-test-results.md`](power-test-results.md).**

Updated: **2026-09-09 ~22:00**  ·  Branch `prestag-power-test-plan`, merging to `main`

## Current objective

**None in flight — the campaign is complete and the rig is idle.** The operator
is away for 12 days from 2026-09-09. Pick up from "Outstanding" below.

## State right now

| | |
| --- | --- |
| worktree | clean, merged to `main` |
| tag firmware | `890a11b` PresTag — PA2 analog, RTC Alarm A ticker |
| tag state | **IDLE**, RTC set, no configuration programmed |
| instrument | `joulescope_server.py` **stopped**, released cleanly, DUT left powered (2.47 V, `Device ID 0x435` answers under reset) |
| in flight | nothing |

## Done

- **Stop 2 works.** Two independent faults, both fixed: PA2/INT1 was a floating
  digital input (~130 µA, visible only in Stop 2), and the LPTIM re-arm cost
  6.3–7.1 ms of Run current per delay. `Q_cycle` 34.06 → 26.82 → **15.26 µC**.
- **Power model measured, not extrapolated**, at three periods on the shipping
  build:

  | state / period | current |
  | --- | --- |
  | IDLE | 0.2810 µA (A1′), 0.2928 (A1), 0.2860 (A5) |
  | FINISHED | **0.2790 µA** |
  | CONFIGURED | 0.5165 µA (13.4 µC per minute wake) |
  | HIBERNATING | **0.3769 µA** (5.05 µC per minute wake) |
  | 10 s | **1.8099 µA** |
  | 60 s | **0.5406 µA** |
  | **90 s (default)** | **0.4517 µA** |

  Fit: `I_rest` **0.2842 µA**, `Q_cycle` **15.26 µC**, `T_knee` **53.7 s**,
  R² 0.999993, worst residual 0.4%.
- **Both cells clear a year at 90 s**: 5.5 mAh **505 d**, 11 mAh **1010 d**
  (379 / 758 d at a 75% derating). 5.5 mAh was 21 days short before the fixes.
- **Phases A, B, C, D all pass.** C6 (scheduled and commanded stop at the 90 s
  default) added: FINISHED at the stop epoch **+1 s**, and `tag-stop` confirmed
  in one poll with an immediate clean download.
- **H3 settled §1.6**: five hibernation wakes in 295 s at exactly 60.0 s, twice
  over. The hour alarm behaves as a minute alarm.
- **§1.7 cursor fix confirmed on hardware.** H3's run opened its hibernate window
  between samples 60 and 120 — the case C5 could not discriminate — and entry
  was at sample 60, not 120.
- **PresTagRaw** aligned with PresTag (`4527184`) and measured: IDLE 0.2792 µA,
  10 s run 1.7746 µA, download PASS. Within 2% of PresTag on both.

## Outstanding

- **T4 — brownout recovery.** The real regression test for the §1.7 fix on the
  *restart* path (H3 covered the hibernation gate). Needs a genuine brownout:
  a debug-probe reset is classified as a monitor attach, resumes via `T_CONT`,
  and would pass while the defect stood. Not run.
- **F3 — unchecked flash status.** `writeStoredConfig()` ignores
  `FLASH_Program_Array()`'s result and `erasePersistent()` never checks its
  erase, so a stale `sconfig` can survive a reset-and-start. Observed once:
  `tag-start` printed `period: 10` while the tag ran at 9 s. **Open, and the
  reason this is not a release qualification.**
- **F2 — `godown(STOP2)` is a silent no-op** on L432:
  `tagPowerEnterTerminalSleep()` handles only Standby and Shutdown and returns
  for anything else, so sub-10 s periods never sleep (530.7 µA flat). Bench-only
  impact; implement it or reject it explicitly rather than returning silently.
- Absolute pressure has never been compared against a local **station** reading,
  so only plausibility is established, not calibration.

## What to watch for

**A killed session leaves the tag unpowered.** The clean release only runs on a
normal exit; a hard kill skips it and the DUT loses its supply through the open
sense path, so the tag resets and any run in progress is gone. This happened
mid-C6: the transition log came back `ABORTED reason=EVENT_POWERFAIL`. After any
crash, assume the run died and check.

**A wedged JS320 can be recovered in software.** It enumerates but its whole
topic tree reads `NOT_FOUND`, or `jsdrv_open` times out. **Repeated
`USBDEVFS_RESET` ioctls fix it** — one is not enough, three in a row with 5 s
between them worked; re-read `busnum`/`devnum` from `/sys/bus/usb/devices/*/`
between attempts because the device renumbers. No replug and no root needed.
Afterwards `s/i/range/mode` reads **0** — sense path open, DUT unpowered, which
downstream looks like a dead tag (`Unable to get core ID`, `initial DEMCR read
failed`) — and starting the server puts it back to auto.

**One server, started once, left running.** Stop/start cycling is what wedges it.

**Do not change the statistics window on a running server.** It races inside the
driver's publish callback. Traces use the server's 0.5 s block; a wake event
occupies one whole block anyway, which is enough to count wakes and to measure
per-event charge.

**Do not poll state during a run.** Every attach connects under reset. A C4 run
captured 3 samples instead of ~30 after eight polls. Read the epochs back from
the download instead, or watch the tag with a trace, which needs no attach —
that is how C6a was confirmed to be sampling at exactly 90.0 s.

**`tag-info` output contains binary.** Piping it straight into `grep` gets you
`binary file matches` and an empty parse; a C6a poller reported a spurious
TIMEOUT for 400 s this way while the tag had finished correctly at +1 s. Use
`strings` first, or `grep -a`.

**Flashing.** `mode=UR` is required — every rest state is Shutdown with the
debug port down, and a hotplug connect fails with `Unable to get core ID` at a
healthy target voltage. Follow **any** erase with `-g 0x08000000`; an erase
leaves the part halted under reset and it boots the ROM bootloader at 14 mA.

**First attach usually fails.** `initial DEMCR read failed`, `couldn't fetch
monitor buffer length`, `empty or invalid acknowledgement`. Always retry.

**Verify the period from the data, not the host's echo.** `tag-start` prints the
requested configuration, not the programmed one (F3).

**Measurement windows** must be a whole number of 60-sample blocks —
`60 × period` seconds. The starting phase does not matter, only the duration.
