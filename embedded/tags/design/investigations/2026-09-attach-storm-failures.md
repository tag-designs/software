---
type: investigation
status: closed
summary: Four attach-storm failures chased in September 2026 -- non-monotonic timestamps, refused downloads, STATE_UNSPECIFIED after reset, and an unserviced stop -- three host-tool faults and one firmware fix.
---

# Attach-Storm Failures, September 2026

Closed 2026-09-07. Three of the four were host-tool faults (the timestamp
check, fixed in 9754dc74 on 2026-09-06; `tag-stop`, `tag-reset` and `tag-dwnld`
settling, the last in a71752f); one was firmware, fixed in 928093d. Cut
verbatim from [Open Issues](../../TODO.md); "AGENTS.md" in the text is the root
agent guide as it then stood.

### RESOLVED: the non-monotonic timestamps were a bug in the check

Filed as "one 30-cycle attach storm produced 4 backwards `ElapsedUs` steps in
7050 rows, an identical repeat produced none", and left strict on purpose.

The tag was never wrong. `check_download()` in `embedded/tools/power_experiment.py`
chose its timestamp column by walking the table's columns in declaration order
and taking the first recognised name. In `ImuAccel` that is `RawElapsedUs`,
which comes before `ElapsedUs`, and `RawElapsedUs` is documented in
`host/libraries/tagcore/sqlitelog/README.md` as *uncorrected elapsed
microseconds from the segment start*. It restarts at zero in every segment by
design, so it steps backwards once per segment boundary.

On one kept database, checked both ways:

| column | backwards steps |
| --- | --- |
| `RawElapsedUs` (what the tool measured) | 5 |
| `ElapsedUs` (the corrected series) | 0 |

Every step was exactly a segment boundary, and the count always equalled the
number of `RESTART_RECOVERY` events -- 5 recoveries, 5 steps. The corrected
series ran straight through: `0..5997500`, then `11407000..14404500`, then
`33016000..36013500`, and so on.

This also explains the two observations in the original note that did not fit a
firmware fault. An undisturbed run was "clean" because it has a single segment
and therefore no restarts. And `ElapsedUs` really does continue monotonically
across `RESTART_RECOVERY` boundaries -- that was checked by hand and was
correct; it simply was not the column the tool was reading.

Fixed by selecting the timestamp column from an explicit preference list, most
corrected first, with segment-relative columns last.

Two things were ruled out along the way with the retained scratchpad, and are
worth not re-chasing:

- **The RTC is not read stale on recovery.** `restartDataCollectionClock()`
  re-bases each segment from the wall clock. Recording the epoch and
  millisecond actually used, across 625 recoveries in three storm sets that all
  reported failures, the base was strictly increasing every time.
- **`restoreLog()` is not rewinding the write cursor.** It ran on 2 of 45 boots
  in a storm round that produced 5 recoveries, so it is not on the recovery
  path at all.

### RESOLVED (host side): the intermittent download failure

`tag-dwnld` failed intermittently after a storm round with "Can't dump logs
from current state". The download was right to refuse: the tag really was still
RUNNING. The fault was in `tag-stop`.

`Tag::Stop()` returns true when the request is *acknowledged*, and the monitor
handler only does `*work |= MON_WORK_STOP` -- the state machine makes the
transition later. `tag-stop` read the status immediately afterwards, saw
RUNNING, printed it and exited 0, reporting a stop that had not happened.
`tag-reset` already polls for IDLE after `Erase()` for exactly this reason;
`tag-stop` never got the same treatment.

It now polls for FINISHED or ABORTED at two-second intervals -- each poll is a
monitor request the tag must service, so hammering the link competes with the
work being waited on -- and fails with the last state seen.

**A second cause, found 2026-09-07 (a71752f):** the same message also came
from `tag-dwnld` itself, on a tag that *was* FINISHED, with an immediate retry
succeeding. `tag-dwnld` read the status once after attaching, and attach
connects under reset, so that read could be STATE_UNSPECIFIED while the tag
booted. It now settles like the other tools, and with `--stop` polls for
FINISHED instead of reading once after a posted `Stop()`. With that, every
`tag-*` tool that judges the tag's state waits for a definite one first.

### RESOLVED (host side): STATE_UNSPECIFIED after reset

Filed as three of twenty reset-and-set-clock cycles reporting
`STATE_UNSPECIFIED` rather than `IDLE`.

`Tag::Attach()` connects under reset, so the tag is still booting when the
host's first request arrives, and `pState->state` is legitimately zero until
the state machine restores it. `tag-reset` issued `GetStatus` immediately after
attach and took that transient zero as the tag's state: it matched neither
RUNNING/HIBERNATING nor FINISHED/ABORTED, so the erase was skipped and it
printed "Final state: STATE_UNSPECIFIED". The `SetRtc` issued next hit the same
unsettled tag and was rejected, which is the neighbouring "SetRtc failed".

The tag was never at fault. At a captured failure the retained state was
healthy -- `valid` was BACKUP_STATE_VALID_MAGIC and `state` was IDLE -- the
tag's own `run_diag` reported `state=2` throughout, and `statusDiagWriteFast()`
never fired, so the fast status path never saw a zero either.

`tag-start` already knew this; its comment says attach needs a round trip to
settle. `tag-reset` now polls for a definite state after attach, at one-second
intervals, with `--settle-timeout`. Twenty rapid reset cycles then ran clean
against one to four failures per ten before, and 60 clock cycles across six
storm sets ran 60/60.

### RESOLVED: a stop request could go unserviced for at least 30 s

**Fixed in 928093d (2026-09-07):** `MON_WORK_ALL` is now in the RUNNING
state's event-wait mask, so a posted stop wakes the main thread on its own.
Qualified twice with `tag_release_check.py`; the storm failures on those runs
were host-tool settle races, fixed in 43119cb and a71752f. The original
analysis follows.

With `tag-stop` waiting properly, one storm set in six failed with "Tag did not
reach a stopped state within 30 s; last state RUNNING". The tag genuinely did
not act on the stop. This was previously invisible: `tag-stop` exited 0 and the
symptom surfaced later as a confusing download refusal.

The suspect is the event wait for the RUNNING state in `main.c`:

```c
eventmask_t wait_events = EVT_HARDWARE_ALL;
if (isMonitorEnabled())
  wait_events |= EVT_MONITOR_ALL;
pending_events = chEvtWaitAny(wait_events);
```

`MON_WORK_ALL` is not in the mask, so a posted `MON_WORK_STOP` cannot wake the
main thread on its own. It is collected by the
`chEvtGetAndClearEvents(MON_WORK_ALL)` at the top of the loop, but only once
something else has woken it -- normally the monitor request's own
`EVT_MONITOR_*`, which is masked in only while `isMonitorEnabled()` is true.

Not proven. At 400 Hz the IMU should wake the loop far sooner than 30 s, so
either that wake path is also blocked or `isMonitorEnabled()` is false at that
moment. Reproduced once in six sets. Adding `MON_WORK_ALL` to the mask is the
obvious change, and it belongs to the state-machine path that AGENTS.md says
must be measured rather than argued.
