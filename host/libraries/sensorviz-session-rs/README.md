---
type: readme
status: current
summary: sensorviz-session, a safe Rust wrapper over sensorViz's Qt-free core through tagcore_c's svz_* functions - so a Rust program opens and shows a tag log exactly as the Qt sensorViz does; building, the API, the JSON the interface carries, and the tests.
---

# sensorviz-session (Rust)

A safe Rust interface to sensorViz's Qt-free core. `Session` wraps the
`svz_*` functions of [`tagcore_c`](../tagcore-c/README.md)
([`sensorviz_c.h`](../tagcore-c/sensorviz_c.h)), which wrap
`sensorviz::Session`
([`session.h`](../../applications/sensorviz/core/session.h)): the code the Qt
sensorViz runs. So a Rust program -- the Rust sensorViz
(`host/applications/sensorviz-rs`), step 4 of
[leaving Qt](../../applications/sensorviz/design/proposals/leaving-qt.md) --
reads a log, runs the display transforms, builds the plot spec, and formats
the hover readout exactly as sensorViz does.

| What | Call |
| --- | --- |
| Open a log as File > Load does | `open(path, Option<&Preferences>)` |
| What the log is and holds | `catalog()` |
| The streams and every choice about them | `view()` |
| What to draw | `spec()`, then `envelope(trace.stream, t0, t1, columns, break_gaps)` per trace |
| The hover readout; a time as text | `hover(t)`, `format_time(t)` |
| Transform parameters, for their controls | `parameters()`, `set_parameter(id, value)` |
| Changing the view | `set_stream_visible`, `set_visible_streams`, `set_stream_range`, `reset_zoom`, `set_colors`, `set_axis_sides`, `set_title`, `set_title_visible`, `set_utc_offset`, `set_cursors_visible`, `set_break_at_gaps` |
| The range dialog's starting range | `range_for_dialog(id)` |
| The compass panel's samples | `compass_samples()` |
| Per-tag preferences and their file | `Preferences::load`, `save`; `Session::apply_preferences`, `remember_preferences`, `forget_preferences` |
| The state dump, for checking | `state_dump()` |

Every setter returns the `Changes` it made -- streams, visibility, ranges,
appearance, title, UTC offset, cursors, gaps, parameters -- and nothing when
nothing changed, so a front end redraws only what it must.

## Building

As for [`taglogwriter`](../taglogwriter-rs/README.md) and
[`calibsession`](../calibsession-rs/README.md):
[`tagcore-c-sys`](../tagcore-c-sys/README.md) links the library, so a program
may use all three.

```sh
cmake -DBUILD_TAGCORE_C=ON <build-dir>
cmake --build <build-dir> --target tagcore_c_static tagcore_c
export TAGCORE_C_LIB_DIR=<build-dir>/Release/lib
cargo test                           # macOS: libtagcore_c.a; Linux, Windows: shared
TAGCORE_C_LINK=dylib cargo test      # the shared library on macOS too
```

## The JSON

Descriptions cross the C interface as JSON, written by `sensorviz_c.cc` and
parsed into this crate's types, which name every field. Keys are camelCase;
numbers read back exactly (17 significant digits); a non-finite number is
`null`; colors are `#rrggbb`; time domains are `epoch` or `elapsed`; sides
`left` or `right`.

| Call | Top level | Holds |
| --- | --- | --- |
| `svz_catalog_json` | object | `path`, `tagType`, `profileName`, `preferenceKey`, `info` (object), `timeDomain`, `collectionStartEpochMs` (or null), `recordSets`, `eventMarkers` (with `times`), `compassCalibration` (`present`, `epoch`, `warning`, `hardIron`, `softIron` row-major, `accelOffset`), `compassSource`, `transformFailures` |
| `svz_view_json` | object | `streams` (`id`, `label`, `units`, `group`, `groupLabel`, `color`, `effectiveColor`, `derived`, `defaultVisible`, `axisSide`, `effectiveAxisSide`, `axisRange` or null, `timeDomain`, `stream` handle, `visible`), `visibleStreamIds`, `customRanges` (id to `lower`, `upper`, `explicit`), `title`, `titleVisible`, `utcOffsetHours`, `cursorsVisible`, `breakAtGaps`, `parameters` (id to value) |
| `svz_spec_json` | object | `timeDomain`, `utcOffsetHours`, `xDataRange` (or null), `traces` (`id`, `name`, `stream`, `color`, `penWidth`, `axis`), `axes` (`side`, `label`, `color`, `mode` auto/linked/fixed, `linkedGroup`, `traces`, `range` or null), `eventMarkerTimes`, `metadataRows`, `title`, `breakAtGaps` |
| `svz_parameters_json` | array | `id`, `title`, `prompt`, `units`, `kind` (number/toggle), `minimum`, `maximum`, `decimals`, `value` |
| `svz_hover_json` | array | strings: the time, then `label: value units` per visible stream |

An `auto` axis takes its range from the data, padded, and widened to include
what it showed before; a renderer keeps that history itself, as the Qt one
does.

## Checking

`tests/session.rs`, on the committed fixtures:

| Test | Checks |
| --- | --- |
| `imutag_describes_and_draws` | The catalog, view and spec parse; every trace's envelope arrives whole, within the time range; the hover readout |
| `compasstag_compass_and_parameters` | 44 compass samples in time order; the compass parameters; declination remaking the heading only |
| `setters_report_changes` | Each setter's change bits, none on a repeat; the pressure range linking altitude; colors, sides and visibility landing in the view |
| `rust_setters_match_the_core_script` | A session driven through these setters reaches exactly the catalog, view and spec the core's own state-dump script reaches |
| `preferences_round_trip` | A file loaded, applied on open, changed, remembered, saved as the Qt app writes it, forgotten |
| `errors_are_reported` | A missing log and a refused preference file come back as errors, the open log kept |

Changing one step of the script-equivalence test (declination 12.4 for 12.5)
fails it. The core underneath is checked by sensorViz's own checks and by
`session_cli` against the Qt app ([test README](../../applications/sensorviz/test/README.md)).

## Maintaining

The declarations are `tagcore_c_sys::svz`
([`svz.rs`](../tagcore-c-sys/src/svz.rs)), written by hand from
`sensorviz_c.h`. When the header changes, change the declaration and the safe
method over it; when a signature, meaning or JSON schema changes
incompatibly, the header raises `SVZ_ABI_VERSION` and `svz.rs` must follow.
A field added to the JSON is ignored here until a type names it.
