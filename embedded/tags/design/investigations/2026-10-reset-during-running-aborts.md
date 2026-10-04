---
type: investigation
status: closed
summary: A non-failure reset during RUNNING aborted PresTag runs under 10 s and every IMUTag run; fixed 2026-10-02 by treating an external reset with valid retained state as a reattach.
---

# A Non-Failure Reset During RUNNING Aborted the Run

Observed 2026-10-01 and 2026-10-02, fixed 2026-10-02 (commit a406eda7, "tags:
an external reset is a reattach, not a failure"). Cut verbatim from two
places: the work-list entry A6 in [Next Release TODO](../../TODO.md), and the
"Still open" entry in [Restart Recovery](../../common/core/design/restart-recovery.md) that A6 points to.

## From Next Release TODO, A6

### A6. A non-failure reset during RUNNING aborts the run (PresTag under 10 s, IMUTag always)

**Status, 2026-10-02: fixed in the firmware, and verified on IMUTag and
PresTag.**

The rule is that only true failures abort.
- `getResetCause()` now records `externalResetAtBoot`: an NRST with the
  retained state valid and no brownout, watchdog, software, low-power or
  option-byte flag. A power-on also sets BORRSTF, so it is excluded.
- Recovery treats such a reset exactly like a monitor reattach
  (`monitorResetRecoveryActive()` on U3, `reattachReset()` elsewhere), so an
  active run resumes with `T_CONT, POWERFAIL`.
- On U3, `deviceInit()` keeps runtime state for it.
- PresTag's `Running(T_CONT, POWERFAIL)` now re-arms the sample ticker. Its
  absence is the likely cause of the 1 s monitor-attach stall below.

On the bench IMUTagNandBmp581, at 100 Hz, a `tag-capture` 33 s into a run was
followed by:
- `RESTART_RECOVERY` segment 1 from 43.8 s, with 2250 samples to 66.3 s,
  after segment 0's 3600 samples;
- a normal FINISHED on the stop command;
- no ABORTED.

The image is entry 2 of that tag's flash log; its currents are to be measured.

On the bench PresTag (`20333050364150040063005F`), at the 1 s period that
used to fail both ways:
- **monitor attach:** page 0 got 38 samples up to the attach; sampling
  continued on page 1 (29 samples);
- **external reset**, from a `tag-xflash` session: sampling continued on
  page 2 (40 samples);
- the marker log holds only CONFIGURED and RUNNING; there is no ABORTED.

On this L4 path a reattach rebuilds the cursor from the page headers, so new
samples start on the next page. That is existing behaviour, a part-used page
per reattach, not data loss.

Other families resume through the same common dispatch. CompassTag and
UIUCTag were not re-tested; their `Running(T_CONT, POWERFAIL)` paths may
need the same re-arm as PresTag.

The original observations follow.


**Observed 2026-10-01, not scheduled.** On the bench PresTag:
- at a 1 s period, a monitor attach left the tag RUNNING but no longer
  sampling;
- at a 1 s period, the plain reset that ends a `tag-capture` or `tag-xflash`
  session was classified `EVENT_POWERFAIL`, and the run went to ABORTED;
- at 10 s, which uses Standby between samples, both left the run intact.

For PresTag this is low priority: periods under 10 s exist only to gather
data quickly on the bench, and the shipping 90 s configuration is not
affected.

**IMUTag is affected in every configuration** (observed 2026-10-02 on
IMUTagNandBmp581, firmware `bce3fe38`). Its run-mode sleep is always Stop 2
(`IMUTAG_RUN_SLEEP_MODE`). The reset ending a `tag-capture` session 8 s into a
run was logged `EVENT_POWERFAIL`, and the run went to ABORTED. In the field
nothing pulses NRST, and a genuine power loss is meant to abort, so the
practical cost falls on the recovery tools: capturing a RUNNING IMUTag ends
its run. Its data up to that point remains downloadable. A monitor attach is
recognised as a monitor reset and does not abort. It matters on the bench: a short-period test
run must not be attached to or captured mid-run, or its data stops there.
Details and the likely mechanism are in `restart-recovery.md`, "Still open".

## From Restart Recovery, "Still open"

- **PresTag at sample periods under 10 s does not survive a reset during
  RUNNING** (observed 2026-10-01, bench PresTag `20333050364150040063005F`,
  firmware `663780af`). Below 10 s the run sleeps in Stop 2 between samples
  (`state_run.c`, `sconfig.lps_period < 10`); from 10 s up it uses Standby.
  - A monitor attach (connect under reset), at 1 s: the tag kept reporting
    RUNNING and wrote no further sample or header. External flash held 27
    samples from the 27 s before the attach and nothing in the five minutes
    after. The FINISHED marker then recorded 60 samples, the cursor rounded
    up to a page by the restart path, not samples written.
  - A plain NRST reset (the end of a `tag-capture` or `tag-xflash` session),
    at 1 s: the boot classified it `EVENT_POWERFAIL` and the run went to
    ABORTED.
  - At 10 s, the same monitor attach and two SWD-session resets left the run
    RUNNING and sampling (11 samples in 110 s, 12 after).
  - Likely mechanism, not yet confirmed: a reset taken in Stop 2 leaves no
    standby flag, so reset classification treats it as a power-on, while the
    Standby case carries `SBF`. The monitor-attach stall is a separate path:
    the run is adopted, but the next wakeup never comes.
  - Not bisected; this is probably not a regression from the `firmware-fix`
    branch, which does not touch the boot or run paths.
  - Low priority: periods under 10 s are a bench convenience for gathering
    data quickly, not a deployed configuration.
  - **Fixed 2026-10-02** (next-release-todo A6). `getResetCause()` records
    `externalResetAtBoot` for an NRST with valid retained state and no
    failure flag, and recovery treats it like a monitor reattach. PresTag's
    `Running(T_CONT, POWERFAIL)` re-arms the sample ticker, which removed the
    1 s monitor-attach stall. Verified on PresTag and IMUTagNandBmp581; only
    true failures abort now.
