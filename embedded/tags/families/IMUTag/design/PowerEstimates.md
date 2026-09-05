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
`embedded/tools/joulescope_measure.py`
at a 3.2935 V supply, gives **6.71 uA** (6.708 / 6.713 / 6.715 / 6.715 uA over
5 s, 30 s and two 10 s windows; charge-integrated and block-mean estimators agree
to better than 0.02%). This matches the *switch to 2gbit flash and bmp581* row
above and the Joulescope UI's own reading.

Idle is not flat: the same runs show the current cycling between 6.18 uA and
10.5 uA, so periodic activity rides on the baseline.

Note that 6.71 uA sits on the **flash-on-in-standby** column (6.6 uA) rather than
flash-off (5.9 uA), which suggests this LDO build has `FLASH_PWR` pulled up.
Inferred from the match, not confirmed against the schematic.

### Measured LDO Board, Full Rate Sweep

Taken with `embedded/tools/power_experiment.py`
on the LDO breakout carrying the IMUTagNandBmp581 daughter card, at a **3.2935 V**
bench supply. Each point is an independent experiment: reset to idle with the
clock set, start with the per-rate configuration, close the monitor session,
measure detached for 60 s, stop, download, erase. Idle was re-measured between
every point to confirm the tag was still reaching standby, since a tag that
stops entering Stop3 invalidates every reading after it.

| Mode | Measured (uA) | Runs | Earlier table (uA) | Delta | 12 mAh battery | 2 Gbit storage | Usable | Binds on |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Idle | 6.6636 | 1 | 6.7 | -0.5% | 1801 h (75.0 d) | -- | 75.0 d | battery |
| 100 Hz | 1104.3374 | 1 | 1100 | +0.4% | 10.87 h | 54.60 h | **10.87 h** | battery |
| 200 Hz | 1181.6701 | 2 | 1180 | +0.1% | 10.16 h | 27.30 h | **10.16 h** | battery |
| 400 Hz | 1331.6389 | 2 | 1330 | +0.1% | 9.01 h | 13.70 h | **9.01 h** | battery |
| 800 Hz | 1631.6040 | 1 | 1630 | +0.1% | 7.35 h | 6.83 h | **6.83 h** | storage |
| 1600 Hz | 1924.1958 | 2 | 1930 | -0.3% | 6.24 h | 3.41 h | **3.41 h** | storage |

Every point falls within 0.5% of the earlier table and most within 0.1%, through
a different instrument path and a different supply voltage. Repeated runs agree
to between 0.008% and 0.16%, so the measurement is well inside the precision
needed for any design decision here.

Idle between points: 6.6904, 6.6999, 6.6814, 6.6848 and 6.6130 uA. Standby was
healthy throughout, so no rate reading is contaminated.

An LDO's input current equals its load current independent of input voltage, so
these figures are directly comparable to the earlier 2.5 V measurements. The same
would not hold for the SMPS board, whose input current scales with supply
voltage; that comparison needs its own run at the deployed cell voltage.

Storage becomes the binding constraint at 800 Hz and above on a 12 mAh cell.
Below that the mission is battery-limited and regulator efficiency is spendable.

#### Confidence and Caveats

Only the 100 Hz point carries a verified download: `ImuAccel: 6000 rows, ran at
100 Hz, 1.00x expected`, with the rate read from the configuration recorded in
the log rather than assumed. The other points had their downloads refused with
"Monitor request not permitted in current tag state", and the refusal correlates
with data being present rather than with the rate, so it is systematic. Those
currents rest on magnitude and reproducibility, not on reading the log back.

Roughly half of all collection attempts do not collect at all, and the pattern is
deterministic rather than random. Four consecutive attempts at both 400 Hz and
1600 Hz alternated exactly:

| Attempt | 400 Hz | 1600 Hz |
| ---: | ---: | ---: |
| 1 | 1331.5883 uA, collected | 1925.1871 uA, collected |
| 2 | 2696.6303 uA, no data | 1709.5761 uA, no data |
| 3 | 1331.6895 uA, collected | 1923.2044 uA, collected |
| 4 | 2697.6081 uA, no data | 1708.4518 uA, no data |

Both outcomes reproduce to four significant figures, and a successful run is
always followed by a failed one. That is state carried between runs, not a race.
The correlation is with data being present on the NAND: a run that collects
leaves pages the next reset must erase, while a failed run leaves nothing. The
failed runs end ABORTED having drawn more than any legitimate rate, so the tag is
bringing sensors up and then never sleeping. Unresolved; see *Open Questions*.

### Measured SMPS Board, Full Rate Sweep (3.3 V)

Taken 2026-09-02 with the same firmware (`b1657d7`) and the same procedure as
the LDO sweep above, on the TPS62840 breakout at a **3.2935 V** bench supply.
Idle was re-checked after every rate point.

> **Not the same daughter card.** The LDO sweep ran on STM32U375 UUID
> `00303143433650090049002E` and this one on `00303143433650090059002E`, so the
> comparison spans two boards, not one regulator swap under a fixed load. That
> is a real confounder and is recorded rather than glossed: it means the
> absolute figures pair a regulator with a particular assembly.
>
> It does not plausibly account for the result. The advantage is 38-39% at
> every point across a 290x range of load current, and part-to-part variation
> in an STM32 and three sensors does not produce a constant ratio over that
> span -- a fixed efficiency difference does. The idle figure is the one most
> exposed, since leakage genuinely varies between parts; 6.65 uA to 4.09 uA is
> far larger than that, but a repeat with the regulators swapped under one
> daughter card would settle it properly. The second card also reports
> `ppm_clock_error: 0` where the first reports `-3.81469727`, confirming they
> are separately calibrated assemblies.

| Mode | LDO (uA) | SMPS (uA) | Delta |
| --- | ---: | ---: | ---: |
| Idle | 6.6457 | **4.0858** | **-38.5%** |
| 100 Hz | 1103.8523 | *failed to start* | -- |
| 200 Hz | 1177.8776 | **727.6414** | **-38.2%** |
| 400 Hz | 1328.7656 | **818.2931** | **-38.4%** |
| 800 Hz | 1628.6156 | **996.4598** | **-38.8%** |
| 1600 Hz | 1924.1887 | **1175.3872** | **-38.9%** |

Idle between points: 4.0864, 4.1023, 4.1208, 4.1323 uA. Every rate point carries
a verified download with the ODR read back from the recorded configuration:
12150, 24600, 49350 and 98550 accelerometer rows at 1.01-1.03x the expected
count.

**The advantage is 38-39% at every point, across a 290x range of load current
from 4 uA to 1.9 mA.** A constant ratio over that span is the signature of a
genuine efficiency difference rather than anything load-dependent, and it is
what the earlier 2.5 V measurements could not show.

#### This reverses the earlier verdict, on supply voltage alone

The "no significant advantage" conclusion below was drawn from data taken at
2.5 V, and it was right for 2.5 V. An LDO's input current equals its load
current whatever the input voltage, so the LDO columns are the same at 2.5 V and
3.3 V; a buck's input current instead falls as the input rises. Raising the
supply from 2.5 V to 3.3 V therefore costs the LDO nothing in current and pays
the buck almost 40%.

#### Consequence at the design point

| 400 Hz, 12 mAh cell | Current | Battery | Storage | Usable | Limited by |
| --- | ---: | ---: | ---: | ---: | --- |
| LDO at 3.3 V | 1328.77 uA | 9.03 h | 13.70 h | **9.03 h** | battery |
| SMPS at 3.3 V | 818.29 uA | 14.66 h | 13.70 h | **13.70 h** | **storage** |

At 400 Hz the SMPS moves the binding constraint from the battery to the NAND.
Usable mission time goes from 9.03 h to 13.70 h, **+52%**, and the cell would
carry a further 0.96 h that the 2 Gbit part cannot store. Past this point more
regulator efficiency buys nothing at 400 Hz without more storage, which is a
useful thing to know before optimising further.

Above 400 Hz the tag was already storage-limited and the SMPS changes only the
margin, not the mission: 800 Hz and 1600 Hz stay at 6.83 h and 3.41 h. Below
400 Hz it is still battery-limited and the saving passes straight through, 200 Hz
going from 10.19 h to 16.49 h. Idle rises from 75.2 to 122.4 days, which matters
for how long a tag can wait between programming and deployment rather than for
mission length.

#### What this does not settle

- The **switching-noise question is untouched** and remains the real gate on
  adoption; see *Outstanding: Sample-Synchronous Supply Noise* below. A 38%
  current saving is worthless if it puts structure into the magnetometer or
  accelerometer band.
- The **`FLASH_PWR` standby polarity** of this board was not verified. The image
  flashed was the standard `b1657d7` build, so the idle figure should be
  compared against the matching LDO column with that in mind.
- The **100 Hz point failed to start** and is missing. The sweep was run with
  `STOP_ON_FAILURE=0`, so the next point's reset erased the marker log before
  the new detail word could be read, and `tag-reset failed: SetRtc failed` in the
  same interval means it is not even known whether this was the start abort or
  the unrelated RTC bug. Re-run that point with `STOP_ON_FAILURE=1`.
- These are **single measurements per point**, not repeats. The LDO sweep agreed
  with itself to within 0.4% across two runs, so the precision is not in doubt,
  but no claim is made here about board-to-board variation.

### Measured SMPS Board, Full Rate Sweep (2026-09-03), with 3.7 V Projection

Taken on the TPS62840 breakout carrying daughter card
`00303143433650090059002E` -- the same card as the 3.3 V sweep above -- at a
measured **3.2936 V**, 120 s per point, on `main` plus two uncommitted changes:
the restart-path NAND unlock in `tagDevicesAfterReset()` and the removal of the
`666` debug sentinel from `state_run.c`.

This is the first sweep of the day whose points are all verified. Every rate
point carries a download at 1.01-1.03x the expected sample count, and idle was
re-measured between every point at 4.886-4.977 uA, so no point was taken while
the tag had stopped reaching Stop3.

| Mode | Measured @3.294 V (uA) | Projected @3.7 V (uA) | 12 mAh @3.294 V | 12 mAh @3.7 V | 2 Gbit storage | Usable @3.7 V | Binds on |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Idle | 4.9419 | 4.40 | 2428 h (101 d) | 2728 h (114 d) | -- | 114 d | battery |
| 100 Hz | 868.1827 | 772.8 | 13.82 h | 15.53 h | 54.60 h | **15.53 h** | battery |
| 200 Hz | 907.0935 | 807.5 | 13.23 h | 14.86 h | 27.30 h | **14.86 h** | battery |
| 400 Hz | 983.0179 | 875.1 | 12.21 h | 13.71 h | 13.70 h | **13.70 h** | dead heat |
| 800 Hz | 1135.5329 | 1010.8 | 10.57 h | 11.87 h | 6.83 h | **6.83 h** | storage |
| 1600 Hz | 1271.5679 | 1131.9 | 9.44 h | 10.60 h | 3.41 h | **3.41 h** | storage |

The 1600 Hz point was re-run on its own to obtain the download verification: in
the sweep its current measured cleanly but the readback timed out, because
120 s at 1600 Hz is roughly 1280 pages. The two currents agree to 0.2%
(1269.19 uA in the sweep, 1271.57 uA on the verified re-run); the verified
figure is the one tabulated.

#### How the 3.7 V column is derived, and what it assumes

The tag cannot be measured at 3.7 V on this bench, so the column is a
projection, not a measurement, and it is only valid for the SMPS.

A buck converter draws input power, not input current, so raising the input
voltage lowers the input current for the same load: `I(3.7) = I(3.294) x
(3.294/3.7)`, a factor of **0.890**. This assumes converter efficiency is
unchanged between the two input voltages, which is optimistic -- TPS62840
efficiency falls slightly as Vin rises at fixed Vout, so the real 3.7 V current
will be a little above these figures. Treat the column as a lower bound with a
few percent of headroom, not a specification.

**The same scaling must not be applied to the LDO.** An LDO's input current
equals its load current regardless of input voltage, so every LDO figure in
this document is already its own 3.7 V figure. This is the whole reason the
regulator comparison depends on supply voltage, and it is why the SMPS
advantage grows as the cell voltage rises rather than staying fixed.

Runtimes are `12000 uAh / I`, against the 12 mAh reference cell, ignoring cell
derating at temperature and end-of-life voltage. Storage limits are unchanged
by supply voltage, so at 800 Hz and above the device still fills long before
the battery empties, and at 400 Hz the two now land within 0.1 h of each other.

#### This sweep disagrees with the 2026-09-02 SMPS sweep above

Same board, same regulator, same procedure, roughly 20% apart:

| Mode | 2026-09-02 (uA) | 2026-09-03 (uA) | Delta |
| --- | ---: | ---: | ---: |
| Idle | 4.0858 | 4.9419 | +21% |
| 200 Hz | 727.6414 | 907.0935 | +25% |
| 400 Hz | 818.2931 | 983.0179 | +20% |
| 800 Hz | 996.4598 | 1135.5329 | +14% |
| 1600 Hz | 1175.3872 | 1271.5679 | +8% |

This is not resolved, and neither sweep should be quoted as authoritative until
it is. A 21% difference in *idle* is the most troubling part, because idle is
the simplest measurement here and the one least able to hide a procedural
difference. Candidates, none confirmed:

- **Instrument range.** This sweep ran with `s/i/range/mode` = 4 (auto),
  recorded in the tool output. The earlier sweep's range was not recorded. A
  fixed manual shunt biases low-current readings differently from autoranging,
  and the idle point is where that matters most.
- **Firmware.** The 09-02 sweep ran on `b1657d7`, which predates the restart
  NAND unlock. Its rate runs were subject to being killed part-way by the
  harness's own monitor polling, and a window that includes post-failure sleep
  averages *low*. That would depress the rate points but not idle.
- **Bench conditions.** Both sweeps report 3.2935-3.2936 V, so the supply is
  not the variable.

The 2026-09-03 numbers are the ones with per-point download verification and
per-point idle checks, so they are the better-evidenced set; that is a reason to
prefer them, not a reason to consider the discrepancy explained.

### Measured SMPS Board, Full Rate Sweep (2026-09-04), after the I2C Idle Fix

First sweep taken after `24c1f86` removed the I2C bus clear from
`tagI2cBusEnd()`. Same TPS62840 breakout, measured at **3.2936-3.2937 V**.

Protocol per point: reset with `--set-rtc`, start, wait 5 s, measure 30 s
running, stop, settle 12 s, measure 30 s stopped, reset. The stopped column is
a check, not a result -- it confirms the tag reached a sleeping terminal state
between every rate, so no run point was taken on a tag that had stopped
sleeping.

| Mode | Running @3.294 V (uA) | Stopped (uA) |
| --- | ---: | ---: |
| Idle | 4.3431 | -- |
| 100 Hz | 669.6557 | 4.3571 |
| 200 Hz | 715.8684 | 4.3159 |
| 400 Hz | 809.6533 | 4.3298 |
| 800 Hz | 992.8078 | 4.3603 |
| 1600 Hz | 1175.3383 | 4.3760 |

#### Compared with the 2026-09-03 sweep: an A/B test, not an inference

The first version of this section said the drop was plausible but unproven,
because this tree already carried an unexplained ~20% sweep-to-sweep
disagreement of the same magnitude. It has now been tested directly.

Both images were built once and flashed alternately, A/B order swapped at each
rate to cancel drift, 120 s per point, same session and same supply:

- **A** = `main` with the fix (`24c1f86`), one clear call site in `i2c_bus.c`
- **B** = `7efe688`, the bus-end clear present, two call sites

| Mode | A, fixed (uA) | B, pre-fix (uA) | Change | B vs the 2026-09-03 sweep |
| --- | ---: | ---: | ---: | ---: |
| Idle | 4.3095 | 1032.9474 | -99.6% | -- |
| 100 Hz | 668.9062 | 863.9646 | -22.6% | 868.18, 0.5% |
| 200 Hz | 714.6061 | 899.2494 | -20.5% | 907.09, 0.9% |
| 400 Hz | 807.1992 | 976.2971 | -17.3% | 983.02, 0.7% |
| 800 Hz | 991.1455 | 1132.6792 | -12.5% | 1135.53, 0.25% |
| 1600 Hz | 1173.2872 | 1267.5105 | -7.4% | 1271.57, 0.3% |

Every B point reproduces the 2026-09-03 sweep to within 1%, and every A point
reproduces the 2026-09-04 sweep above to within 0.2%. The instrument and the
method are therefore repeatable to well under a percent, and the difference
between the two sweeps is the firmware. **The saving is real and it is the
fix.**

That also settles what the 2026-09-03 sweep was measuring: pre-fix firmware,
with SDA and SCL parked as GPIO against the board's 4.7k pull-ups for part of
every duty cycle. It does not settle the separate 2026-09-02 versus 2026-09-03
disagreement recorded earlier in this document, which involved different
daughter cards and remains open.

One feature of the result is not explained. If the cost were a simple DC pin
leak it would be a constant number of microamps at every rate, but the absolute
saving falls steadily as the rate rises -- 195.1, 184.6, 169.1, 141.5 and
94.2 uA from 100 Hz to 1600 Hz. Something about how much of each duty cycle the
pins spend parked evidently varies with sample rate. The direction and size of
the effect are established; its mechanism is not.

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

Use `embedded/tools/joulescope_measure.py`,
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
| **SMPS at 3.3 V, as measured** | **818 uA** | 14.66 h | **13.70 h** | **storage** |

The last row was added after the fact and settles the question the rest of this
section was reasoning towards: the projection was close, and at 3.3 V the buck
already reaches the flash ceiling. See *Measured SMPS Board, Full Rate Sweep
(3.3 V)*.

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

#### Verdict: LDO Until the Cell Voltage Is Settled (superseded 2026-09-02)

> **Superseded.** This was written from 2.5 V measurements and is correct for
> 2.5 V. The board has since been measured at 3.3 V, where the buck wins 38-39%
> at every operating point and takes the 400 Hz design point from 9.03 h to
> 13.70 h. See *Measured SMPS Board, Full Rate Sweep (3.3 V)* above. The
> reasoning below is kept because its central claim held up: the choice turns on
> the supply voltage, and the condition it set -- evaluate the SMPS properly if
> the cell is above ~3.5 V -- is what the new measurement did.

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

- **What cell voltage and chemistry does the deployed tag use?** Still not
  recorded in this tree, and still the input that decides the regulator, though
  the answer is now less finely balanced: measured at 3.3 V the buck wins 38-39%
  everywhere, so anything at or above 3.3 V favours it decisively and only a
  cell sitting near 2.5 V favours the LDO.
- Does the BMM350 or LSM6DSV16X noise floor show structure at or near the sample
  rate on the SMPS board? **This is now the only thing gating adoption**, the
  efficiency question having been settled at 3.3 V.
- Which `FLASH_PWR` standby polarity was used for the SMPS idle measurement, so
  it can be compared against the correct LDO column?
- Does `tag-start --set-rtc` fail intermittently because of the external RTC on
  this breakout? Two of four verification cycles aborted at
  "RTC sync failed while writing tag clock", and boots frequently report
  `rtcInitializedAtBoot` and `clockTrusted` false.

## Resolved: Every Second Collection Attempt (2026-09-02)

Boot cleanup was claiming IDLE over a marker log that still ended in FINISHED,
and zeroing the external page cursor with it. Because the host erase path only
runs from FINISHED or ABORTED, `tag-reset` skipped the erase, the next run
started on a dirty NAND and collected nothing, and only the resulting ABORTED
was erasable — hence the exact alternation. Cause, fix, and hardware
verification are in
[`embedded/tags/design/restart-recovery.md`](../../../design/restart-recovery.md).

After the fix, four consecutive reset/start/detach/stop/download cycles at
400 Hz all succeeded, each returning 47 external pages and 7050 accelerometer
samples. The download refusals disappeared with it: they were the tag correctly
declining to dump from IDLE.

## Idle Current at 3.3 V

Repeat idle readings on the LDO board at 3.2935 V, the same supply as the
measured rate sweep above. The 2.5 V figures elsewhere on this page belong to
the earlier estimate and comparison tables, not to that sweep.

| condition | idle current |
| --- | ---: |
| firmware as shipped | 6.5423, 6.5521, 6.6586, 6.6731 uA |
| `TAG_RECOVERY_TRACE` on, before the flash-flag fix | 995.2679, 995.2135, 995.1618 uA |
| `TAG_RECOVERY_TRACE` on, after it | 6.7054, 6.7009 uA |
| `debug_log` module enabled, after it | 1710.2836, 1709.7712 uA |

Idle is unchanged from the 6.61-6.70 uA measured across the sweep above, so the
boot-cleanup fix costs nothing at idle.

The 995 uA row was not what it looked like. It was read as evidence that
retained diagnostics cost the tag Stop3, and the trace was defaulted off
because of it. The real cause was a missing pre-sleep clear of the STM32U3
flash error and ECC flags: while one is latched the power controller aborts the
low-power transition or wakes straight back out of `__WFI()`, so any change
that happened to touch internal flash could move the tag from 6.6 uA to run
current. `tagPowerClearFlashErrorFlags()` fixed it, and the same build then
measured 6.705 uA. See
[`embedded/tags/design/restart-recovery.md`](../../../design/restart-recovery.md).

**Stale as of 2026-09-04.** That fix was applied to `tagPowerEnterStop3()`,
which was then the live terminal path. It no longer is: the tag enters standby
via `tagPowerEnterStandby()`, and Stop3 is `__attribute__((unused))`. The
pre-sleep clear therefore runs on no path the tag takes today, and attempts to
put it on the live paths have measured 1036 uA at idle against 4.94 uA without
it. See [`../../design/open-issues.md`](../../design/open-issues.md).

The `debug_log` row is a different fault and is still open. It was retested
after the flash-flag fix and stayed at 1.71 mA, which confirms the module's own
long-standing bug rather than this mechanism. It remains excluded.
