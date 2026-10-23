---
type: design
status: current
summary: How a tag's configuration is defined, which fields each tag supports (its default config, read by tagcore's ConfigShape), how qtmonitor shows and writes it with fixed panels per tag type, how files are merged, the fields with tag-specific meanings, and how all of it is checked without hardware.
---

# Tag Configuration Model

A tag's configuration is the protobuf `Config` message in
[`proto/tagdata.proto`](../../../proto/tagdata.proto). **Which fields a tag
supports is stated by its default configuration**,
`embedded/proto-c/<name>-proto-c/default-config.json`, and by nothing else.
The firmware compiles that file in. The host reads it through tagcore's
`ConfigShape`, which drives three things:

- qtmonitor's Configuration tab shows fixed panels, chosen by tag type and
  checked against the shape;
- Restore and `tag-start --merge` copy only fields the tag supports;
- `qtmonitor --check-config-panels` fails if the panels and the default
  configs disagree.

What qtmonitor sends for any input is pinned by a headless probe and golden
results, so the configuration path is checked without a tag.

The reasons, and the alternatives rejected, are in
[decision 0032](../../decisions/0032-host-config-shape-and-fixed-panels.md).

## The Config message

`Config` holds the tag type, a schedule (`active_interval`, up to two
`hibernate` intervals, `period`, `start_delay`), the BitTag log format
(`bittag_log`), and sensor sub-messages (`adxl362`, `lsm6`). No tag uses all of
it. The host receives one from the tag (`GetConfig`) and sends one back to
start it (`Start`).

`Config` is proto3, which does not record whether a zero-valued scalar was set.
A field the tag supports with value 0 is indistinguishable, on the wire and
after decoding, from a field it does not support. So the decoded message cannot
say which fields a tag has. Sub-messages (`has_adxl362()`) and repeated fields
(`hibernate_size()`) do carry presence. Scalars and enums do not.

## What a tag supports: its default configuration

Each tag type's `default-config.json` lists every field the tag supports, with
zeros written out. The firmware build turns it into the image's built-in
default (`embedded/proto-c/config-gen.py`, which rejects any key that is not a
`Config` field), and BitTag (`BITTAG_LE`) and BitTagNG report it as is in IDLE.

[`ConfigShape`](../../../host/libraries/tagcore/configshape.h) reads the same
files on the host. tagcore's `CMakeLists.txt` embeds each file's text in a
generated `defaultconfigs.cc`, without interpreting it. At run time the first
call to `ConfigShape::forTag()` parses every file into a
`google::protobuf::Struct`, which keeps zero-valued keys, and walks it against
the `Config` descriptor. The result is one set of dotted paths per tag type,
such as `adxl362.freq` or `period`.

The walk has these properties:

- Each key is checked at its own level, so `range` is accepted inside
  `adxl362` and rejected inside `lsm6`.
- JSON names are stored as .proto names: IMUTag's `accelRng` becomes
  `lsm6.accel_rng`.
- Two files for the same tag type are unioned: `prestag-proto-c` and
  `prestagraw-proto-c` are both `PRESTAG`.

Today's shapes:

| Tag type | Fields |
| --- | --- |
| `BITTAG` | schedule with hibernation; `bittag_log`; `adxl362` range, freq, filter, both thresholds, `inactive_sec` |
| `BITTAG_LE` | schedule with hibernation; `bittag_log`; `adxl362` both thresholds, `inactive_sec` |
| `BITPRESTAG` | schedule with hibernation; `adxl362` both thresholds, `inactive_sec` |
| `BITTAGNG` | schedule with hibernation; `adxl362` `accel_type`, `act_thresh_g`, `inactive_sec` |
| `UIUCTAG` | schedule with hibernation; `adxl362` `act_thresh_g`, `inactive_sec` |
| `COMPASSTAG` | schedule with hibernation |
| `PRESTAG` | schedule with hibernation; `period` |
| `IMUTAG` | `start_delay`; `lsm6` odr, accel_rng, gyro_rng. No schedule interval and no hibernation. |

A default config that lists a field the firmware ignores, or omits one it
uses, is not caught on the host. For example, UIUCTag's firmware reports
`adxl362.accel_type` but its default config does not list it, so the host
treats the field as unsupported and passes it through. The firmware is the authority on what it
does. [Tag-reported configuration presence](proposals/tag-reported-config-presence.md)
is the deferred proposal that would close that gap.

## qtmonitor: fixed panels per tag type

The Configuration tab is one scrolling page of panels. Every panel is built
once, with its titles, labels, ranges and tooltips fixed. The tag type selects
which panels are shown, from one table (`panelsForTag()` in
`host/applications/qtmon/tagconfiguration/configtab.cpp`). Each panel lists
the fields it owns (`paths()`), reads exactly those (`load()`), and writes
exactly those (`store()`), whatever is on screen. `GetConfig()`, used by Start
and Save, starts from the tag's own configuration and lets each shown panel
write its fields. A field no panel owns passes through unchanged.

The panels, the table and how to add to them are in
[the tagconfiguration README](../../../host/applications/qtmon/tagconfiguration/README.md).

`qtmonitor --check-config-panels` compares the table with every shape. It fails
if a panel owns a field the tag's default config does not list, if the default
config lists a field no shown panel owns, or if a tag type has one without the
other.

## Files and the merge

qtmonitor's **Save** writes the whole `Config` as protobuf JSON, with every
field printed, including zeros for fields the tag does not have.

**Restore** in qtmonitor and **`tag-start --merge`** use the same rule,
`ConfigShape::merge()`. It copies a field into the tag's configuration only if
the tag supports it and the file speaks for it:

- **The file speaks for its keys,** zeros included, as found by
  `ConfigShape::forFile()`. A field the file leaves out keeps its value, so a
  file can hold just the settings to change.
- **A file whose `tag_type` is another tag type with a default config speaks
  only for that type's fields.** The padding Save writes for fields the file's
  own tag lacks is ignored, so a BitTagNG file's `"bittag_log":
  "BITTAG_UNSPECIFIED"` does not reach a BitTag.
- **Messages are merged field by field.** `hibernate` is copied element by
  element, up to the tag's own count. `tag_type` is never copied.

`tag-start` without `--merge` programs the file exactly as written.

## Fields with tag-specific meanings

Several fields are reused with a meaning that depends on the tag. qtmonitor
gives each meaning its own panel or panel variant. The firmware conversion is
in each family's `config.c`.

| Field | Meaning |
| --- | --- |
| `adxl362.act_thresh_g`, `inact_thresh_g` | Thresholds in g. BitTagNG and UIUCTag: ADXL367 wake-up threshold, 0.2-0.7 g. BitTag LE and BitPresTag: wake-mode thresholds at a fixed 4 g range. |
| `adxl362.inactive_sec` | Seconds on classic BitTag. A sample count everywhere else: 1-255 at the ADXL362 wake rate (BitTag LE, BitPresTag), 3-12 at the ADXL367's 6.25 Hz (BitTagNG, UIUCTag). |
| `adxl362.range`, `freq`, `filter` | Classic BitTag only. Fixed by the other families' firmware. |
| `adxl362.accel_type` | The ADXL367 on BitTagNG and UIUCTag; both firmwares report it. Only BitTagNG's default config lists it, so qtmonitor writes it for BitTagNG (as ADXL367 when the tag reported it, ADXL362 otherwise) and passes UIUCTag's through unchanged. |
| `start_delay` | IMUTag: minutes + 1. proto3 cannot send a 0, so the firmware reads 1 as "now" and turns a 0 into 1. qtmonitor shows minutes and sends minutes + 1. |
| `period` | PresTag's sampling period in seconds. The firmware rejects 0. |
| `hibernate` | At most two intervals (`Config.hibernate max_count:2` in `embedded/proto-c/default-options/tagdata.options`). An interval starting at 0 or `INT32_MAX` is unset. |

## Firmware responsibilities

Each family's `readConfig()` reports the fields it supports, and its
`writeConfig()` stages what the host sends. Because proto3 never sends a zero,
`writeConfig()` must read an absent field as 0, and must ignore fields the tag
does not have: a file written for another tag, or by an older qtmonitor, may
carry them. A change to either function is a firmware change, verified per
[Verifying a firmware change](../../bench/verifying-firmware.md).

## How it is checked

None of these needs a tag:

- [`config_shape_check`](../../../host/libraries/tagcore/test/README.md) checks
  that every default config loads, that the shapes reproduce the field
  visibility qtmonitor used before `ConfigShape`, and the lookup and merge
  rules;
- `qtmonitor --check-config-panels` checks the panels against the shapes;
- the [configuration probe](../../../host/applications/qtmon/test/README.md),
  `qtmonitor --config-probe`, runs 686 generated cases through a real
  Configuration tab. It records what Start would send and a fingerprint of
  every visible control, and compares them with committed golden results.
  Every accepted change to the goldens is listed, with its reason, in
  `expected-differences.md`.

A change to a default config, a panel, the merge, or the probe's cases goes
through all three. The one thing they cannot see is the USB path to a real tag,
which one live start on any tag covers.
