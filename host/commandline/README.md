# Command-Line Tools

Command-line host tools live here. They link the Qt-free `tagcore` target from
`../libraries/tagcore`.

Distributed tools:

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

Keep command-line behavior independent of Qt so these tools remain lightweight
and usable in scripts.

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
