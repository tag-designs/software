---
type: results
status: current
summary: Append-only log of UIUCTag power measurements -- resting states measured and good; the 2026-10-06 run verdict was retracted after the apparent fault proved to be the measurement.
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
