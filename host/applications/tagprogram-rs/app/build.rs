//! Stages the page and runs Tauri's build step through the shared helper
//! (`tagapp-shell-build`). Tag Programmer links no C++ library, so it needs
//! no rpath. See `../design/build-and-packaging.md`.

/// Stages the page, kit and Tauri inputs and runs tauri-build.
fn main() {
    tagapp_shell_build::build(tagapp_shell_build::Rpath::Never);
}
