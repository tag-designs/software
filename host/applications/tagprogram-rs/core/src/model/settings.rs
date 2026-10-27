//! Persisted choices: a JSON object in a file at the caller's path.
//!
//! Replaces qtprogram's `QSettings`, with its keys (see the constants). A
//! key that is absent differs from one set to an empty or false value: the
//! first-run "Attach Automatically?" prompt is shown only while
//! [`AUTO_ATTACH`] has never been set. Every change is written through at
//! once, as `QSettings` would eventually.
//!
//! The app keeps the file at `settings.json` in the platform config
//! directory; tests and the headless examples pass a scratch path. The keys
//! are qtprogram's so the meaning of each value carries over, but the file
//! is not shared with qtprogram: `QSettings` stores elsewhere, in its own
//! format.

use serde_json::{Map, Value};
use std::path::{Path, PathBuf};

/// The release last chosen, e.g. `"fw-v0.7"`; removed when a file is chosen.
pub const RELEASE: &str = "firmwareRelease";
/// The ELF file last chosen; removed when a release is chosen.
pub const FILE: &str = "firmwareFile";
/// qtprogram 1.x's name for [`FILE`], read when that is absent.
pub const LEGACY_IMAGE: &str = "lastImage";
/// Recently chosen ELF files, newest first (a list of strings).
pub const RECENT_FILES: &str = "recentFiles";
/// The tag type last chosen.
pub const TARGET: &str = "target";
/// The record file in use.
pub const RECORD_PATH: &str = "recordPath";
/// The serial of the base last used.
pub const BASE_SERIAL: &str = "baseSerial";
/// Attach automatically (a bool). Its absence means the operator has not
/// yet been asked.
pub const AUTO_ATTACH: &str = "resetsAcknowledged";
/// The label last typed.
pub const LAST_LABEL: &str = "lastLabel";
/// How many recent files are kept.
pub const RECENT_COUNT: usize = 6;

/// The settings file and its values.
///
/// Values are held in memory and every change is saved whole; there is no
/// locking, so two processes using the same file would overwrite each
/// other's changes.
#[derive(Debug, Clone)]
pub struct Settings {
    /// The JSON file the settings are read from and written to.
    path: PathBuf,
    /// The top-level JSON object, key by key, as last loaded or set.
    values: Map<String, Value>,
}

impl Settings {
    /// Reads the settings at `path`. A missing or unreadable file, or one
    /// that is not a JSON object, gives empty settings (it is overwritten by
    /// the next change).
    pub fn load(path: impl Into<PathBuf>) -> Settings {
        let path = path.into();
        let values = std::fs::read(&path)
            .ok()
            .and_then(|b| serde_json::from_slice::<Value>(&b).ok())
            .and_then(|v| match v {
                Value::Object(m) => Some(m),
                _ => None,
            })
            .unwrap_or_default();
        Settings { path, values }
    }

    /// The file the settings live in.
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// `key` has been set (to anything, including `false` or `""`).
    pub fn contains(&self, key: &str) -> bool {
        self.values.contains_key(key)
    }

    /// The raw value of `key`, if set.
    pub fn get(&self, key: &str) -> Option<&Value> {
        self.values.get(key)
    }

    /// `key` as a string; `""` if absent or not a string.
    pub fn string(&self, key: &str) -> String {
        self.get(key)
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string()
    }

    /// `key` as a bool; `default` if absent or not a bool.
    pub fn bool(&self, key: &str, default: bool) -> bool {
        self.get(key).and_then(Value::as_bool).unwrap_or(default)
    }

    /// `key` as a list of strings; empty if absent. Non-string items are
    /// dropped.
    pub fn list(&self, key: &str) -> Vec<String> {
        self.get(key)
            .and_then(Value::as_array)
            .map(|a| {
                a.iter()
                    .filter_map(|v| v.as_str().map(str::to_string))
                    .collect()
            })
            .unwrap_or_default()
    }

    /// The ELF file last chosen: [`FILE`], else qtprogram 1.x's
    /// [`LEGACY_IMAGE`], else `""`.
    pub fn firmware_file(&self) -> String {
        let file = self.get(FILE).or_else(|| self.get(LEGACY_IMAGE));
        file.and_then(Value::as_str).unwrap_or("").to_string()
    }

    /// Sets `key` and writes the file.
    ///
    /// # Errors
    ///
    /// [`Settings::save`]'s error. The value stays set in memory even when
    /// the write fails.
    pub fn set(&mut self, key: &str, value: impl Into<Value>) -> Result<(), String> {
        self.values.insert(key.to_string(), value.into());
        self.save()
    }

    /// Removes `key` (making it absent) and writes the file.
    ///
    /// # Errors
    ///
    /// [`Settings::save`]'s error; the key stays removed in memory.
    pub fn remove(&mut self, key: &str) -> Result<(), String> {
        self.values.remove(key);
        self.save()
    }

    /// Puts `path` first in [`RECENT_FILES`], without duplicates, keeping
    /// at most [`RECENT_COUNT`].
    ///
    /// # Errors
    ///
    /// [`Settings::save`]'s error.
    pub fn add_recent_file(&mut self, path: &str) -> Result<(), String> {
        let mut recent = self.list(RECENT_FILES);
        recent.retain(|p| p != path);
        recent.insert(0, path.to_string());
        recent.truncate(RECENT_COUNT);
        self.set(RECENT_FILES, recent)
    }

    /// Writes the settings: to a temporary file beside the target, then
    /// renamed over it, so a crash never leaves half a file. Creates the
    /// directory if needed.
    ///
    /// The temporary file is the target path with its extension replaced by
    /// `json.tmp` (`settings.json.tmp`). The JSON is pretty-printed.
    ///
    /// # Errors
    ///
    /// `"Cannot write <path> (<io error>)."` when the directory cannot be
    /// created or the file cannot be written or renamed.
    pub fn save(&self) -> Result<(), String> {
        let fail = |e: std::io::Error| format!("Cannot write {} ({e}).", self.path.display());
        if let Some(dir) = self.path.parent() {
            std::fs::create_dir_all(dir).map_err(fail)?;
        }
        let text = serde_json::to_string_pretty(&self.values).unwrap();
        let tmp = self.path.with_extension("json.tmp");
        std::fs::write(&tmp, text).map_err(fail)?;
        std::fs::rename(&tmp, &self.path).map_err(fail)
    }
}

/// Round-trip, absence, legacy-key and recent-file tests in temporary
/// directories.
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_and_absence() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("sub/settings.json");
        let mut s = Settings::load(&path);
        assert!(!s.contains(AUTO_ATTACH));
        assert!(s.bool(AUTO_ATTACH, true));
        s.set(AUTO_ATTACH, false).unwrap();
        s.set(RELEASE, "fw-v0.6").unwrap();
        s.set(RECENT_FILES, vec!["/a.elf", "/b.elf"]).unwrap();

        let mut t = Settings::load(&path);
        assert!(t.contains(AUTO_ATTACH));
        assert!(!t.bool(AUTO_ATTACH, true));
        assert_eq!(t.string(RELEASE), "fw-v0.6");
        assert_eq!(t.list(RECENT_FILES), ["/a.elf", "/b.elf"]);
        assert_eq!(t.string(TARGET), "");
        t.remove(RELEASE).unwrap();
        assert!(!Settings::load(&path).contains(RELEASE));
    }

    #[test]
    fn legacy_file_and_recent_list() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.json");
        std::fs::write(&path, r#"{"lastImage": "/old.elf"}"#).unwrap();
        let mut s = Settings::load(&path);
        assert_eq!(s.firmware_file(), "/old.elf");
        s.set(FILE, "/new.elf").unwrap();
        assert_eq!(s.firmware_file(), "/new.elf");
        for i in 0..8 {
            s.add_recent_file(&format!("/{i}.elf")).unwrap();
        }
        s.add_recent_file("/5.elf").unwrap();
        assert_eq!(
            s.list(RECENT_FILES),
            ["/5.elf", "/7.elf", "/6.elf", "/4.elf", "/3.elf", "/2.elf"]
        );
    }

    #[test]
    fn corrupt_file_reads_as_empty() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.json");
        std::fs::write(&path, "[1, 2").unwrap();
        assert!(!Settings::load(&path).contains(TARGET));
    }
}
