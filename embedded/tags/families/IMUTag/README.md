---
type: readme
status: current
summary: What the IMUTag family shares between IMUTagNand and IMUTagNandBmp581, with an annotated list of family design notes.
---

# IMUTag Family

Shared application code for the active `IMUTagNand` and `IMUTagNandBmp581`
build variants lives here. Older breakout, BMM350 bring-up, U375 flash, and
L432 NAND variants have been moved under `embedded/tags/archive/`.

The variants share the IMUTag data-log format, configuration handling, device
binding table, sensor orchestration, RUN-state acquisition flow, and default
ChibiOS configuration. Variant directories keep their board selection,
processor-specific makefile choice, firmware identity strings, and any
temporary bring-up overrides.

`IMUTagNand` uses the generated `IMUTagNandv1` board files configured for
STM32U375 and selects the 1 Gbit GD5F SPI-NAND plus LPS22HH pressure modules.
`IMUTagNandBmp581` uses the generated `IMUTagNandv2` board files and selects
the BMP581 pressure module plus the 2 Gbit GD5F2GM7RE SPI-NAND module while
preserving the IMUTag protocol family.

Design notes:

- [`design/sample-timing.md`](design/sample-timing.md): how sample times are
  produced -- a jitter-free raw 32.768 kHz trigger clock, a smooth-calibrated
  STM32 RTC, 1024 Hz page anchors with resync flags, and host reconstruction
  from sample count.
- [`design/internal-header-checkpoints.md`](design/internal-header-checkpoints.md):
  the sparse STM32U3 internal-flash checkpoints that let NAND-backed targets
  recover their external log cursor after a reset.
- [`design/power.md`](design/power.md): measured current per sample rate at
  3.7 V and the battery and storage runtime each implies.
- [`TODO.md`](TODO.md): open power and timing questions.

Records:

- [`design/investigations/2026-09-imutag-regulator-and-sleep-sweeps.md`](design/investigations/2026-09-imutag-regulator-and-sleep-sweeps.md):
  the superseded estimates, Stop 1 era and LDO-versus-SMPS sweeps behind the
  current power figures.
- [`design/investigations/start-abort-diagnostics.md`](design/investigations/start-abort-diagnostics.md):
  the intermittent abort at start and the marker detail word added to diagnose
  it. Closed as probably resolved.
- [`design/investigations/2026-08-imutagnandbmp581-first-breakout-bringup.md`](design/investigations/2026-08-imutagnandbmp581-first-breakout-bringup.md):
  first bench validation of `IMUTagNandBmp581`.
- [`design/proposals/imutag-nand-bmp581-development-plan.md`](design/proposals/imutag-nand-bmp581-development-plan.md)
  and [`design/proposals/sample-timing-implementation-plan.md`](design/proposals/sample-timing-implementation-plan.md):
  the plans these were built from, kept for the record.
