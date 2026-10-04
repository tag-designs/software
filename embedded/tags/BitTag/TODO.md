---
type: worklist
status: current
summary: Open BitTag power-qualification work -- tool support, unmeasured phases, and a release-check bound.
---

# BitTag TODO

- **Give `tag_lifecycle_check.py` an attach retry.** The first attach to a
  sleeping BitTag fails and wakes the part, so the tool dies at
  `[1/5] reset to idle` and Phase A of the
  [power test plan](design/power-test-plan.md) has to be run by hand.
- **Run Phase B2 (format independence, `bittag-default.json`) and Phase D
  (activity sensitivity).** Neither was run in the first qualification.
- **Set a run-current bound and add BitTag to `tag_release_check.py`** once a
  second board or session agrees with the first. About 0.584 uA (1.15x the
  measured 0.5082 uA) would match IMUTag's margin. Release-check coverage for
  every tag is tracked in
  [next-release-todo.md](../design/next-release-todo.md).
- **Sweep current against cell voltage**, from a fresh cell down to the 2.00 V
  firmware floor. BitTag has no regulator, so nothing measured at 2.5 V
  transfers to another voltage.
