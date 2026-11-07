---
type: proposal
status: proposed
summary: "Give the internal-flash state-marker log a budget instead of letting it fill -- the run's closing markers (FINISHED/ABORTED, then sRESET) always fit, the rest of the configured schedule is reserved, brown-out restarts and other unscheduled markers use only the spare, and the first refusal writes one overflow marker so a full log is never silent."
---

# A budget for the state-marker log

The state-marker log (`sEpoch[]`, written by `recordState()` in
`common/core/src/persistent.c`) stops recording, silently, once it is full.
Reset recovery reads it after every power loss, so a full log does not only
lose history: it can bring a tag back in the wrong state. This proposes a
budget, with no change to the log's size or layout:

1. **Closing markers always fit.** The last two free slots are kept for the
   markers that end a run: FINISHED or ABORTED, then the sRESET that starts
   the erase.
2. **The rest of the schedule is reserved.** Before writing anything else, the
   firmware works out how many markers the configured schedule still needs
   and keeps that many slots free.
3. **Unscheduled markers use the spare only.** A brown-out restart or another
   unscheduled marker is written only while free slots exceed rules 1 and 2.
4. **A refusal is never silent.** The first refused marker writes one overflow
   marker, and every refusal is counted.

Rule order is priority order. Recovery depends on rules 1 and 2, so nothing
displaces them, not even the first power loss of a run.

## The log today

**Size.** `sEPOCH_SIZE` is `10 + _TagState_MAX` plus 2 per hibernate
interval. `_TagState_MAX` is 10 (`CALIBRATE`) and every target allows two
hibernate intervals (`Config.hibernate max_count:2`), so:

| Targets | Record | Slots |
| --- | --- | --- |
| STM32L4 tags with hibernation (BitTag, PresTag, CompassTag, UIUCTag) | 24 B | 24 |
| STM32U3 IMUTag (no hibernation) | 32 B | 20 |

A normal run with both hibernate intervals writes 8 markers: CONFIGURED,
RUNNING, two HIBERNATING/RUNNING pairs, FINISHED and sRESET. So 16 slots are
spare on an L4 tag, and the log fills only when something unusual happens.
The erase that ends a run (`erasePersistent()`) clears it, and IDLE writes
no marker.

**Who writes markers.** Each is a `T_INIT` transition:

| Writer | State | Reasons | Kind |
| --- | --- | --- | --- |
| `Configured()` | CONFIGURED | `STARTCMD` | scheduled |
| | | `BROWNOUT` (recovery) | unscheduled |
| `Running()` (family `state_run.c`) | RUNNING | `STARTTIM`, `STARTCMD`, `ENDHIB` | scheduled |
| | | `BROWNOUT` (recovery restart) | unscheduled |
| `Hibernating()` | HIBERNATING | `STARTHIB` | scheduled |
| | | `BROWNOUT` (recovery) | unscheduled |
| `Finished()`, `Aborted()` | FINISHED, ABORTED | any | closing |
| `resetEnter()` | sRESET | `RESETCMD` | closing; a repeat while already erasing is unscheduled |

Resuming after a monitor attach (`T_CONT`, `POWERFAIL`) writes nothing, by
design.

**What fills it.** Unscheduled markers have no limit:

- Every brown-out that recovery restarts from writes another RUNNING,
  HIBERNATING or CONFIGURED marker restating the state the tag was already in.
  A failing cell that browns out repeatedly fills the log this way.
- `resetEnter()` writes an sRESET marker on every erase request, including a
  retry while already erasing. `state_machine.c` records a tag that looped
  sRESET to ABORTED seven times, "with the marker log filling towards the
  point where recordState() silently stops recording". The loop itself has
  been fixed; the unlimited write has not.

**Why a full log is worse than silent.** After a power loss the backup
registers are gone, and recovery takes the state from the last marker (see
[Restart Recovery](../restart-recovery.md)). With the log full:

- a run that finished after the log filled has no FINISHED marker, and
  recovers in its last recorded state, RUNNING or HIBERNATING, which recovery
  then aborts or restarts;
- an erase whose sRESET marker was refused recovers as FINISHED or ABORTED
  over a half-erased log, instead of resuming the erase.

The only trace of a refusal is a scratchpad `ESLF` record, and field builds
have no scratchpad
([Field Data Extraction](../../../../design/proposals/field-data-extraction.md),
Gap 2).

**Resets and faults are mostly covered already.** `embedded/tags/TODO.md`
said they leave no marker. The code now does better:

| Event | Today |
| --- | --- |
| Hard fault while RUNNING, no monitor | `_unhandled_exception()` latches `EXCEPTION`; boot writes ABORTED(`EXCEPTION`) |
| Power loss while active, no monitor | ABORTED(`POWERFAIL`) |
| Brown-out with a trusted clock | restart, writing an unscheduled marker (above) |
| Monitor attach, plain NRST | resume, no marker (correct) |
| Hard fault while HIBERNATING | **resumes with no marker**: `Hibernating(T_CONT, OK)` |
| Watchdog reset | does not occur: no tag enables the IWDG (`STM32_WDG_USE_IWDG FALSE`) |

Two gaps remain: a fault during hibernation, and every fault or brown-out
once the log is full. The budget closes the second. The first becomes an
unscheduled marker, HIBERNATING(`EXCEPTION`).

## The rules

`recordState()` gains a class argument: closing, scheduled or unscheduled.
Each call site passes the class shown in the table above. Before writing, it
counts the free slots from the end of the log (it already scans for the first
blank) and compares them with the reserve.

**Rule 1: closing markers always fit.** The last two free slots are reserved
for closing markers. FINISHED or ABORTED may use either slot. sRESET may use
the last one, and the first sRESET of an erase always can, because erase is
accepted only from FINISHED, ABORTED or sRESET. Both slots are needed: after
the run's last marker, the erase's own sRESET marker is what lets recovery
resume an interrupted erase. A repeated sRESET, written while already erasing,
adds nothing for recovery, so it is unscheduled.

The closing markers cannot sit at a fixed position at the end of the array.
Recovery and `system_logAck()` stop at the first blank slot, so a marker after
a blank would never be read. Each closing marker goes in the next free slot,
and rule 1 is enforced by refusing anything else when only two remain.

Rule 1 holds even if the arithmetic of rule 2 is wrong, for example after the
clock jumps backwards and the tag re-enters a hibernate interval it has
already passed.

**Rule 2: the rest of the schedule is reserved.** The reserve is computed from
`sconfig.hibernate[]`, `sconfig.stop`, `timestamp` and the state:

| State | Scheduled markers still to come |
| --- | --- |
| CONFIGURED | RUNNING, plus 2 for each interval that starts before `stop` |
| RUNNING | 2 for each interval that starts after `timestamp` and before `stop` |
| HIBERNATING | RUNNING (`ENDHIB`), plus 2 for each later interval |

A scheduled marker may use any slot except those reserved by rule 1. Writing
HIBERNATING also keeps the slot for its RUNNING, so a pair is never split; if
the pair does not fit, HIBERNATING is refused and the tag hibernates without a
marker, which recovery handles as an ordinary power loss while RUNNING.

**Rule 3: unscheduled markers use the spare only.** An unscheduled marker is
written only while free slots exceed the closing reserve (2), the schedule
reserve, and 1 for an overflow marker not yet written. Refusing one is safe
for recovery: every unscheduled marker restates a state the log already
records (RUNNING after RUNNING, HIBERNATING after HIBERNATING, sRESET after
sRESET), except HIBERNATING(`EXCEPTION`), whose state is also HIBERNATING.

**Rule 4: a refusal is never silent.**

- **Overflow marker.** The first refusal writes one marker into the slot rule 3
  kept for it: the current state, the time, and a new reason,
  `EVENT_MARKERLOG_FULL`. A reader then sees when the log filled and in which
  state, followed by the closing markers.
- **Count.** Every refusal, including scheduled markers refused when rule 2's
  arithmetic was wrong, increments a counter in `pState`. It survives Standby
  and attaches, not a power loss.
- On STM32U3 the count goes into the closing marker's `transition_detail`
  (open question 2). The L4 record has no spare field, so there the overflow
  marker is the only record in flash.

## Recovery and readers

- **Recovery is unchanged.** It still takes the last valid marker's state. The
  overflow marker carries the current state, so it reads like any other
  marker.
- **`system_logAck()` and the host** see one new reason code. Host tools pass
  unknown `State.Event` values through as numbers; the text logs, the SQLite
  writer and `decode_capture.py` should name it.
- **The capture tools** read `sEpoch` from the identity record's layout, which
  does not change.

## Changes

| Where | Change |
| --- | --- |
| `proto/tag.proto` | `State.Event.EVENT_MARKERLOG_FULL = 21`, additive |
| `common/core/src/persistent.c` | `recordState(reason, class)`: free-slot count, reserve, overflow marker, counter |
| `common/core/src/state_machine.c` | Pass the class at each call site; a schedule-reserve function; HIBERNATING(`EXCEPTION`) when a fault interrupts hibernation |
| Family `state_run.c` (PresTag, CompassTag, BitPresTag, IMUTag) | Pass the class from `Running()` |
| `pState` (each family's backup layout) | A refused-marker counter |
| Host: `txtlogs`, `sqlitelog`, `decode_capture.py` | Name the new reason |
| [Restart Recovery](../restart-recovery.md) | Describe the budget once built |

This touches `state_machine.c` and `persistent.c`, so it goes in the batched
firmware qualification, measured with `tag_lifecycle_check.py` and qualified
with `tag_release_check.py`.

## Verification

- **Offline.** The budget is pure arithmetic over a slot count and a
  schedule. Following the standalone pattern in
  `embedded/tags/UIUCTag/test/README.md`, compile the reserve function and
  the decision against stubs and assert:
  - closing markers always fit;
  - a hibernate pair is never split;
  - unscheduled markers stop at the spare;
  - the overflow marker is written exactly once;
  - a clock jump that re-enters an interval still leaves both closing slots.
- **Bench, on one L4 and one U3 tag:**
  - drive more unscheduled markers than the spare: repeated erase requests
    while erasing, and brown-outs from a supply dip. The log must end with the
    overflow marker, then FINISHED/ABORTED and sRESET;
  - cut power during an erase on a full log. The tag must resume the erase;
  - fault during hibernation with a debug image that faults on command. The
    log must show HIBERNATING(`EXCEPTION`).

## Open questions

1. **The new reason code.** `EVENT_MARKERLOG_FULL` is the only protocol
   change. Without it, an overflow marker would need an existing reason such as
   `UNKNOWN`, which readers could not tell from a real unknown failure.
   Proposed: add it.
2. **The U3 count.** `transition_detail` is reason-scoped; ABORTED(`CONFIGERROR`)
   already uses it for a word index. Either put the count in the top byte of
   every closing marker's detail and keep the low 24 bits reason-scoped, or
   leave the count to `pState` and captures. Proposed: the top byte. A
   configuration error happens at the start of a run, before any refusal, so
   the two do not collide in practice.
3. **A fault during hibernation.** This proposes a marker and resuming, as
   today. Aborting instead, as a fault while RUNNING does, would lose a run
   that may be healthy.
