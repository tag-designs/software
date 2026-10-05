---
type: investigation
status: closed
summary: Two ADC defects dating to the initial commit, found because one breakout reported 3.9 V and 100 C; the sweep that sized the fix; its verification on three tags; and a 2.2x drop in PresTag's resting floor that is measured, bounded, and still unexplained.
---

# The internal ADC, and PresTag's resting floor

2026-10-04 to 2026-10-05. Everything here is on the bench at 2.496 V, the
unregulated cell voltage the L432 tags run from. Open items are in
[the tag worklist](../../TODO.md) and the per-tag worklists it links.

## 1. What started it

Qualifying UIUCTag on a breakout, the state markers reported `vdd=3.77-3.96`
and `temp=92.5-111.4` while the Joulescope read 2.4960 V and the room was about
25 C. The same code read correctly on BitTag and CompassTag, which is what made
it interesting: a defect that depends on the board is usually a margin that was
never there.

The two outputs are not independent. `recordState()` calls `tagStatusMeasure()`
calls `adcVDD()`, which samples VREFINT and the temperature sensor in one call,
and the temperature is computed **from** the voltage:

```c
tmp = (300 * VREF_CAL) / raw;            /* tmp is now vdd100 */
*vdd100 = (uint16_t) tmp;
tmp = (((1300-300) * raw * tmp) / 300);  /* vdd feeds straight into temp */
```

So one bad sample corrupts both, and the arithmetic says which way: reported
`vdd100` averaged 385 against a true 250, 1.54x high, which means the VREFINT
sample read about 35% low. An unsettled reference reads low. That was the
thread to pull.

Ruled out first, so the diagnosis did not rest on a guess: all four tags call
`adcVDD()` identically with no other ADC users; `TAG_STATUS_FIXED_VDD100` is
defined only by the two IMUTag targets, which also take the STM32U3
early-return and so report `vdd=1.8 temp=0`; and the SQLite `states` table, an
independent decode path, carried the same bad numbers, so it was never a host
display problem.

## 2. Two defects, both from the initial commit

**No settling time.** `adc1Stop()` clears `VREFEN` and `TSEN`, so every call
re-enables both and must wait for their startup again -- and the wait,
`chThdSleepMilliseconds(1)`, was commented out.

**`SMPR2` indexed one field too high.** The register holds `SMP10..SMP18`,
three bits each, `SMP10` at bit 0 -- ST's own header gives
`ADC_SMPR2_SMP10_Pos = 0` and `SMP11_Pos = 3`. The code used
`(channel - 9) * 3` where it should be `(channel - 10) * 3`, so **every channel
from 10 up had its sampling time written into its neighbour's field** and kept
the reset value of 0, which is 2.5 cycles. Channel 17 is the temperature
sensor. This was wrong on every tag using this code, silently.

`git log -S` puts both, and the exponential average that sat on top of the
result, in the **initial commit**. The filter had therefore always been
smoothing a measurement that was never read correctly.

## 3. Sizing the fix by measurement

640.5 cycles and a 1 ms delay fixed it, but neither number was measured. A
sweep on the breakout, with the original configuration re-run as a control:

| Delay | Sampling | Result |
| --- | ---: | --- |
| none | 47.5 | **fails** -- 3.77-4.00 V, 183-210 C *(control, reproduced the fault)* |
| none | 92.5 | correct |
| none | 247.5 | correct |
| none | 640.5 | correct |
| 1 ms | 47.5 | correct |
| **200 us** | **247.5** | **correct -- 2.47-2.48 V, 24.6-24.9 C** |

Either change alone is sufficient *on this board*. Both were kept, because they
guard different failures: the delay is an absolute time, while the cycle count
scales with the core clock -- `adc1Start()` selects `CKMODE = AHB/1`, so at
24 MHz even 640.5 cycles is about 27 us, below the temperature sensor's ~120 us
startup. A cycles-only fix would have margin that varies by target.

The cost ordering was the opposite of what had been assumed. Sampling is cheap
-- 640.5 cycles is 54-326 us for the pair depending on clock -- and the sleep
is not. The settled configuration, 200 us plus 247.5 cycles, is about a quarter
of the first attempt. Fixed in `54135465`.

## 4. Verified on three tags

| Tag | Before | After |
| --- | --- | --- |
| UIUCTag breakout | 3.77-3.96 V, 92.5-111.4 C | 2.47-2.49 V, 23.9-25.3 C |
| BitTag | already correct | 2.48-2.49 V, 22.3-23.0 C |
| PresTag | already correct | 2.48-2.49 V, 24.5-25.2 C |

And it costs nothing measurable. On BitTag, same board and config across three
builds: 0.5082 µA before any fix, 0.5068 with the ADC fix, 0.5089 with the
write-site change as well -- a 0.4% spread with no ordering.

**A published estimate was wrong and is corrected here.** The fix had been
predicted to cost about +0.03 µA on BitTag's run current, perhaps 6%. That
assumed the settling delay burned active current; `chThdSleepMicroseconds()` is
a thread sleep, so the MCU idles through it and only the conversions are
active.

## 5. PresTag's resting floor, which is the open question

A six-point sweep at 2.4961 V, 1200 s per point, with the stored period read
back from the tag before each measurement because `writeStoredConfig()` does
not check its own write:

| Period | 15 s | 20 s | 30 s | 45 s | 60 s | 90 s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `RUNNING` | 1.1260 | 0.8760 | 0.6176 | 0.4541 | 0.3665 | 0.2788 µA |

`I(T) = I_rest + Q/T` fits with `I_rest` **0.1122 µA**, `Q_cycle` **15.23 µC**,
R² 0.99994, max residual 0.0035 µA.

Against the September campaign:

| | 2026-09-09 | 2026-10-05 |
| --- | ---: | ---: |
| `I_rest` | 0.2842 µA | **0.1122 µA** |
| `Q_cycle` | 15.26 µC | **15.23 µC** |

**The per-sample energy is unchanged to 0.2% across two independent campaigns,
and the whole improvement is in the resting floor.** That is the useful part:
it rules out the sampling, the sensor and the write path, and points at
something that drew constant current in sleep and no longer does. 45 s now
costs what 90 s cost in September.

### Board condition is a live competing explanation

A second PresTag, UUID `203330503641500400490058`, measured **0.1341 µA** idle
on the same firmware as the first board's **0.1261 µA** -- 6.3% apart. That
was taken as evidence that board-to-board spread could not explain a figure
123% higher.

**That reasoning is weak, because it assumes spread is bounded by what two
well-cleaned boards show.** The operator's observation is that the difference
between these two could simply be cleaning: surface leakage from flux residue.
The scale fits. 8 nA at 2.5 V is a path of about 300 GΩ, ordinary for ionic
contamination; the 155 nA separating September from today is about 16 GΩ,
which is not an extreme number for a poorly rinsed assembly. Leakage is also
strongly humidity- and temperature-dependent, and the operator reports the
2026-10-03 session was taken on a hot day in a warm room.

So there are two candidate explanations and they call for opposite work:

| Explanation | Discriminating test |
| --- | --- |
| Something in the firmware changed | run the September build on a board measured today |
| The September board was dirtier, or the conditions were | measure that board on current firmware |

The September entry records no UUID, so the second test cannot be run -- the
board cannot be identified. That is the reason the recording checklist now
demands one. But the first test can be run on either board, and it is the
same question asked from the other side.

### The firmware is exonerated

`890a11b` was checked out into a worktree, rebuilt, and flashed to PresTag
board B, which had just been measured on `d41b5357`. The build identity was
confirmed on the tag rather than assumed:

```
githash: "890a11b1"
Current state: IDLE
  current  (charge/time) :         0.1267 uA
```

| board B, same board, same session | `IDLE` |
| --- | ---: |
| September build `890a11b` | **0.1267 µA** |
| current build `d41b5357` | **0.1341 µA** |

**5.8% apart, and the dearer figure is on the current firmware.** One board,
two builds, one session: there is no firmware effect to find, and the
bisection of `890a11b..d41b5357` is cancelled. The earlier LSE A/B -- crystal
0.1199 µA against bypass 0.1261 µA -- was a true negative rather than a near
miss, because there was never a 155 nA firmware term for it to explain.

So the quantity to explain is not a 2.2x improvement. It is a single
unreproducible September reading. **0.2810 µA should be marked suspect, not
superseded**: it has not been reproduced on either board under either build,
and it was anomalous when it was taken. BitTag carries strictly more silicon
in sleep -- an ADXL362 in shutdown on top of the RV-3028 and the L432 that
PresTag also has -- and reads 0.1169 µA. PresTag cannot legitimately rest at
2.4x BitTag. The parts budget closes on today's figures and not on
September's:

| | measured | difference | part added |
| --- | ---: | ---: | --- |
| BitTag | 0.1169 µA | | |
| PresTag | 0.1261 µA | +9.2 nA | AT25XE external flash, 7 nA ultra-deep |
| UIUCTag | 0.1628 µA | +36.7 nA | ADXL367, 40 nA |

Both steps land within about 11 nA of the datasheet figure for the part that
was added. There is no room in that budget for the 155 nA September carried,
which is the arithmetic case that it was never a property of the design.
What remains is board condition -- surface leakage from flux residue is
humidity- and temperature-dependent, the session was taken on a hot day, and
155 nA at 2.5 V is a 16 GΩ path, unremarkable for a poorly rinsed assembly --
or something about that session. Neither is worth further bench time.

One loose end: the fitted intercept, 0.1122 µA, and the directly measured idle,
0.1261 µA, are 11% apart where September had them within 1.1%. The fit
describes rest *between samples in a run*; the measurement is terminal sleep in
`IDLE`. They need not be identical, but the gap is now large enough to deserve
an explanation.

## 6. What else came out of it

- **`FLASH_SR.PEMPTY`.** A mass erase sends the part to the ROM bootloader
  until a power cycle. `flash_release.py --erase` now erases and programs in
  one invocation, which never opens the window that latches the flag, and
  clears it conditionally if it is set -- reading first, because writing 1
  toggles it. `tagmonitor.cc`'s check is now gated on an L4 device ID: it was
  reading `0x40022010` on every attach, which on an STM32U375 is
  `FLASH_OPTKEYR`, not `FLASH_SR`.
- **A byte-level backup of `.calibration` is not a backup of its meaning.**
  Dumping the page before a mass erase and writing the same bytes back gave a
  byte-identical readback and calibration that did not work.
- **BitTag sampled VDD and temperature on every wake** and fed an average,
  though both are only consumed when a record is written -- 35 conversions per
  stored value at the default format. Moved to the write site and the filter
  removed, in `d41b5357`.
- **The stored-config write is unchecked on seven of eight tags**, written up
  separately in
  [the proposal](../proposals/stored-config-write-is-unchecked.md).

## 7. What this leaves for the release

Every distributed image changes, so all five targets need requalifying. The
board files changed too: all five clock LSE from the RV-3028 but only
PresTagv3 was configured for it, so the other four carried the boot hang
`d16a930f` fixed. PresTag's default period moved from 90 s to 60 s.
