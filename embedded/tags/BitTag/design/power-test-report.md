---
type: investigation
status: superseded
superseded-by: embedded/tags/BitTag/design/power-results.md
summary: First BitTag power qualification at fw-v0.5: a pass at 0.12 uA resting and 0.51 uA running, and four config defects found.
---

# BitTag Power Test Report

One block per session. Numbers live in
[`power-results.md`](power-results.md); this is what they mean and
what went wrong getting them. Procedure:
[`power-test-plan.md`](power-test-plan.md).

## 2026-10-03 — first qualification, fw-v0.5

**PASS.** BitTag sleeps at **0.122 uA** and records at **0.508 uA** from a
2.496 V cell. Every gate met, by wide margins.

### What the numbers say

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

### What went wrong, and what it cost

Four defects, none of which review had caught. Three were in work produced
earlier the same day and were found only by running it.

**The configs named the wrong tag type.** `BITTAG` rather than `BITTAG_LE`.
The JSON had been validated against the `Config` protobuf and passed, because
`BITTAG` is a valid enumerator — validation proves a config parses, not that it
matches the firmware. Caught by comparing `tag-info` against the config.

**The configs omitted `active_interval`**, so `end_epoch` programmed as 0 and
the first run ended **one second** after it started:
`CONFIGURED 18:55:23, RUNNING 18:55:23, FINISHED 18:55:24 EVENT_ENDTIM`, no
data. `tag-start` replaces the stored configuration rather than merging, so the
firmware's `INT32_MAX` default did not apply. Cost one run.

**The first attach to a sleeping BitTag always fails.** `initial DEMCR read
failed`, and the failed attach wakes the part to ~376 uA; the next attach
succeeds. This is repeatable and is the reason
**`tag_lifecycle_check.py` cannot currently drive a BitTag** — it has no attach
retry, so it died at `[1/5] reset to idle` on both attempts and left the tag
awake. Phase A was run by hand instead. Fixing the tool is the obvious
follow-up; it is the same class of gap as the `--use-server` bug fixed in
`ef6033d` before CompassTag's plan could run.

That last one also produced the session's one false alarm: a 376 uA reading on
a tag reporting `IDLE`, which looks exactly like the CompassTag
Standby-decline fault. It was a failed attach holding the part awake. The
distinguishing test is cheap — reset, settle, measure again — and a real sleep
fault survives it while this does not.

**`tag-test` must be run from `IDLE`.** From `FINISHED` it fails with
`SetRtc failed: Monitor request not permitted in current tag state`. The plan
had it after the download with no reset.

### A claim in the plan that the hardware contradicted

The plan says BitTag waits up to 60 s in `CONFIGURED` because
`TAG_CONFIGURED_IMMEDIATE_START` is defined only by the two IMUTag targets, and
specifies `--settle 75` on that basis. The define genuinely is absent, but the
marker log shows `CONFIGURED` and `RUNNING` in the **same second** in all three
starts observed (18:55:23, 18:57:58, 19:26:00). The inference from the source
was wrong. `--settle 75` is retained as cheap insurance, but the stated reason
has been corrected rather than left to look confirmed.

### Follow-ups

- Give `tag_lifecycle_check.py` an attach retry, so BitTag can be qualified by
  the standard tool rather than by hand.
- Phase B2 (format independence) and Phase D (activity sensitivity) were not
  run.
- With a baseline now in hand, a run bound of about **0.584 uA** (1.15x the
  measured 0.5082) would be the IMUTag-equivalent margin.
