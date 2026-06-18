# QtMonitor Configuration Modules

QtMonitor uses the tag `Config` message for two related purposes:

- current values to show, edit, save, restore, and send to a tag;
- the shape of the configuration UI for a tag family.

Because `Config` uses proto3, scalar fields with value `0` do not carry
presence after protobuf parsing. QtMonitor therefore does not infer scalar
field visibility from decoded values. Instead, visibility is generated at build
time from each tag's `embedded/proto-c/*-proto-c/default-config.json`.

## Model Flow

`ConfigTab` owns the editable model:

- `current_config_` is the current editable configuration.
- Attach, Read, and Restore replace or merge into `current_config_`.
- Save and Start call `GetConfig()` and use the resulting model.

Configuration modules must treat `current_config_` as the base model. A module
should only modify the fields it owns and should leave hidden or unsupported
fields unchanged.

Restore is a value overlay. A restored file is merged into the current
tag-shaped config; it does not define the UI shape for the attached tag.

## Generated Visibility

`host/applications/qtmon/CMakeLists.txt` runs
`cmake/GenerateConfigVisibility.cmake` over:

```text
embedded/proto-c/*-proto-c/default-config.json
```

The generated source provides:

```cpp
const ConfigFieldVisibility &configFieldVisibilityForTag(TagType tag_type);
```

`ConfigFieldVisibility` lives in `configfieldvisibility.h`. It records JSON
field presence, including scalar fields whose default value is zero.

Default config files must use `tag_type`. A missing or misspelled `tag_type`
is a build-time error.

## Adding A New Tag

1. Add or update the tag's `embedded/proto-c/<tag>-proto-c/default-config.json`.
2. Include every field the tag supports, even if the default value is `0`.
3. Use `UNSPECIFIED` enum values only when the field should be present but have
   no selected operational value.
4. Update firmware `readConfig()` and `writeConfig()` for the tag.
5. Rebuild `qtmonitor`; the visibility table is regenerated automatically.
6. Verify that unsupported fields from restored config files are ignored.

For existing QtMonitor modules, adding a field to the default JSON is enough to
make that field visible only if the module already has a control and a
visibility bit for it.

## Adding A New Field

1. Add the field to `proto/tagdata.proto` if it does not already exist.
2. Add a visibility bit to `ConfigFieldVisibility`.
3. Update `GenerateConfigVisibility.cmake` to detect the JSON key. Include both
   snake_case and lowerCamelCase spellings if both may appear in defaults.
4. Update the owning configuration module to:
   - show/hide the control from the visibility bit;
   - load the value from `Config` in `SetConfig()`;
   - write the value in `GetConfig()` only when the visibility bit is true.
5. Add the field to the appropriate tag default JSON files.
6. Update tag firmware conversion code.

## Adding A New Module

Each module implements `ConfigInterface` and should also provide:

```cpp
bool SetConfig(const Config &config,
               const ConfigFieldVisibility &visibility);
```

`ConfigTab::SetConfig()` should pass the generated visibility object to the
module. The module should:

- set `active = false` and hide itself when its top-level visibility bit is
  false;
- store the supplied visibility object;
- use visibility bits, not scalar values, to decide which controls are visible;
- in `GetConfig()`, modify only fields whose visibility bits are true.

Keep ownership boundaries clear. For example:

- `Schedule` owns `active_interval`, `hibernate`, `period`, and `start_delay`.
- `BitTagLogTab` owns `bittag_log`.
- `Adxl362Config` owns `adxl362`.
- `Lsm6dsvConfig` owns `lsm6`.

Do not clear another module's fields.

## Tag-Specific Field Semantics

Prefer reusing existing `Config` fields when a new tag needs narrower ranges or
labels but not a new wire format. The module may adapt labels, suffixes, ranges,
and tooltips using `Config.tag_type()` or a sensor selector field, while still
writing the same protobuf fields.

BitTagNG is the current example. It uses an ADXL367 in wake-up mode but reuses
`Config.adxl362`:

- `adxl362.act_thresh_g` is the absolute ADXL367 wake-up threshold in g.
- `adxl362.inactive_sec` is interpreted by BitTagNG firmware as an inactivity
  sample count at 6 Hz, despite the historical field name.

Document any such semantic overload in both the QtMonitor module and the tag
firmware conversion code.

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

Use generated visibility for scalar-supported fields. Use decoded config values
only as values.
