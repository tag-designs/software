---
type: results
status: current
summary: Measured IMUTagNandBmp581 current at 3.7 V on the SMPS board per sample rate, and the battery and storage runtime each rate implies.
---

# IMUTag Power and Runtime

At a 3.7 V cell, the SMPS (TPS62840) IMUTagNandBmp581 board draws **5.5-6.7 uA
idle** and **539 uA at 100 Hz rising to 1003 uA at 1600 Hz** while running. On
a 12 mAh cell with the 2 Gbit NAND, runtime is battery-limited at 100 and
200 Hz (22.3 h and 20.7 h) and storage-limited from 400 Hz up (13.7 h, 6.83 h,
3.41 h). Quote idle shelf life as 74 days until the idle split below is
understood.

These figures are for `IMUTagNandBmp581` in its shipping configuration:
terminal sleep Stop 3, run sleep Stop 2. `IMUTagNand` (LPS22HH, 1 Gbit NAND)
does not set `IMUTAG_RUN_SLEEP_STOP2` and stays in Stop 1 during runs; it has
not been measured at 3.7 V.

The datasheet estimates, the Stop 1 era, and the LDO-versus-SMPS sweeps at
2.5 V and 3.3 V that this sweep checks are in
[investigations/2026-09-imutag-regulator-and-sleep-sweeps.md](investigations/2026-09-imutag-regulator-and-sleep-sweeps.md).
No figure there is current. Open power questions are in [`../TODO.md`](../TODO.md).

## Measured SMPS Board, Full Rate Sweep at 3.7 V (2026-10-03), Shipping Configuration

Two of the earlier sweeps (2026-09-03 and 2026-09-07, in
[the 2026-09 sweeps](investigations/2026-09-imutag-regulator-and-sleep-sweeps.md))
carry a projected 3.7 V column, because the bench then had no 3.7 V supply. One
was added to the Joulescope for this sweep, on 2026-10-03. This is the first
**measured** 3.7 V sweep, on the
same daughter card `00303143433650090059002E` and the same TPS62840 breakout as
the 3.3 V sweeps, so the projection is checked rather than trusted.

Taken with `power_sweep_imutag.sh`, 120 s per rate point, at a supply measured at
**3.6931-3.6932 V**. Firmware `b025e7ba`, target `IMUTagNandBmp581`, shipping
configuration (terminal sleep Stop 3, run sleep Stop 2). **Every rate point
carries a verified download**, 1.01-1.03x the expected sample count, and idle
was re-measured after every rate. All five rate points passed the download
check; idle points have no download to check.

Projected figures are the 2026-09-07 measurements scaled to the supply actually
used, `x (3.2937/3.6930) = 0.8919`, so they differ by ~0.2% from the 2026-09-07
sweep's `@3.7 V` column, which scales to 3.7 V exactly.

| Mode | Measured (uA) | Projected (uA) | Delta | Download | 12 mAh battery | 2 Gbit storage | Usable | Binds on |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | --- |
| Idle | 5.5193 | 7.08 | -22.1% | -- | 90.6 d | -- | 90.6 d | battery |
| 100 Hz | 538.6038 | 539.5 | -0.17% | 12150 rows, 1.01x | 22.28 h | 54.6 h | **22.28 h** | battery |
| 200 Hz | 579.5728 | 581.0 | -0.25% | 24600 rows, 1.02x | 20.70 h | 27.3 h | **20.70 h** | battery |
| 400 Hz | 662.4015 | 665.3 | -0.43% | 49200 rows, 1.02x | 18.12 h | 13.7 h | **13.70 h** | storage |
| 800 Hz | 826.5109 | 831.5 | -0.60% | 98550 rows, 1.03x | 14.52 h | 6.83 h | **6.83 h** | storage |
| 1600 Hz | 1003.3790 | 1010.8 | -0.74% | 197100 rows, 1.03x | 11.96 h | 3.41 h | **3.41 h** | storage |

Idle is the mean of the six readings taken through the sweep (5.4992-5.5419 uA,
a 0.77% spread): the first over 120 s, the five after each rate over 20 s.
Raw per-point data: [`sweep-3v7-20261003.csv`](sweep-3v7-20261003.csv).

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

At 400 Hz the point was also measured three times by a different harness, the
life-cycle walk (`tag_lifecycle_check.py`, 60 s windows) inside
`tag_release_check.py` runs at 3.6930 V: 664.98, 665.47 and 661.35 uA against
the sweep's 662.40. They are recorded in commit `cb86063c` (2026-10-03); the
builds measured are not. Four measurements across two harnesses span 4.12 uA,
0.62%.

### Idle does not agree with itself across days

The sweep's six idle readings are tight and sit well below the 7.08 uA
projection:

| When | Readings (uA) | n | Mean (uA) | Spread | 12 mAh shelf life |
| --- | --- | ---: | ---: | ---: | ---: |
| 2026-10-02, release checks | 6.7758, 6.7596, 6.7605, 6.7996, 6.5963, 6.6708, 6.6560, 6.6786 | 8 | 6.7122 | 203 nA | 74.5 d |
| 2026-10-03, life-cycle | 5.45, 5.43, 5.44 | 3 | 5.4400 | 20 nA | 91.9 d |
| 2026-10-03, sweep | 5.5277, 5.5419, 5.5045, 5.4992, 5.5130, 5.5294 | 6 | 5.5193 | 43 nA | 90.6 d |

Seventeen readings, two populations, and the split falls on the **day**
boundary rather than on the procedure: both 2026-10-03 sets agree to 0.08 uA
across two different harnesses, and the eight from 2026-10-02 agree to 203 nA
across two reboots of the host. The 2026-10-02 and life-cycle readings are
recorded in commit `b1e7ba88`, without the builds measured. Nothing here fails -- every reading is soundly asleep
-- but 1.2 uA is 22% of the figure that sets shelf life, and it separates 74
days from 91 on a 12 mAh cell.

The two populations sit near two values the 2026-09 regulator comparison
already distinguishes: its *Regulator and Flash-Power Comparison* table
([investigation](investigations/2026-09-imutag-regulator-and-sleep-sweeps.md)) lists SMPS idle at 5.5 uA and
LDO-with-flash-powered-in-standby at 6.6 uA, against 5.52 (2026-10-03) and
6.71 (2026-10-02) here. The match is weaker than it looks: that table was
measured from a 2.5 V supply, these at 3.69 V, and its 6.6 uA is the LDO
board, not this SMPS one. It points, at most, at `FLASH_PWR` standby state -- which that section flags as inferred
from a numeric match and never confirmed against the schematic. But the
day-boundary pattern fits a bench variable just as well (ambient temperature is
the obvious candidate, and was not recorded), and nothing here distinguishes
the two. One coincidence and one correlation, not a finding.

This is a second entry in the unresolved idle column, alongside the 21% idle
difference between the 2026-09-02 and 2026-09-03 sweeps. (The +82.9% idle
cost of Stop 3 over Standby in the same investigation is explained there: it is
the ~3.6 uA price of a terminal sleep that is entered every time.) **Quote shelf life from the pessimistic
population** (74 d) until it is understood, and record bench temperature on the
next idle measurement.

### What binds at 3.7 V

On measured figures rather than projected ones, the picture is unchanged from
the 2026-09-07 sweep: the crossover sits between 200 and 400 Hz. At and above
400 Hz the 2 Gbit device fills first -- 13.70 h against 18.12 h of battery at
400 Hz -- so the power saving becomes margin for cold, cell derating and idle
either side of the run, not more samples. Below 400 Hz the mission is
battery-limited and the saving is runtime directly.

At 1600 Hz the gap is 3.5x: 11.96 h of battery against 3.41 h of storage.
The lever at high rates remains bytes per sample, not microamps.

### Consequence for the release gate

`tag_release_check.py --run-max-ua` was rebased from 850 uA to **760 uA** on
these measurements, preserving the proportional margin the 3.3 V pair had
(850/750). Against the measured 400 Hz figure of 662.40 uA that leaves 98 uA of
headroom, and a layout regression of the size this bound exists to catch
(~200 uA at 3.3 V, ~178 uA here) still fails it. The bound is supply-voltage
dependent for exactly the reason this section exists, so moving the bench
supply without rebasing it makes it either toothless or a false-failure
generator. See [*Bound run current, not just idle*](../../../../../docs/bench/power-testing.md)
in the power-testing procedure.


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

Against the battery, on the 2 Gbit part IMUTagNandBmp581 carries, the two
limits cross near 300 Hz; at the 400 Hz design point the flash fills first.

![Recording time against sample rate: battery limit (12 mAh over the measured current) and 2 Gbit flash limit, crossing near 300 Hz; at 400 Hz the flash fills at 13.7 h, the battery would last 18.1 h](../../../../../host/docs/src/images/imutag-recording-limits.svg)

The chart is drawn from `sweep-3v7-20261003.csv` by
[`plot_recording_limits.py`](plot_recording_limits.py); rerun it after a new
sweep.


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
