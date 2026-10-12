---
type: investigation
status: open
summary: Datasheet error budget for BMM350 and LSM6DSV dip, heading and field-strength estimates after qtcalibrate, 2026-10-12 -- magnetometer offset drift with temperature dominates, and calibrating at bird temperature or re-fitting the offset at the release site removes most of it.
---

# IMUTag Orientation Error Budget (2026-10)

On IMUTagNandBmp581 the sensor-limited error after calibration is dominated by
one term: the **BMM350 offset drift with temperature, typically 200 nT/K per
axis**, which our single-temperature calibration does not track. A tag
calibrated on a 20 °C bench and worn at about 38 °C carries roughly 4° of dip
error and 9° of heading error from that term alone. Calibrated at bird
temperature, or with its offset re-fitted at the release site, the same tag is
sensor-limited at under 1° of dip. The LSM6DSV contributes under 0.1° of dip
from every term its datasheet specifies. The 3.1-3.5° dip spread measured on
the bench is therefore not explained by either datasheet, and the most likely
causes are the terms neither specifies: accelerometer per-axis gain mismatch
and misalignment between the two sensors.

This is a datasheet analysis, not a measurement. Every figure below is
derived from typical values plus the stated assumptions; the measurements
that would replace them are listed at the end.

## Sources and assumptions

| Source | Used for |
| --- | --- |
| Bosch BMM350 datasheet, BST-BMM350-DS001-27, Tables 2 and 3 (pages 9-10) | Magnetometer offset, gain, temperature, noise and nonlinearity |
| ST LSM6DSV datasheet, DS13476 Rev 5, Table 3 (page 11) | Accelerometer offset, temperature and noise |
| [`bmm350_tag.c`](../../../../common/sensors/mag/src/bmm350_tag.c) | What factory compensation the firmware applies |
| [`sensors.c`](../../src/sensors.c), [`lsm6dsv16x.c`](../../../../common/sensors/imu/lsm6dsv16x.c) | Sensor modes, rates and filtering in use |
| [`magcal.c`](../../../../../../host/applications/qtcalibrate/magcal/magcal.c), [qtcalibrate README](../../../../../../host/applications/qtcalibrate/README.md) | What our calibration fits |

Both datasheets quote typical values as roughly mean ±1σ and give no maximum
for most of the terms used here; ST notes its typical values are not
guaranteed. All figures below are therefore 1σ estimates.

The geometry assumes a field of F = 50 µT at dip I = 64°, so a horizontal
component of about 22 µT. An error of 1 µT perpendicular to the field then
costs about **1.15° of dip** (1/F rad) and about **2.6° of heading** (1/F·cos I
rad), and an error of 1 µT along the field costs 1 µT (2 %) of field strength.
Each 1 mg of error in the measured gravity direction costs 1 mrad, about
**0.057° of dip**.

## What the firmware and calibration already remove

The firmware applies the full Bosch factory compensation. `bmm350ReadMagUT()`
reads the die temperature in the same burst as X/Y/Z, corrects it with the OTP
temperature trims, and applies OTP sensitivity, offset, offset temperature
coefficient (TCO), sensitivity temperature coefficient (TCS) and cross-axis
corrections to every sample, on both the logging and the calibration paths.
The coefficient scalings match the Bosch SensorAPI. One difference: the driver
always subtracts the 25.49 °C temperature offset, where the SensorAPI (as
recalled, not checked against a copy) adds it when the converted value is
negative; the two differ only below a die temperature of about −25 °C.

qtcalibrate then fits, at one temperature:

- a hard-iron offset V, in µT;
- a symmetric soft-iron matrix normalised to unit determinant, so it can
  correct axis gains and cross-axis terms but not a rotation;
- the field magnitude B, left free;
- an accelerometer zero-g offset, in mg, with no gain terms.

| Datasheet term | Value | After calibration |
| --- | --- | --- |
| BMM350 zero-field offset, before or after soldering | ±25 µT | Removed by V |
| BMM350 gain error after API compensation | ±1 % x/y, ±3 % z | Differences between axes removed by the soft-iron matrix; absolute scale left at ±1-2 % because B is free |
| BMM350 cross-axis residual | not specified | Removed by the soft-iron off-diagonal terms, given good attitude coverage |
| BMM350 z hysteresis after full-scale sweep | 0.4 % of FSS | Cleared by the magnetic reset the part performs every ODR tick in normal mode |
| LSM6DSV zero-g offset | ±12 mg | Removed by the accelerometer offset fit; ST notes it shifts with mounting stress, so the fit is needed per board |

## What remains

### Magnetometer

| Term | Datasheet | Dip | Heading | Field strength |
| --- | --- | --- | --- | --- |
| Offset drift (TCO error), per kelvin from the calibration temperature | ±200 nT/K | 0.23°/K | 0.52°/K | 0.2 µT/K |
| Sensitivity drift (TCS), per kelvin | ±0.010 %/K | none | none | 0.005 µT/K |
| Noise per sample, low-noise mode | see below | 0.15-0.37° | about 0.35° | 0.13-0.32 µT |
| Nonlinearity | ±10 µT x/y, ±20 µT z over ±2000 µT | not specified at Earth-field scale; shows in the fit residual | | |
| Field shock above 250 mT | up to ±100 µT, ±1 % | invalidates the calibration | | |

The TCO row says "-40 °C to 85 °C" and, unlike the TCS row, does not say
"after API compensation". It is read here as the residual after the factory
TCO correction because it is named an *error* and because the specification
chapter states that values are given after API correction. The drift is
assumed linear in temperature, independent between axes, and the board's own
hard iron is assumed not to drift. None of those three is stated in the
datasheet.

The noise figure is 190 nT rms (x/y) and 450 nT rms (z) at 100 Hz with
averaging of 2. The firmware uses averaging of 4 (`BMM350_PERF_LOW_NOISE`),
estimated at about 135 and 320 nT assuming white noise.

Translated into deployment cases:

| Calibrated at | Worn at | ΔT | Offset per axis | Dip | Heading | Field strength |
| --- | --- | --- | --- | --- | --- | --- |
| 20 °C bench | 38 °C | 18 K | 3.6 µT | about 4.1° | about 9.4° | 3.6 µT (7 %) |
| about 38 °C | 38 ± 3 °C | 3 K | 0.6 µT | about 0.7° | about 1.6° | 0.6 µT (1 %) |
| about 38 °C | 15 °C (torpor, altitude) | 23 K | 4.6 µT | about 5.3° | about 12° | 4.6 µT (9 %) |

A fixed offset in the sensor frame does not produce a fixed dip bias: its
projection onto the field changes with the bird's attitude, so it widens the
dip distribution as well as shifting its mean. It cannot be removed by
subtracting a dip correction.

### Accelerometer

The calibration stream runs the LSM6DSV at ±4 g, high-performance mode,
120 Hz. Collection uses the externally triggered mode with LPF2 disabled
(LPF1 at ODR/2); its noise is assumed equal to high-performance mode.

| Term | Datasheet | Dip |
| --- | --- | --- |
| Noise density, high-performance | 60 µg/√Hz, about 0.46 mg rms at 120 Hz | about 0.03° per sample |
| Zero-g change with temperature | ±0.07 mg/°C | 0.004°/K; 0.07° over 18 K |
| Sensitivity change with temperature | ±0.01 %/°C | under 0.05° over 18 K |
| Per-axis sensitivity mismatch | not specified for the accelerometer | up to about 0.3° per 1 % mismatch, uncorrected by our fit |
| Cross-axis sensitivity, nonlinearity | not specified | unknown |

The accelerometer's temperature drift is about 60 times smaller than the
magnetometer's in dip terms. Temperature handling is needed for the
magnetometer only.

### Shared between the sensors

- **Misalignment between the magnetometer and accelerometer** goes into dip
  one-for-one. Neither datasheet specifies die-to-package alignment, board
  placement adds to it, and the symmetric soft-iron matrix cannot represent
  it.
- **Accelerations from the bird's own motion** corrupt the gravity direction.
  Dip is only meaningful from steady periods (perched, gliding) or averaged
  over many wingbeats.

## Dip and field strength compared

For an offset error with no preferred direction, the relative effect on dip and
on field strength is the same, about δ/F. They differ in everything else:

| Error | Field strength | Dip |
| --- | --- | --- |
| Magnetometer offset | affected | affected |
| Absolute magnetometer scale (B left free) | ±1-2 % | none |
| Accelerometer offset, gain, noise | none | affected |
| Sensor misalignment | none | one-for-one |
| Flight accelerations | none | large unless averaged |

In a dipole approximation at 64° dip, dip changes by about 0.77° and field
strength by about 1 % per degree of latitude. The 1 µT offset therefore costs
roughly 2° of latitude in field strength and 1.5° in dip. Field strength is
immune to every accelerometer term, which makes it the more robust of the two
in flight once its scale is anchored; real gradients vary by region and also
run east-west, so a study area's gradients should come from WMM, not this
approximation.

## Measured so far

The only BMM350 figures are the two IMUTagNandv2 qtcalibrate captures of
2026-10-11 cited in [`custom.h`](../../../../IMUTagNandBmp581/inc/custom.h):
after the axis remap, inclination 63.8° and 63.5°, dip spread 3.5° and 3.1°.
At a fixed temperature the datasheet terms above predict well under 1°, so the
spread is dominated by something neither datasheet covers: the bench
environment, accelerometer gain mismatch, or sensor misalignment. The
[qtcalibrate quality experiments](../../../../../../host/docs/design/investigations/2026-10-qtcalibrate-quality-experiments.md)
were run on a CompassTag, whose accelerometer and magnetometer are different
parts; their residuals and the 69-75 mg accelerometer offset do not apply
here.

## How the temperature term can be removed

The bird helps. A tag worn on the back sits near body temperature, about 38 °C,
except in torpor or high-altitude flight. Two approaches follow, and they
combine:

1. **Calibrate at bird temperature.** Running the qtcalibrate sweep with the
   tag at about 38 °C, for example in a pre-warmed insulated enclosure, brings
   ΔT from about 18 K to a few kelvin.
2. **Re-fit the offset at the release site.** The release site's field is
   known from WMM or IGRF (F, I, D). Data recorded on the bird at the site,
   after the tag has warmed and before it leaves, constrains the
   three-component sensor-frame offset through |m − V| = F and the angle
   between (m − V) and gravity equal to I. This uses the real deployment
   temperature and surroundings, and holding the radius at F also fixes the
   absolute scale for field strength. Bench calibration is still needed for
   the soft-iron matrix and accelerometer offset. The fit can be done on the
   host after download. The reference is limited by WMM accuracy and local
   anomalies, which a one-off survey of the site removes.

Temperature is already logged. Each NAND page header carries the latest BMP581
temperature in hundredths of a degree (`rawtemp`, set in
[`state_run.c`](../../src/state_run.c)), which is frequent enough for a tag
whose temperature changes over minutes. The BMP581 is a usable stand-in for
the BMM350 die: at milliamp currents the board's self-heating is a fraction of
a kelvin and the difference between two sensors millimetres apart is smaller
still, and a 1 K mismatch costs only 0.2 µT. Fitting the offset against BMP581
temperature directly also absorbs any steady difference between the two.
Torpor and high-altitude periods can then be corrected with a measured slope or
flagged by temperature.

## Open measurements

- **The real offset drift of our parts.** Calibrate the same tag at bench and
  bird temperature, or hold a calibrated tag in a fixed orientation through a
  temperature change, and regress the offset against BMP581 temperature. This
  replaces the typical 200 nT/K, tests the linearity assumption, and gives a
  per-tag slope.
- **Accelerometer per-axis gain.** A still, multi-orientation fit of
  accelerometer offset and gains would measure the term the datasheet leaves
  unspecified.
- **Sensor misalignment.** Not separable from the soft-iron fit as it stands;
  a fit that includes a rotation between the two sensors would need better
  attitude coverage than current sweeps.
- **The 25.49 °C sign handling below −25 °C**, against a copy of the Bosch
  SensorAPI `bmm350.c`.
