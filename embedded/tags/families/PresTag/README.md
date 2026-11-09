---
type: readme
status: current
summary: PresTag family members (PresTag, PresTagRaw, PresTagHR) and their log formats, with links to the power test plan, results, open items and host simulation.
---

# PresTag Family

This family contains the shared application code for PresTag pressure-log
targets that use the PresTagv3 board and LPS27 pressure sensor.

Family members:

- `PresTag`: exports converted pressure samples as `PresTagLog`.
- `PresTagRaw`: exports raw pressure pages as `PresTagRawLog`.
- `PresTagHR`: logs the LPS27's full 24-bit pressure and exports it as
  `PresTagHRLog`; see [PresTagHR](#prestaghr) below.

All three report `PRESTAG` in monitor configuration. The target name and
firmware string identify the build variant; the protobuf payload identifies the
download log format.

## PresTagHR

PresTagHR (`PRESTAG_HR_LOG`) stores each sample as the five LPS27 output bytes,
`PRESS_OUT_XL` through `TEMP_OUT_H`: a 24-bit pressure at 4096 LSB/hPa and the
16-bit temperature. PresTag keeps only the top 16 bits of the pressure, a step
of 1/16 hPa (about 0.5 m of altitude). A page is 48 samples in the same
240 bytes as a PresTag page, so headers, erase and paging are unchanged; 4 MB
holds 838,860 samples, 194 days at the 20 s default period. The layout is
`t_PresTagHRSample` in `include/prestag_log_format.h`.

- **Driver:** `lps27GetPressureTempFull()`, beside `lps27GetPressureTemp()`,
  which is left as it was so the other LPS27 images do not change.
- **Low-noise mode** is a stored-configuration option, `Config.lps27.low_noise`,
  kept in `t_storedconfig.lps_low_noise` (stored-config layout 3). PresTagHR
  always reports `lps27`, and the other variants never do, which is how
  Tag Monitor knows to offer the setting. It costs about half as much again
  per sample and is where the extra resolution pays; see the 2026-11-09 entry
  in [power results](design/power-results.md).
- **Failed read:** the sample keeps its slot with pressure 0x800000; the host
  writes no row for it, so it shows as a one-period gap.
- **Capture rebuild:** decoder `prestag_hr` in
  `host/libraries/tagcore/recovery/capturesource.cc`.

Why it was built this way is
[decision 0047](../../../../docs/decisions/0047-prestaghr-full-resolution-pressure.md);
open items are in the [TODO](../../../../TODO.md#prestag).

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
- [TODO](../../../../TODO.md#prestag): open items (F2, F3, T4, re-measuring hibernation).

## Host simulation

[`test/`](test/README.md) compiles the real `state_run.c` and `datalog.c` for
the host against stubs, fills a fake external flash, and checks every page
through the real `data_logAck()`. It is not part of any build.
