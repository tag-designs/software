---
type: worklist
status: current
summary: Open BitTag power-qualification work -- adding BitTag to the release check.
---

# BitTag TODO

- **Add BitTag to `tag_release_check.py`.** The run-current bound is set:
  a second session (2026-11-01, same board, 2.4960 V) read 0.5081 uA at
  `BITPERSEC` against the first's 0.5073 uA, so the plan's life-cycle command
  now passes `--run-max-ua 0.584` (1.15x, IMUTag's margin). Release-check coverage for
  every tag is tracked in
  [firmware TODO](../TODO.md).
