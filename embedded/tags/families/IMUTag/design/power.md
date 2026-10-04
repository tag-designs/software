---
type: results
status: current
summary: Measured IMUTagNand current at 3.7 V on the SMPS board per sample rate, storage and battery runtime limits, and superseded LDO-era history.
---

# Power Estimation for the IMUTagNand

Measured power and storage limits for the IMUTagNand, and the runtime each
implies.

**The current numbers are the SMPS board at 3.7 V**, immediately below: that is
the shipping regulator at the deployed cell voltage, measured across every
sample rate with a verified download at each point. Storage limits follow, then
bench method and what is still open.

Everything from *Design History* onward is superseded. It is kept because the
reasoning is worth preserving -- the LDO-versus-SMPS comparison, the
layout-dependent Stop 1 current that made three sweeps disagree, and the
2.5 V/3.3 V measurements the 3.7 V projection was built from -- but no figure
there should be quoted as current. In particular the LDO columns describe a
board that was left behind by the SMPS version, and an LDO's input current does
not scale with supply voltage the way the shipping board's does.

## Measured SMPS Board, Full Rate Sweep at 3.7 V (2026-10-03), Shipping Configuration

Every earlier sweep, now under *Design History*, carries a projected 3.7 V
column, and every one of them says the tag cannot be measured at 3.7 V on this
bench. It can now: a 3.7 V supply
was added to the Joulescope. This is the first **measured** 3.7 V sweep, on the
same daughter card `00303143433650090059002E` and the same TPS62840 breakout as
the 3.3 V sweeps, so the projection is checked rather than trusted.

Taken with `power_sweep_imutag.sh`, 120 s per point, at a supply measured at
**3.6931-3.6932 V**. Firmware `b025e7ba`, target `IMUTagNandBmp581`, shipping
configuration (terminal sleep Stop 3, run sleep Stop 2). **Every rate point
carries a verified download**, 1.01-1.03x the expected sample count, and idle
was re-measured after every rate. 6/6 points passed.

Projected figures are the 2026-09-07 measurements scaled to the supply actually
used, `x (3.2937/3.6930) = 0.8919`, so they differ by ~0.2% from that section's
`@3.7 V` column, which scales to 3.7 V exactly.

| Mode | Measured (uA) | Projected (uA) | Delta | Download | 12 mAh battery | 2 Gbit storage | Usable | Binds on |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | --- |
| Idle | 5.5193 | 7.08 | -22.1% | -- | 90.6 d | -- | 90.6 d | battery |
| 100 Hz | 538.6038 | 539.5 | -0.17% | 12150 rows, 1.01x | 22.28 h | 54.6 h | **22.28 h** | battery |
| 200 Hz | 579.5728 | 581.0 | -0.25% | 24600 rows, 1.02x | 20.70 h | 27.3 h | **20.70 h** | battery |
| 400 Hz | 662.4015 | 665.3 | -0.43% | 49200 rows, 1.02x | 18.12 h | 13.7 h | **13.70 h** | storage |
| 800 Hz | 826.5109 | 831.5 | -0.60% | 98550 rows, 1.03x | 14.52 h | 6.83 h | **6.83 h** | storage |
| 1600 Hz | 1003.3790 | 1010.8 | -0.74% | 197100 rows, 1.03x | 11.96 h | 3.41 h | **3.41 h** | storage |

Idle is the mean of the six readings taken through the sweep (5.4992-5.5419 uA,
a 0.77% spread). Raw per-point data: [`sweep-3v7-20261003.csv`](sweep-3v7-20261003.csv).

Unlike the 2026-09-03 sweep, the 1600 Hz readback succeeded here, so no point
rests on magnitude alone.

### The buck projection holds at every rate, and errs the wrong way

Every rate lands **below** its projection, by 0.17% at 100 Hz rising
monotonically to 0.74% at 1600 Hz. The implied input-voltage ratio falls the
same way -- 0.8903, 0.8896, 0.8880, 0.8865, 0.8853 against the nominal 0.8919.

The direction matters more than the size. The projection was published as a
*lower bound*, reasoning that TPS62840 efficiency falls as Vin rises at fixed
Vout, so reality should sit "a little above" it. **It sits below at all five
rates, and increasingly so with load.** Whatever the converter does between
3.3 V and 3.7 V, it is not paying the assumed penalty; it is doing slightly
better at 3.7 V than constant-power scaling predicts, and the advantage grows
with load. The `x 0.890` scaling is therefore a mildly conservative estimate,
not a floor.

This is a systematic trend across five points, not scatter: the deltas are
monotonic in rate and each point is a verified 120 s measurement. It is not
explained here, and the mechanism is worth knowing before the figure is leaned
on at a rate that was not measured.

At 400 Hz the point was also measured three times by a different harness
(`tag_lifecycle_check.py`, 60 s windows): 664.98, 665.47 and 661.35 uA against
the sweep's 662.40. Four measurements across two harnesses span 4.12 uA, 0.62%.

### Idle does not agree with itself across days

The sweep's six idle readings are tight and sit well below the 7.08 uA
projection:

| When | Readings (uA) | n | Mean (uA) | Spread | 12 mAh shelf life |
| --- | --- | ---: | ---: | ---: | ---: |
| 2026-10-02, release checks | 6.7758, 6.7596, 6.7605, 6.7996, 6.5963, 6.6708, 6.6560, 6.6786 | 8 | 6.7122 | 203 nA | 74.5 d |
| 2026-10-03, life-cycle | 5.45, 5.43, 5.44 | 3 | 5.4400 | 20 nA | 91.9 d |
| 2026-10-03, sweep | 5.5277, 5.5419, 5.5045, 5.4992, 5.5130, 5.5294 | 6 | 5.5193 | 43 nA | 90.6 d |

Seventeen readings, two populations, and the split falls on the **day**
boundary rather than on the procedure: both of today's sets agree to 0.08 uA
across two different harnesses, and yesterday's eight agree to 203 nA across
two reboots of the host. Nothing here fails -- every reading is soundly asleep
-- but 1.2 uA is 22% of the figure that sets shelf life, and it separates 74
days from 91 on a 12 mAh cell.

The two populations sit near two values this document already distinguishes:
the *Regulator and Flash-Power Comparison* table lists SMPS idle at 5.5 uA and
LDO-with-flash-powered-in-standby at 6.6 uA, and today's mean is 5.52. That
points at `FLASH_PWR` standby state -- which that section flags as inferred
from a numeric match and never confirmed against the schematic. But the
day-boundary pattern fits a bench variable just as well (ambient temperature is
the obvious candidate, and was not recorded), and nothing here distinguishes
the two. One coincidence and one correlation, not a finding.

This is a third entry in the unresolved idle column, alongside the 21% idle
difference between the 2026-09-02 and 2026-09-03 sweeps and the +82.9% idle
cost of Stop 3 over Standby. **Quote shelf life from the pessimistic
population** (74 d) until it is understood, and record bench temperature on the
next idle measurement.

### What binds at 3.7 V

On measured figures rather than projected ones, the picture is unchanged from
the 2026-09-07 sweep: the crossover sits between 200 and 400 Hz. At and above
400 Hz the 2 Gbit device fills first -- 13.70 h against 18.12 h of battery at
400 Hz -- so the power saving becomes margin for cold, cell derating and idle
either side of the run, not more samples. Below 400 Hz the mission is
battery-limited and the saving is runtime directly.

At 1600 Hz the gap is now 3.5x: 11.96 h of battery against 3.41 h of storage.
The lever at high rates remains bytes per sample, not microamps.

### Consequence for the release gate

`tag_release_check.py --run-max-ua` was rebased from 850 uA to **760 uA** on
these measurements, preserving the proportional margin the 3.3 V pair had
(850/750). Against the measured 400 Hz figure of 662.40 uA that leaves 98 uA of
headroom, and a layout regression of the size this bound exists to catch
(~200 uA at 3.3 V, ~178 uA here) still fails it. The bound is supply-voltage
dependent for exactly the reason this section exists, so moving the bench
supply without rebasing it makes it either toothless or a false-failure
generator. See `AGENTS.md`, *Bound run current, not just idle*.


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


## Measurement Method: A Joulescope Hazard Worth Knowing

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
  it can be compared against the correct LDO column? Sharpened by the 3.7 V
  measurement: idle there fell into two tight clusters, 6.7122 and 5.4400 uA on
  the same image and board, which sit near the 6.6 uA flash-on and 5.5 uA SMPS
  columns. If that is `FLASH_PWR` standby state varying with what the tag did
  beforehand, it is worth 17 days of shelf life on a 12 mAh cell and should be
  settled against the schematic rather than by numeric coincidence.
- Does `tag-start --set-rtc` fail intermittently because of the external RTC on
  this breakout? Two of four verification cycles aborted at
  "RTC sync failed while writing tag clock", and boots frequently report
  `rtcInitializedAtBoot` and `clockTrusted` false.


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

## Design History

History: see [investigations/2026-09-imutag-regulator-and-sleep-sweeps.md](investigations/2026-09-imutag-regulator-and-sleep-sweeps.md).
