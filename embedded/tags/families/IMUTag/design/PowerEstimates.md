# Power Estimation for the IMUTagNand

The following are datasheet estimates of power consumption. Core voltage is
1.8 V, CPU clock 12.5 MHz, peripheral clock 25 MHz.

## Idle Power (Processor in Standby)

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 0.25 | |
| BMM350 | 1.8 | |
| LSM6DSV16X | 2.6 | |
| LPS22HH | 0.9 | |
| GD5F1GQ5REYFGR | 50.0 | |
| **Total** | **55.8** | |

## Run Power in Stop1 (100 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
12.5 Hz ODR, LPS22HH - 10 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 57.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 107.000 | Low noise |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1014** | |

## Run Power in Stop1 (200 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
25 Hz ODR, LPS22HH - 25 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 96.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 265.000 | Low noise |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1211** | |

## Run Power in Stop1 (400 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
50 Hz ODR, LPS22HH - 50 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 175.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 530.000 | Low noise |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1555** | |

## Run Power in Stop1 (800 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 low-noise -
100 Hz ODR, LPS22HH - 100 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 335.000 | Low noise |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 338.000 | Low current |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1523** | |

## Run Power in Stop1 (1600 Hz Sample Rate)

This estimate is for the periods when there is no activity. BMM350 regular -
200 Hz ODR, LPS22HH - 200 Hz ODR.

| Component | Current (uA) | Notes |
| --- | ---: | --- |
| TPS7A0218PDBVR | 0.025 | |
| RV3028-C7 | 0.045 | |
| STM32U375 | 150.000 | |
| BMM350 | 370.000 | Regular |
| LSM6DSV16X | 650.000 | 670 uA in high accuracy |
| LPS22HH | 482.000 | Low current |
| GD5F1GQ5REYFGR | 50.000 | |
| **Total** | **1702** | |

This does not include the cost of reading data or writing flash. This is just
the idle periods.

## Actual Measurements

Note: these measurements used software I2C, which averages 2.4 mA for 1.9 ms.
Battery runtime is computed directly from the measured average current and does
not include capacity derating.

| Mode | Estimate (uA) | Measured (uA) | 12 mAh Runtime | 20 mAh Runtime | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| Idle | 55 | | | | Standby does not work; getting 20 uA in Stop3 |
| 100 Hz | 1014 | 1520 | 7.89 h | 13.2 h | |
| 200 Hz | 1211 | 1600 | 7.50 h | 12.5 h | |
| 400 Hz | 1555 | 1800 | 6.67 h | 11.1 h | |
| 800 Hz | 1523 | 2259 | 5.31 h | 8.85 h | BMM350 switched to low current |
| 1600 Hz | 1702 | 2630 | 4.56 h | 7.60 h | |

The estimates did not include communication costs with I/O devices and memory.
Updated to use hardware I2C.

Page write is 11 uJ. So memory write is 1.4 J for 128k pages.

## Storage-Limited Runtime

Each flash page contains 150 IMU samples. These estimates assume the external
memory is filled with contiguous 2048-byte log pages and use raw memory
capacity: 1 Gbit is 65,536 pages and 2 Gbit is 131,072 pages. Actual runtimes
will be slightly lower after bad blocks and metadata/checkpoint overhead.

| Sample Rate | Page Rate | 1 Gbit Runtime | 2 Gbit Runtime |
| ---: | ---: | ---: | ---: |
| 100 Hz | 0.667 pages/s | 27.3 h (1.14 d) | 54.6 h (2.28 d) |
| 200 Hz | 1.333 pages/s | 13.7 h (0.57 d) | 27.3 h (1.14 d) |
| 400 Hz | 2.667 pages/s | 6.83 h (0.28 d) | 13.7 h (0.57 d) |
| 800 Hz | 5.333 pages/s | 3.41 h (0.14 d) | 6.83 h (0.28 d) |
| 1600 Hz | 10.667 pages/s | 1.71 h (0.07 d) | 3.41 h (0.14 d) |

## After Fixing Stop1 Code

| Mode | Estimate (uA) | Measured (uA) | 12 mAh Runtime | 20 mAh Runtime | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| Idle | 55 | 14.2 | 845 h (35.2 d) | 1408 h (58.7 d) | |
| 100 Hz | 1014 | 920 | 13.0 h | 21.7 h | |
| 200 Hz | 1211 | 998 | 12.0 h | 20.0 h | |
| 400 Hz | 1555 | 1260 | 9.52 h | 15.9 h | |
| 800 Hz | 1523 | 1700 | 7.06 h | 11.8 h | BMM350 switched to low current |
| 1600 Hz | 1702 | 2300 | 5.22 h | 8.70 h | |

## switch to 2gbit flash and bmp581

| Mode | Estimate (uA) | Measured (uA) | 12 mAh Runtime | 20 mAh Runtime | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| Idle | 55 | 6.7 | 1791 h (74.6 d) | 2985 h (124 d) | |
| 100 Hz | 1014 | 1100 | 10.9 h | 18.2 h | |
| 200 Hz | 1211 | 1180 | 10.2 h | 17.0 h | |
| 400 Hz | 1555 | 1330 | 9.02 h | 15.0 h | |
| 800 Hz | 1523 | 1630 | 7.36 h | 12.3 h | BMM350 switched to low current |
| 1600 Hz | 1702 | 1930 | 6.22 h | 10.4 h | |

## Regulator and Flash-Power Comparison (2 Gbit GD5F, BMP581)

Two regulator builds of the u375 breakout base were compared, both carrying the
IMUTagNandBmp581 daughter card:

- **LDO** — TPS7A0218PDBVR, fixed 1.8 V, 25 nA quiescent.
- **SMPS** — TPS62840**YBG** buck, 1.8 V set by the VSET resistor. The YBG
  (WCSP-6) package has neither a MODE nor a STOP pin, so the converter is always
  in automatic PFM/PWM; there is no forced-PWM strap to get wrong.

Both builds carry the **same components and the same firmware image**. The only
difference is the `FLASH_PWR` pull direction in standby, and `FLASH_PWR` is
asserted throughout collection, so it can only affect the idle row. The
regulator is the sole material variable at every logging rate.

> **Both builds were measured from a 2.5 V bench supply**, not 3.3 V and not a
> cell. This single number dominates the interpretation, because an LDO's
> efficiency is exactly Vout/Vin: **1.8/2.5 = 72.0%**, which is a high bar. The
> same measurements read against an assumed 3.3 V input would put the buck 25-30
> points below its datasheet and look like a fault. They are not. See
> *Sensitivity to Cell Voltage* below.

**Runtime figures use a 12 mAh cell as the reference.** A 20 mAh cell is a
heavier solution that is not always appropriate for a deployment, so 12 mAh is
the case that should drive design decisions.

| Mode | LDO, flash off in standby (uA) | LDO, flash on in standby (uA) | SMPS (uA) |
| --- | ---: | ---: | ---: |
| Idle | 5.9 | 6.6 | 5.5 |
| 100 Hz | 1100 | 1100 | 1150 |
| 200 Hz | 1180 | 1180 | 1210 |
| 400 Hz | 1330 | 1330 | 1300 |
| 800 Hz | 1630 | 1630 | 1500 |
| 1600 Hz | 1920 | 1920 | 1670 |

> **Units.** The idle row was recorded on a milliamp range; the raw readings were
> 0.0059, 0.0066 and 0.0055 mA and are converted to microamps here. A nanoamp
> reading is not physically possible: 5.9 nA is below the TPS7A0218's own 25 nA
> quiescent current.

An independent JS320 reading of the LDO build in idle, taken with
[`embedded/tools/joulescope_measure.py`](../../../../tools/joulescope_measure.py)
at a 3.2935 V supply, gives **6.71 uA** (6.708 / 6.713 / 6.715 / 6.715 uA over
5 s, 30 s and two 10 s windows; charge-integrated and block-mean estimators agree
to better than 0.02%). This matches the *switch to 2gbit flash and bmp581* row
above and the Joulescope UI's own reading.

Idle is not flat: the same runs show the current cycling between 6.18 uA and
10.5 uA, so periodic activity rides on the baseline.

Note that 6.71 uA sits on the **flash-on-in-standby** column (6.6 uA) rather than
flash-off (5.9 uA), which suggests this LDO build has `FLASH_PWR` pulled up.
Inferred from the match, not confirmed against the schematic.

### Measurement Method: A Joulescope Hazard Worth Knowing

The stock `pyjoulescope_driver` CLI entry points **power-cycle the device under
test**, and on an IMUTag that corrupts `pState` in the RTC backup registers. Both
`measure` and `statistics` call `Driver.open(device)` with no `mode`, which the
driver documents as equivalent to `'defaults'`: it pushes the metadata default
for every writable topic. On a JS320:

- `s/i/range/mode` defaults to `0` (`off`), which opens the current-sense path
  and cuts DUT power at every open;
- `s/i/range/select`, the manual shunt selection, also defaults to `0` (`off`).
  `statistics` then sets mode `5` (`manual`), so the current path stays open for
  the entire run and every sample reads approximately zero.

Observed symptoms: `statistics` returned one plausible window followed by exact
zeros (the board dying, then unpowered), while repeated `measure` calls left the
tag drawing 1.71 mA with a disrupted clock until qtmonitor resynchronised it.

Use [`embedded/tools/joulescope_measure.py`](../../../../tools/joulescope_measure.py),
which opens with `mode='restore'`, never writes range `0`, holds one session
across windows, restores the range configuration it found, and computes average
current from accumulated charge rather than a mean of window means.

### Load Switch Verdict: Not Worth It

The load switch buys 0.7 uA out of a 6.6 uA idle budget, and nothing at any
logging rate. In energy terms a full day of idling saves 0.017 mAh, about
55 seconds of additional logging at 100 Hz. On a 12 mAh cell, idle-only life goes
from 75.8 days with the flash powered to 84.7 days with it switched off. Over the
multi-month horizon needed for that 8.9 days to accumulate, cell self-discharge
and the capacity derating this document excludes are both larger terms than the
0.7 uA being chased.

GD5F deep power-down (`0xB9`) already reaches within 0.7 uA of a fully unpowered
part, exits in 30 us with no re-initialization, and is interlocked against an
in-progress program or erase by `gd5fWaitReady()`. A hard rail cut has no such
interlock, and requires tPUW plus reset plus feature-register, ECC and
block-unlock restoration on every wake.

**Decision:** strap `FLASH_PWR` high and use deep power-down for standby. Keep
the switch footprint as a recovery aid, but do not cycle the rail between
collection bursts. This resolves the open question in
`imutag-nand-bmp581-development-plan.md`.

### Regulator Comparison: Ordinary Behaviour, Not a Fault

Because the components and firmware match and the TPS7A0218 passes load current
straight through, the LDO column **is** the 1.8 V rail load. That makes the SMPS
column a direct efficiency measurement against the 2.5 V input:

| Mode | 1.8 V load (uA) | SMPS input (uA) | Buck efficiency | LDO (fixed) |
| --- | ---: | ---: | ---: | ---: |
| 100 Hz | 1100 | 1150 | 68.9% | 72.0% |
| 200 Hz | 1180 | 1210 | 70.2% | 72.0% |
| 400 Hz | 1330 | 1300 | **73.7%** | 72.0% |
| 800 Hz | 1630 | 1500 | 78.2% | 72.0% |
| 1600 Hz | 1920 | 1670 | 82.8% | 72.0% |

69% rising to 83% across 1.1-1.9 mA is an ordinary light-load buck curve:
modest where fixed and switching losses dominate, improving as the converter
moves toward continuous conduction. It crosses the LDO's flat 72% at almost
exactly 400 Hz, which is precisely where the measured crossover sits. Nothing
here requires a fault, a wrong passive, or a measurement artifact.

Two honest caveats. The buck's 69% at 1.1 mA is still somewhat below what the
TPS62840 datasheet implies — its headline is 80% efficiency at a 1 uA load, three
decades lower — so there is probably some headroom. And 2.5 V is an unusually
favourable input for the LDO, so this table is the buck's *worst* case, not a
general verdict.

### Sensitivity to Cell Voltage

This is now the question that decides the regulator, and it is unresolved.

An LDO's input current equals its load current, independent of input voltage,
because the pass element carries the load directly. A buck's input current scales
roughly as 1/Vin. So the two diverge as the supply rises, and the LDO's advantage
at 2.5 V evaporates on a higher-voltage cell.

At 400 Hz on a 12 mAh cell, where the LDO draws 1330 uA and gives 9.02 h at any
input voltage, against the 13.7 h storage ceiling:

| Cell | LDO usable | Buck at measured 74% | Buck at 88% |
| ---: | ---: | ---: | ---: |
| 2.5 V | 9.02 h | 9.24 h (+2%) | 11.03 h (+22%) |
| 3.0 V | 9.02 h | 11.08 h (+23%) | 13.23 h (+47%) |
| 3.7 V | 9.02 h | **13.67 h (+52%)** | 13.70 h (+52%, capped) |
| 4.2 V | 9.02 h | 13.70 h (+52%, capped) | 13.70 h (+52%, capped) |

The striking entry is 3.7 V: **the buck exactly as built, with no efficiency
improvement at all, would deliver 13.67 h against the 13.7 h flash ceiling** —
both resources fully consumed, and +52% over the LDO. Nothing needs fixing for
that; it needs only a higher supply voltage.

So the SMPS question is not "is the converter healthy" — it is. It is entirely
"what voltage does the deployed tag run from", and **that number is recorded
nowhere in this repository.** It should be, because it moves the regulator
decision from a 2% curiosity to a 52% design win.

It is also worth establishing whether the 2.5 V bench setting is representative
of the intended supply or was incidental to these measurements.

### Design Point: 400 Hz, 12 mAh

400 Hz is the intended operating point — the rate that gives meaningful flight
dynamics for songbirds — and 12 mAh is the reference cell.

| Case | Current | Battery | Usable | Limited by |
| --- | ---: | ---: | ---: | --- |
| LDO (any Vin) | 1330 uA | 9.02 h | **9.02 h** | battery |
| SMPS at 2.5 V, as measured | 1300 uA | 9.23 h | **9.23 h** | battery |
| SMPS at 3.7 V, same efficiency | 878 uA | 13.67 h | **13.67 h** | both, balanced |

**The 2 Gbit upgrade already banked the easy win.** At 400 Hz a 1 Gbit part caps
at 6.83 h, below the 9.02 h the battery supports, so the design was
storage-limited. 2 Gbit lifts the ceiling to 13.7 h and makes it battery-limited
— roughly +32%, already realised.

**Reaching that ceiling needs 876 uA**, a 34% cut from 1330 uA. Sensor
configuration cannot span it: each 100 uA saved is worth 0.68 h, so plausible
savings from magnetometer ODR/mode and MCU clock choices — a few hundred
microamps — are worth perhaps +15-20%. Worth taking on their own merits, and free
of new parts and noise risk, but not 34%. Only the regulator, at a cell voltage
above roughly 3.5 V, closes the gap.

Duty cycling does not change this. Idle is 6.6 uA against 1330 uA logging, a
200:1 ratio, so the logging term dominates above about 0.5% duty.

#### Verdict: LDO Until the Cell Voltage Is Settled

At a 2.5 V supply the LDO is the right choice, and comfortably so: it is more
efficient than the buck at 100 and 200 Hz, within 2% at 400 Hz, and carries none
of the switching-noise risk described below. The measured SMPS advantage at the
design point is 12 minutes.

That verdict is contingent, not final. On a 3.7 V cell the same buck hardware is
worth +52% and lands exactly on the flash ceiling. Settle the cell voltage first;
if it is above ~3.5 V, the SMPS deserves a proper evaluation including the noise
measurement, and the next breakout should be built to support it:

- A 0-ohm jumper or sense resistor in series with the 1.8 V rail, so rail current
  can be measured directly rather than inferred from input current.
- Accessible Vin and Vout test points.
- Ideally **both regulators on one board, jumper-selectable**, which removes
  every confounder in a single stroke.

### Storage-Limited vs Battery-Limited Runtime

Usable mission time is `min(battery, storage)`. Using the 2 Gbit column from
*Storage-Limited Runtime* above, the boards **as built at 2.5 V**, on a 12 mAh
cell:

| Rate | Storage (2 Gbit) | LDO battery | SMPS battery | LDO usable | SMPS usable | Binds on |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 100 Hz | 54.6 h | 10.9 h | 10.4 h | **10.9 h** | **10.4 h** | battery |
| 200 Hz | 27.3 h | 10.2 h | 9.92 h | **10.2 h** | **9.92 h** | battery |
| 400 Hz | 13.7 h | 9.02 h | 9.23 h | **9.02 h** | **9.23 h** | battery |
| 800 Hz | 6.83 h | 7.36 h | 8.00 h | **6.83 h** | **6.83 h** | storage |
| 1600 Hz | 3.41 h | 6.25 h | 7.19 h | **3.41 h** | **3.41 h** | storage |

The LDO column holds at any input voltage; the SMPS column is specific to 2.5 V
and improves as the supply rises.

On a 12 mAh cell the storage bound only bites at **800 Hz and above**; on 20 mAh
it bites from 400 Hz. Below the crossover the mission is battery-limited and
regulator efficiency is fully spendable; above it the flash fills first, and no
regulator change buys any usable mission time.

The lever at high rates is bytes per sample, not microamps. At 13.6 bytes per
sample (150 samples per 2048-byte page), any packing or compression win converts
one-for-one into mission time at 800 and 1600 Hz.

### Outstanding: Sample-Synchronous Supply Noise

Independent of any power result, a switching converter on a board carrying a
BMM350 and an LSM6DSV16X needs a noise check before the SMPS is adopted, and the
concern is sharper than generic switching ripple.

The load is **modulated at the sample rate**: every sample event is a current
burst. In Power-Save Mode the PFM pulse rate varies with load current, so the
converter's pulse timing becomes correlated with the sampling itself, and rail
ripple then appears synchronously with each sample. Synchronous artifacts do not
average out and land squarely in the band of interest — for songbird flight
dynamics, wingbeat fundamentals and their low harmonics. An LDO contributes no
switching component at all, only a PSRR rolloff.

A DC field from the inductor calibrates out as a hard-iron offset and is not the
worry. The worry is a supply artifact locked to the sample clock, which would
degrade the measurement rather than the mission duration. That is the wrong trade
for an instrument, and it is why the LDO carries the benefit of the doubt until
this is tested.

Testable with existing tooling: log on both boards under matched conditions and
compare noise floors and spectra in sensorViz, looking specifically for structure
at and around the sample rate and its subharmonics.

## Open Questions

- **What cell voltage and chemistry does the deployed tag use?** Not recorded in
  this tree, and it now decides the regulator: 2% for the buck at 2.5 V versus
  52% at 3.7 V. Also: was the 2.5 V bench setting representative or incidental?
- Is the buck's 69% at 1.1 mA improvable? It is below what the datasheet implies,
  though no longer anomalous. Only worth pursuing if the cell voltage makes the
  SMPS viable.
- Does the BMM350 or LSM6DSV16X noise floor show structure at or near the sample
  rate on the SMPS board? This is what actually gates adoption.
- Which `FLASH_PWR` standby polarity was used for the SMPS idle measurement, so
  it can be compared against the correct LDO column?
