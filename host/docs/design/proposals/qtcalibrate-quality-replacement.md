---
type: proposal
status: proposed
summary: Replace the inherited magcal/quality.c with owned code: a leverage-based retention policy, robust residual statistics, an accelerometer dip-consistency check, and a heading-accuracy headline metric.
---

# Replacing qtcalibrate's Calibration Quality Code

Done as of 2026-10-10, bar the heading-accuracy metric of phase 4.
`magcal/quality.c` is deleted and `CompassData::choose_discard_magcal()` is one
line calling the leverage rule; what the two did now lives in
[`magquality.h`](../../../libraries/sensoranalysis/magquality.h) and
[`magretention.h`](../../../libraries/sensoranalysis/magretention.h), with the
accelerometer split off into
[`accelcalibration.h`](../../../libraries/sensoranalysis/accelcalibration.h).
The measurements behind it are in
[the investigation](../investigations/2026-10-qtcalibrate-quality-experiments.md).

What follows is the proposal as written. `qtcalibrate` judged a magnetometer
calibration with four numbers produced by `magcal/quality.c`, and decided which
samples to keep with `CompassData::choose_discard_magcal()` in
[`compassdata.cpp`](../../../applications/qtcalibrate/compassdata.cpp). Both
were inherited from PJRC's MotionCal. This proposal replaces them with code we
own, keeping the equal-area coverage idea and discarding the rest.

The Freescale solver in
[`magcal.c`](../../../applications/qtcalibrate/magcal/magcal.c) is out of scope
and does not change.

## Purpose

The present metrics answer the wrong question. They report how well a fitted
ellipsoid reproduces the samples that were used to fit it, which is a measure
the fit can satisfy while the calibration is still bad. What an operator
calibrating a tag needs to know is whether the samples *determine* the
calibration, and what heading error the result implies. Those are different
questions and need different statistics.

The retention policy has the same problem in a sharper form: it decides which
sample to throw away using the calibration derived from the samples it has
already kept, and its own source comment identifies the resulting feedback loop
as the main risk.

## Problem

### What the current code computes

| Metric | Definition | What it actually measures |
| --- | --- | --- |
| `quality_surface_gap_error` | Weighted count of the 100 equal-area regions holding 0, 1 or 2 samples (penalty 1.0, 0.2, 0.01) | Coverage, as an unnormalised penalty with no upper bound and no units |
| `quality_magnitude_variance_error` | Sample standard deviation of `|m_cal|` over the buffer, as a percent of the mean | Geometric residual spread — the most honest of the four |
| `quality_wobble_error` | Norm of the mean *signed* per-region offset from the ideal sphere, as a percent of radius | A residual hard-iron estimate, weakened by sign cancellation |
| `quality_spherical_fit_error` | `magcal.FitError`, the solver's algebraic residual | Not a geometric distance, and artificially floored |

`sphere_region()` partitions the sphere into 1 + 15 + 34 + 34 + 15 + 1 bands
with boundaries at ±78.52° and ±42.84°. Those boundaries are correct: the cap
area formula `2*pi*h` with `h = 1 - sin(lat)` makes every region exactly 1/100
of the sphere. The equal-area idea is sound and is kept.

### Defects found

1. **Southern-band reference points are at the wrong longitudes.** In
   `quality_reset()`, regions 84–98 compute `longitude` from `(i - 1)` where
   the matching offset is `(i - 84)`. Region 84's reference point lands near
   204° instead of 12°. The bug is inherited from upstream MotionCal, not
   introduced here. It corrupts `quality_wobble_error` for 15 of 100 regions.

2. **Sample retention is O(N^2) per sample.** Once the buffer is full,
   `choose_discard_magcal()` runs a nearest-pair scan over `MAGBUFFSIZE = 650`
   entries — about 211,000 distance evaluations, each constructing two
   `QVector3D` temporaries — for every sample added. The solver itself only
   runs every 20th sample (`waitcount` in `MagCal_Run`); the discard scan has no
   such rate limit.

3. **The retention policy reads a stale coverage figure.** `choose_discard_magcal()`
   branches on `quality_surface_gap_error()`, whose backing histogram is only
   rebuilt by `CompassData::qualityUpdate()` on a 200 ms UI timer while samples
   arrive on a 100 ms timer. The gap figure is also computed against whatever
   calibration was current at the last rebuild, not the current one.

4. **The outlier branch cannot help when it is needed.** It only runs once
   coverage is already good (`gaps < 25.0f`), which is exactly when outliers
   matter least. Early in a capture, when a single bad sample can drag the fit,
   only the random nearest-pair branch runs.

5. **Module-global mutable state.** `count`, `spheredist`, `spheredata`,
   `sphereideal` and the three cache flags are file-scope statics. The module
   cannot hold two calibrations, cannot be unit tested in isolation, and is not
   reentrant. `sphereideal` is exported non-`const` through the header.

6. **`quality_update()` has no bound check.** It writes `magnitude[count]` and
   increments `count` without testing against `MAGBUFFSIZE`. It is safe only
   because its single caller resets first and iterates exactly `MAGBUFFSIZE`
   times. A second caller overflows the array.

7. **The displayed fit error is not comparable across solvers.** `MagCal_Run`
   floors `trFitErrorpc` at 12% for the 4-element solver and 7.5% for the
   7-element solver. The number therefore steps discontinuously as the sample
   count crosses 100 and 150, which reads to an operator as a regression.

8. **The four numbers are unlabelled.** They are printed into a single
   `QString::asprintf` with no units, no names and no pass/fail thresholds.

9. **`quality.c` carries a copyright notice with no licence grant.** Every
   other inherited file in `magcal/` carries the Freescale BSD-3-Clause text.

## Goals

- Replace `quality.c` and `choose_discard_magcal()` with code the project owns
  and can license.
- Make coverage a structural property of the buffer rather than an emergent
  property of a heuristic.
- Report at least one metric that is independent of the residual the fit
  minimises, so that over-fitting is visible.
- Report a headline number in degrees of heading error.
- Keep the retention policy O(1) amortised per sample.
- Keep the metrics computable incrementally so the 200 ms full rebuild can go
  away.

## Non-Goals

- Changing the Freescale solver, its 4/7/10-element progression, or the
  `MagCalibration_t` wire representation written to tags.
- Moving to a 12-parameter asymmetric fit. That is a solver change and belongs
  in its own proposal; see [Accelerometer data](#accelerometer-data) for why
  this document stops short of it.
- Changing the capture file schema `tag-designs.qtcalibrate.calibration-capture.v1`,
  beyond adding fields to the `quality` block.
- Automatic stop-when-good. The new metrics make that possible; deciding the
  thresholds needs field data we do not have yet.

## Survey of techniques

### Coverage

The question behind coverage is whether the sample set determines the
parameters. Three families of answer, in increasing strength:

**Patch occupancy** — what we have. Cheap, explainable, visualisable, and it
maps directly onto the operator's mental model of "turn the tag until the ball
fills in". Its weakness is that it is a proxy: 100 occupied patches with one
sample each is reported as perfect coverage and is a poor conditioning.

**Second-moment isotropy.** Accumulate `S = sum(m_hat * m_hat^T)` over unit-
normalised calibrated samples. For uniform coverage all three eigenvalues equal
`N/3`; `lambda_min / lambda_max` falls toward zero as the samples approach a
plane. This is O(1) incremental, needs a 3x3 eigensolve only when reported, and
catches the single most dangerous real failure — coplanar data from a tag spun
on a bench without being tipped — more sharply than patch counting does.

**Information-matrix conditioning.** Build the design matrix `D` whose rows are
the 10-element measurement vector the solver already forms
(`[x^2, 2xy, 2xz, y^2, 2yz, z^2, x, y, z, 1]`), and look at `D^T D`. Its
condition number states observability directly and scale-free; `det(D^T D)^(1/p)`
(D-optimality) gives an information-volume measure that normalises against the
value an ideal uniform sphere of the same N would produce, yielding an
interpretable 0–1 efficiency. This is the standard optimal-experiment-design
scalarisation and it is the quantity the retention policy below optimises.

The three are complementary, not competing: patch occupancy for the operator,
isotropy as a cheap guard, conditioning as the policy's objective.

### Residual

The current magnitude-variance metric uses a plain standard deviation, which a
handful of outliers can dominate. On the checked-in fixture the ordinary
standard deviation of `|m_cal|` over all 775 samples is **0.90%** against a
MAD-based robust estimate of **0.82%** — close, because that capture is clean.
An earlier capture taken before the tag-side axis correction showed 8.61%
against 1.78%, a factor of five driven entirely by a few extreme points.

So the honest case for the robust statistic is not that it flatters a good
capture; it is that it stays meaningful on a bad one, and the operator cannot
tell which kind they have while collecting. Reporting the robust spread
alongside a 95th-percentile residual says more than either moment alone.

Residual *structure* matters as much as residual size. A fit whose residuals
show a systematic pattern with direction has an unmodelled term — an
asymmetric soft iron the 9-parameter model cannot express. Regressing the
per-patch mean residual on patch index detects this; the current wobble metric
gropes toward it but averages signed offsets, so opposite-side errors cancel.

A hold-out residual — fit on a random 80%, score the magnitude residual on the
held-out 20% — catches the over-fitting case where `FitError` looks excellent
because the model has absorbed the noise. The fit is fast enough to afford this
on the 200 ms timer.

### Parameter uncertainty

With residual variance `sigma^2`, the parameter covariance is
`sigma^2 * (D^T D)^-1`, giving standard errors on the hard-iron vector in µT
and on B. The quantity an operator can act on is the heading error those
uncertainties imply: a hard-iron error `dV` perpendicular to the horizontal
field produces a heading error of roughly `dV / (B * cos(dip))` radians. The
honest version propagates the full covariance by sweeping synthetic headings
through the fitted and perturbed calibrations — which is what Tom Judd's
`gendat3.py` does when it reports "estimated final compass accuracy expressed in
degrees".

**This should be the headline number.** Four unlabelled percentages are not
actionable; "heading accuracy ±1.4° (95%)" is, and it is the number that decides
whether a tag is fit to deploy.

### Accelerometer data

The cited reference,
[juddzone.com — Precision 3D Compass Calibration](http://www.juddzone.com/ALGORITHMS/least_squares_precision_3D_ellipsoid.html),
does **not** propose rejecting samples on acceleration magnitude. What it
proposes is using the accelerometer as an additional *constraint*: the dot
product `m_cal · a_hat` is constant for a fixed site, and imposing that
constraint lets the fit resolve all nine elements of an asymmetric
transformation matrix instead of the six a symmetric one allows. That is a
solver change, and this proposal does not make it. The page's relevance here is
its remark that precise accelerometer data is required and "not always possible,
especially on a moving platform" — which is where a motion gate would come in.

Tested against the fixture, the acceleration-magnitude gate is **not supported
as a magnetometer-sample rejection criterion**:

| Gate on `abs(\|a\| - 1 g)` | Samples kept | Regions covered | Robust residual spread |
| --- | --- | --- | --- |
| none | 775 (100%) | 94 / 100 | 0.82% |
| < 100 mg | 671 (87%) | 92 / 100 | 0.80% |
| < 50 mg | 399 (51%) | 89 / 100 | 0.80% |
| < 30 mg | 246 (32%) | 78 / 100 | 0.75% |

The correlation between `abs(|a| - 1 g)` and `abs(|m_cal| - median)` across the
capture is **0.051** — essentially none. Discarding 68% of the samples buys a
0.07-point improvement in residual spread and costs sixteen regions of
coverage. On this evidence a motion gate on the magnetometer sample is clearly
not worth its cost. (One capture, n = 775, from a CompassTagAT25 turned by hand
on a bench; this is a reason to leave the gate out of the first implementation,
not a settled result.)

The accelerometer is still worth having, in three other places:

1. **Dip consistency as an independent metric.** The angle between `m_cal` and
   `a_hat` should equal `90° - dip` at every sample. Its spread across the
   buffer is a quality measure that does *not* come from the residual the
   solver minimises, so unlike `FitError` it cannot be improved by over-fitting.
   This is Judd's constraint used as a diagnostic rather than as an objective,
   which keeps the solver untouched. It is the most valuable thing the
   accelerometer gives us within this scope.

2. **Gating that metric, not the sample.** When `abs(|a| - 1 g)` is large,
   `a_hat` is not gravity, so the dip residual for that sample is meaningless.
   Gate the *dip metric* on acceleration magnitude and keep the magnetometer
   sample in the fit. This is the clean resolution of the rejection question:
   the gate belongs on the metric that depends on gravity, not on the one that
   does not.

3. **Attitude coverage.** Two samples can occupy the same magnetic patch at
   different tag attitudes. Binning on (magnetic patch × gravity patch) is a
   strictly stronger coverage statement than magnetic patch alone, and it is
   the coverage a 12-parameter fit would need if we ever adopt one.

Acceleration is not currently plumbed into the solver path at all:
`MainWindow::processCalibrationSample()` passes only the magnetometer vector to
`CompassData::addData()`. Carrying it alongside is a prerequisite for all three.

## Proposed design

### `magquality` — a new owned module

A context struct replaces the file-scope statics, so the module is testable,
reentrant, and able to hold a second calibration for hold-out scoring.

```c
typedef struct {
    /* coverage */
    uint16_t patch_count[MAGQ_PATCHES];
    Point_t  patch_sum[MAGQ_PATCHES];
    uint16_t patches_occupied;

    /* incremental second moment of unit-normalised samples */
    float    S[3][3];

    /* residual accumulators */
    float    resid[MAGQ_MAX_SAMPLES];
    uint16_t resid_n;

    /* dip consistency, gated on |a| */
    float    dip_resid[MAGQ_MAX_SAMPLES];
    uint16_t dip_n;
} MagQuality_t;
```

**Patch scheme.** Keep equal-area patches, but generate them from a Fibonacci
lattice rather than latitude bands: `N` points with `z_k = 1 - (2k+1)/N` and
`lon_k = k * golden_angle`. Nearest-centre lookup is a bounded search over the
few candidates in the neighbouring `z` slab, so it stays O(1) with a small
precomputed index. This removes the banding artefact at the band boundaries,
removes the hand-written region arithmetic that defect 1 lives in, makes `N` a
parameter instead of a constant woven through the code, and is derived from
published geometry rather than adapted from inherited source.

**Reported metrics.**

| Name | Units | Replaces |
| --- | --- | --- |
| Heading accuracy (95%) | degrees | — (new headline) |
| Coverage | patches occupied / N, plus `lambda_min/lambda_max` | `quality_surface_gap_error` |
| Residual spread (robust) | % of B | `quality_magnitude_variance_error` |
| Residual 95th percentile | % of B | — |
| Residual hard iron | µT | `quality_wobble_error` |
| Dip consistency | degrees | — (independent of the fit) |
| Fit error | % | `quality_spherical_fit_error`, unfloored |

Each gets a name, a unit, and a green/amber/red threshold in the UI rather than
a bare percentage in a shared label.

**Residual hard iron** replaces wobble properly: take the patch-weighted
centroid of unit-normalised calibrated samples — one vote per occupied patch,
not per sample, so dense patches do not dominate — and report its norm in µT.
For a correct calibration it is near zero, and unlike the present metric it
does not cancel opposing errors.

> **This does not work, and what was built instead uses the magnitude.**
> Assigning samples to patches by direction pins each patch's mean direction
> to that patch's centre, so the bias being looked for is averaged away: on a
> planted 2 µT offset the construction above reads 0.05. An offset `d` puts a
> sample in direction `u` at `|B + d.u|`, so the magnitude residual *is* `d.u`,
> and regressing residual on direction across the occupied patches recovers
> `d` itself — 1.97 on that offset, 0.00 with none, and unchanged by six
> thousand readings dwelling in one direction, which is the robustness the
> patch weighting was wanted for.

### Retention policy

Replace both branches of `choose_discard_magcal()` with one rule drawn from
optimal experiment design. For design matrix `D`, the leverage of sample `i` is

```
h_i = d_i^T (D^T D)^-1 d_i
```

and removing row `i` scales `det(D^T D)` by `(1 - h_i)` (matrix determinant
lemma). So **the least informative sample is the one with the lowest leverage** —
which is precisely what the nearest-pair heuristic is reaching for, since a
sample with a close neighbour is redundant, but computed exactly and in O(p^2)
per candidate with `p = 10`, against O(N^2) for the scan it replaces. Maintain
`(D^T D)^-1` by rank-1 update and downdate as samples enter and leave.

The same quantity gives the outlier test for free. An influential outlier is
one with both high leverage and a large studentised residual; Cook's distance

```
C_i = (r_i^2 / (p * s^2)) * (h_i / (1 - h_i)^2)
```

combines them. The policy becomes:

- if any `C_i` exceeds the outlier threshold, eject `argmax C_i`;
- otherwise eject `argmin h_i`.

One framework replaces two unrelated heuristics, it runs at every sample count
rather than only once coverage is good, and it needs no stale coverage figure.

Two guards against the feedback loop the original comment warns about:

- **Patch floor.** Never eject the last sample in an occupied patch. Coverage
  becomes an invariant the optimiser cannot trade away, rather than something
  the heuristic is hoped to produce.
- **Probation.** A newly added sample is not a candidate for ejection for the
  next `k` additions, so a transient bad calibration cannot immediately purge
  the evidence that would correct it.

### Plumbing

`CompassData::addData()` and `MainWindow::processCalibrationSample()` grow an
optional accelerometer argument, stored alongside each buffer entry. Nothing
else needs it; the solver continues to see magnetometer vectors only.

## Evidence

All figures above come from the checked-in replay fixture
[`qtcalibrate-samples-20261010-145438.json`](../../fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json)
(775 samples from a CompassTagAT25, accelerometer in milli-g, B = 45.66 µT),
scored with the calibration stored in the same file. That capture agrees with
the magnetometer axis contract in
[docs/shared/sensor-axes.md](../../../../docs/shared/sensor-axes.md); the
fixture it replaced did not, which is why the figures here differ from earlier
drafts. Before implementation the same analysis
should be repeated over several captures taken with a known reference heading,
so that the heading-accuracy metric can be checked against ground truth rather
than against itself.

## Phasing

| Phase | Work | Verifiable by |
| --- | --- | --- |
| 1 | New `magquality` module with the context struct, Fibonacci patches, robust residual statistics, isotropy; computed in parallel with the existing metrics and logged, with nothing switched over | Replay the fixture; both metric sets printed side by side |
| 2 | Plumb accelerometer through `addData`; add the gated dip-consistency metric | Fixture replay shows a dip spread consistent with the site |
| 3 | Add `(D^T D)^-1` maintenance and the leverage/Cook's-distance retention policy behind a flag, with the patch floor and probation guards | Replay with both policies; compare final calibration and wall-clock cost |
| 3a | Fit the accelerometer zero-g offset from the same sweep; apply it when deriving dip and orientation; show it with the other constants. Host-side only | Replay the existing captures: offset near 77 mg, dip spread about 31 percent better |
| 4 | Heading-accuracy propagation; relabel the UI with named, united, thresholded metrics | Captures at known headings |
| 5 | Delete `quality.c` and `choose_discard_magcal()`; extend the capture `quality` block | `docs.py check`; replay fixtures regenerate |

### Where phase 3 stands

Built and flag-gated, off by default, as `--leverage-retention`. Measured on a
four-minute capture, about 2,400 samples and 1,760 evictions, replayed both
ways. The curves coincide exactly until the buffer fills at 650, which is the
control: the same code on the same data, so everything after it is real.
Steady state, over the last quarter of each run:

| | inherited | leverage | delta |
| --- | --- | --- | --- |
| direction spread | 0.906 ± 0.014 | 0.934 ± 0.021 | **+0.028** |
| dip spread | 3.613 ± 0.101 deg | 3.307 ± 0.080 deg | **−0.307** |
| attitude diversity | 2.440 ± 0.031 | 2.099 ± 0.024 | −0.342 |
| coverage | 100/100 | 100/100 | — |

The dip spread result is the one that carries weight, because dip is not a
quantity the solver optimises and nothing in the leverage computation knows
about gravity. A better direction spread could be the policy flattering the
measure it was designed around; a better dip spread cannot be.

**The improvement arrives only after coverage saturates.** Split at the tick
where coverage reaches 100/100, the dip spread delta is +0.021 while patches
are still filling and −0.357 afterwards. While a new direction is still
available, which sample is discarded hardly matters; once none is, the only
lever left is which samples are kept. That is the opposite of what one might
assume from the end numbers alone.

Two findings against the policy, both honest:

- **Attitude diversity fell, and the metric is at fault rather than the
  policy.** It is a per-patch mean -- distinct gravity patches per occupied
  magnetometer patch -- so it rewards uneven occupancy: a patch holding thirty
  samples reaches more gravity bins than one holding six. Leverage spreads
  samples more evenly, so each patch holds fewer. Direction spread rising and
  attitude falling are one redistribution counted twice with opposite signs.
  The sound measure is the total count of distinct (magnetometer, gravity)
  cells, which `MagQuality` already computes and does not yet log.
- **The outlier threshold is wrong for a buffer this size.** Cook's distance
  scales with leverage, and with 650 samples the mean leverage is p/n = 0.015,
  so the textbook `D > 1` needs a 25-sigma residual -- about 9 uT, a fifth of
  the field. Nothing fired because nothing could. A five-sigma rule is
  `D > 0.04`. The right value wants measuring on a capture with real
  disturbances in it, not another guess.

One observation worth recording because it is easy to misread: the isotropy
sag partway through a collection, from about 0.72 down to 0.50 and back, is
**not** the discard policy. The first eviction on this fixture happens 86
percent of the way through, long after the sag, and both policies show it
identically. It is the geometry of a partial sweep.

Phases 1–3 each leave the tree working with the old behaviour intact, so the
switchover is a single reviewable commit at phase 5.

## Licensing

`quality.c` carries a copyright notice with no licence grant, which is the
reason this replacement is worth doing independently of its technical merits.
The replacement should be written from the published geometry — the spherical
cap area relation, the Fibonacci lattice construction, and the standard
leverage and Cook's-distance definitions — and not adapted line by line from
the inherited file. Choosing a different patch construction rather than
re-deriving the latitude-band one makes that separation easy to demonstrate.
`magcal.c` and `matrix.c` keep their Freescale BSD-3-Clause headers and are
untouched.

This is an engineering judgement, not legal advice; if the ambiguity matters
for distribution it should be confirmed with counsel.

## Calibrating the accelerometer

The dip spread has a floor of about 3.5 degrees that is flat across every
rotation rate, so nothing about how the tag is turned explains it. The
accelerometer does.

At rest in any orientation an accelerometer reads gravity, so a calibration
sweep traces a sphere of radius one g. Fitting that sphere on the reference
captures puts its centre **77 mg from the origin**, dominated by **−71 mg on z**
— the axis perpendicular to the board. The LIS2DU12 datasheet gives ±11 mg
typical after factory trim, so this is seven times out of family, but that
figure is measured on the bare part: soldering it to a board adds package
stress, and z is the axis that stress acts on. Post-assembly offsets well beyond
the trimmed figure are the expected outcome, not a defective part.

A fixed offset in the tag frame produces a tilt error that varies with
orientation — maximal when the offset is perpendicular to gravity, zero when
parallel — so it appears as scatter rather than bias, and has no reason to care
how fast the tag is turning. That is the observed signature exactly. The implied
RMS tilt error is 3.1 degrees against an observed floor of 3.5.

Running the fit over both captures, with the gating described below:

| | 775-sample capture | 2,425-sample capture |
| --- | --- | --- |
| recovered offset | 78.2 mg | 75.9 mg |
| fitted radius | 995.0 mg | 993.7 mg |
| dip spread before | 4.18 deg | 3.67 deg |
| dip spread after | 2.88 deg | 2.54 deg |
| improvement | 31% | 31% |

The two agree to 2.3 mg, which is what makes the offset worth storing: it is a
stable property of the tag, not noise.

### Why a sphere, and why no buffer

**Four parameters, not nine.** Adding per-axis scale terms took the same
captures from 30 to 34 percent, which does not pay for the machinery. The
fitted radius lands within one percent of one g, so the sensitivity constant is
already right and only the offset is worth removing.

**No buffer and no retention policy.** A sphere fit is linear least squares, so
the entire state is ten running sums; each sample is folded in and discarded in
constant time. The magnetometer needs a buffer because it refits ten parameters
repeatedly as the solution improves and must be able to remove a sample again.
Gravity has constant magnitude by definition, and four parameters determine it.

**Two passes.** The gate cannot be tight on the first pass, because the offset
being estimated is itself several percent of one g and a tight gate would
reject orientations rather than motion. So the first pass gates at 15 percent,
and the second re-gates at 8 percent on the magnitude *after* correction, which
removes the circularity of gating on the quantity being estimated. On the
reference captures that moves the estimate from 71 mg to 78 mg.

### What it does not fix

Dip spread lands at 2.5 degrees, not the 1 degree a pure-offset model predicts,
so roughly 2.5 degrees remains unexplained — and the per-axis scale result says
it is not accelerometer linearity. The inclination also stays at 63.6 degrees
against the World Magnetic Model's 66 to 67, unmoved by the correction, so that
gap is a separate matter.

### Scope

*Superseded 2026-10-10: the offset is now written to the tag as
`CalibrationConstants.accelerometer` and applied by SensorViz; see
[decision 0027](../../../../docs/decisions/0027-firmware-l432-calibration-pinned-to-top-page.md)
for the storage layout.*

Host-side for now. The constants are fitted during a sweep, applied when
deriving dip and orientation, shown with the other calibration constants and
recorded in the capture, but not written to the tag: storing them there is a
firmware change and `CalibrationConstants` would need an `AccelConstants`
field, which is additive but is a contract both sides compile against.

One consequence worth stating. The dip metric is recomputed from the buffer on
every quality tick, so it tracks both calibrations as they evolve and tightens
visibly during a sweep. That does not cost its independence: the accelerometer
fit minimises deviation of `|a|` from a sphere and the magnetometer fit
minimises deviation of `|m|`, and neither optimises the angle between them. Dip
spread remains a quantity no solver in the system is targeting. It does mean an
early reading is not comparable with a late one, so the value recorded in a
capture should be a final pass taken after collection stops.

## The calibration environment is not controllable

Calibration happens with the tag tethered to a computer over USB, because that
is how samples reach the host. The computer, its supply and the cable sit
within centimetres of the magnetometer, and the room is whatever room the user
is in. Users are not ours to instruct. This is a constraint to design around,
not a problem with a fix.

It is measurable. On the reference captures the fitted field is **12.0 and 12.3
percent below** the World Magnetic Model value for the site (45.66 and 45.53
against 51.9 uT), and the inclination is about **3 degrees shallow** (63.6
against 66 to 67). Both captures agree with each other to 0.3 percent in field
and 0.16 degrees in dip, so the bench is repeatable even though it is wrong in
absolute terms.

What that does and does not cost:

- **Heading is unaffected by a uniform error.** It depends on the direction of
  the calibrated vector, not its length, so a field read 12 percent low gives
  the same heading. `B` is the one constant that comes out as a measurement of
  the room rather than of the Earth, and nothing downstream of heading uses it
  beyond the solver's 22-67 uT sanity range, which 45.7 passes.
- **Distortion fixed relative to the tag is calibrated out, correctly.** That
  is what hard and soft iron terms are for, and a cable routed the same way
  every time is indistinguishable from the tag's own iron. It transfers to the
  field with the tag.
- **Distortion that changes as the tag moves cannot be calibrated out.** A
  laptop that stays put while the tag is rotated through the space beside it
  presents a different field at each orientation. That smears the sphere, and
  it is the irreducible part.

The consequence for this proposal is that the metrics fall into two classes,
and they cannot carry the same kind of threshold:

| Class | Metrics | What a threshold means |
| --- | --- | --- |
| Portable | coverage, direction spread | Operator technique. A number means the same on any bench, because it describes how the tag was turned. |
| Environment limited | dip spread, residual spread | The bench. Their floor is set by the room and the cable, not by the tag, so the same tag scores differently in two places. |

So a fixed pass/fail on dip spread is not portable, however convenient it would
be. What is defensible: flag the portable metrics against absolute thresholds,
report the environment-limited ones as numbers to compare against that user's
own previous captures, and reserve hard failures for the cases that are
unambiguous anywhere -- no coverage, or a design the data cannot determine.

Two cheap checks remain available to anyone who wants them, neither needing a
field trip. Repeating a capture in a different spot in the same room separates
a uniform local field, which is harmless, from one that varies across the
sweep, which is not. And comparing fitted `B` against the World Magnetic Model
for the site says how disturbed the bench is, as a one-off sanity check rather
than something to wire into the app -- indoors it would report a large residual
that is real and has nothing to do with the sensor.

## Open questions

1. What heading accuracy counts as good enough for a deployed songbird tag?
   Provisionally: **under 2 degrees is good enough, and under 1 degree is at
   the limit of what most eCompasses achieve** -- so 1 degree is the floor
   worth designing against rather than a target to chase. This is judgement,
   not measurement; it needs experiment before it hardens into a pass/fail
   threshold in the UI. Until then the metrics are reported without a verdict,
   which is the honest presentation of a number whose acceptable range nobody
   has established yet.

   The dip spread is a weaker guide than it first looks. It is an upper bound
   on heading error rather than an estimate, since it carries calibration
   error, accelerometer error and sample timing together -- and, as the section
   above sets out, the bench as well. A spread of 3 to 4 degrees on a tethered
   indoor capture is mostly a statement about the room. Answering this question
   needs a measurement that does not inherit the calibration environment.
2. Is `MAGBUFFSIZE = 650` still the right buffer size once retention is
   principled? A smaller, better-chosen set may fit as well.
3. How many patches? 100 is inherited. With a Fibonacci lattice, N becomes a
   tuning parameter, and the right value depends on how many samples an
   operator can realistically collect.
4. Should the hold-out residual run on the UI timer, or only when the operator
   asks for a quality report?
5. Does the 12-parameter asymmetric fit justify its own proposal? The dip
   consistency metric from phase 2 is the evidence that would decide it: a
   persistent dip spread that the symmetric fit cannot reduce is the signature
   of a transformation matrix that needs its off-diagonal terms freed.
