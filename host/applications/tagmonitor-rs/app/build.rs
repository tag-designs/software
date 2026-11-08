//! Stages the page and runs Tauri's build step through the shared helper
//! (`tagapp-shell-build`). With the `sqlitelog` feature, off Windows, the
//! binary gets an rpath where a shared `libtagcore_c` is found
//! (`TAGCORE_C_RPATH`, else `TAGCORE_C_LIB_DIR`); see
//! `../design/build-and-packaging.md`.

/// Stages the page, kit and Tauri inputs and runs tauri-build.
fn main() {
    tagapp_shell_build::build(tagapp_shell_build::Rpath::WithFeature("sqlitelog"));
}
