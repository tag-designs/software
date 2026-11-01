---
type: worklist
status: current
summary: Open Tag Monitor work - check on hardware that Quit closes the window when the tag has hung.
---

# Tag Monitor TODO

Current behaviour is in [README.md](README.md). Delete an item when it is done.

- **Tag Monitor must quit even when the tag has hung.** On 2026-11-01 a PresTag
  running a `main` image (`afa3a6f8`) was sent Start, Stop, Erase and
  Synchronize Clock in one session; the clock set hung the tag in
  `rtc_enter_init()` with the monitor masked (the RTC relock fixed on branch
  `bittag-prestag-fixes`). Tag Monitor then showed `IDLE (no ans...)` and would
  not quit; it had to be killed. **Fixed, not yet tried on a hung tag:** Quit
  still detaches first, but exits after `QUIT_DETACH_WAIT` (5 s,
  `core/src/engine/monitor.rs`) if the detach has not come back, with a
  warning in the Error Log. To check: hang a PresTag on an image older than
  `f25ac618` by the sequence above, quit, and confirm the window closes in
  about 5 s and the next attach recovers the tag.
