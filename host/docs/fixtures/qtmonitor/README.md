# QtMonitor Fixture Captures

This directory stores fake-tag fixture JSON used to generate `qtmonitor`
documentation screenshots. Fixtures are maintainer data: they are kept in the
repository so screenshots can be regenerated, but they are not part of the
end-user host documentation site.

## Capture Tool

Use `qtmonitor-fixture-capture` from the build tree to capture a real connected
tag:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target qtmonitor-fixture-capture

/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor-fixture-capture.app/Contents/MacOS/qtmonitor-fixture-capture \
  --id compasstag \
  --label CompassTag \
  --state idle \
  --fallback-config embedded/proto-c/compasstag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/compasstag.json \
  --print-summary
```

On non-macOS builds, use the executable path produced under the build tree's
`bin/` directory.

The tool captures:

- `TagInfo`
- current/default `Config`
- one named `Status` snapshot
- voltage from `Tag::Voltage()`

`--state` names the captured status slot in the fixture. It does not transition
the tag, start logging, stop logging, erase flash, or run self-tests. Put the
tag in the state you want to document before running the command.

## Identity Policy

By default, captures preserve the real tag UUID and source path. That is useful
for maintainer fixtures because it proves which physical tag produced the data.

Use `--sanitize` only when a fixture should avoid exposing hardware identity in
published screenshots:

```sh
qtmonitor-fixture-capture \
  --id prestag \
  --label PresTag \
  --state idle \
  --sanitize \
  --fallback-config embedded/proto-c/prestag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/prestag.json
```

The tool always clears erase-progress counters when the captured status is not
`sRESET`, because those fields are meaningful only while erase/reset progress is
being reported.

## Fixture Shape

Each fixture follows schema `tag-designs.qtmonitor.fake-tag.v1`:

```json
{
  "schema": "tag-designs.qtmonitor.fake-tag.v1",
  "id": "compasstag",
  "label": "CompassTag",
  "captured_at_utc": "2026-08-26T23:59:43Z",
  "info": {},
  "config": {
    "source": "captured-default",
    "fallback_ref": "embedded/proto-c/compasstag-proto-c/default-config.json",
    "value": {}
  },
  "voltage": 2.84,
  "statuses": {
    "idle": {}
  }
}
```

The `config.value` object is protobuf JSON from `GetConfig()`. The
`fallback_ref` points to the checked-in default config used when a live capture
is not available or when maintainers want to compare hardware defaults against
source-controlled defaults.

Use `"source": "fallback-default"` for a fixture whose configuration was built
from the checked-in default instead of captured from the attached hardware. This
is useful for classic or historical tag variants when a closely related tag can
provide representative identity/status fields but the configuration UI should
show a different default tag type.

## Screenshot Replay

`qtmonitor` can replay one fixture directly for documentation screenshots:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target qtmonitor

/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --fake-fixture host/docs/fixtures/qtmonitor/compasstag.json \
  --capture-main-screenshots

/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --fake-fixture host/docs/fixtures/qtmonitor/compasstag.json \
  --capture-config-screenshots
```

The main capture writes `qtmonitor-main-idle.png`,
`qtmonitor-main-running.png`, and `qtmonitor-main-finished.png`. The config
capture writes `qtmonitor-config-<fixture-id>-schedule.png` and
`qtmonitor-config-<fixture-id>-sensors.png` only when the fixture exposes
user-configurable sensor controls.

The disconnected first screen does not need a fixture:

```sh
/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --capture-startup-screenshot

/Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
  --capture-error-log-screenshot
```
