# Command-Line Tools

Command-line host tools live here. Most hardware-facing tools link the Qt-free
`tagcore` target from `../libraries/tagcore`. `dataprocessing` is the current
exception: it reuses the existing `sensoranalysis` math library, so it is built
when the Qt/sensor-analysis host stack is enabled.

Distributed tools:

- `dataprocessing`: copy SQLite logs and materialize derived/calibrated sensor
  streams. The initial implementation supports CompassTag calibrated vectors
  and canonical orientation streams.
- `tag-dwnld`: download tag logs using the shared tag log writer interface.
- `tag-info`: inspect tag/base information.
- `tag-reset`: reset a tag.
- `tag-start`: start logging.
- `tag-stop`: stop logging and print the resulting tag status.
- `tag-cal`: calibration helper.
- `tag-test`, `tag-test-example`, `tag-monitor-test`: developer/test tools.

Maintainer-only build-tree tools:

- `qtmonitor-fixture-capture`: capture `TagInfo`, default `Config`, `Status`,
  and voltage from a real tag into the fixture JSON consumed by qtmonitor
  documentation screenshot automation. This tool is built for maintainers but
  is not installed into distributed host packages.

Keep direct tag-operation tools independent of Qt so they remain lightweight and
usable in scripts. Processing tools may link host analysis libraries when that
avoids duplicating sensor math.

Example qtmonitor fixture capture preserving the real tag identity:

```sh
qtmonitor-fixture-capture \
  --id compasstag \
  --label CompassTag \
  --state idle \
  --fallback-config embedded/proto-c/compasstag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/compasstag.json \
  --print-summary
```

Example sanitized capture for screenshots that should not show a real device
UUID:

```sh
qtmonitor-fixture-capture \
  --id prestag \
  --label PresTag \
  --state idle \
  --sanitize \
  --fallback-config embedded/proto-c/prestag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/prestag.json
```

`--state` names the captured status slot in the fixture; it does not drive the
tag into that state. Put the tag in the desired state before running the tool.

## DataProcessing

`dataprocessing` is a post-processing tool for SQLite logs. It keeps the input
database untouched, copies it to a new output path, writes derived tables and
stream metadata, and records processing provenance in a `ProcessingRun` table.

Current processors:

- `compass-calibrated`: writes `CompassCalibrated` with raw acceleration copied
  beside calibrated magnetometer x/y/z values.
- `compass-orientation`: writes `CompassOrientation` with canonical
  magnetic-frame yaw, pitch, roll, dip, field strength, acceleration magnitude,
  and quaternion columns.

Example:

```sh
dataprocessing \
  --input host/docs/fixtures/sensorviz/compasstag.db3 \
  --output /tmp/compasstag-processed.db3 \
  --processor compass-calibrated \
  --processor compass-orientation \
  --if-exists replace \
  --print-summary
```

Use `--list-processors` to see whether a log contains the inputs needed by the
current processors. Heading is not materialized; downstream tools can convert
the stored yaw to a display heading by applying their own declination and
mounting convention.
