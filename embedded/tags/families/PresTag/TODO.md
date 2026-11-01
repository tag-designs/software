---
type: worklist
status: current
summary: Open PresTag items -- the config write that can still be refused (F3), the silent Stop 2 no-op (F2), the brownout regression test, and measurements the hourly hibernation alarm made stale.
---

# PresTag TODO

- **F3: the config write can still be refused.** Part 1 of
  [the proposal](../../design/proposals/stored-config-write-is-unchecked.md)
  landed in `dee8654d` (`fw-v0.6`): `writeStoredConfig()` (`src/config.c`)
  now reads the write back with `storedConfigVerify()` and the start aborts on
  a mismatch, so a stale `sconfig` is a loud refusal rather than a tag running
  9 s when `tag-start` printed `period: 10`. The write itself can still fail:
  `sconfig` shares its page, so it cannot be erased first. Part 2, its own
  page, is tracked in [the firmware worklist](../../TODO.md). Still open here:
  `erasePersistent()` does not check that its erase happened.
- **F2: make `godown(STOP2)` sleep or refuse.** On the L432,
  `tagPowerEnterTerminalSleep()` handles only Standby and Shutdown and returns
  silently for anything else. So a period under 10 s, where `Running()` returns
  `STOP2`, never sleeps: it measured 530.7 µA flat. Only the bench is affected.
  Implement it, or reject it explicitly.
- **T4: brownout recovery with an odd page count.** This is the regression test
  for the log-cursor fix on the restart path
  ([investigation](design/investigations/2026-09-prestag-log-cursor-round-up.md)).
  It needs a genuine brownout. A probe reset takes a different path and would
  pass while the defect stood. Not run.
- **Re-measure HIBERNATING and repeat H3** since `060a566` made `ALARM_HOUR`
  hourly. The recorded 0.3769 µA and the five-wakes-in-295 s trace describe the
  once-a-minute alarm. Hibernation exit can now take up to an hour after the
  window closes. Check C5's exit expectation against that.
- **Compare absolute pressure against a local station reading.** Only
  plausibility and self-consistency have been shown so far.
- **State the cell's self-discharge figure** so the derated lifetime is not a
  placeholder. It used a 0.75 derating with no datasheet behind it.
- **Measure B1, B4 and B5** (1 s, 15 s and 30 s) if extra fit leverage is
  wanted. They are optional; the field fit already rests on three measured
  points.
