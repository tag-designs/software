---
type: worklist
status: current
summary: Open PresTag items -- the unchecked config write (F3), the silent Stop 2 no-op (F2), the brownout regression test, and measurements the hourly hibernation alarm made stale.
---

# PresTag TODO

- **F3: check the flash status on the config write.** `writeStoredConfig()`
  (`src/config.c`) ignores `FLASH_Program_Array()`'s result, and
  `erasePersistent()` never checks that its erase happened. On an L4 a program
  into a double word that has not been erased is refused, so a stale `sconfig`
  can survive a reset-and-start. It was seen once: `tag-start` printed
  `period: 10` while the tag ran at 9 s. This is the reason the 2026-09
  campaign is not a release qualification.
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
