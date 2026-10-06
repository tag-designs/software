---
type: results
status: current
summary: Append-only log of UIUCTag power measurements -- the 2026-10-06 fw-v0.6 attempt, which failed because the run collects nothing while every resting state passes.
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

### 2026-10-06  `fw-v0.6` qualification attempt — **RESULT UNDER RE-TEST, DO NOT RELY**

> **The run in this entry was not undisturbed, despite what it says below.**
> The script issued `tag-start` twice (the `twice` helper, so the second
> attached to an already-RUNNING tag) and then ran `tag-info` to confirm
> RUNNING. Both connect through reset, so the run was reset once or twice at
> its start. That is the very mechanism under investigation, so this entry
> cannot distinguish the fault from the measurement. The resting-state figures
> are unaffected and stand; **the `RUNNING` row and the FAIL verdict do not.**
> A genuinely undisturbed run -- single `tag-start`, its own reported status
> used as confirmation, no attach until the run is ended -- is in progress.


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
