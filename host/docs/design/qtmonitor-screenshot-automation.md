# QtMonitor Documentation Screenshot Automation Design

## Purpose

The current Tag Monitor documentation relies on hand-captured screenshots from
real tags. That makes the documentation hard to update, hard to review, and
hard to keep representative as tag families gain or hide configuration fields.

This design extends the deterministic screenshot workflow piloted for
`qtcalibrate` to `qtmonitor`. The main difference is that `qtmonitor` depends
on three monitor responses rather than one sample stream:

- `TagInfo`, which shapes the Tag Information panel and identifies the firmware.
- `Config`, which shapes and populates the Configure tab.
- `Status`, which drives the Tag State tab, enabled controls, log counts, test
  state, voltage, and data-download availability.

The implementation should let maintainers generate user-guide screenshots
without connected hardware while still exercising the real `qtmonitor` widgets.

## Goals

- Provide a fake-tag mode that feeds `qtmonitor` realistic monitor responses.
- Capture the default configuration screen for every tag type that is included
  in the maintained documentation inventory.
- Capture a small, representative state matrix for the Tag State tab:
  `IDLE`, `RUNNING`, `FINISHED`, and `ABORTED`.
- Make fixture data easy to regenerate from source-controlled defaults and, when
  available, built firmware metadata.
- Keep normal `qtmonitor` behavior unchanged when fake-tag options are absent.
- Document the maintainer hooks clearly enough that future tag families can be
  added without reverse-engineering the screenshot pipeline.

## Non-Goals

- Do not emulate complete firmware behavior or data-log downloads in the first
  pass.
- Do not require every tag type to be captured in every state.
- Do not treat every directory under `embedded/tags/` as a documentation target;
  archived, experimental, test-only, and dead-end firmware targets should be
  excluded unless they are deliberately added to the maintained inventory.
- Do not replace hardware integration tests for monitor RPCs.
- Do not hand-build static mock screenshots; generated screenshots should come
  from the real Qt widgets.

## Current QtMonitor Dependencies

`MainWindow::Attach()` currently owns the live monitor discovery sequence:

1. Find and attach a USB base through `Tag`.
2. Call `GetTagInfo()`.
3. Call `GetConfig()`.
4. Call `GetStatus()`.
5. Populate the Tag Information panel.
6. Attach `ConfigTab` and `LogScreen` to the live `Tag`.
7. Start polling `GetStatus()` every 400 ms.

`ConfigTab::SetConfig()` already has the right shape for documentation
automation: it accepts a `Config` message, derives visibility from
`config.tag_type()`, and populates the real schedule and sensor sub-tabs. That
means the default configuration screenshots can be generated without simulating
most monitor commands.

## Fixture Sources

### Maintained Tag Inventory

The screenshot generator should not discover documentation targets by scanning
all tag directories. The repository contains active firmware, hardware variants,
archived experiments, and one-off test targets. Instead, introduce a small
source-controlled inventory, for example:

```text
host/docs/fixtures/qtmonitor/tags.json
```

The inventory should name the tag fixtures that are part of the maintained user
documentation:

```json
{
  "schema": "tag-designs.qtmonitor.documentation-tags.v1",
  "tags": [
    {
      "id": "bittag-le",
      "label": "BitTag low-energy",
      "tag_type": "BITTAG_LE",
      "default_config": "embedded/proto-c/bittag-proto-c/default-config.json",
      "fixture": "host/docs/fixtures/qtmonitor/bittag-le.json",
      "document": true
    },
    {
      "id": "prestag",
      "label": "PresTag",
      "tag_type": "PRESTAG",
      "default_config": "embedded/proto-c/prestag-proto-c/default-config.json",
      "fixture": "host/docs/fixtures/qtmonitor/prestag.json",
      "document": true
    }
  ]
}
```

This inventory gives maintainers an explicit place to make product/documentation
decisions:

- include current tag products and active development targets;
- exclude `embedded/tags/archive/`;
- exclude test fixtures such as `stop1test`;
- exclude dead-end or superseded tag variants until they matter to users again;
- map several firmware targets to one documentation fixture when their
  user-visible `qtmonitor` configuration is the same.

The generator may still offer a discovery mode that reports unrepresented
`default-config.json` files, but missing discovery entries should be warnings,
not automatic documentation targets.

### Default Configurations

The best source for a tag's documentation configuration is the configuration
reported by the tag itself through `GetConfig()` when the tag is in its
factory/default state. The fixture capture tool can therefore create both the
fake tag identity and the default Configure-tab payload in one pass.

The checked-in default config files remain important as fallback, comparison,
and bootstrap sources:

```text
embedded/proto-c/*-proto-c/default-config.json
```

These files already define the tag type and field presence used by the
generated `ConfigFieldVisibility` table. For each documented inventory entry,
the screenshot pipeline should use the captured fixture config when present and
fall back to the referenced default config when a hardware capture has not been
made yet.

Current default config candidates that can seed inventory entries:

- `bitprestag-proto-c/default-config.json`
- `bittag-legacy-proto-c/default-config.json`
- `bittag-ng-proto-c/default-config.json`
- `bittag-proto-c/default-config.json`
- `compasstag-proto-c/default-config.json`
- `imutag-proto-c/default-config.json`
- `prestag-proto-c/default-config.json`
- `prestagraw-proto-c/default-config.json`

### Tag Information

`TagInfo` can be partially inferred from the built firmware image, but not all
fields are available from binaries alone. Firmware strings such as
`FIRMWARE_STRING`, `BOARD_NAME`, `VERSION_HASH`, `GIT_REPO`, build time, and
source path are compiled into firmware and can be recovered from the ELF or from
build metadata. Hardware-derived fields such as UUID and flash size either come
from MCU registers at runtime or from board/storage knowledge.

Use a layered approach:

1. Prefer an explicit fixture or generated manifest entry for stable
   documentation values.
2. Fill firmware identity fields from built firmware metadata when available.
3. Fall back to deterministic documentation defaults for non-essential hardware
   fields, for example `000000000000000000000000`, representative flash sizes,
   and a board name derived from the tag target.

The important documentation property is consistency: every fake tag should have
plausible information, but the screenshots should not imply a real device UUID.

### Fixture Capture Tool

A small command-line capture tool helps maintainers create and refresh the
per-tag fixtures from real hardware:

```text
qtmonitor-fixture-capture
```

The capture tool should use the same `tagcore` monitor API as `qtmonitor` and
write the fixture schema consumed by fake mode. It should be intentionally
narrow: capture monitor metadata, configuration, status snapshots, and voltage;
do not download data logs or drive destructive commands by default.
It is a maintainer-only build target and should not be installed into
distributed host packages.

Initial command shape:

```text
qtmonitor-fixture-capture \
  --id prestag \
  --label PresTag \
  --state idle \
  --sanitize \
  --fallback-config embedded/proto-c/prestag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/prestag.json
```

Future capture workflow options may add inventory-aware updates and multi-state
merge capture, for example:

```text
qtmonitor-fixture-capture --inventory host/docs/fixtures/qtmonitor/tags.json --update prestag
qtmonitor-fixture-capture --merge --status-name finished --output host/docs/fixtures/qtmonitor/prestag.json
```

Minimum captured responses:

- `GetTagInfo()` for real firmware and board identity.
- `GetConfig()` for the tag's current/default configuration.
- `GetStatus()` for the current state.
- `Voltage()` for the status panel.

Optional capture modes:

- `--capture-default-config` records the current `GetConfig()` response as the
  fixture's canonical Configure-tab payload. Maintainers should use this when
  the attached tag has been reset to its default configuration.
- `--status-name <name>` stores the current status under a named slot such as
  `idle`, `running`, `finished`, or `aborted`.
- `--merge` updates only the captured portions of an existing fixture, keeping
  curated labels and documentation notes intact.
- `--sanitize` replaces hardware UUIDs and local paths with documentation-safe
  placeholders.
- `--print-summary` shows tag type, firmware string, git hash, state, counts,
  voltage, and output path before writing.

The tool should refuse to perform state-changing actions such as stop, erase,
start, self-test, or calibration unless a future option explicitly asks for
that behavior. For the first implementation, maintainers can reset or configure
the real tag into the desired state, run the capture command, and name the
captured config or status.

### Status States

Status fixtures should be compact and state-focused. For documentation, the key
fields are:

- `state`
- `test_status`
- `internal_data_count`
- `external_data_count`
- `millis`
- `erase_sectors_total_plus_one`
- `sectors_erased`
- optional `debug_message`

Voltage is currently read through `Tag::Voltage()` rather than `Status.voltage`,
so fake mode needs a voltage value in the fixture or fake tag object.

## Proposed Fixture Format

Use one JSON file per fake tag, stored under:

```text
host/docs/fixtures/qtmonitor/
```

Example shape:

```json
{
  "schema": "tag-designs.qtmonitor.fake-tag.v1",
  "id": "prestag",
  "label": "PresTag",
  "notes": "Captured from a maintained PresTag fixture and sanitized for docs.",
  "captured_at_utc": "2026-08-26T00:00:00.000Z",
  "info": {
    "tag_type": "PRESTAG",
    "board_desc": "PresTagv3",
    "uuid": "000000000000000000000000",
    "intflashsz": 512,
    "extflashsz": 16777216,
    "firmware": "PresTagv4, Firmware version 1",
    "gitrepo": "git@github.com:tag-designs/software.git",
    "githash": "documentation",
    "source_path": "/embedded/tags/PresTag",
    "build_time": "documentation fixture",
    "qtmonitor_min_version": 2.0
  },
  "config": {
    "source": "captured-default",
    "fallback_ref": "embedded/proto-c/prestag-proto-c/default-config.json",
    "value": {
      "tag_type": "PRESTAG"
    }
  },
  "voltage": 3.02,
  "statuses": {
    "idle": {
      "state": "IDLE",
      "test_status": "ALL_PASSED",
      "internal_data_count": 0,
      "external_data_count": 0,
      "millis_offset_ms": 0
    },
    "running": {
      "state": "RUNNING",
      "test_status": "ALL_PASSED",
      "internal_data_count": 42,
      "external_data_count": 7,
      "millis_offset_ms": 0
    },
    "finished": {
      "state": "FINISHED",
      "test_status": "ALL_PASSED",
      "internal_data_count": 128,
      "external_data_count": 512,
      "millis_offset_ms": 0
    },
    "aborted": {
      "state": "ABORTED",
      "test_status": "EXT_FLASH_FAILED",
      "internal_data_count": 32,
      "external_data_count": 64,
      "millis_offset_ms": 0,
      "debug_message": "Documentation fixture: simulated abort"
    }
  }
}
```

For generated fixtures, storing an inline `config` object is also acceptable.
`$ref` is friendlier for maintainers because default config changes are picked
up without duplicating protobuf JSON.

Capture-generated fixtures should prefer inline `info`, `config`, `voltage`,
and `statuses`. If the config was captured from real hardware, mark it as
`captured-default` and optionally keep a `fallback_ref` to the source-tree
default config for comparison. If no hardware capture exists yet, the fixture
may use only a `$ref` to the maintained default config.

## Fake Tag Architecture

### Preferred Application Boundary

Introduce a small monitor-session abstraction used by `MainWindow` and
`ConfigTab`:

```cpp
class MonitorSession {
public:
  virtual ~MonitorSession() = default;
  virtual bool isAttached() const = 0;
  virtual bool attach(QWidget *parent) = 0;
  virtual void detach() = 0;
  virtual bool getInfo(TagInfo &info) = 0;
  virtual bool getConfig(Config &config) = 0;
  virtual bool getStatus(Status &status) = 0;
  virtual bool voltage(float &voltage) = 0;
  virtual bool setRtc() = 0;
  virtual bool stop() = 0;
  virtual bool erase() = 0;
  virtual bool test(TestReq request) = 0;
  virtual bool calibrate() = 0;
};
```

Then provide:

- `UsbMonitorSession`, a thin adapter around the current `Tag`.
- `FakeMonitorSession`, backed by the fixture JSON.

This avoids teaching `Tag` about documentation-only behavior and keeps fake
mode out of `tagcore`.

### Smaller First Step

If the abstraction is too large for the first implementation, add a
`MainWindowOptions` struct and fake-mode branches in `MainWindow` only:

- `loadFakeTagFixture(path)`
- `setupFakeTag()`
- `applyFakeStatus(name)`
- `captureFakeScreenshots()`

This is faster, but it will leave more live/fake branching in the UI. If we take
this route, the design should still aim to collapse those branches into
`MonitorSession` once the screenshot workflow proves itself.

## Command-Line Hooks

Add maintainer-only CLI options to `qtmonitor`. The first implementation uses
fixture-file options local to `MainWindow`; the broader inventory-oriented
options below remain future work.

```text
--fake-fixture <path>
--fake-state <idle|running|finished|aborted>
--capture-startup-screenshot
--capture-main-screenshots
--capture-config-screenshots
--screenshot-dir <dir>
--no-auto-attach
```

Expected behavior:

- No fake options: unchanged live hardware behavior.
- `--capture-startup-screenshot`: open with no USB probing and capture the
  disconnected Tag State tab.
- `--fake-fixture ... --capture-main-screenshots`: load the fake fixture, apply
  idle, running, and finished display states, capture the Tag State tab for
  each state, and exit.
- `--fake-fixture ... --capture-config-screenshots`: load the fake fixture,
  apply idle state, capture the Configure schedule and sensor tabs, and exit.
- Future `--capture-all-default-configs`: iterate over all default config JSON files,
  or preferably over all `document: true` inventory entries, attach each as a
  fake tag, show the Configure tab, capture schedule and sensor views as needed,
  and exit.

As with `qtcalibrate`, screenshot slots should run after the window is shown so
native window resources and child widgets are fully realized.

## Screenshot Coverage

### Required Per Tag

Every documented inventory entry should get at least one default Configure
screenshot. The generator should create predictable names based on the inventory
`id`. The schedule screenshot is always required:

```text
qtmonitor-config-bittag-schedule.png
qtmonitor-config-bittag-le-schedule.png
qtmonitor-config-bittagng-schedule.png
qtmonitor-config-bitprestag-schedule.png
qtmonitor-config-compasstag-schedule.png
qtmonitor-config-imutag-schedule.png
qtmonitor-config-prestag-schedule.png
qtmonitor-config-prestagraw-schedule.png
```

The sensors screenshot is generated only for tags with active sensor
configuration controls:

```text
qtmonitor-config-bittag-sensors.png
qtmonitor-config-bittag-le-sensors.png
qtmonitor-config-bittagng-sensors.png
qtmonitor-config-bitprestag-sensors.png
qtmonitor-config-imutag-sensors.png
qtmonitor-config-prestag-sensors.png
qtmonitor-config-prestagraw-sensors.png
```

If a tag has no meaningful controls on the Sensors sub-tab, qtmonitor hides the
tab and skips the sensor screenshot instead of producing a blank image.

### Required Representative States

State screenshots do not need to be repeated for every tag. A recommended set:

- `qtmonitor-startup.png`: no tag attached.
- `qtmonitor-main-idle.png`: representative tag attached, controls enabled for
  sync, self-test, configuration, and calibration when supported by that tag.
- `qtmonitor-main-running.png`: stop enabled, configuration disabled.
- `qtmonitor-main-finished.png`: data download and erase enabled.
- `qtmonitor-main-aborted.png`: data download and erase enabled, error/debug
  context visible.
- `qtmonitor-error-log.png`: disconnected Error Log tab, showing the log pane
  and save affordance.

Screenshot capture should prefer Qt's layout model over image post-processing:
select the target tab, let `adjustSize()` and `sizeHint()` settle the window,
then apply a capture-size guardrail before grabbing the native frame. Avoid
using image cropping as the primary sizing mechanism because it can hide the
bottom of widgets such as the Error Log text pane.

Use a tag with both internal and external counts for `FINISHED` and `ABORTED`
so the download region is meaningful.

## Documentation Structure

The user guide should explain qtmonitor in this order:

1. Opening the application and attaching a tag.
2. Tag State tab and control availability.
3. Tag Information fields.
4. Configuration workflow.
5. Per-tag default configuration examples.
6. Data download states.
7. Error log and troubleshooting.
8. Maintainer notes for regenerating screenshots.

The fake-tag implementation details belong at the end or in developer docs, not
in the main user-facing narrative.

## Implementation Plan

1. Add a `MainWindowOptions` struct to `qtmonitor`, mirroring the useful parts
   of the `qtcalibrate` screenshot hooks.
2. Add CLI parsing in `main.cpp` for fake-tag and screenshot options.
3. Add fixture loading:
   - parse protobuf JSON for `TagInfo`, `Config`, and `Status`;
   - resolve `$ref` paths relative to the source tree;
   - prefer captured inline config and fall back to source-tree defaults;
   - allow generated fixtures to override only the fields that differ from
     defaults.
4. Add `host/docs/fixtures/qtmonitor/tags.json` as the curated documentation
   inventory.
5. Add `qtmonitor-fixture-capture` to create/update one fake-tag fixture from a
   real connected tag.
6. Add a fake monitor session or first-step fake branches in `MainWindow`.
7. Make `ConfigTab` usable with fake mode by avoiding live `Tag` calls while
   showing default configs. Start and Read should match normal visual state but
   return before sending hardware commands.
8. Add screenshot helpers using native-frame capture, same as `qtcalibrate`.
9. Add a fixture generator script or CMake helper that reads the curated
   inventory and can warn about unrepresented default configs.
10. Generate the required screenshots into `host/docs/src/images/`.
11. Update `host/docs/src/apps/qtmonitor.md` to use the generated images.
12. Document the maintainer commands in `host/applications/qtmon/README.md` or
    a new `host/applications/qtmon/design/` note.

## Validation

- `cmake --build <build-dir> --target qtmonitor`
- `cmake --build <build-dir> --target docs`
- `git diff --check`
- Visual review of generated screenshots:
  - no clipped window edges;
  - Configure tab matches each tag's default config visibility;
  - status controls match `IDLE`, `RUNNING`, `FINISHED`, and `ABORTED`;
  - fake UUID/build strings are visibly documentation fixtures if shown.

For regression checks, add a lightweight script that asserts every
`document: true` inventory entry has a corresponding generated screenshot entry.
The same script can report, but should not fail on, default configs or tag
directories that are not part of the curated inventory.

## Open Questions

- Should firmware metadata extraction come from ELF files, package manifests,
  or a generated CMake metadata file? ELF extraction is attractive because it
  reflects the compiled image, but a manifest is more stable and easier to
  validate.
- Which active tag targets belong in the first maintained documentation
  inventory, and which are dead ends or internal variants?
- Should fake mode live behind a general host-library monitor abstraction, or
  remain a `qtmonitor`-local maintainer hook until a second application needs
  it?
- Do we want per-tag state screenshots later for tags whose download controls or
  log formats differ substantially?
- Should the screenshot generator fail when a default config has no matching
  fake `TagInfo`, or synthesize one automatically and warn?
