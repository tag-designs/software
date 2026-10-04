---
type: worklist
status: current
summary: Open questions for the IMUTag family -- cell voltage, supply noise on the SMPS board, the idle split, the 3.7 V projection trend, and timing-metadata gaps.
---

# IMUTag Family: Open Items

## Power

- **Record the deployed cell voltage and chemistry.** It is not recorded in
  this tree. The current figures in [power.md](design/power.md) and the
  release gate's `--run-max-ua` bound assume a 3.7 V cell; the SMPS board's
  input current scales with supply voltage, so a different cell moves both.
- **Check for sample-synchronous supply noise on the SMPS board.** The load is
  modulated at the sample rate: every sample event is a current burst. In
  Power-Save Mode the TPS62840's PFM pulse rate varies with load current, so its
  pulse timing can correlate with sampling and put rail ripple on every sample.
  Synchronous artifacts do not average out and land in the band of interest
  (for songbird flight, wingbeat fundamentals and their low harmonics). A DC
  field from the inductor calibrates out as a hard-iron offset; a supply
  artifact locked to the sample clock does not. Test: log on an LDO board and
  an SMPS board under matched conditions and compare BMM350 and LSM6DSV16X
  noise floors and spectra in sensorViz, looking for structure at and around
  the sample rate and its subharmonics.
- **Explain the idle split.** Idle on the same image and board fell into two
  tight populations, 6.7122 uA (2026-10-02) and 5.44-5.52 uA (2026-10-03),
  split by day rather than by procedure. Candidates: `FLASH_PWR` standby state
  (settle against the schematic, not by numeric match) or bench temperature
  (record it on the next idle measurement). Worth 17 days of shelf life on a
  12 mAh cell. Until then quote shelf life from the 6.71 uA population.
- **Explain why measured 3.7 V run current falls below the projection**,
  increasingly with load (-0.17% at 100 Hz to -0.74% at 1600 Hz), before the
  scaled figure is used at a rate that was not measured.
- **Measure `IMUTagNand` at 3.7 V.** Only `IMUTagNandBmp581` has been measured;
  `IMUTagNand` sleeps in Stop 1 during runs, not Stop 2.

## Sample timing

- Report the applied STM32 `CALP/CALM` values with the download. Only the
  RV-3028 offset in ppm (`TagInfo.ppm_clock_error`) is reported now.
- Decide whether the `millis` field of `t_DataHeader` should be renamed in a
  future binary-format revision; it holds 1/1024 s ticks and flags, not
  milliseconds.
