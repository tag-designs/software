---
type: results
status: current
summary: Append-only log of BitTag power measurements with build, board, supply and config for each session.
---

# BitTag Power Measurement Log

Append-only. One block per measurement session, newest last. The procedure is
[`power-test-plan.md`](power-test-plan.md); the narrative write-up is
[`power-test-report.md`](power-test-report.md).

## 2026-10-03 — first qualification, fw-v0.5

| | |
| --- | --- |
| Release | `fw-v0.5`, commit `fdcec161`, tree clean |
| Image | `BitTag.bin` sha256 `c80b0c36dfbb61009ac8a5bf5fd13a9e28dbdb3fae6200908ed2963a4b96d651` |
| Toolchain | arm-none-eabi-gcc 14.2.1 (pinned) |
| Board | BitTag V6, UUID `2035374D303150190059002F` |
| Supply | 2.4960 V, unregulated, stable across every measurement |
| Config | `bittag-bitpersec.json` — `BITTAG_LE`, `BITTAG_BITPERSEC`, ADXL362 R4G/S50/AAquarter, 0.35 g act and inact, 0.24 s inactive |
| Instrument | JS220 via `joulescope_server.py`, `--use-server` |
| qtmonitor | detached |
| Artifacts | `release-checks/bittag-20261003-144858/` |

### Currents

| State | Current (uA) | Window | Note |
| --- | ---: | ---: | --- |
| Cold, never attached | 0.1227 | 120 s | taken before the first attach of the session |
| `IDLE`, after attach and clean detach | 0.1224 | 120 s | |
| `RUNNING`, run 1 | 0.5083 | 1200 s | state confirmed `RUNNING` before the window |
| `RUNNING`, run 2 | 0.5081 | 1200 s | state confirmed; agrees with run 1 to 0.04% |
| `FINISHED` | 0.1214 | 120 s | data on the tag, not yet read |
| `IDLE`, after a full cycle | 0.1213 | 120 s | after run, stop, download, reset |

Four resting states: mean **0.1220 uA**, spread 1.4 nA (1.1%).
Running: mean **0.5082 uA**, 4.17x resting.

Per mAh of cell, ignoring derating: **342 days** resting, **82.0 days**
recording at one activity bit per second.

### Verdict

| Gate | Result |
| --- | --- |
| Supply 2.45-2.55 V on every measurement | pass, 2.4960 V throughout |
| `IDLE` <= 5 uA | pass, 0.1224 uA — 40x margin |
| `FINISHED` <= 5 uA and within 20% of `IDLE` | pass, 0.1214 uA, 0.8% from `IDLE` |
| Resting repeatability | pass, 4 of 4, 1.1% spread |
| `RUNNING` two runs within 5% | pass, 0.04% |
| State confirmed `RUNNING`, not `CONFIGURED` | pass, both runs |
| `tag-test` | pass, `ALL_PASSED` (from `IDLE`) |
| Download table present, timestamps monotonic | pass |

**PASS.** This is the first BitTag power qualification and establishes the
baseline for future sessions.

### Download

24 records over a 23 m 28 s run, `internal_pages=24`:

| Table | Rows | Check |
| --- | ---: | --- |
| `Activity` | 1440 | monotonic, span 1439 s = 24 x 60 one-second bits; **100% zero**, correct for an undisturbed tag |
| `Voltage` | 24 | monotonic; 2.48-2.49 V, agrees with the Joulescope |
| `CoreTemperature` | 24 | monotonic; 27.6-28.3 C |
| `states` | 3 | `CONFIGURED`, `RUNNING`, `FINISHED EVENT_STOPCMD` |

### Not measured

Phase B2 (format independence, `bittag-default.json`) and Phase D (activity
sensitivity) were not run.
