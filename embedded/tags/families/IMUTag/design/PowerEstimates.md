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

All rows below were measured with the board fed from a **3.3 V** bench supply,
not from a cell. Two regulator builds of the u375 breakout base were compared:

- **LDO** — TPS7A0218PDBVR, fixed 1.8 V, 25 nA quiescent. Measured on the
  breakout revision that carries the NAND load switch.
- **SMPS** — TPS62840**YBG** buck, 1.8 V set by the VSET resistor. The YBG
  (WCSP-6) package has neither a MODE nor a STOP pin, so the converter is
  always in automatic PFM/PWM; there is no forced-PWM strap to get wrong.

Both builds carry the **same components and the same firmware image**. The only
difference is the `FLASH_PWR` pull direction in standby, and `FLASH_PWR` is
asserted throughout collection, so it can only affect the idle row. The two LDO
columns therefore differ in idle only, and the regulator is the sole material
variable at every logging rate.

**Runtime figures below use a 12 mAh cell as the reference.** A 20 mAh cell is
a heavier solution that is not always appropriate for a deployment, so 12 mAh
is the case that should drive design decisions; 20 mAh is shown alongside for
comparison only.

| Mode | LDO, flash off in standby (uA) | LDO, flash on in standby (uA) | SMPS (uA) |
| --- | ---: | ---: | ---: |
| Idle | 5.9 | 6.6 | 5.5 |
| 100 Hz | 1100 | 1100 | 1150 |
| 200 Hz | 1180 | 1180 | 1210 |
| 400 Hz | 1330 | 1330 | 1300 |
| 800 Hz | 1630 | 1630 | 1500 |
| 1600 Hz | 1920 | 1920 | 1670 |

> **Units.** The idle row was recorded on a milliamp range; the raw readings
> were 0.0059, 0.0066 and 0.0055 mA and are converted to microamps here. A
> nanoamp reading is not physically possible: 5.9 nA is below the TPS7A0218's
> own 25 nA quiescent current.

### Load Switch Verdict: Not Worth It

The load switch buys 0.7 uA out of a 6.6 uA idle budget, and nothing at any
logging rate. In energy terms a full day of idling saves 0.017 mAh, about
55 seconds of additional logging at 100 Hz. On a 12 mAh cell, idle-only life
goes from 75.8 days with the flash powered to 84.7 days with it switched off.
Over the multi-month horizon needed for that 8.9 days to accumulate, cell
self-discharge and the capacity derating this document explicitly excludes are
both larger terms than the 0.7 uA being chased.

GD5F deep power-down (`0xB9`) already reaches within 0.7 uA of a fully
unpowered part, exits in 30 us with no re-initialization, and is interlocked
against an in-progress program or erase by `gd5fWaitReady()`. A hard rail cut
has no such interlock, and requires tPUW plus reset plus feature-register, ECC
and block-unlock restoration on every wake.

**Decision:** strap `FLASH_PWR` high and use deep power-down for standby. Keep
the switch footprint as a recovery aid, but do not cycle the rail between
collection bursts. This resolves the open question in
`imutag-nand-bmp581-development-plan.md`.

### Regulator Comparison: The Buck Is Far Below Its Own Specification

Both builds carry the same components and the same firmware image, differing
only in the `FLASH_PWR` pull direction in standby. The 1.8 V rail load is
therefore identical at each rate, and because the TPS7A0218 passes load current
straight through (25 nA quiescent), **the LDO column is the 1.8 V load
current**. That makes the SMPS column a direct efficiency measurement rather
than a confounded comparison.

An LDO's efficiency is fixed at Vout/Vin = 1.8/3.3 = **54.5%** at every load.

| Mode | 1.8 V load (uA) | SMPS input at 3.3 V (uA) | Buck efficiency |
| --- | ---: | ---: | ---: |
| Idle | 5.9 | 5.5 | 58.5% |
| 100 Hz | 1100 | 1150 | 52.2% |
| 200 Hz | 1180 | 1210 | 53.2% |
| 400 Hz | 1330 | 1300 | 55.8% |
| 800 Hz | 1630 | 1500 | 59.3% |
| 1600 Hz | 1920 | 1670 | 62.7% |

The buck is only breaking even with the LDO around 400 Hz and never exceeds
63%. **The TPS62840 datasheet headline is 80% efficiency at 1 uA of load**
(3.6 V to 1.8 V), and a buck of this class should be near 90% at 1-2 mA. These
measurements sit 25-30 points below that across the entire range, including at
light load where the part is specified to be at its best.

That shortfall is not a property of the TPS62840, and it is not an LDO-versus-
SMPS architectural result. With components and firmware now eliminated as
variables, it is a buck implementation problem on that board.

> The idle row is weak evidence on its own — at 5.5 uA, meter resolution of a
> few tenths of a microamp moves the efficiency figure by ten points or more.
> The 100-1600 Hz rows, at 1-2 mA, are the solid measurements.

#### The Shortfall Is Unambiguous, Whatever Its Cause

One observation needs no modelling. At 100 and 200 Hz **the SMPS board draws
more total input current than the LDO board at the same input voltage with the
same load.** A buck can only lose to a linear regulator if its efficiency is
below Vout/Vin, i.e. below 54.5%. No decomposition of the load, and no argument
about which rail feeds what, can explain that away.

#### What to Check

The output capacitor is 10 uF and the inductor is one of the parts from the
datasheet's Table 4, so the two passives that most commonly cause this are
already correct. That leaves two candidates.

1. **The measurement path — now the leading suspect.** A buck draws *pulsating*
   input current; an LDO draws smooth DC. The datasheet is explicit: "Because
   the buck converter has a pulsating input current, a low-ESR input capacitor
   is required", and "when operating from a high impedance source, a larger
   input buffer capacitor is recommended." **A current meter in series is a high
   impedance source.** DMM current shunts are commonly tens to hundreds of ohms
   on low ranges.

   The consequences all push the same way, and only against the buck:

   - Real dissipation in the shunt is `I_rms^2 * R`, and for PFM operation the
     input-current crest factor is large, so `I_rms` is several times `I_avg`.
     Closing the 100 Hz gap between the measured 1150 uA and the ~667 uA a 90%
     buck would draw takes 1.6 mW of loss — which is `I_rms = 4 mA` in a 100 Ohm
     shunt, a crest factor of only about 6 over the average. Entirely ordinary
     for PFM at light load.
   - Input voltage sags at the converter during each pulse, raising duty and
     compounding the loss.
   - Meters average a pulsed waveform less accurately than a DC one.

   This hypothesis also predicts the *shape* of the measured curve, which the
   others do not. Crest factor is highest in PFM at light load and falls as the
   converter moves toward continuous conduction, so the artifact should be worst
   at the lightest logging rate and shrink as the rate rises — which is exactly
   the 52% at 100 Hz rising to 63% at 1600 Hz that was measured. It also
   predicts a negligible effect at true idle, where absolute power is so small
   that `I_rms^2 * R` is fractions of a microwatt; consistent with the idle row,
   the one place the buck measures better than the LDO.

   **Fix, and it costs one component:** clip a low-ESR bulk capacitor of
   10-100 uF directly across the SMPS board's input pins, downstream of the
   meter, and re-measure. The meter then supplies smooth average current while
   the capacitor sources the pulses locally. A large drop in the reading
   confirms the artifact. Two supporting checks: note the meter's burden voltage
   on the range used (burden voltage divided by current gives the shunt
   resistance), and measure the input voltage *at the board* while logging to
   see whether it is sagging below 3.3 V.

2. **Rail voltage.** Still unverified, and still worth a DMM reading while
   logging. If VSET selected the wrong output, every efficiency figure above is
   computed against the wrong Vout. The datasheet warns that parasitic current
   or more than 100 pF between VSET and GND "can cause false RSET readings and a
   faulty output voltage to be set." Note that a rail materially above 1.8 V
   would be its own problem: the GD5F2 deep power-down command is valid only on
   1.8 V devices, and a 1.8 V NAND has little headroom above that.

Lower priority, if both of the above come back clean: input capacitor effective
value (1 uF minimum, 4.7 uF nominal), and the VOS sense point — DCS-Control
senses the output through VOS in an AC loop, so it must land at the output
capacitor rather than the switch node, with the low-inductance ground return
the datasheet calls critical.

#### The Prize If It Is Fixed

At 90% conversion the input current becomes 0.606x the rail load. On a 12 mAh
cell:

| Rate | LDO (uA) | Buck at 90% (uA) | LDO usable | Buck usable | Gain |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 100 Hz | 1100 | 667 | 10.9 h | 18.0 h | **+65%** |
| 200 Hz | 1180 | 715 | 10.2 h | 16.8 h | **+65%** |
| 400 Hz | 1330 | 806 | 9.02 h | 13.7 h | **+52%** (storage caps it) |
| 800 Hz | 1630 | 988 | 6.83 h | 6.83 h | 0% |
| 1600 Hz | 1920 | 1164 | 3.41 h | 3.41 h | 0% |

**The lighter cell widens the window in which the regulator matters.** On
12 mAh a working buck pays off at three of the five rates; on 20 mAh only at
two, because the 20 mAh cell is already storage-capped at 400 Hz and so cannot
spend the improvement there. Efficiency and capacity are substitutes, and the
less capacity the design carries, the more the efficiency is worth. If the
weight budget is pushing toward 12 mAh, that is an argument *for* pursuing the
buck, not against it.

**Do not retire the SMPS variant on the strength of the measurements above** —
they record a board that is 25-30 points off datasheet, not a verdict on the
architecture. Equally, do not adopt it until the measurement has been repeated
with input bulk capacitance and the rail voltage confirmed, because as measured
it is genuinely worse than the LDO at 100 and 200 Hz.

### Input Voltage Caveat

3.3 V is the LDO's most favourable operating point, and the bias grows with
cell voltage. An LDO's input *current* is independent of Vin because the pass
element carries the load current directly; a buck's input current falls as
1/Vin. Battery capacity is rated in mAh, so measuring at 3.3 V understates the
buck by however much the deployed cell exceeds it. For 100 Hz on a 12 mAh cell,
a 90% buck gives 16.4 h at 3.0 V, 18.0 h at 3.3 V, 20.2 h at 3.7 V and 22.9 h
at 4.2 V, against the LDO's 10.9 h at any of them.

Raising Vin is not, however, a way to work around the efficiency shortfall. The
loss measured above is real dissipation in the converter or its passives, and
feeding it a higher voltage does not recover it.

**The deployed cell voltage is not recorded anywhere in this repository.** It
needs to be, because every runtime figure here depends on it.

### Design Point: 400 Hz, 12 mAh

400 Hz is the intended operating point — the rate that gives meaningful flight
dynamics for songbirds — and 12 mAh is the reference cell. Everything below is
that single case.

| Case | Current | Battery | Usable | Limited by |
| --- | ---: | ---: | ---: | --- |
| LDO, as measured | 1330 uA | 9.02 h | **9.02 h** | battery |
| SMPS, as measured | 1300 uA | 9.23 h | **9.23 h** | battery |
| SMPS at 90% | 806 uA | 14.89 h | **13.70 h** | storage |

Three things follow.

**The 2 Gbit upgrade already banked the easy win.** At 400 Hz a 1 Gbit part caps
at 6.83 h, below the 9.02 h the battery supports, so the design was
storage-limited. 2 Gbit lifts the ceiling to 13.7 h and makes it battery-limited
— roughly +32%, already realised.

**The measured SMPS advantage is 12 minutes.** That is the only hard number
available: +2.3%, comfortably inside measurement scatter. Not a reason to add an
inductor.

**A working buck is the only remaining lever big enough to fill the flash.**
Reaching the 13.7 h ceiling needs 876 uA, a 34% cut from 1330 uA. Nothing in the
sensor configuration gets there: at 400 Hz each 100 uA saved is worth 0.68 h, so
the plausible savings from magnetometer ODR/mode and MCU clock choices — a few
hundred microamps — are worth perhaps +15-20%. Real, free of new parts and free
of noise risk, and worth taking, but not 34%. Only conversion efficiency spans
that gap, and at 90% it lands almost exactly on the ceiling: 14.89 h of battery
against 13.7 h of flash, both resources fully used. That is an unusually clean
design point, which is what makes the unresolved efficiency question worth
returning to rather than dropping.

Duty cycling does not change the conclusion. Idle is 6.6 uA against 1330 uA
logging, a 200:1 ratio, so the logging term dominates above about 0.5% duty. At
1 h/day the LDO gives 8.1 days and a 90% buck 12.5 days — the same ~55% spread
as continuous operation.

#### Verdict: LDO for Now

The extra complexity and switching noise are **not** justified on the evidence
available:

- The only measured advantage is 12 minutes at the design point.
- The larger advantage is inferred from a datasheet, not observed, and the
  boards in hand cannot test it — the rail pins are not accessible, so neither
  the input-bulk-capacitance test nor a direct rail-current measurement can be
  performed.
- The noise risk falls on the primary measurement rather than on runtime, which
  is the wrong thing to gamble on a flight-dynamics instrument. See below.

This is a decision about what to build next, not a closed question. The SMPS
remains the only path to filling the flash at 400 Hz, so the next breakout
should be built to answer it:

- A 0-ohm jumper or sense resistor in series with the 1.8 V rail, so rail
  current can be measured directly rather than inferred from input current.
- Accessible Vin and Vout test points, and a footprint for input bulk
  capacitance below the meter.
- Ideally **both regulators on one board, jumper-selectable.** That removes
  every confounder discussed above in a single stroke and turns this into a
  ten-minute measurement.

### Storage-Limited vs Battery-Limited Runtime

Usable mission time is `min(battery, storage)`. Using the 2 Gbit column from
*Storage-Limited Runtime* above, the boards **as built**, on a 12 mAh cell:

| Rate | Storage (2 Gbit) | LDO battery | SMPS battery | LDO usable | SMPS usable | Binds on |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 100 Hz | 54.6 h | 10.9 h | 10.4 h | **10.9 h** | **10.4 h** | battery |
| 200 Hz | 27.3 h | 10.2 h | 9.92 h | **10.2 h** | **9.92 h** | battery |
| 400 Hz | 13.7 h | 9.02 h | 9.23 h | **9.02 h** | **9.23 h** | battery |
| 800 Hz | 6.83 h | 7.36 h | 8.00 h | **6.83 h** | **6.83 h** | storage |
| 1600 Hz | 3.41 h | 6.25 h | 7.19 h | **3.41 h** | **3.41 h** | storage |

On a 12 mAh cell the storage bound only bites at **800 Hz and above**; on a
20 mAh cell it bites from 400 Hz. Below the crossover the mission is
battery-limited and regulator efficiency is fully spendable; above it, the
flash fills first. **No regulator change buys any usable mission time at or
above the crossover.** The SMPS's 8-13% advantage at 800 and 1600 Hz converts
to exactly zero hours.

Conversely, on 12 mAh the rates where a regulator improvement *is* spendable are
100, 200 and 400 Hz — and those are precisely the rates where the buck as built
measures level with or worse than the LDO. Both halves point the same way: the
SMPS is worth pursuing only for the low-rate case, and only once the efficiency
shortfall is fixed.

The lever at high rates is bytes per sample, not microamps. At 13.6 bytes per
sample (150 samples per 2048-byte page), any packing or compression win
converts one-for-one into mission time at 800 and 1600 Hz.

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
degrade the measurement rather than the mission duration. That is the wrong
trade for an instrument, and it is why the LDO carries the benefit of the doubt
until this is tested.

Testable with existing tooling and no bench equipment: log on both boards under
matched conditions and compare noise floors and spectra in sensorViz, looking
specifically for structure at and around the sample rate and its subharmonics.

## Open Questions

- What is the deployed cell voltage and chemistry? Not recorded in this tree.
- Why is the TPS62840 measuring 25-30 points below its datasheet efficiency?
  Cout (10 uF) and the inductor (a Table 4 part) are confirmed correct, so the
  open suspects are the measurement path's burden resistance against a pulsating
  input current, and the actual rail voltage — in that order.
- Does adding 10-100 uF of bulk capacitance at the SMPS board's input, below the
  meter, change the reading? This is the cheapest discriminating test available,
  but the rail pins are not accessible on the boards in hand, so it must wait
  for a breakout built with the test points listed under *Design Point*.
- Does the BMM350 or LSM6DSV16X noise floor show structure at or near the sample
  rate on the SMPS board? This is the question that actually gates adoption.
- Which `FLASH_PWR` standby polarity was used for the SMPS idle measurement, so
  it can be compared against the correct LDO column?
