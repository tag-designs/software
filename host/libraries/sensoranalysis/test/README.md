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

They are off by default. Turn them on in the build tree you already have --
this only sets a cache variable, so it does not reconfigure from scratch --
and build the one you want:

```sh
BUILD=~/Build/tag-designs/software-vcpkg-release      # the macos-vcpkg preset
cmake -B $BUILD -DBUILD_SENSORANALYSIS_CHECKS=ON
cmake --build $BUILD --target magquality_check
```

Every binary lands in `$BUILD/Release/bin` (`Debug` for the debug preset, and
whatever configuration a multi-configuration generator built on Windows). A
check takes no arguments and exits non-zero when it fails:

```sh
$BUILD/Release/bin/magquality_check
```

To build all of them at once, name each target, or build everything:

```sh
cmake --build $BUILD --target accelcalibration_check gravityfit_check \
                              magquality_check magretention_check capture_replay
```

## `capture_replay`

Not a check: a tool. It replays a saved `qtcalibrate` capture straight through
the library and prints what the calibration code makes of it, sample by
sample.

```sh
REPLAY=$BUILD/Release/bin/capture_replay
FIXTURE=host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-163303.json

$REPLAY $FIXTURE
$REPLAY $FIXTURE --csv > trace.csv          # one row per tick, for plotting
$REPLAY $FIXTURE --patches 64 --every 500
```

A fixture path relative to the repository root resolves wherever the tool is
run from: the source root is compiled in at configure time, the same way
`qtcalibrate` resolves `--replay-capture`. An absolute path works too. Output
redirection is ordinary shell, so `trace.csv` lands in the current directory.

It reports the accelerometer offset as it develops, how far it moves over the
second half of the sweep, patch coverage and evenness, and the robust
inclination spread with and without the offset applied -- the last computed
through `CompassProcessor`, the same routine `qtcalibrate` uses, and gated the
same way `MagQuality` gates it.

The point is the questions a single end-of-run number cannot answer: does the
estimate settle or wander, when does a metric stop improving, did a change
move anything. Asking them through `qtcalibrate` needs the Qt build, a window
and a run in real time; this needs a fixture and a second, which makes a
design question cheap enough to actually test. The sliding-window fit that
made the offset wander 13 mg was found this way.

On the committed 2425-sample fixture it reports an inclination spread of
3.61 degrees uncorrected and 2.47 with the offset removed, against the 2.58
`qtcalibrate` logs for the same capture -- the small difference being that
`qtcalibrate` measures over its retained 650 samples rather than all of them.

## `accelcalibration_check`

| Group | What it pins down |
| --- | --- |
| offset recovery | A planted 77 mg offset comes back to within 3 mg from a full sweep, with the radius at one g and the residual reporting the scatter that was planted rather than zero. |
| keeps everything | However long the sweep, every accepted reading contributes; there is no buffer to overflow and nothing is discarded. |
| even occupancy | A thousand readings in one direction against two thousand over the sphere do not invent an offset: each occupied patch carries the same weight however long the operator lingered in it. |
| settling | With 30 mg of noise the offset moves under 3 mg over the second half of a sweep. The rule this replaced kept the newest readings per patch, which made the fit a sliding window that wandered 13 mg peak to peak on the reference capture -- 0.76 degrees of tilt. |
| motion rejection | Readings at 1.8 and 0.2 g never reach the buffer and are counted as gated. |
| refusal | A yaw-only sweep is refused on patch count; a single-arc sweep occupies 14 of 32 patches and is refused on evenness instead. The second case is why both preconditions exist -- a patch count alone calls that sweep broad. |
| order independence | Fed forward and backward the two fits agree to rounding, which they must: the fit is a function of sums. |

Validated against the committed 2425-sample fixture as well as synthetic data:
it settles at 74.5 mg with 1.3 mg of movement over the second half of the
sweep, against the 74.8 mg a per-patch-weighted fit over that whole capture
gives offline.

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

## `magretention_check`

| Group | What it pins down |
| --- | --- |
| leverage | The sample contributing least to the fit is the one chosen, and the hat-matrix trace identity holds: the leverages sum to the parameter count. |
| outliers | A planted bad magnitude is evicted as an outlier rather than as uninformative, and with the rule off leverage alone never removes it -- it is a high-leverage sample, not a redundant one. |
| not outliers | A lone sample in an otherwise empty direction, at an honest radius, survives. Being the only reading in a direction is not evidence that it is wrong, and under Cook's distance it was accused precisely because it was informative. |
| threshold units | A 3 sigma residual is kept and a 6 sigma one is evicted, so the configured threshold means what it says. |
| guards | Probation protects a newly added sample, and no eviction ever empties an occupied patch. |

The dip and attitude cases are the ones worth keeping honest: they are the
metrics the solver does not optimise, which is what makes them worth having.
