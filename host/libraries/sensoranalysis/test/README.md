---
type: readme
status: current
summary: How to build and run the offline sensoranalysis assertion checks for the magnetometer and accelerometer calibration metrics.
---

# sensoranalysis Offline Checks

Assertion programs that drive the calibration quality metrics with synthetic
data, so the metrics can be developed and regression checked with no tag, no
capture and no calibration. Every case has an answer known in advance.

They are plain `main()` programs with `assert()` and a failure count, not a
registered test framework, because this repository has no test harness to
register with. Build them explicitly:

```sh
cmake -DBUILD_SENSORANALYSIS_CHECKS=ON <build-dir>
cmake --build <build-dir> --target magquality_check
```

The binary lands in the build tree's `bin` and takes no arguments. It exits
non-zero when a check fails.

## `accelcalibration_check`

| Group | What it pins down |
| --- | --- |
| offset recovery | A planted 77 mg offset comes back to within 3 mg from a full sweep, with the radius at one g and the residual reporting the scatter that was planted rather than zero. |
| bounded buffer | However long the sweep, the buffer never exceeds patches x perPatch, and a long one fills every slot. |
| even occupancy | A sweep that dwells on one hemisphere for a thousand readings cannot crowd the rest out: per-patch slots bound what any one direction can take. This is the property the shared buffer did not have. |
| motion rejection | Readings at 1.8 and 0.2 g never reach the buffer and are counted as gated. |
| refusal | A yaw-only sweep is refused on patch count; a single-arc sweep occupies 14 of 32 patches and is refused on evenness instead. The second case is why both preconditions exist -- a patch count alone calls that sweep broad. |
| order independence | Fed forward and backward, with every slot filled and nothing displaced, the two fits agree to rounding. |

Validated against the committed 2425-sample fixture as well as synthetic data:
the 256 held readings give an offset within 1.6 mg of the fit over all 2223
gated readings in that capture, which is what the small independent buffer
rests on.

## `gravityfit_check`

| Group | What it pins down |
| --- | --- |
| offset recovery | A planted 77 mg offset -- the one measured on a real tag -- comes back to within a milli-g, the fitted radius is one g, and an unbiased sensor yields no offset rather than a small invented one. |
| motion rejection | A sweep with a quarter of its samples taken while the tag was thrown about still lands within five milli-g, and the second pass is never worse than the first. |
| refusal | Too few samples, a tag held in one attitude throughout, and a sphere nowhere near one g are all refused. The middle case matters most: a single attitude determines no sphere, and inventing a centre there would write a fabricated offset from a sweep that never happened. |

## `magquality_check`

| Group | What it pins down |
| --- | --- |
| patch lattice | Centres are unit vectors, each centre falls in its own patch, and a dense uniform sample set fills the busiest and emptiest patch to within 1.5x -- so a patch count means what it says. |
| coverage and isotropy | Uniform data reaches every patch with isotropy near one. Coplanar data -- a tag spun flat without being tipped -- still occupies many patches, so only the isotropy catches it. |
| dip consistency | A constant inclination comes back with no spread; a known 2 degree scatter comes back as about 2 degrees, since the MAD is scaled to agree with a standard deviation. |
| acceleration gate | Samples outside the gate are excluded from the dip statistics and cannot drag the inclination, while still counting toward coverage. That split is the point of the gate. |
| attitude diversity | Sweeping gravity within one magnetometer patch raises diversity above one; holding one attitude gives exactly one. Without an accelerometer, the accelerometer metrics report nothing rather than guessing. |

The dip and attitude cases are the ones worth keeping honest: they are the
metrics the solver does not optimise, which is what makes them worth having.
