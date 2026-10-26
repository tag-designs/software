//! Assembles the page Tauri serves, then runs Tauri's build step.
//!
//! The page lives in `../ui` and loads the shared kit from `host/ui-kit` by a
//! relative path, so it also opens as a plain file in a browser. Tauri serves
//! one directory, so `dist/` gets a copy of both, with the kit's path
//! rewritten to `ui-kit/`.

use std::fs;
use std::path::Path;

const KIT_HREF: &str = "../../../ui-kit/";

fn copy_dir(from: &Path, to: &Path, rewrite: bool) {
    fs::create_dir_all(to).expect("create dist");
    for entry in fs::read_dir(from).expect("read front end") {
        let entry = entry.expect("dir entry");
        let path = entry.path();
        if path.is_dir() {
            continue;
        }
        let dest = to.join(entry.file_name());
        if rewrite && path.extension().is_some_and(|e| e == "html") {
            let html = fs::read_to_string(&path).expect("read html");
            fs::write(&dest, html.replace(KIT_HREF, "ui-kit/")).expect("write html");
        } else {
            fs::copy(&path, &dest).expect("copy front end");
        }
    }
}

fn main() {
    let ui = Path::new("../ui");
    let kit = Path::new("../../../ui-kit");
    let dist = Path::new("dist");
    let _ = fs::remove_dir_all(dist);
    copy_dir(ui, dist, true);
    copy_dir(kit, &dist.join("ui-kit"), false);
    println!("cargo:rerun-if-changed=../ui");
    println!("cargo:rerun-if-changed=../../../ui-kit");
    tauri_build::build()
}
