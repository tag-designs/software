//! Download Data…: the attached tag's log into a SQLite file.
//!
//! Port of `host/applications/qtmon/core/downloadjob.cc`, with the page loop
//! of `host/commandline-rs/src/bin/tag-dwnld.rs`: the writer decides how far
//! each page advances the download and where it ends. The file is written by
//! the C++ writer through the `taglogwriter` crate, so it is the one
//! qtmonitor and `tag-dwnld` write. Built without the `sqlitelog` feature,
//! [`run`] refuses, and everything else in Tag Monitor still works.
//!
//! [`run`] blocks the tag thread for as long as the download takes; nothing
//! else may use the [`Tag`] meanwhile.

use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;

use tagcore::Tag;

/// The extension of a download file, with its dot.
pub const DOWNLOAD_EXTENSION: &str = ".db3";

/// The file name the save dialog suggests.
pub const DEFAULT_DOWNLOAD_NAME: &str = "untitled.db3";

/// How a download ended.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct DownloadResult {
    /// The log was written: possibly empty, or cut short by cancel.
    pub ok: bool,
    /// The user cancelled; what was downloaded is kept.
    pub cancelled: bool,
    /// Log records written (the next download index).
    pub records: i32,
    /// Why it failed; empty when `ok`.
    pub error: String,
}

impl DownloadResult {
    /// A failed download, with `error` logged.
    fn failed(error: String, records: i32) -> Self {
        log::error!("{error}");
        DownloadResult {
            ok: false,
            cancelled: false,
            records,
            error,
        }
    }
}

/// The file a download is written to, from the path the user chose: a name
/// with no extension (or an empty one) gets [`DOWNLOAD_EXTENSION`]; any
/// other is kept as typed. qtmonitor's `downloadPath()` with its one format.
pub fn download_path(chosen: &Path) -> PathBuf {
    match chosen.extension() {
        Some(ext) if !ext.is_empty() => chosen.to_path_buf(),
        _ => {
            let mut name = chosen.as_os_str().to_os_string();
            if name.to_string_lossy().ends_with('.') {
                name = name.to_string_lossy().trim_end_matches('.').into();
            }
            name.push(DOWNLOAD_EXTENSION);
            PathBuf::from(name)
        }
    }
}

/// Downloads the attached tag's log into `path`, replacing it.
///
/// Reads the status (for the record count) and the configuration, creates
/// the file, writes the header from the tag, then requests pages from index
/// 0. Each page's records advance the index; a NODATA page below the record
/// count advances it by one; the download ends when the writer consumes
/// nothing or the count is reached. `cancel` is checked between pages, and
/// what was written is kept. `progress` is called with (0, count) before the
/// first page and with (records, count) after each.
///
/// A tag with no records gives `ok` and 0 records, and creates no file.
#[cfg(feature = "sqlitelog")]
pub fn run(
    tag: &Tag,
    path: &Path,
    cancel: &AtomicBool,
    mut progress: impl FnMut(i32, i32),
) -> DownloadResult {
    use std::sync::atomic::Ordering;
    use std::time::Instant;
    use tagcore::proto::ack;
    use taglogwriter::{LogHeader, LogWriter};

    let started = Instant::now();
    // The writer logs to stderr, at DEBUG until told otherwise; follow the
    // level the UI chose.
    taglogwriter::set_debug(log::log_enabled!(log::Level::Debug));
    // The external page count where there is one (IMUTag's NAND); older
    // firmware reports only the internal count.
    let status = match tag.status() {
        Ok(status) => status,
        Err(_) => {
            return DownloadResult::failed(
                format!("Could not read tag status: {}", tag.debug_message()),
                0,
            );
        }
    };
    let max_count = if status.external_data_count > 0 {
        status.external_data_count
    } else {
        status.internal_data_count
    };
    if max_count <= 0 {
        log::info!("No log records to download");
        return DownloadResult {
            ok: true,
            ..DownloadResult::default()
        };
    }

    let Ok(config) = tag.config() else {
        return DownloadResult::failed("Could not read tag config".into(), 0);
    };
    let mut writer = match LogWriter::create(path, &config, true) {
        Ok(writer) => writer,
        Err(error) => return DownloadResult::failed(format!("Could not open output: {error}"), 0),
    };

    // The header, from the requests readTagLogHeader() makes, in its order:
    // calibration constants until one is refused, then the state log in
    // chunks until one is refused or empty.
    let Ok(info) = tag.tag_info() else {
        return DownloadResult::failed("Could not read tag info".into(), 0);
    };
    let mut calibration = Vec::new();
    while let Ok(constants) = tag.read_calibration(calibration.len() as i32) {
        calibration.push(constants);
    }
    let mut states = Vec::new();
    while let Ok(chunk) = tag.state_log(states.len() as i32) {
        if chunk.states.is_empty() {
            break;
        }
        states.extend(chunk.states);
    }
    let header = LogHeader {
        config,
        info,
        calibration,
        states,
    };
    if let Err(error) = writer.write_header(&header) {
        return DownloadResult::failed(format!("Download header write failed: {error}"), 0);
    }
    if let Err(error) = writer.begin_log() {
        return DownloadResult::failed(format!("Could not begin log download: {error}"), 0);
    }

    let mut count = 0i32;
    let mut error = String::new();
    progress(0, max_count);
    while !cancel.load(Ordering::Relaxed) {
        let page = match tag.data_log(count) {
            Ok(page) => page,
            Err(_) => {
                error = format!(
                    "GetDataLog failed at entry {count}: {}",
                    tag.debug_message()
                );
                break;
            }
        };
        if !page.error_message().is_empty() {
            error = page.error_message().to_string();
            break;
        }
        if page.err == ack::Err::Nodata as i32 && count < max_count {
            log::info!("skipping missing log block {count}");
            count += 1;
            progress(count, max_count);
            if count >= max_count {
                break;
            }
            continue;
        }
        // The records this page held, which is where the next request
        // starts; 0 at the end of the log.
        let consumed = match writer.write_log(&page) {
            Ok(consumed) => consumed as i32,
            Err(e) => {
                error = format!("Could not write log data: {e}");
                break;
            }
        };
        log::debug!("retrieved log block {count} len={consumed}");
        if consumed == 0 {
            log::info!("no data");
            break;
        }
        count += consumed;
        progress(count, max_count);
        if count >= max_count {
            break;
        }
    }
    let cancelled = cancel.load(Ordering::Relaxed) && error.is_empty();

    // Commit what was written, whatever ended the loop.
    if let Err(e) = writer.end_log()
        && error.is_empty()
    {
        error = format!("Could not finish log download: {e}");
    }

    log::info!(
        "Download Elapsed time: {:.2} seconds",
        started.elapsed().as_secs_f64()
    );
    log::info!("Downloaded {count} blocks");
    progress(count, max_count);

    if !error.is_empty() {
        return DownloadResult::failed(error, count);
    }
    DownloadResult {
        ok: true,
        cancelled,
        records: count,
        error,
    }
}

/// Refuses: this build has no SQLite writer (the `sqlitelog` feature is off).
#[cfg(not(feature = "sqlitelog"))]
pub fn run(
    _tag: &Tag,
    _path: &Path,
    _cancel: &AtomicBool,
    _progress: impl FnMut(i32, i32),
) -> DownloadResult {
    DownloadResult::failed(
        "This Tag Monitor was built without SQLite log support (feature sqlitelog).".into(),
        0,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    /// qtmonitor's downloadPath() cases that apply to one format.
    #[test]
    fn download_path_adds_only_a_missing_extension() {
        let p = |s: &str| download_path(Path::new(s)).to_string_lossy().into_owned();
        assert_eq!(p("/d/run"), "/d/run.db3");
        assert_eq!(p("/d/run."), "/d/run.db3");
        assert_eq!(p("/d/run.db3"), "/d/run.db3");
        assert_eq!(p("/d/run.DB3"), "/d/run.DB3");
        assert_eq!(p("/d/run.sqlite"), "/d/run.sqlite");
    }
}
