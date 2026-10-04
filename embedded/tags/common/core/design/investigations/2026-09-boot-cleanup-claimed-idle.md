---
type: investigation
status: closed
summary: How boot cleanup asserting IDLE over a non-empty marker log made every second IMUTag collection fail, and the two defects fixed on 2026-09-02.
---

# Boot Cleanup Claimed IDLE Over a Non-Empty Log

Fixed 2026-09-02. Cut verbatim from
[Restart Recovery](../restart-recovery.md), which keeps the invariant.

## Boot Cleanup Must Not Claim IDLE (fixed 2026-09-02)

`IDLE => empty state log` is an invariant of this firmware. `Idle()` records no
marker precisely so that an empty log is what makes an idle tag resolve to idle,
and reset recovery relies on it: it seeds `TagState_IDLE` before walking
`sEpoch`, so a tag with no markers needs no marker to be found.

`tagResetRuntimeStateForPowerInit()` in `core/src/main.c` broke that invariant.
It set `pState->state = TagState_IDLE` directly, bypassing `Idle()`, and also
zeroed `pages` and `external_blocks`. Reached with markers still in the log, it
produced a tag whose live state read IDLE while internal flash still ended in
`FINISHED` with a nonzero external page count.

That is unrecoverable from the host, because the erase path only runs from
FINISHED or ABORTED. `tag-reset` saw IDLE, skipped the erase, and the next run
started on a dirty NAND, collected nothing, and aborted. The abort *was*
erasable, so the run after it succeeded — the observed symptom was every second
collection failing, with downloads refused whenever data was present.

Two defects combined:

- `deviceInit()` cleared `pState->valid` even when called with `force`, which
  the terminal transitions all do (`Finished()`, `Aborted()`, `Reset()`,
  `SelfTest()`). That opened a window spanning all of the device power
  sequencing in which any reset — a host tool detaching and the next one
  attaching is enough — was classified by `getResetCause()` as `resetPower` with
  no valid retained state, which is the condition that runs the cleanup. The
  clear bought nothing: the same block restores the magic unconditionally at the
  end. It is now done only for a genuine power init.
- The cleanup asserted IDLE regardless of the log. It now claims IDLE only when
  `stateLogEmpty()`, and otherwise leaves `STATE_UNSPECIFIED` so recovery must
  resolve the state from the marker log. Leaving the state unspecified also
  keeps the monitor-attach branch from adopting it — `validTagState()` rejects
  UNSPECIFIED — which is what forces the scan. The log cursors are left
  untouched in that case, because recovery owns them: `restoreLog()` when
  detached, the retained values under a monitor.

The marker scan itself was never at fault. Instrumentation caught it repairing
the damage on a detached boot: entry state IDLE, resolved FINISHED from three
markers ending in `EVENT_STOPCMD`. The failure only persisted when a monitor was
attached, because that branch trusts retained state and never reads the log.

Verified on hardware: a counter in `tagResetRuntimeStateForPowerInit()`
incremented exactly once per failing cycle before the fix and never after it,
across four reset/start/detach/stop/download cycles, with two consecutive
successful runs — which had not happened once in the preceding runs.
