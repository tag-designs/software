---
type: readme
status: current
summary: PresTag family members and their log formats, with links to the power test plan, results, open items and host simulation.
---

# PresTag Family

This family contains the shared application code for PresTag pressure-log
targets that use the PresTagv3 board and LPS27 pressure sensor.

Family members:

- `PresTag`: exports converted pressure samples as `PresTagLog`.
- `PresTagRaw`: exports raw pressure pages as `PresTagRawLog`.

Both members report `PRESTAG` in monitor configuration. The target name and
firmware string identify the build variant; the protobuf payload identifies the
download log format.

## Design notes

- [Power and Schedule Test Plan](design/power-test-plan.md): what the firmware
  draws in each state, the 10 s sleep-mode boundary, why a power window is 60
  sample periods, the linear average-current law behind the lifetime estimate,
  and the schedule and data checks. The shared rig procedure is
  [power testing](../../../../docs/bench/power-testing.md).
- [Power Results](design/power-results.md): append-only record of every
  measurement with its conditions, plus the campaign's fit, lifetime and gate
  verdicts. Cite entries from here rather than restating figures.
- [Power campaign investigation](design/investigations/2026-09-prestag-stop2-and-power-campaign.md)
  and [log-cursor investigation](design/investigations/2026-09-prestag-log-cursor-round-up.md):
  how the 2026-09 figures were reached, and the cursor fix.
- [TODO](TODO.md): open items (F2, F3, T4, re-measuring hibernation).

## Host simulation

[`test/`](test/README.md) compiles the real `state_run.c` and `datalog.c` for
the host against stubs, fills a fake external flash, and checks every page
through the real `data_logAck()`. It is not part of any build.
