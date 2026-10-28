---
type: readme
status: current
summary: The qtcalibrate replay fixtures (a CompassTagAT25 and an IMUTagNandBmp581 capture), the axis check each must pass, and the commands that replay them and generate the documentation screenshots.
---

# QtCalibrate Documentation Fixtures

Store curated `qtcalibrate` sample captures here for documentation screenshot
generation and replay tooling.

| Fixture | Tag | Firmware | Samples | Axis check |
| --- | --- | --- | --- | --- |
| `qtcalibrate-samples-20261010-145438.json` | CompassTagAT25 | -- | 775 | inclination 63.65 degrees, spread 4.11 |
| `qtcalibrate-samples-20261028-171428.json` | IMUTagNandBmp581 | `7cef2ba9`, 2026-10-28 | 578 | inclination 63.71 degrees, spread 2.86 |

The CompassTag capture is the baseline: the documentation screenshots, the
sensoranalysis checks and the Tag Calibrator's replay tests use it. The IMUTag
capture is the first from firmware that maps the BMM350 into the host's axis
frame itself (`IMUTAG_MAG_TAG_*`); Tag Calibrator's replay test checks that it
keeps scoring as above. Both are replays of a real sweep at one site, so their
dip is the same.

A fixture must agree with the magnetometer axis contract in
[docs/shared/sensor-axes.md](../../../../docs/shared/sensor-axes.md).
Check a candidate before committing it:

```sh
host/libraries/sensoranalysis/tools/check_orientation_frames.py CAPTURE.json
```

The fixture this one replaced was captured before the tag-side axis correction
existed and disagreed with that contract, so every orientation screenshot
generated from it showed a heading the current firmware would not produce.

Replay one with:

```sh
./qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json
TAGCALIBRATE_REPLAY=$PWD/host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261028-171428.json \
  cargo run -p tagcalibrate        # in host/applications/tagcalibrate-rs
```

Use `--replay-percent 0`, `25`, `50`, or `100` to prepare milestone states for
documentation screenshots.

Generate the baseline milestone screenshot set with:

```sh
./qtcalibrate --capture-startup-screenshot

./qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json \
  --capture-replay-screenshots

./qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/qtcalibrate-samples-20261010-145438.json \
  --capture-orientation-screenshot
```

Use `--orientation-pose heading,pitch,roll,dip,field,gravity` to override the
default documentation pose.

Before committing a capture, check that it contains only calibration-window
sample data and summary values. Do not commit private device identifiers,
serial numbers, local paths, operator names, or unrelated tag configuration.
