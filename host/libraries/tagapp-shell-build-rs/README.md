---
type: readme
status: current
summary: "tagapp-shell-build, the build-script half of the shared Tauri shell -- one call from each application's build.rs that stages the page and the UI kit, writes the one capabilities file, sets TAURI_CONFIG, adds the libtagcore_c rpath and runs tauri-build, writing nothing into the source tree."
---

# tagapp-shell-build (Rust)

Each Tauri application's `app/build.rs` is one call:

```rust
fn main() {
    tagapp_shell_build::build(tagapp_shell_build::Rpath::WithFeature("sqlitelog"));
}
```

It runs inside the application's build script, so `CARGO_MANIFEST_DIR`,
`OUT_DIR` and the `CARGO_FEATURE_*` variables it reads are the
application's. `build`:

1. **Stages the page.** The page lives in the application's `ui/` and loads
   the shared kit from `host/ui-kit` by a relative path (`../../../ui-kit/`),
   so it also opens as a plain file in a browser. Tauri embeds one
   directory, so the page and the kit are copied into `$OUT_DIR/tauri`, with
   the kit's path rewritten to `ui-kit/` in the HTML.
2. **Writes the capabilities.** [`capabilities.json`](capabilities.json),
   compiled into this crate, becomes `capabilities/default.json` in the
   stage. It is the one permission set every application's window gets:
   `core:default`, `core:window:allow-set-title`, `dialog:default`,
   `opener:default`, and `opener:allow-open-url` for `file://**` (the
   installed user guide). Applications keep no capabilities file of their
   own.
3. **Copies** the application's `icons/`, `tauri.conf.json` and `Cargo.toml`
   into the stage, and prints `cargo:rerun-if-changed` for each source.
4. **Points `generate_context!` at the staged page** through `TAURI_CONFIG`
   (`{"build":{"frontendDist":"<canonical staged dist>"}}`; canonical so a
   Windows drive path is not read as a URL).
5. **Adds the rpath** to a shared `libtagcore_c` when `rpath` asks for one,
   off Windows, when the library is linked shared (the default everywhere
   but macOS): `TAGCORE_C_RPATH` when set (a package's `$ORIGIN/../lib`),
   else `TAGCORE_C_LIB_DIR`.
6. **Runs `tauri_build::build()`** with the stage as its working directory,
   since tauri-build writes `gen/schemas` there and reads `Cargo.toml` from
   it.

Layout of `$OUT_DIR/tauri`, rebuilt from scratch on every run:

```text
dist/            ../ui files, *.html with the kit path rewritten to ui-kit/
dist/ui-kit/     host/ui-kit files
capabilities/    default.json: this crate's capabilities.json
icons/           ../app/icons
tauri.conf.json  Cargo.toml
gen/schemas/     written by tauri-build
```

Only the top level of each source directory is copied. Any failure panics,
which fails the build with the message. Nothing is written into the source
tree, so a release build works from a read-only checkout.

| `Rpath` | Used by | Why |
| --- | --- | --- |
| `Never` | Tag Programmer | links no C++ library |
| `WithFeature("sqlitelog")` | Tag Monitor | `libtagcore_c` is linked only for Download, behind that feature |
| `Always` | Tag Calibrator, Tag View | their cores are in `libtagcore_c` |

## Building and checking

```sh
cd host
cargo build -p tagmonitor        # runs the helper in tagmonitor's build script
ls target/debug/build/tagmonitor-*/out/tauri
cargo clippy -p tagapp-shell-build --all-targets
```

A change to `capabilities.json` rebuilds this crate, and so reruns every
application's build script. The run-time half is
[`tagapp-shell`](../tagapp-shell-rs/README.md); the staging is read line by
line in [chapter 10 of the tutorial](../../design/tutorial/10-tauri-shell.md#buildrs-staging-the-page).
