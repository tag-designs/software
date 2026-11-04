---
type: results
status: current
summary: Append-only log of UIUCTag power measurements -- resting states good; a reattach that boots as a power init stopped collection until the 2026-11-03 fix; the 2026-11-04 mailbox image qualifies; firmware 2.0 (minute records) runs at 0.79 uA.
---

# UIUCTag Power Measurement Log

**Append-only.** Each completed measurement gets one entry with a timestamp,
the conditions it was taken under, and the numbers. Never edit or delete an
entry — a measurement that turned out to be wrong gets a later entry saying
so, because the wrong ones are how the reasoning is reconstructed.

Procedure: [`power-test-plan.md`](power-test-plan.md), and the shared
[power testing procedure](../../../../docs/bench/power-testing.md).

## Format

```
### YYYY-MM-DD HH:MM  <short title>
- **build**: release tag and image SHA-256, or git hash and whether dirty
- **board**: UUID, and which physical unit if it matters
- **conditions**: supply, what was attached, settle time, window
- **result**: the numbers
- **notes**: anything that qualifies them
```

Rig, unless an entry says otherwise: ST-Link, Joulescope JS220 via
`joulescope_server.py --use-server`, `charge/time` figure, supply ~2.496 V,
qtmonitor and the Joulescope desktop app detached.

---

_No measurements recorded yet._

### 2026-10-06  `fw-v0.6` qualification attempt — **VERDICT RETRACTED**

> **The FAIL below is wrong. There is no wake fault; the measurement caused
> it.** The operator afterwards ran this same image while watching a Joulescope
> current trace, with nothing connected to the tag: it woke on the minute mark
> and wrote on the 300 s grid, which is the designed behaviour. Every run of
> mine that reported otherwise had attached to a RUNNING tag, and an attach
> connects through reset.
>
> **The resting-state figures stand** — they were taken on an idle tag, which is
> where attaching is safe, and they are the tightest readings of any tag this
> week. **The `RUNNING` row, the download row and the verdict do not.**
>
> No qualification has been completed for UIUCTag: the harness cannot drive
> this target (below) and the by-hand runs were invalid. That is missing
> evidence, not a known fault.

- **image**: `fw-v0.6` (`56e5e6a0`), downloaded from the GitHub release, not
  rebuilt. `UIUCTag.elf` sha256
  `e77f5d7b7b956d3a294eb4dcbd979b89d2cbd20a1a97ef6ba9a8fb75692c0c66`, flashed
  with `flash_release.py` against its in-archive manifest: tree clean,
  toolchain 14.2.1.
- **board**: `2036354B3032500800520028` — the board the bring-up report used.
- **conditions**: supply 2.4960 V, unregulated 2.5 V cell, shipped default
  config (300 s sample period). **No monitor attach inside any measurement
  window.**
- **result**:

  | State | Gate | Measured | Verdict |
  | --- | --- | ---: | --- |
  | `IDLE`, four trials | ≤ 0.4 µA | 0.1583 / 0.1583 / 0.1583 / 0.1584 µA | **pass**, 0.06% spread |
  | `IDLE`, clock set | ≤ 0.4 µA | 0.1581 µA | **pass** |
  | **`RUNNING`, 1800 s** | **above the floor, and collecting** | **0.2409 µA, 0 samples** | **FAIL** |
  | `FINISHED` | ≤ 0.4 µA, within 20% of `IDLE` | 0.1572 µA | **pass** |
  | `IDLE` after cycle | ≤ 0.4 µA | 0.1572 µA | **pass** |
  | download | samples 300 s apart | **"No log records to download"** | **FAIL** |
  | attach storms | — | **not run** (see below) | — |

- **verdict**: **FAIL. `fw-v0.6` is not qualified for UIUCTag, and must not
  fly.** Six samples were due in the 1800 s window and none was written;
  `external_pages` stayed 0 and the download is empty. Every resting state is
  excellent — the idle readings are the tightest of any tag measured this week
  — but a tag that rests beautifully and records nothing is a failure, not a
  near miss.

- **The current corroborates it without any register evidence.** Bring-up
  measured **0.76 µA** running, undisturbed, on this same board; this run
  measured **0.2409 µA**. The ~0.5 µA shortfall is the sample writes that did
  not happen, at 248 µJ each. `RUNNING` sits only 0.083 µA above `IDLE`, which
  is the ADXL367 watching for motion: **the tag is armed for activity and not
  for time.** It wakes when shaken; the RTC minute alarm does not fire.

- **The harness could not run this target.** `tag_release_check.py` aborted at
  the life-cycle step with `Monitor attach failed: initial DEMCR read failed`
  — the attach-from-sleep problem that also stops it driving BitTag. The
  [test plan](power-test-plan.md) listed this as unknown for UIUCTag; it is now
  known. The phases were run by hand as the plan prescribes, each command
  issued twice.

- **Attach storms were deliberately skipped** (`--storm-sets 0`). They attach
  during a run, which on this target would confound the question being asked.
  They remain untested here and should be run once the wake fault is fixed.

- **notes**: earlier runs on this board were polled with `tag-info` during
  collection, which resets the tag; those numbers are withdrawn and are not the
  basis of this entry. See [the worklist](../TODO.md).

### 2026-10-06  `fw-v0.6` functional run — **collects correctly**

- **image**: `fw-v0.6` (`56e5e6a0`), flashed from the GitHub release.
- **board**: `2036354B3032500800520028`. **conditions**: 2.496 V, shipped
  default config, started 21:06:51Z and stopped 21:28:40Z.
- **method**: started once, then **nothing attached for the whole run**. The
  operator watched a Joulescope current trace, which needs no connection, and
  saw it wake on the minute mark and write on the 300 s grid. The tag was
  attached only to stop it, which is one of the two reasons to attach a running
  tag. The download below is a second, independent check on the same run.
- **result**: every slot present, none missed.

  | Stream | Records | Spacing | First → last |
  | --- | ---: | ---: | --- |
  | `Activity` | 20 | **exactly 60 s** | 21:07:00Z → 21:26:00Z |
  | `Pressure` | 5 | **exactly 300 s** | 21:07:00Z → 21:27:00Z |
  | `Temperature` | 5 | **exactly 300 s** | 21:07:00Z → 21:27:00Z |

  The run began at 21:06:51 and the first sample is at 21:07:00 — the first
  minute boundary, which is what the design specifies for the first wake of a
  run. The 60 s activity buckets and the 300 s sample grid are both exact.

- **verdict**: **UIUCTag collects correctly on `fw-v0.6`.** This settles the
  question the three invalid runs above could not. It is not a qualification --
  no current was measured during this run, because the Joulescope had been
  released to observe it -- but it is unambiguous on function.

- **`external_pages` is not evidence of what a run collected.** It read **0**
  immediately after the stop, in the same session whose download returned five
  samples and twenty activity buckets. It is the live counter during a run and
  cannot be trusted once a run has ended. **Use the download.** Reading it as
  "nothing was stored" is a large part of how this tag was wrongly written off,
  and `tag_release_check.py`'s own download check already does the right thing.

- **what a qualification still needs**: a clean `RUNNING` current over a window
  of several sample periods, with nothing attached. The resting states are
  already measured (previous entry) and are good. `tag_release_check.py` cannot
  drive this target, so the run phase must be taken by hand: start once, leave
  it alone, measure, then stop.

### 2026-10-08  `UIUCTag` release qualification, `fw-v0.6` — **PASS**

- **image**: `fw-v0.6` (`56e5e6a0`), flashed from the published release;
  identity read back before and after.
- **board**: `2036354B3032500800520028` — the board of the bring-up report.
- **conditions**: **supply verified at 2.4960 V before powering the tag.** The
  rig had been left at 3.6932 V from an IMUTag session; at that voltage this
  tag read 328 µA and reported 3.30 V internally, and the STM32L432's absolute
  maximum VDD is 3.6 V. Corrected first; no harm resulted. Shipped default
  config, 300 s sample period. Measured by hand — the harness cannot attach to
  this tag from sleep.
- **result**:

  | Point | Gate | Measured | Verdict |
  | --- | --- | ---: | --- |
  | supply | 2.45–2.55 V | 2.4960 V | **pass** |
  | `IDLE`, clock set | ≤ 0.4 µA | 0.1572 µA | **pass** |
  | **`RUNNING`, 1800 s** | above the floor, collecting | **0.5620 µA** | **pass** |
  | `FINISHED` | ≤ 0.4 µA, within 20% of `IDLE` | 0.1598 µA | **pass**, 1.7% from idle |
  | `IDLE` after a full cycle | ≤ 0.4 µA | 0.1598 µA | **pass** |
  | download, pressure | 300 s apart | 7 records, **exactly 300 s**, 16:23:00→16:53:00Z | **pass** |
  | download, temperature | 300 s apart | 7 records, **exactly 300 s** | **pass** |
  | download, activity | 60 s buckets | 30 records, **exactly 60 s** | **pass** |
  | `tag-test` | `ALL_PASSED` | `ALL_PASSED` | **pass** |

- **verdict**: **PASS. `fw-v0.6` is qualified for UIUCTag.** The run began
  16:22:35Z and the first sample landed 16:23:00Z — the first minute boundary,
  as the design specifies — with no slot missed in 31 minutes. `tag-start`
  confirmed `RUNNING` in-session, which the fixed `--start-timeout` now makes
  possible.

- **Running current is 26% below the bring-up figure**, 0.5620 µA against the
  0.76 µA measured undisturbed on this same board on 2026-09-26. Lower is not a
  budget concern and this is not treated as a finding, but the difference is
  real and unexplained. The likeliest cause is accelerometer configuration
  rather than the firmware: the bring-up session was still resolving an
  activity-pegged ADXL367 and a threshold that was 2x off, and a pegged
  activity line costs current continuously. Worth confirming the next time this
  board runs, by comparing the activity data rather than the current.

- **`tag-test` is intermittent on this target**, as recorded on 2026-10-06: it
  returned no result on the first attempt here and `ALL_PASSED` on the second,
  matching the 4-of-6 pass rate measured then. The failures are host-side link
  timeouts at the first transaction, not device-test failures.

### 2026-11-02  `tag_release_check.py` end to end, main `12af820d` — **PASS**

- **image**: main `12af820d` (the L4 monitor fix and the RTC relock removal on
  top of `fe7e35b5`), built locally; the check ran with `--skip-build` on the
  flashed image.
- **board**: `2036354B3032500800520028` (the breakout), on
  `tag-breakout-base-l432-v1` (fw `be8f9062`, a dirty build of 2026-10-28).
- **conditions**: supply 2.4961 V, checked before the first attach. Shipped
  default config (`embedded/tools/power-configs/uiuctag-default.json`).
- **result**: the new UIUCTag profile, with no hand steps:

  | Point | Gate | Measured | Verdict |
  | --- | --- | ---: | --- |
  | `IDLE`, four 120 s trials | ≤ 0.3 µA | 0.1512 / 0.1508 / 0.1516 / 0.1516 µA | **pass** |
  | `RUNNING`, 1800 s | 0.3–0.646 µA | 0.55 µA | **pass** |
  | `FINISHED` | ≤ 0.3 µA | 0.15 µA | **pass** |
  | `IDLE` after a full cycle | ≤ 0.3 µA | 0.15 µA | **pass** |
  | download, activity | 1/60 Hz, ≥ 0.75x | 30 rows, 1.00x | **pass** |

- **A/B against the previous image**: resting current 0.1521 µA on `fe7e35b5`
  and 0.1507 µA on `12af820d`, 120 s each, so the two firmware changes cost
  nothing at rest. Running current is within 2% of the 0.5620 µA qualification.
- **Monitor checks**: `tag-test` passed six times out of six with no base
  0x81, and Tag Monitor's `session_check` passed every step; see the L4
  monitor item in `../../TODO.md`.
- **notes**: the profile's run floor (0.3 µA) also fails the "wakes but does
  not collect" run of 2026-10-06, which read 0.24 µA. The harness's first
  `tag-info` after start printed `Info failed` once and the retry read
  `RUNNING`; possibly the same system-bus loss as the self-test poll below,
  not investigated.

### 2026-11-03  `7079bf32` fails: an attach while awake stops collection; fixed — **PASS with the fix**

- **images**:
  - `sync-monitor-commands` `7079bf32` (SHA-256 `9cdede8af3ce7e5d…`), the image
    that failed;
  - fw-v0.7 (`169cda2c`), the control;
  - `7079bf32` plus the re-arm fix in `../src/state_run.c` (SHA-256
    `49a5246ebeb1ec63…`). This image was built in a clean worktree and reports
    githash `7079bf32`, so the SHA is what identifies it. Only `Running()`
    differs from `7079bf32`.
- **board**: `2036354B3032500800520028` (the breakout), on
  `tag-breakout-base-l432-v1`.
- **conditions**: supply 2.4960 V, shipped default config.
- **result, `7079bf32`**: `tag_release_check.py` failed. The 1800 s run read
  0.244 µA with no activity rows, the "wakes but does not collect" signature.
- **A/B, 1200 s each**. The harness polls `tag-info` straight after
  `tag-start`, a single attach that lands about a second into the run:

  | Image | Attach after start | Run current | Activity / pressure rows |
  | --- | --- | ---: | --- |
  | fw-v0.7 | yes | 0.5629 µA | 20 / 5 |
  | `7079bf32` | yes | 0.2434 µA | 0 / 0 |
  | `7079bf32` | no | 0.5629 µA | 20 / 5 |

- **cause, confirmed**:
  - **The trace.** A Joulescope trace with no attach shows the tag awake for
    about 1.5 s after `tag-start`. It then settles at its 0.42 µA run floor
    and wakes on the minute mark.
  - **Awake versus Standby.** An attach inside that window resets an awake
    tag, so the Standby flag is clear and `getResetCause()` classifies the
    boot as `resetPower`. The L432 `deviceInit()` power-init path then calls
    `disableAllAlarms()`.
  - **No re-arm.** Recovery sees a reattach and resumes with
    `Running(T_CONT, POWERFAIL)`, which re-armed nothing.
  - **The two cases.** On `7079bf32`, with a 420 s trace each, then a
    `tag-capture` that halts the core before any firmware runs:

    | Attach | Minute wakes | `RTC_CR` | Rows |
    | --- | --- | --- | --- |
    | at once | none, median 0.2425 µA | `0x00000020`: ALRAE and ALRAIE clear | none |
    | after 10 s, in Standby | present | `0x00001120`: both set | 5 activity, 2 pressure |

  - **Why fw-v0.7 escaped.** Its `tag-start` polled in the same session until
    RUNNING, so the harness's attach came later. By then the tag was in
    Standby, and an attach from Standby boots as `resetStandby` and keeps its
    alarm. The branch's synchronous reply ends `tag-start` at once, which
    moved the attach into the awake window. The defect itself is older:
    the 2026-10-06 register reads of `ALRAE = 0` after attaches to a running
    fw-v0.6 tag were this defect. They were not, as that day's retraction
    above concluded, only an artefact of the measurement.
- **fix**: `Running(T_CONT)` now re-initialises the accelerometer and re-arms
  the minute alarm for POWERFAIL as well as EXCEPTION, as PresTag has done
  since `a406eda7`. The sequencer simulation's reattach phase fails without
  the fix and passes with it.
- **result, with the fix**:
  - `tag-test`: `ALL_PASSED`.
  - Attach at once: minute wakes, `RTC_CR` = `0x00001120`, 5 activity and
    2 pressure rows in 420 s, the same as the attach after 10 s.
  - `tag_release_check.py`:

    | Point | Gate | Measured | Verdict |
    | --- | --- | ---: | --- |
    | `IDLE`, four 120 s trials | ≤ 0.3 µA | 0.1584 / 0.1584 / 0.1584 / 0.1582 µA | **pass** |
    | `RUNNING`, 1800 s | 0.3–0.646 µA | 0.56 µA | **pass** |
    | `FINISHED` | ≤ 0.3 µA | 0.16 µA | **pass** |
    | `IDLE` after a full cycle | ≤ 0.3 µA | 0.16 µA | **pass** |
    | download, activity | 1/60 Hz, ≥ 0.75x | 30 rows, 1.00x | **pass** |

- **notes**: `tag-flash` printed "Programming failed" during verify while
  flashing `7079bf32`. The tag then reported `7079bf32` and passed
  `tag-test`. The fix image flashed cleanly. Not investigated.

### 2026-11-04  the monitor on the shared-memory mailbox, `4b409426` — **PASS**

- **setup**: the same breakout, UUID `2036354B3032500800520028`, on
  `tag-breakout-base-l432-v1` at 2.496 V. Image `4b409426` (SHA-256
  `a8c29c23fe4aa2e8…`): the re-arm fix plus the L4 mailbox
  (`TAG_L4_MONITOR_MAILBOX`). Host tools built from the same commit.
- **functional**:
  - `tag-info -d`: monitor version 0x21, session over the block, path magic
    `0x554C0004`.
  - `tag-test` 6 of 6 `ALL_PASSED`, then 5 cycles of `tag-reset --set-rtc`
    plus `tag-test`: no base `0x81`. The DebugMonitor image (`49a5246e…`) over
    the same 5 cycles: one `0x81`, a DEMCR read. Neither image showed the
    NANOPB lost request any more; the host's DMA1 clock fix removed that from
    both.
  - Attach straight after `tag-start`, 420 s, then capture and download:
    minute wakes, `RTC_CR` = `0x00001120`, 5 activity and 2 pressure rows, the
    same as the DebugMonitor image.
  - A v4.0 host fails its attach (`Monitor Start failed`) in under 0.6 s.
- **current after an attach**, 1 s windows:
  - after a clean session: 0.155 µA from the first window;
  - after a session left idle until the heartbeat dropped it: 0.1543 µA;
  - after the v4.0 host's refused attach: about 355 µA for 10 s (the attach
    grace), then 0.155 µA. About 1 µAh per refused attach.
- **`tag_release_check.py`**:

  | Point | Gate | Measured | Verdict |
  | --- | --- | ---: | --- |
  | `IDLE`, four 120 s trials | ≤ 0.3 µA | 0.1544 / 0.1545 / 0.1545 / 0.1546 µA | **pass** |
  | `RUNNING`, 1800 s | 0.3–0.646 µA | 0.56 µA | **pass** |
  | `FINISHED` | ≤ 0.3 µA | 0.16 µA | **pass** |
  | `IDLE` after a full cycle | ≤ 0.3 µA | 0.16 µA | **pass** |
  | download, activity | 1/60 Hz, ≥ 0.75x | 30 rows, 1.00x | **pass** |

- **notes**:
  - The life-cycle log has two `0x81` lines, one in the state check after
    the start and one in the first DebugMonitor write of the reset's attach.
    Both steps went on to pass (the attach retries). The 2026-11-03
    DebugMonitor run had none. Not yet investigated.
  - `tag-start` replied `CONFIGURED`. UIUCTag starts on the next minute
    alarm, and the reply may report either state.
  - `214fc7fe`, which moves the mailbox images to `DEBUGVERSION_MAILBOX`,
    then flashed: version 0x21 over the DebugMonitor and in the block,
    `tag-test` 3 of 3 with no `0x81`. Not release-checked; the change is
    two constants.

### 2026-11-04  firmware 2.0, one 8-byte record a minute — **0.79 µA running**

- **build**: main `cc3f799e` plus the uncommitted firmware 2.0 working tree
  ([decision 0044](../../../../docs/decisions/0044-uiuctag-minute-records.md)).
  Two images, from the same tree a few minutes apart:
  - `UIUCTag-minute-v2.elf`, SHA-256 `8d8173c0…`: the intermediate field
    order, pressure first.
  - `UIUCTag-minute-v2-actfirst.elf`, SHA-256 `94e55950…`: the shipped order,
    activity first. 80 bytes less text; otherwise the same per-minute work,
    one BMP585 forced conversion and one aligned 8-byte program.
- **board**: UUID `2036354B3032500800520028`, on `tag-breakout-base-l432-v1`.
- **conditions**: 2.496 V. `tag_lifecycle_check.py --use-server`, config
  `uiuctag-default.json`, settle 75 s, rest 120 s, run gate widened to
  0.3–3.0 µA so the run was measured rather than judged. Each image flashed
  after `tag-flash erase`, then `tag-test` `ALL_PASSED`.
- **result**:

  | Point | Pressure first, 6000 s run | Activity first, 2400 s run |
  | --- | ---: | ---: |
  | `IDLE`, clock set | 0.16 µA | 0.16 µA |
  | `RUNNING` | 0.79 µA | 0.79 µA |
  | `FINISHED` | 0.16 µA | 0.16 µA |
  | `IDLE` after a full cycle | 0.16 µA | 0.16 µA |
  | download | failed, host side (below) | pass |

  The activity-first download: 41 pressure and temperature rows exactly 60 s
  apart (977.1–977.5 hPa, 23.6–24.0 °C), 120 activity rows exactly 20 s apart
  (40 observed minutes; the run's first minute correctly unobserved, all 0 %
  on a still bench), and two checkpoints 1920 s apart, the second block opened
  on schedule.
- **notes**:
  - Running current rises from 0.56 µA (five-minute firmware, 2026-11-04
    mailbox entry) to 0.79 µA: about 0.23 µA for a pressure conversion every
    minute instead of every five. The release check's
    UIUCTag `run_max_ua` was raised from 0.646 to 0.8 µA by user decision;
    that leaves about 1 % above this figure, against 15 % for the other tags.
  - The first attempt aborted at `tag-start` with an empty error. The image
    shrank, so `.persistent` moved from `0x0800B800` to `0x0800B000`, onto a
    page `tag-flash program` did not erase; the stored-config verify failed
    and the start went to `ABORTED` (`CONFIGERROR`). Captured
    (`capture-20261104-155952`), then `tag-flash erase` before programming.
    `tag-start` prints its refusal on stdout, so the check showed no reason.
  - The pressure-first download failed because `tag-dwnld` had not been
    rebuilt and still carried the 12-byte decoder ("expected a multiple of 12
    up to 288"). The check's final reset then erased the log, so that run's
    records are lost; its current figures are unaffected.
  - Activity counts above 0 were not exercised on the bench; the simulations
    and host checks cover them.
