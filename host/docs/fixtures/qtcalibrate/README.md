# QtCalibrate Documentation Fixtures

Store curated `qtcalibrate` sample captures here for documentation screenshot
generation and replay tooling.

Recommended baseline fixture name:

```text
good-sphere-v1.json
```

Replay it with:

```sh
qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json
```

Use `--replay-percent 0`, `25`, `50`, or `100` to prepare milestone states for
documentation screenshots.

Generate the baseline milestone screenshot set with:

```sh
qtcalibrate --capture-startup-screenshot

qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json \
  --capture-replay-screenshots

qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json \
  --capture-orientation-screenshot
```

Use `--orientation-pose heading,pitch,roll,dip,field,gravity` to override the
default documentation pose.

Before committing a capture, check that it contains only calibration-window
sample data and summary values. Do not commit private device identifiers,
serial numbers, local paths, operator names, or unrelated tag configuration.
