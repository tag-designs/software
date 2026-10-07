---
type: results
status: current
summary: Append-only log of BitTag power measurements -- build, board, supply, config, numbers and interpretation per session.
---

# BitTag Power Measurement Log

Append-only. One block per measurement session, newest last, each with its
numbers and what they mean. The procedure is
[`power-test-plan.md`](power-test-plan.md), and the shared rig procedure is
[power testing](../../../../docs/bench/power-testing.md). What went wrong
getting the first session's numbers is in
[its investigation](investigations/2026-10-bittag-first-qualification.md).

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

### Interpretation

The resting states are the result worth keeping. `IDLE` (0.1224 uA),
`FINISHED` (0.1214 uA), post-cycle `IDLE` (0.1213 uA) and the cold
never-attached baseline (0.1227 uA) agree within 1.1%. That matters for two
reasons: it is 40x below the 5 uA gate, and `IDLE` and `FINISHED` *should*
match because `tagDevicesApplyPowerState()` routes both through the same
shutdown, so agreement confirms the prediction rather than merely passing a
threshold.

The cold baseline is the unusual one. CompassTag's plan notes that a
never-attached figure cannot normally be obtained, because every tool attaches.
Here the tag was found already asleep at the start of the session, so a 120 s
window was taken before anything touched it. It agrees with the post-attach
number to 0.3 nA, which is the cleanest possible evidence that **BitTag does
not suffer the CompassTag `DBGMCU`/`C_DEBUGEN` fault** — the risk this plan
existed to check.

Running is 4.17x resting and reproduced to 0.04% across two 1200 s windows.
The 1200 s window was chosen because the alignment error for a once-a-minute
wake is 1/N; at 20 minutes that is 5%, and the two runs came in far inside it.

Per mAh of cell: 342 days resting, 82 days recording.

---

### 2026-10-05  ADC fix verified on BitTag — no measurable power cost

- **build**: `54135465` (`adcVDD()` settling delay restored at 200 us, SMPR2
  index corrected, internal channels at 247.5 cycles), on top of `fw-v0.5`.
  Development build, not a release image, so this is a comparison and not a
  qualification.
- **board**: BitTag V6, UUID `2035374D303150190059002F` — the same unit as the
  2026-10-03 qualification, so the comparison varies one thing.
- **conditions**: supply 2.4961 V, `bittag-bitpersec.json`, resting window
  120 s, running window 1200 s, `joulescope_server.py --use-server`.
- **result**:

  | State | 2026-10-03, before | 2026-10-05, after | Delta |
  | --- | ---: | ---: | ---: |
  | `IDLE` | 0.1224 uA | **0.1169 uA** | -4.5% |
  | `RUNNING` | 0.5082 uA (mean of two) | **0.5068 uA** | -0.3% |

  State markers read 2.48-2.49 V and 22.3-23.0 C against a true 2.4961 V.
- **verdict**: the fix costs nothing measurable. Both states came in slightly
  *below* the earlier figures, and idle moved further than running, which is
  the signature of measurement scatter rather than of the change.
- **notes**: a cost of about +0.03 uA on the run had been predicted, on the
  assumption that the 1 ms settling delay burned active current. That was
  wrong twice over. The delay is now 200 us, and more importantly
  `chThdSleepMicroseconds()` is a thread sleep, so the MCU idles through it;
  only the two conversions are active, and those are tens of microseconds.
  **Recorded because the prediction was published before the measurement** --
  the earlier estimate in this tree should not be trusted.

  Temperature read 22.3-23.0 C here against 28.1-28.3 C on 2026-10-03. The
  SMPR2 fix changed the temperature channel's sampling time from the reset
  2.5 cycles to 247.5, so a systematic shift was plausible; the operator
  reports 10-03 was a hot day and the study was warm. Not resolved by
  measurement, and no independent thermometer was used. Treat any logged
  temperature from before `54135465` as suspect, but do not read this pair as
  evidence of its size.

### 2026-10-05  Sampling moved to the write site — control at BITPERSEC

- **build**: the above plus the BitTag change that samples VDD and temperature
  immediately before `writeDataLog()` instead of on every wake, and drops the
  exponential average.
- **board/conditions**: as above — same unit, 2.4961 V, `bittag-bitpersec.json`,
  1200 s window.
- **result**: `RUNNING` **0.5089 uA**. Download: `Voltage` 23 rows spanning
  2.48-2.49 V, `CoreTemperature` 23 rows spanning 22.5-24.2 C, `Activity` 1380
  rows all zero — 23 records x 60 one-second bits, correct for an undisturbed
  tag.
- **verdict**: the records still carry sensible values, now sampled at the
  moment they are used. Three run figures on one board and one config:

  | Build | `RUNNING` |
  | --- | ---: |
  | before any fix (10-03) | 0.5082 uA |
  | ADC fix | 0.5068 uA |
  | ADC fix + write-site sampling | 0.5089 uA |

  A 0.4% spread with no ordering, which is scatter.
- **notes**: **this format is a control, not a demonstration.** At
  `BITPERSEC` a record is written every wake, so the change does not alter how
  often `adcVDD()` runs and no saving is expected or seen. The redundancy it
  removes is at `BITSPERMIN` and longer, where a record covers 10 to 35 wakes;
  the firmware default is `BITSPERFIVEMIN`, which has never been power
  measured. Phase B2 of the plan covers that format and remains unrun, so the
  size of the saving is still unmeasured — and, given a conversion costs tens
  of microseconds of active time, it is expected to sit below what a 1200 s
  window can resolve.

### 2026-10-07  `BitTag` release qualification, `fw-v0.6` — **PASS on power; one data-timestamp anomaly**

- **image**: `fw-v0.6` (`56e5e6a0`), flashed from the release by the operator,
  identity read back from the tag. `tag-test` run by the operator after
  flashing.
- **board**: `2035374D303150190059002F`, "BitTag V6, Firmware version 2".
- **conditions**: 2.4961 V throughout, unregulated 2.5 V cell. Measured by
  hand: `tag_release_check.py` cannot attach to this tag from sleep. No attach
  storms — the storm is an IMUTag test.
- **result**, both configurations:

  | Point | `BITPERSEC` (1 bit/s) | `BITSPERFIVEMIN` (shipped default) |
  | --- | ---: | ---: |
  | `IDLE`, clock set | 0.1185 µA | 0.1174 µA |
  | **`RUNNING`** | **0.5073 / 0.5068 µA** | **0.4941 µA** |
  | `FINISHED` | 0.1178 µA | 0.1176 µA |
  | `IDLE` after a full cycle | 0.1178 µA | — |
  | activity records | 1860, **every delta exactly 1 s**, 31 min | 7, every delta exactly 300 s |

- **verdict on power**: **PASS.** Two `BITPERSEC` windows agreed to **0.1%**
  (0.5073, 0.5068) and both land within **0.25%** of the `fw-v0.5` published
  figure of 0.508 µA — across a release that changed shared ADC code, added the
  stored-configuration verify to BitTag's write path and altered its board's LSE
  configuration. Resting states are unchanged within 3% of `fw-v0.5`'s
  0.1224 µA.

- **The logging rate is nearly free.** 300x less logging buys **2.6%**
  (0.5073 → 0.4941 µA). BitTag's run current is set by the ADXL362 watching for
  activity, not by how often a bit is written. **Do not estimate BitTag battery
  life from the record rate** — the two are almost independent.

- ~~**OPEN: `BITSPERFIVEMIN` activity timestamps fall outside the run.**~~
  **Retracted the same day: this is correct behaviour and I misread it.**

  BitTag writes on an **absolute** grid, not a run-relative one:
  `sample_period = chunk_period * chunk_number` (300 x 7 = **2100 s** for
  `BITSPERFIVEMIN`), and `bt_state_run.c` aligns to it with
  `lastwrite = (timestamp / sample_period) * sample_period`. The record's epoch
  was `15:50:00Z`, which satisfies `epoch % 2100 == 0` exactly, so it covers
  **15:15:00Z -> 15:50:00Z** — a window that legitimately began 27 minutes
  before the run started at 15:41:44Z. The buckets preceding the run are simply
  zero. Nothing is misdated, in the firmware or the downloader.

  **`BITPERSEC` only looked correct because its window is short.** Identical
  mechanism, `sample_period` 60 s, so its overhang before the run start is at
  most 60 s — measured at 38 s. **The overhang scales with `sample_period`**,
  which is 35x larger in the default configuration.

  So: expect a `BITSPERFIVEMIN` download to begin up to 2100 s before the run,
  and a `BITSPERFOURMIN` one up to 1920 s. That is the design. The only
  genuinely arguable point is cosmetic: `sqlitelog/bittag.cc` labels each bucket
  with its **end** time (`timestamp - bucket_period * (bucket_number - 1 - i)`),
  so the first bucket's own start — `15:15:00Z` here — never appears as a row.
  Consistent, and not worth changing without a reason.

- **Pre-run buckets are correctly empty, and that is verified.** A stored
  record carries one timestamp for all the buckets it covers (7 x 9 = 63 bits
  in a 64-bit word), so a record written on the absolute grid legitimately
  spans time before the run began — and the buckets covering that time read
  **0.0**, which is what they must. Confirmed on the 2026-10-07 default run:
  the five buckets ending 15:20:00Z to 15:40:00Z, all before the 15:41:44Z
  start, are all zero.

- **GAP: the activity path was never shown to respond.** Every activity value
  in every BitTag run on 2026-10-07 is zero — 1860, 1860 and 7 records across
  the three runs — because the tag sat undisturbed. **A dead activity path and
  a stationary tag produce identical data**, so none of these runs distinguishes
  them. What was verified is that records are written on schedule with correct
  counts, spacing and coverage; what was *not* verified is that the recorded
  values track reality.

  The test is short and needs an operator: start a run, move the tag for a
  known interval, stop, and confirm non-zero buckets at the times it was moved
  and zeros either side. At `BITPERSEC` a minute of movement is enough to place
  it to the second. Until then, read this qualification as covering power and
  record-keeping, not activity sensing.
