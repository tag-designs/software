---
type: worklist
status: superseded
superseded-by: embedded/tags/families/CompassTag/TODO.md
summary: Live handoff for the finished CompassTag power campaign: rig state, completed work and outstanding checks as of 2026-09-22.
---

# CompassTag Power Testing — Live Status

**Overwritten by whichever session is driving the rig. Read this first on
connect; do not accumulate history here — that belongs in
[`power-results.md`](power-results.md).**

History (the handoff and what was done): see [investigations/2026-09-compasstag-standby-after-attach.md](investigations/2026-09-compasstag-standby-after-attach.md).


## Outstanding

- **Reflash and re-measure `CompassTag` (MX25R) and `CompassTagAT25`
  (non-breakout) with the fix.** Both reproduced the pre-fix fault on
  separate physical boards this session; neither was re-measured after the
  fix. They share the fixed file (`pwr-l432.c`) with no override, so the fix
  should apply identically — confirm it on hardware before calling this
  closed across the family.
- A genuine cold power-cycle re-confirmation post-fix (VBAT/supply fully
  removed, not just SWD-disconnected) has not been done. The 376 nA figure in
  the report is the *pre-fix* cold measurement, used as the target.
- Two other CompassTag findings from this same session remain open and
  unrelated to this fix:
  - The ABORTED-on-first-boot fix (`379e3f1`) is committed but was never
    verified on real hardware.
  - `isMonitorEnabled()`'s `MONCONNECTED` latch bug and the host's blind
    `MONITORSTOP` success path (plan §4) are both real, both unrelated to
    this fix, and both still open.
