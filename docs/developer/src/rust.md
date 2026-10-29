# Rust Reference

rustdoc reference for the project's Rust crates, private items included: the
Rust counterpart of the [API Reference](api.md). Use it for each crate's types,
functions and their contracts; use the Markdown design documents for
architecture, rationale and workflows. Every item is documented (each crate
warns on `missing_docs` and `clippy::missing_docs_in_private_items`), and the
crates link to one another and share one search.

<!-- The crate links are HTML rather than Markdown on purpose: rustdoc writes
their targets after MkDocs has built and checked the site, so MkDocs would
report each Markdown link as broken. -->

## Libraries

| Crate | Source | What it is |
| --- | --- | --- |
| <a href="rust/tagcore/index.html">tagcore</a> | `host/libraries/tagcore-rs` | Talking to tag firmware monitors through a tag base: the Rust port of tagcore's `Tag`, the protobuf messages, ConfigShape |
| <a href="rust/tagcore_c_sys/index.html">tagcore-c-sys</a> | `host/libraries/tagcore-c-sys` | Links `tagcore_c`, the C interface to the C++ host libraries, and declares its functions; the one crate that does, so the wrappers below can be used together |
| <a href="rust/taglogwriter/index.html">taglogwriter</a> | `host/libraries/taglogwriter-rs` | Writing tag SQLite logs through the C++ writer in `tagcore_c` |
| <a href="rust/calibsession/index.html">calibsession</a> | `host/libraries/calibsession-rs` | The magnetometer and accelerometer calibration session and the compass display angles, through the C++ core in `tagcore_c` |
| <a href="rust/sensorviz_session/index.html">sensorviz-session</a> | `host/libraries/sensorviz-session-rs` | Opening and viewing tag logs through sensorViz's C++ core in `tagcore_c`: the catalog, view, plot spec, envelopes, hover text, transforms and preferences |
| <a href="rust/tagprobe/index.html">tagprobe</a> | `host/libraries/tagprobe-rs` | Programming tags through the tag bases with probe-rs |

## Applications

The toolkit-free core of each Rust application: its model, its engine and the
rules behind its window. The Tauri shell beside each (`app/`) is not in the
reference; it is a page that forwards the page's commands to the engine, and
its own module documentation describes it.

| Crate | Source | What it is |
| --- | --- | --- |
| <a href="rust/tagprogram_core/index.html">tagprogram_core</a> | `host/applications/tagprogram-rs/core` | Tag Programmer: programming, testing and recording tags |
| <a href="rust/tagmonitor_core/index.html">tagmonitor_core</a> | `host/applications/tagmonitor-rs/core` | Tag Monitor: the port of qtmonitor's core -- attach, check, configure, start and download tags |
| <a href="rust/tagcalibrate_core/index.html">tagcalibrate_core</a> | `host/applications/tagcalibrate-rs/core` | Tag Calibrator: the port of qtcalibrate's behaviour, over the C++ calibration session |
| <a href="rust/sensorviz_core/index.html">sensorviz_core</a> | `host/applications/sensorviz-rs/core` | sensorViz: the port of the Qt sensorviz's behaviour, over sensorViz's C++ core |

## Command-line tools

| Crate | Source | What it is |
| --- | --- | --- |
| <a href="rust/tag_tools/index.html">tag_tools</a> | `host/commandline-rs` | The options, logging and base selection the tools share |
| <a href="rust/tag_dwnld/index.html">tag_dwnld</a>, <a href="rust/tag_info/index.html">tag_info</a>, <a href="rust/tag_start/index.html">tag_start</a>, <a href="rust/tag_stop/index.html">tag_stop</a>, <a href="rust/tag_reset/index.html">tag_reset</a>, <a href="rust/tag_test/index.html">tag_test</a>, <a href="rust/tag_cal/index.html">tag_cal</a>, <a href="rust/tag_cal_write/index.html">tag_cal_write</a>, <a href="rust/tag_peek/index.html">tag_peek</a> | `host/commandline-rs/src/bin` | The Rust tag tools, one page each |
| <a href="rust/tag_attach_cycle/index.html">tag_attach_cycle</a>, <a href="rust/tag_monitor_test/index.html">tag_monitor_test</a>, <a href="rust/tag_side_channel_check/index.html">tag_side_channel_check</a>, <a href="rust/tag_test_example/index.html">tag_test_example</a>, <a href="rust/qtmonitor_fixture_capture/index.html">qtmonitor_fixture_capture</a> | `host/commandline-rs/src/bin` | Bench and test tools |

## Building it

The `rust_docs` target runs `cargo doc` for each crate (the list is
`DEVELOPER_RUST_DOCS` in the top-level `CMakeLists.txt`), and
`developer_docs` runs it after MkDocs, as it runs Doxygen. It needs `cargo`;
without it the portal is built without this reference and these links do not
resolve. Nothing is linked, so the crates that link `libtagcore_c` do not
need the library to be built first.

```sh
cmake --build <build-dir> --target rust_docs        # the reference alone
cmake --build <build-dir> --target developer_docs   # the whole portal
```

To check one crate's documentation as you change it, from its directory:

```sh
cargo clippy --all-targets 2>&1 | grep -c "missing documentation"   # expect 0
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items
```
