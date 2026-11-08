//! Stages the page and runs Tauri's build step through the shared helper
//! (`tagapp-shell-build`). Off Windows, when `libtagcore_c` is linked as a
//! shared library, the binary gets an rpath where it is found
//! (`TAGCORE_C_RPATH`, else `TAGCORE_C_LIB_DIR`): sensorViz's core is in it.

/// Stages the page, kit and Tauri inputs and runs tauri-build.
fn main() {
    tagapp_shell_build::build(tagapp_shell_build::Rpath::Always);
}
