---
type: readme
status: current
summary: PresTag family members and their log formats, the high-resolution and low-noise configuration options, with links to the power test plan, results, open items and host simulation.
---

# PresTag Family

This family contains the shared application code for PresTag pressure-log
targets that use the PresTagv3 board and LPS27 pressure sensor.

Family members:

- `PresTag`: exports converted pressure samples as `PresTagLog`, or, when its
  stored configuration selects high resolution, full-resolution pages as
  `PresTagHRLog`; see [Configuration options](#configuration-options).
- `PresTagRaw`: exports raw pressure pages as `PresTagRawLog`. It has neither
  configuration option.

Both members report `PRESTAG` in monitor configuration. The target name and
firmware string identify the build variant; the protobuf payload identifies the
download log format.

## Configuration options

PresTag's stored configuration carries two LPS27 options, `Config.lps27`
(stored-config layout 3). Both default to off, which is PresTag's original
behaviour: the 16-bit pressure, low-power conversion, and the sensor read
PresTag has always used.

- **`high_resolution`** stores each sample as the five LPS27 output bytes,
  `PRESS_OUT_XL` through `TEMP_OUT_H`: a 24-bit pressure at 4096 LSB/hPa and the
  16-bit temperature (`t_PresTagHRSample`, `include/prestag_log_format.h`).
  The default format keeps only the top 16 bits of the pressure, a step of
  1/16 hPa (about 0.5 m of altitude). A page is 48 samples in the same
  240 bytes as a default page of 60, so headers, erase and paging are
  unchanged; 4 MB holds 838,860 samples, 194 days at 20 s. The samples per page
  follow the stored configuration (`prestagSamplesPerPage()`), fixed for a run.
  A failed read keeps its slot with pressure 0x800000, and the host writes no
  row for it.
- **`low_noise`** converts in the LPS27's low-noise mode, through
  `lps27GetPressureTempFull()`. In the default format the pressure's low byte
  is then dropped, which gives exactly the 16-bit value the default read
  returns. It costs about half as much again per sample and is where the extra
  resolution pays; see the 2026-11-09 entry in
  [power results](design/power-results.md).

PresTag always reports `lps27`; PresTag up to fw-v0.8 and PresTagRaw never send
it, which is how Tag Monitor knows to offer the options. The capture rebuild
(`prestag` decoder in `host/libraries/tagcore/recovery/capturesource.cc`) takes
the format from the captured stored configuration.

Why it was built this way is
[decision 0047](../../../../docs/decisions/0047-prestag-high-resolution-pressure.md);
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
