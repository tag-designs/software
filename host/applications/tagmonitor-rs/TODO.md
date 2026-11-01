---
type: worklist
status: current
summary: Open Tag Monitor work - quitting while the tag does not answer.
---

# Tag Monitor TODO

Current behaviour is in [README.md](README.md). Delete an item when it is done.

- **Tag Monitor must quit even when the tag has hung.** On 2026-11-01 a PresTag
  running a `main` image (`afa3a6f8`) was sent Start, Stop, Erase and
  Synchronize Clock in one session; the clock set hung the tag in
  `rtc_enter_init()` with the monitor masked (the RTC relock fixed on branch
  `bittag-prestag-fixes`). Tag Monitor then showed `IDLE (no ans...)` and would
  not quit; it had to be killed. Quitting should not wait on an RPC the tag
  will never answer: cancel or abandon the call in flight, detach without
  waiting for the tag, and exit.
