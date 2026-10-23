---
type: readme
status: current
summary: How qtmonitor's Configuration tab is built from fixed panels selected by tag type, how panels read and write Config, the cross-check against the tags' default configs, and adding tags, fields and panels.
---

# QtMonitor Configuration Panels

This README covers the panels. The configuration model as a whole (where a
tag's supported fields come from, the merge, the fields with tag-specific
meanings, and how it is checked) is in
[Tag Configuration Model](../../../../docs/shared/design/configuration-model.md);
the decisions behind it are in
[decision 0032](../../../../docs/decisions/0032-host-config-shape-and-fixed-panels.md).

The Configuration tab is one page of **panels**. Every panel is built once,
when `ConfigTab` is constructed, with its titles, labels, ranges and tooltips
fixed for the tags it serves. The attached tag's type selects which panels are
shown, from one table in `configtab.cpp` (`panelsForTag()`). A panel is shown
or hidden as a whole; no panel decides per widget what to show.

| Panel (`PanelId`) | Class and variant | Owns | Tags |
| --- | --- | --- | --- |
| `IntervalSchedule` | `SchedulePanel`, Interval | `active_interval`, `hibernate` | BitTag, BitTag LE, BitPresTag, BitTagNG, UIUCTag, CompassTag |
| `IntervalScheduleWithPeriod` | `SchedulePanel`, IntervalWithPeriod | the above and `period` | PresTag |
| `StartDelaySchedule` | `SchedulePanel`, StartDelay | `start_delay` | IMUTag |
| `BitTagLogFormat` | `BitTagLogPanel` | `bittag_log` | BitTag, BitTag LE |
| `Adxl362Standard` | `Adxl362Panel`, Standard | `adxl362` range, freq, filter and both thresholds, inactivity in seconds | BitTag |
| `Adxl362Wake` | `Adxl362Panel`, Wake362 | `adxl362` thresholds, inactivity as samples | BitTag LE, BitPresTag |
| `Adxl367Wake` | `Adxl362Panel`, Wake367 | `adxl362` threshold, inactivity as samples | UIUCTag |
| `Adxl367WakeWithType` | `Adxl362Panel`, Wake367 with `accel_type` | the above and `adxl362.accel_type` | BitTagNG |
| `Lsm6Imu` | `Lsm6Panel` | `lsm6` | IMUTag |

The schedule variants share one class because they share the "Logging
Schedule" group box and its controls. Hibernation shows up to two rows, the
firmware maximum (`Config.hibernate max_count:2`). Each row has an **In use**
checkbox: an interval starting at 0 or INT32_MAX loads unchecked, and an
unchecked row is sent as 0 to 0 (1 January 1970), which never applies.

## How a panel reads and writes Config

`ConfigPanel` (`configpanel.h`) has three methods:

- `paths()` lists the `Config` fields the panel owns, as `ConfigShape`
  paths;
- `load()` fills the controls from those fields. It returns false only when
  the panel cannot show this configuration: the BitTag log panel does that for
  a log format it has no button for, and warns for a classic BitTag with none;
- `store()` writes those fields from the controls, whatever is on screen.

`ConfigTab` is a configuration editor holding one **document**: a `Config`
(`current_config_`), its tag type, its source (`Source::Defaults`, `File` or
`Tag`) and, for a file, its path. It works with no tag attached.

- Loading a document (`load()`, through `LoadDefaults()`, `OpenFromJsonText()`
  or `SetConfig()`) shows the tag type's panels and calls `load()` on each.
- `GetConfig()`, used by Save and Start, copies the document and calls
  `store()` on each shown panel. A field no panel owns passes through
  unchanged.
- Open makes a file the document: its tag type's defaults
  (`ConfigShape::defaultConfig()`) with the file merged on top by
  `ConfigShape::forFile()` and `ConfigShape::merge()` (see
  `host/libraries/tagcore/configshape.h`). A file that names no tag type is
  opened as a type the user picks.
- Unsaved changes: just after loading, what the panels would write is kept
  (`baseline_`); `isModified()` compares it with what they would write now.
  Every control's change signal re-checks, and `documentChanged()` reports a
  change, which MainWindow shows in the title.
- Editing is disabled only while the document is the attached tag's own
  configuration and the tag is not IDLE.
- Recent files are kept in `QSettings` under `recentConfigurationFiles`.

## The cross-check

`qtmonitor --check-config-panels` compares the panel table with every tag
type's `ConfigShape`, which is read from
`embedded/proto-c/*-proto-c/default-config.json`. It fails if:

- a panel owns a field the tag's default config does not list;
- the default config lists a field no shown panel owns;
- a tag type has a default config but no table entry, or the reverse.

Run it after changing a default config or the panel table. The
configuration probe (`../test/README.md`) then shows whether anything a tag
or a user could see has changed.

## Adding A New Tag

1. Add or update the tag's `embedded/proto-c/<tag>-proto-c/default-config.json`.
   Include every field the tag supports, even if its default value is `0`.
   Use `UNSPECIFIED` enum values only when the field should be present but
   have no selected operational value.
2. Update firmware `readConfig()` and `writeConfig()` for the tag.
3. Add a row to `panelsForTag()` in `configtab.cpp`, reusing panels where the
   fields mean the same thing.
4. Run `qtmonitor --check-config-panels`, regenerate the probe's cases, and
   take goldens for the new tag (`../test/README.md`).

## Adding A New Field

1. Add the field to `proto/tagdata.proto` if it does not already exist.
2. Add it to the default config of each tag that supports it.
3. Add a control to the panel that owns it: list the path in `paths()`, read
   it in `load()`, write it in `store()`.
4. Update tag firmware conversion code.
5. Run `qtmonitor --check-config-panels`, regenerate the probe's cases, and
   check the goldens.

Open and `tag-start --merge` handle the new field without change.

## Adding A New Panel

Subclass `ConfigPanel`, building every control in the constructor. Give it a
`PanelId` and an object name in `ConfigTab`'s constructor, in page order, and
select it in `panelsForTag()`. Keep ownership disjoint: two panels shown for
the same tag must not own the same field.

## Tag-Specific Field Semantics

Prefer reusing existing `Config` fields when a new tag needs narrower ranges or
labels but not a new wire format. Give each meaning its own panel, or its own
variant of a panel, with its labels, suffixes, ranges and tooltips fixed at
construction, writing the same protobuf fields. `Adxl362Panel`'s three
variants are the example.

BitTagNG is one example. It uses an ADXL367 in wake-up mode but reuses
`Config.adxl362`:

- `adxl362.act_thresh_g` is the absolute ADXL367 wake-up threshold in g.
- `adxl362.inactive_sec` is interpreted by BitTagNG firmware as an inactivity
  sample count at 6 Hz, despite the historical field name.

BitTag low-energy firmware (`BITTAG_LE`) and BitPresTag also use wake-mode
activity detection with the existing ADXL362 protobuf fields:

- `adxl362.act_thresh_g` and `adxl362.inact_thresh_g` remain thresholds in g.
- `adxl362.inactive_sec` is interpreted as an inactivity sample count at the
  wake-mode sample rate, not as seconds.
- ADXL362 range, output data rate, and anti-alias filter are fixed by firmware
  and are intentionally hidden from the QtMonitor sensor UI.

Document any such semantic overload in both the panel and the tag firmware
conversion code.

## Proto3 Presence Notes

Message presence is reliable:

```cpp
config.has_active_interval()
config.has_adxl362()
config.has_lsm6()
```

Scalar presence is not reliable:

```cpp
config.period() != 0
config.start_delay() != 0
config.adxl362().act_thresh_g() != 0.0f
```

Use `ConfigShape` for which fields a tag supports, and the panel table for
which controls a tag shows. Use decoded config values only as values.
