# QtMonitor Fixture Captures

This directory stores fake-tag fixture JSON used to generate `qtmonitor`
documentation screenshots. Fixtures are maintainer data: they are kept in the
repository so screenshots can be regenerated, but they are not part of the
end-user host documentation site.

## Documenting a New Tag Type

Use this checklist when adding a tag type to the `qtmonitor` user guide:

1. Choose a stable fixture id and label. Use lower-kebab ids that match the tag
   family, for example `bittag`, `bittag-le`, `bitprestag`, `prestag`,
   `imutag`, or `compasstag`.
2. Find the matching default configuration under `embedded/proto-c/*/`, for
   example `embedded/proto-c/imutag-proto-c/default-config.json`. Use that path
   as `--fallback-config`.
3. Capture a connected tag in the state you want to document, usually `idle`:

   ```sh
   cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target qtmonitor-fixture-capture

   /Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor-fixture-capture.app/Contents/MacOS/qtmonitor-fixture-capture \
     --id imutag \
     --label IMUTag \
     --state idle \
     --fallback-config embedded/proto-c/imutag-proto-c/default-config.json \
     --output host/docs/fixtures/qtmonitor/imutag.json \
     --print-summary
   ```

4. Confirm the printed `tag_type` matches the fixture id and fallback config.
   If the hardware reports a related tag type, rename the fixture or choose the
   more appropriate fallback before generating screenshots.
5. Use `"source": "captured-default"` when `config.value` came from
   `GetConfig()`. Use `"source": "fallback-default"` when building a fixture
   from a checked-in default config, such as a classic or historical tag variant
   that does not have matching hardware available.
6. Generate the tag-specific Configuration screenshots:

   ```sh
   cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target qtmonitor

   /Users/geobrown/Build/tag-designs/software/build-host/bin/qtmonitor.app/Contents/MacOS/qtmonitor \
     --fake-fixture host/docs/fixtures/qtmonitor/imutag.json \
     --capture-config-screenshots
   ```

   This creates `qtmonitor-config-<id>-schedule.png` and, when the tag exposes
   user-configurable sensor fields, `qtmonitor-config-<id>-sensors.png`.
7. Do not regenerate the representative Tag State or Error Log screenshots for
   every tag. Those are shared walkthrough images and should be updated only
   when the common workflow image set intentionally changes.
8. Add or update the tag section in `host/docs/src/apps/qtmonitor.md`, and add
   any new image names to `host/docs/src/reference/images.md`.
9. Validate the result:

   ```sh
   cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target docs
   git diff --check
   ```

10. Visually review the generated screenshots for clipped controls, missing
    tabs, incorrect tag type labels, and unexpected UUID/source-path exposure.

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
