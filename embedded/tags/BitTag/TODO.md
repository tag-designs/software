---
type: worklist
status: current
summary: Open BitTag power-qualification work -- tool support, unmeasured phases, and a release-check bound.
---

# BitTag TODO

- **Run Phase D (activity sensitivity).** It has never been run in any
  qualification: start a run, move the tag for a known period, and confirm the
  activity counts respond. Phase B2 (format independence) closed on
  2026-10-07 ([results](design/power-results.md)).
- **Add BitTag to `tag_release_check.py`.** The run-current bound is set:
  a second session (2026-11-01, same board, 2.4960 V) read 0.5081 uA at
  `BITPERSEC` against the first's 0.5073 uA, so the plan's life-cycle command
  now passes `--run-max-ua 0.584` (1.15x, IMUTag's margin). Release-check coverage for
  every tag is tracked in
  [firmware TODO](../TODO.md).
- **Sweep current against cell voltage**, from a fresh cell down to the 2.00 V
  firmware floor. BitTag has no regulator, so nothing measured at 2.5 V
  transfers to another voltage.
