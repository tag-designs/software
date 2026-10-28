---
type: readme
status: current
summary: calibsession, a safe Rust wrapper over the tag calibration session and compass display angles in tagcore_c - so a Rust program calibrates exactly as qtcalibrate does; building, the API, and the replay test against qtcalibrate's own figures.
---

# calibsession (Rust)

A safe Rust interface to the live magnetometer and accelerometer
calibration. `Session` wraps the `tcs_*` functions of
[`tagcore_c`](../tagcore-c/README.md), which wrap sensoranalysis's Qt-free
`calibcore::CalibrationSession`
([`calibration_session.h`](../sensoranalysis/core/calibration_session.h)).
That is the code qtcalibrate's `CompassData` runs, so a Rust program -- the
Tag Calibrator (`host/applications/tagcalibrate-rs`)
first -- calibrates exactly as qtcalibrate does. Two functions,
`display_angles` and `model_rotation`, give the heading, pitch and roll every
CompassTag display shows and the rotation qtcalibrate turns its tag model by.

| What | Call |
| --- | --- |
| Feed a sample; true when the solver accepted a new fit | `add_sample(mag, Option<accel>)` |
| Recompute the quality figures and the accelerometer fit (the caller's tick) | `update_quality()` |
| The figures | `quality()`, `accel_fit()`, `evictions()` |
| The fit, and constants to or from a tag | `constants()`, `set_constants()`, `set_accel_offset()`, `field()` |
| The buffer, calibrated, for the sphere | `points()`, `calibrate()` |
| The filtered live compass | `orientation(mag, accel)` |
| Start again | `clear()` |

Units are the calibration stream's: µT and mg; quaternions are `[w, x, y, z]`.

## Building

As for [`taglogwriter`](../taglogwriter-rs/README.md), which links the same
library:

```sh
cmake -DBUILD_TAGCORE_C=ON <build-dir>
cmake --build <build-dir> --target tagcore_c_static tagcore_c
export TAGCORE_C_LIB_DIR=<build-dir>/Release/lib
cargo test
```

macOS links the static archive, Linux and Windows the shared library;
[`tagcore-c-sys`](../tagcore-c-sys/README.md) does the linking for this crate
and the other wrappers, so a program may use them together.

## Checking

`tests/replay.rs` replays qtcalibrate's reference capture
(`host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json`)
through a `Session` as qtcalibrate's own replay does -- the live compass, then
the sample, and the quality tick every second sample -- and requires every
figure qtcalibrate logged for the same replay, to the precision it logs them:
coverage 90/100, isotropy 0.722, attitude 2.06 over 185 cells, dip
63.54 ± 2.70, the accelerometer offset, radius and residual, 691 readings held
of 775 offered with 84 gated, and 125 evictions, 105 by leverage and 20 as
outliers. It passed on 2026-10-28.

The bit-for-bit evidence is on the C++ side, where the session was moved out
of qtcalibrate: see
[`sensoranalysis/test/README.md`](../sensoranalysis/test/README.md).

Every item is documented (`missing_docs` and
`clippy::missing_docs_in_private_items` warn):

```sh
cargo clippy --all-targets 2>&1 | grep -c "missing documentation"   # expect 0
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items
```
