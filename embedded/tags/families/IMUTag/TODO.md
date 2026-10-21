---
type: worklist
status: current
summary: Open questions for the IMUTag family -- cell voltage, supply noise on the SMPS board, the idle split, the 3.7 V projection trend, and timing-metadata gaps.
---

# IMUTag Family: Open Items

## Power

- **Find out whether the BMP581 runs far faster than intended.**
  - *The code:* collection puts the BMP581 in `BMP5_POWERMODE_CONTINUOUS`
    (`bmp581_config_continuous_device()`, called from `sensors.c`), with the
    ODR that `select_pressure_rate()` picks (25 Hz at 100 Hz IMU ODR).
  - *The datasheet* (BST-BMP581-DS004 §4.3.6) says CONTINUOUS mode ignores
    the ODR setting and converts as fast as the oversampling allows. Its
    Table 9 gives about 255 Hz at OSR_P ×4, OSR_T ×1. The firmware's OSR_T ×2
    makes that somewhat lower.
  - *Possible cost:* the sensor would be converting all the time, where NORMAL
    mode at 25 Hz with the same oversampling converts about 4 ms in every
    40 ms. The datasheet's 75 µA at "high resolution", 30 Hz, about a third
    duty, implies an active current near 200 µA. So the difference could be
    a large share of the 539 µA measured at 100 Hz ([power.md](design/power.md)).
    That is an estimate, not a measurement.
  - *Effect on data:* none on per-sample noise, which is set by the
    oversampling. Only the age of the pressure value read once per
    superframe changes: under 5 ms now, up to 40 ms at 25 Hz.
  - *Test:*
    1. Count DRDY pulses on `LPS_DRDY`, or compare current traces of the
       current image against a build using `BMP5_POWERMODE_NORMAL` at the same
       ODR. Either settles the rate without attaching.
    2. If NORMAL is adopted, measure with `tag_lifecycle_check.py` and
       qualify with `tag_release_check.py`, per
       [Verifying a firmware change](../../../../docs/bench/verifying-firmware.md).

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
