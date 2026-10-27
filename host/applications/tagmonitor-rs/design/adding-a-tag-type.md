---
type: procedure
status: current
summary: How to make Tag Monitor (Rust, and qtmonitor with it) support a new tag type - the TagType value, the default configuration, the panel table, a new panel when the tag has new fields, the probe cases and goldens, downloads, and the checks that say it is done.
---

# Adding a tag type to Tag Monitor

Tag Monitor knows a tag type in four places. Most are shared with qtmonitor,
and the two Tag Monitors are held to the same configuration bytes, so a tag
type is added to both in one change:

| Where | What it decides | Shared with qtmonitor |
| --- | --- | --- |
| `TagType` in `proto/tagdata.proto` | The type's name and number | yes, the same proto |
| `embedded/proto-c/<name>-proto-c/default-config.json` | Which Config fields the tag has (its ConfigShape), and New Configuration's values | yes, embedded by both |
| The panel table (`panels_for_tag` in `core/src/model/configform.rs`; `panelsForTag()` in qtmonitor's `tagconfiguration/configtab.cpp`) | Which panels the Configuration tab shows | a copy in each, which must agree |
| The SQLite writer (`host/libraries/tagcore/sqlitelog.cc` and `sqlitelog/`) | Whether Download Data... can write its log | yes, the same C++ writer |

Nothing else in Tag Monitor is per tag type, except two details listed under
[Other per-type rules](#other-per-type-rules). The commands, start checks,
status strip, prompts and menus treat every tag type alike.

Work through the steps in order. Paths are relative to the repository root.

## 1. The TagType value

Add the value to `enum TagType` in `proto/tagdata.proto`, if the firmware
does not already report one. The protobuf types are generated from this file
for the firmware (nanopb), the C++ host libraries and tagcore-rs; nothing in
Tag Monitor lists tag types by hand. Read `proto/README.md` before changing
the proto: a value once used must keep its number.

## 2. The default configuration

The tag's `default-config.json` is the source of truth for which Config
fields it has. It lists every field the tag supports, zeros included,
because a proto3 message cannot otherwise say which zero-valued fields are
meaningful ([configuration model](../../../../docs/shared/design/configuration-model.md)).

- A new tag gets one in its proto-c directory,
  `embedded/proto-c/<name>-proto-c/default-config.json`, made when the tag
  directory is created from the template (`embedded/tags/README.md`,
  **Template Tag Directory**). It must name the new `tag_type`.
- tagcore-rs embeds every `default-config.json` when it is built
  (`host/libraries/tagcore-rs/build.rs`); the C++ tagcore reads them when
  CMake configures, so **re-run CMake** after adding or changing one.
- Two directories may share a tag type; the first by directory name is used.

From this alone, New Configuration lists the type, opening a file of that
type works, and Start sends the file's fields merged onto these defaults.

## 3. The panel table

Decide which panels show the tag's fields. The panels, in page order:

| Index | Name | Owns |
| --- | --- | --- |
| 0 | `IntervalSchedule` | `active_interval`, `hibernate` |
| 1 | `IntervalScheduleWithPeriod` | the above, and `period` |
| 2 | `StartDelaySchedule` | `start_delay` |
| 3 | `BitTagLogFormat` | `bittag_log` |
| 4 | `Adxl362Standard` | `adxl362.range`, `freq`, `filter`, both thresholds, `inactive_sec` |
| 5 | `Adxl362Wake` | `adxl362` thresholds and `inactive_sec` (range fixed at 4 g) |
| 6 | `Adxl367Wake` | `adxl362.act_thresh_g`, `inactive_sec` |
| 7 | `Adxl367WakeWithType` | the above, and `adxl362.accel_type` |
| 8 | `Lsm6Imu` | `lsm6.odr`, `accel_rng`, `gyro_rng` |

`Panel::paths` in `configform.rs` is the exact list for each.

**If existing panels cover every field the default configuration lists**, add
one row in each table, with the same panels in the same order:

```rust
// core/src/model/configform.rs, panels_for_tag()
TagType::Newtag => &[0, 6],
```

```cpp
// host/applications/qtmon/tagconfiguration/configtab.cpp, panelsForTag()
{NEWTAG, {IntervalSchedule, Adxl367Wake}},
```

Then check both tables against every default configuration:

```sh
cd host/applications/tagmonitor-rs && cargo test --test panels
QT_QPA_PLATFORM=offscreen <qtmonitor> --check-config-panels
```

Each names any field the tag lists that no panel edits, any panel field the
tag does not have, and any tag type with a default configuration but no
panels. A field that a panel's field lies inside (or around) counts as
covered, so `adxl362` passes through whole when only some of its fields are
edited; a field no panel owns is sent as the document holds it.

## 4. A new panel, when the tag has new fields

If the tag has fields no panel edits, write a panel for them, in both
programs. Start from the closest existing panel.

**In qtmonitor** (`host/applications/qtmon/tagconfiguration/`): a
`ConfigPanel` subclass with `paths()`, `load()` and `store()`; add it to the
`PanelId` enum and the `names` array in `configtab.cpp` (both in page order),
build it in the constructor, and use it in `panelsForTag()`. See that
directory's `README.md`.

**In Tag Monitor** (`core/src/model/configform.rs`):

1. Model the panel's state with the widget types (`DoubleSpin`, `IntSpin`,
   `DateEdit`, `RadioGroup`), set up exactly as the Qt constructor sets up
   its widgets, in the same order: a `QDoubleSpinBox` rounds its limits to
   the decimals in force when they are set.
2. Add a `Panel` variant (or a new variant of an existing panel, as
   `AdxlVariant` does), and give it `load` and `store` that read and write
   exactly its `paths`, as the Qt panel's do. Copy the Qt code's order of
   operations: a value set before or after its range clamps differently.
3. `controls`: one `Control` per widget, with the Qt widget's group titles
   (`path`), label, tooltip and enabled state, character for character. The
   probe compares these with the Qt widgets, so a typo fails the probe.
4. `edit`: what each user change does, including any slot the Qt widget
   triggers (as a range button re-limits the thresholds). Give each control
   an `id` no other shown panel uses.
5. `paths`, `title`, a name in `PANEL_NAMES` and an entry in
   `ConfigForm::new`, at the same index as qtmonitor's `PanelId`.

The page needs no change unless the panel needs a kind of control the
renderer does not draw (radio, check, number, datetime); then add it to
`fieldNode`/`updateControl` in `ui/app.js` and to the form section of the
[contract](ui-engine-contract.md).

## 5. Probe cases and goldens

The configuration probe is what proves the two Tag Monitors send the same
bytes for the new type. Its cases are generated from the default
configurations, so a new type gets cases for every field once they are
regenerated (from the repository root):

```sh
cmake -DBUILD_QTMON_CHECKS=ON <build-dir>
cmake --build <build-dir> --target config_probe_cases qtmonitor
<build-dir>/Release/bin/config_probe_cases . host/applications/qtmon/test/cases
host/applications/qtmon/test/probe.sh <qtmonitor> host/applications/qtmon/test/golden
```

The goldens come from **qtmonitor**, the reference; the Rust port is the
build under test. For a change to an existing type, take the goldens from the
commit before the change, as `host/applications/qtmon/test/README.md`
explains, and record any intended difference in its `expected-differences.md`.
Then:

```sh
cd host/applications/tagmonitor-rs && cargo test --test config_probe -- --nocapture
```

It prints each differing case by key (`sent_hex`, `document`, `layout`,
`dialogs`, `open`, `ui`), with the lines that differ. `ui` differences name
the control; `sent_hex` alone usually means a `store` that writes a field
differently, or a widget set up in another order.

## 6. Downloads

Download Data... writes through the C++ SQLite writer, which must know the
tag's log format. Follow the recipe at the top of
`host/libraries/tagcore/sqlitelog.cc` and `host/libraries/tagcore/sqlitelog/README.md`
(the schema in `host/docs/src/reference/sqlite-logs.md`). Its first step,
`isTagLogStorageFormatSupported()`, is the one that is easy to miss.

Tag Monitor needs no change: `tagcore_c` is rebuilt with the writer, and a
CMake build links it. Until the writer supports the type, Download refuses
with the writer's own message ("Could not open output: ... does not support
tag type ..."), before writing anything.

## Other per-type rules

Only two rules outside the form look at the tag type:

- `erase_sector_maximum` in `core/src/model/tagrules.rs` (and
  `core/tagrules.cc`) estimates an IMUTag's erase from its page count, for
  firmware that does not report the erase total. A tag that reports
  `erase_sectors_total_plus_one`, as current firmware does, needs nothing.
- Start is offered only when the document's type is the attached tag's
  (`commands.rs`), which needs nothing either.

## Docs and samples

- The user manual's **Tag Configuration** section, in
  `host/docs/src/apps/qtmonitor.md`, gets a subsection for the type, with a
  screenshot from qtmonitor's capture.
- Optionally, regenerate the browser mock's sample if the new panel should be
  in it: `cargo run -p tagmonitor-core --example form_view -- NEWTAG`
  prints the panels; `ui/mock-engine.js` says where they go.

## Done when

- [ ] `cargo test --test panels` and `qtmonitor --check-config-panels` pass;
- [ ] `cargo test` passes, `config_probe` included, against goldens
      regenerated from qtmonitor;
- [ ] New Configuration ▸ the type shows the expected panels, in the app
      (`TAGMONITOR_NO_ATTACH=1 cargo run -p tagmonitor`);
- [ ] on a tag of the type, `core/examples/session_check.rs` passes: attach,
      sync, self-test, read, start, run, stop, download, erase. It changes the
      tag; follow the bench procedure first (`docs/bench/`).
