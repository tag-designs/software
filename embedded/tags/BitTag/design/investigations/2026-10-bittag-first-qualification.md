---
type: investigation
status: closed
summary: Observations from the first BitTag power qualification (2026-10-03) cut from the test plan -- the empty earlier attempt, the immediate start, and the failing first attach to a sleeping tag.
---

# BitTag First Power Qualification: Observations (2026-10)

Cut verbatim from [`../power-test-plan.md`](../power-test-plan.md) (the
introduction, section 1 and section 2). The session itself, run on 2026-10-03
against `fw-v0.5`, passed; its numbers are in
[`../power-test-results.md`](../power-test-results.md) and its write-up in
[`../power-test-report.md`](../power-test-report.md).

## Before this plan

There is no BitTag qualification procedure today. The one previous attempt
(`release-checks/release-BitTag-20261001-114455`) measured nothing: it was
invoked with an empty `--config` and `--run-max-ua 1.0`, and with no Joulescope
server running. This plan exists so that cannot happen silently again.

## Start was immediate, not deferred

> **Measured 2026-10-03: the transition was immediate.** The marker log shows
> `CONFIGURED` and `RUNNING` in the same second in all three starts observed
> (18:55:23, 18:57:58, 19:26:00). The define really is absent, so the
> inference above does not describe what this tag does; the mechanism is not
> understood. `--settle 75` is kept as cheap insurance against a start that
> *does* wait, not because a wait has been seen.

## The first attach to a sleeping BitTag fails

Observed throughout the first execution, 2026-10-03, and entirely repeatable:

- With the tag asleep in Standby (~0.12 uA), the first monitor attach fails
  with `Monitor attach failed: initial DEMCR read failed`.
- **That failed attach wakes the part**, which then sits at ~376 uA.
- The next attach, to the now-awake tag, succeeds.

So every host command must be issued **twice** against a sleeping BitTag: the
first is a wake-up that reports failure, the second does the work. A sequence
that resets, measures, and then attaches again will hit this at every attach,
because each measurement leaves the tag asleep.

Two consequences:

- **`tag_lifecycle_check.py` cannot currently drive a BitTag.** It has no
  attach retry, so it fails at `[1/5] reset to idle` every time, reports
  `FAILED`, and leaves the tag awake. Phase A below is therefore run by hand
  until the tool retries. This is the same class of gap as the `--use-server`
  bug that had to be fixed (`ef6033d`) before CompassTag's plan could run.
- **A tag found at ~376 uA has probably just had a failed attach**, not a
  sleep fault. Distinguish them: reset it, let it settle, and measure again.
  A genuine failure to reach Standby survives that; this does not. Both
  readings taken after a clean detach on 2026-10-03 were 0.1224 and 0.1227 uA,
  against 376 uA immediately after a failed attach.
