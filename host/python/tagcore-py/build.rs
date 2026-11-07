//! Build script: the link arguments an extension module needs (on macOS,
//! `-undefined dynamic_lookup`, so the module resolves Python's symbols
//! from the interpreter that imports it).

fn main() {
    pyo3_build_config::add_extension_module_link_args();
}
