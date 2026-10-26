//! The CSV record of programmed tags.
//!
//! Port of qtprogram's `recordlog.cpp`. One row per programming or retest
//! attempt, appended as it ends; the file is RFC 4180 CSV (UTF-8, quoted only
//! where a field needs it) so a spreadsheet opens it. The first seven columns
//! are the specification's and identify the file; the rest follow.

use chrono::{DateTime, Utc};
use serde::Serialize;
use std::io::Write;
use std::path::{Path, PathBuf};

/// The columns, in file order.
pub const HEADER: [&str; 14] = [
    "label",
    "tag_type",
    "uuid",
    "firmware_version",
    "git_hash",
    "date",
    "test_status",
    "result",
    "image_sha256",
    "image",
    "erase",
    "voltage_v",
    "probe_serial",
    "host_version",
];
/// Header columns a file must start with to be accepted as a record.
const REQUIRED_COLUMNS: usize = 7;
const LABEL_COLUMN: usize = 0;
const UUID_COLUMN: usize = 2;

/// One attempt (qtprogram's `ProgramRecord`).
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ProgramRecord {
    /// The board's physical label, typed by the operator.
    pub label: String,
    /// Build target, e.g. `"CompassTagAT25"`.
    pub tag_type: String,
    /// The MCU's unique ID, as `TagInfo` reports it.
    pub uuid: String,
    /// Release tag, e.g. `"fw-v0.7"`, or `"custom"`.
    pub firmware_version: String,
    /// What the tag reports after programming, else the image's.
    pub git_hash: String,
    /// When the attempt ended; written in UTC, ISO 8601, to the second.
    pub date: DateTime<Utc>,
    /// `TestResult` name, or empty if the tests did not run.
    pub test_status: String,
    /// `OK`, `TEST_FAILED`, `PROGRAM_FAILED: (reason)`, `NO_ANSWER`,
    /// `MISMATCH` or `CANCELLED`.
    pub result: String,
    /// SHA-256 of the ELF programmed.
    pub image_sha256: String,
    /// The ELF's file name.
    pub image: String,
    /// `none`, `below-calibration` or `all`.
    pub erase: String,
    /// Target voltage at the connect check, volts; negative if unknown.
    pub voltage: f64,
    /// The base's ST-LINK serial.
    pub probe_serial: String,
    /// The program's version.
    pub host_version: String,
}

impl Default for ProgramRecord {
    fn default() -> Self {
        ProgramRecord {
            label: String::new(),
            tag_type: String::new(),
            uuid: String::new(),
            firmware_version: String::new(),
            git_hash: String::new(),
            date: DateTime::<Utc>::UNIX_EPOCH,
            test_status: String::new(),
            result: String::new(),
            image_sha256: String::new(),
            image: String::new(),
            erase: String::new(),
            voltage: -1.0,
            probe_serial: String::new(),
            host_version: String::new(),
        }
    }
}

impl ProgramRecord {
    /// This row's fields, in [`HEADER`] order, unquoted. An unknown voltage
    /// is empty; a known one has two decimals.
    pub fn fields(&self) -> Vec<String> {
        vec![
            self.label.clone(),
            self.tag_type.clone(),
            self.uuid.clone(),
            self.firmware_version.clone(),
            self.git_hash.clone(),
            self.date.format("%Y-%m-%dT%H:%M:%SZ").to_string(),
            self.test_status.clone(),
            self.result.clone(),
            self.image_sha256.clone(),
            self.image.clone(),
            self.erase.clone(),
            if self.voltage < 0.0 {
                String::new()
            } else {
                format!("{:.2}", self.voltage)
            },
            self.probe_serial.clone(),
            self.host_version.clone(),
        ]
    }
}

/// `field` as it is written: quoted, with quotes doubled, only if it holds a
/// comma, a quote or a line break.
pub fn quote(field: &str) -> String {
    if !field.contains([',', '"', '\n', '\r']) {
        return field.to_string();
    }
    format!("\"{}\"", field.replace('"', "\"\""))
}

/// Parses CSV text into rows of fields. Accepts LF or CRLF line ends and
/// quoted fields spanning lines; blank lines are skipped.
pub fn parse(text: &str) -> Vec<Vec<String>> {
    let mut rows = Vec::new();
    let mut row = Vec::new();
    let mut field = String::new();
    let (mut quoted, mut any) = (false, false);
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if quoted {
            match c {
                '"' if chars.peek() == Some(&'"') => {
                    field.push('"');
                    chars.next();
                }
                '"' => quoted = false,
                _ => field.push(c),
            }
            continue;
        }
        match c {
            '"' => {
                quoted = true;
                any = true;
            }
            ',' => {
                row.push(std::mem::take(&mut field));
                any = true;
            }
            '\n' | '\r' => {
                if c == '\r' && chars.peek() == Some(&'\n') {
                    chars.next();
                }
                if any || !field.is_empty() {
                    row.push(std::mem::take(&mut field));
                    rows.push(std::mem::take(&mut row));
                }
                row.clear();
                field.clear();
                any = false;
            }
            _ => {
                field.push(c);
                any = true;
            }
        }
    }
    if any || !field.is_empty() {
        row.push(field);
        rows.push(row);
    }
    rows
}

/// An I/O error as text, without Rust's " (os error N)" suffix.
fn reason(e: &std::io::Error) -> String {
    let text = e.to_string();
    match text.find(" (os error") {
        Some(i) => text[..i].to_string(),
        None => text,
    }
}

fn add_distinct(list: &mut Vec<String>, value: &str) {
    if !value.is_empty() && !list.iter().any(|v| v == value) {
        list.push(value.to_string());
    }
}

/// The open record file and the rows read from it.
#[derive(Debug, Default)]
pub struct RecordLog {
    path: Option<PathBuf>,
    rows: Vec<Vec<String>>,
}

impl RecordLog {
    /// No file open.
    pub fn new() -> RecordLog {
        RecordLog::default()
    }

    /// Opens an existing record, reading its rows.
    ///
    /// Errors: the file does not exist or cannot be read, or its first line
    /// does not start with the header's first seven columns. On error the
    /// previously open file stays open.
    pub fn open(&mut self, path: &Path) -> Result<(), String> {
        let p = path.display();
        let Ok(bytes) = std::fs::read(path) else {
            return Err(if path.exists() {
                format!("Cannot read {p}.")
            } else {
                format!("{p} does not exist.")
            });
        };
        let mut rows = parse(&String::from_utf8_lossy(&bytes));
        let expected = &HEADER[..REQUIRED_COLUMNS];
        if rows
            .first()
            .is_none_or(|h| h.len() < REQUIRED_COLUMNS || h[..REQUIRED_COLUMNS] != *expected)
        {
            return Err(format!(
                "{p} is not a Tag Programmer record: its first line should be the header \"{}\".",
                HEADER.join(",")
            ));
        }
        rows.remove(0);
        self.rows = rows;
        self.path = Some(path.to_path_buf());
        Ok(())
    }

    /// Creates a new record holding only the header, and opens it.
    ///
    /// Errors: the file already exists, or it cannot be written.
    pub fn create(&mut self, path: &Path) -> Result<(), String> {
        let p = path.display();
        if path.exists() {
            return Err(format!("{p} already exists."));
        }
        std::fs::write(path, format!("{}\n", HEADER.join(",")))
            .map_err(|e| format!("Cannot create {p}: {}.", reason(&e)))?;
        self.path = Some(path.to_path_buf());
        self.rows.clear();
        Ok(())
    }

    /// Forgets the open file.
    pub fn close(&mut self) {
        self.path = None;
        self.rows.clear();
    }

    /// The open file, if any.
    pub fn path(&self) -> Option<&Path> {
        self.path.as_deref()
    }

    /// A file is open.
    pub fn is_open(&self) -> bool {
        self.path.is_some()
    }

    /// Rows in the file, header excluded.
    pub fn rows(&self) -> &[Vec<String>] {
        &self.rows
    }

    /// Number of rows, header excluded.
    pub fn count(&self) -> usize {
        self.rows.len()
    }

    /// Checks the open file can be appended to, as before an attempt: a
    /// spreadsheet holding it locked must be found before a tag is erased,
    /// not after.
    pub fn writable(&self) -> Result<(), String> {
        let Some(path) = &self.path else {
            return Err("No record file is open.".into());
        };
        std::fs::OpenOptions::new()
            .append(true)
            .open(path)
            .map(|_| ())
            .map_err(|e| {
                format!(
                    "Cannot write {} ({}). Close it in any other program.",
                    path.display(),
                    reason(&e)
                )
            })
    }

    /// Appends `record` as one line and flushes it.
    pub fn append(&mut self, record: &ProgramRecord) -> Result<(), String> {
        let shown = self
            .path
            .as_ref()
            .map(|p| p.display().to_string())
            .unwrap_or_default();
        let Some(path) = &self.path else {
            return Err(format!("Cannot write {shown} (No record file is open)."));
        };
        let fields = record.fields();
        let line = fields
            .iter()
            .map(|f| quote(f))
            .collect::<Vec<_>>()
            .join(",")
            + "\n";
        let write = || -> std::io::Result<()> {
            let mut file = std::fs::OpenOptions::new().append(true).open(path)?;
            file.write_all(line.as_bytes())?;
            file.flush()
        };
        write().map_err(|e| format!("Cannot write {shown} ({}).", reason(&e)))?;
        self.rows.push(fields);
        Ok(())
    }

    /// Distinct non-empty labels recorded for `uuid`, oldest first.
    pub fn labels_for_uuid(&self, uuid: &str) -> Vec<String> {
        let mut out = Vec::new();
        for row in self
            .rows
            .iter()
            .filter(|r| r.get(UUID_COLUMN).is_some_and(|u| u == uuid))
        {
            add_distinct(&mut out, &row[LABEL_COLUMN]);
        }
        out
    }

    /// Distinct non-empty UUIDs recorded for `label`, oldest first.
    pub fn uuids_for_label(&self, label: &str) -> Vec<String> {
        let mut out = Vec::new();
        for row in self
            .rows
            .iter()
            .filter(|r| r.len() > UUID_COLUMN && r[LABEL_COLUMN] == label)
        {
            add_distinct(&mut out, &row[UUID_COLUMN]);
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::TimeZone;

    fn record(label: &str, uuid: &str) -> ProgramRecord {
        ProgramRecord {
            label: label.into(),
            tag_type: "BitTag".into(),
            uuid: uuid.into(),
            firmware_version: "fw-v0.6".into(),
            date: Utc.with_ymd_and_hms(2026, 10, 26, 13, 5, 9).unwrap(),
            result: "PROGRAM_FAILED: (no \"answer\", retried)".into(),
            voltage: 3.2349,
            ..Default::default()
        }
    }

    #[test]
    fn quoting_and_parsing() {
        assert_eq!(quote("plain"), "plain");
        assert_eq!(quote("a,b"), "\"a,b\"");
        assert_eq!(quote("say \"hi\""), "\"say \"\"hi\"\"\"");
        assert_eq!(quote("two\nlines"), "\"two\nlines\"");
        let rows = parse("a,\"b,c\",\"d\"\"e\"\r\n\r\n,\n\"x\ny\",z");
        assert_eq!(
            rows,
            [vec!["a", "b,c", "d\"e"], vec!["", ""], vec!["x\ny", "z"]]
        );
        let fields = record("L1", "U1").fields();
        assert_eq!(fields[5], "2026-10-26T13:05:09Z");
        assert_eq!(fields[11], "3.23");
        assert_eq!(ProgramRecord::default().fields()[11], "");
        let line = fields
            .iter()
            .map(|f| quote(f))
            .collect::<Vec<_>>()
            .join(",");
        assert_eq!(parse(&line), [fields]);
    }

    #[test]
    fn create_append_reopen() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("programmed-tags.csv");
        let mut log = RecordLog::new();
        assert_eq!(log.writable().unwrap_err(), "No record file is open.");
        log.create(&path).unwrap();
        assert_eq!(
            log.create(&path).unwrap_err(),
            format!("{} already exists.", path.display())
        );
        log.writable().unwrap();
        log.append(&record("BitTag-017", "U1")).unwrap();
        log.append(&record("BitTag-018", "U1")).unwrap();
        log.append(&record("BitTag-017", "U2")).unwrap();
        log.append(&record("BitTag-017", "U1")).unwrap();

        let mut again = RecordLog::new();
        again.open(&path).unwrap();
        assert_eq!(again.count(), 4);
        assert_eq!(again.rows(), log.rows());
        assert_eq!(again.labels_for_uuid("U1"), ["BitTag-017", "BitTag-018"]);
        assert_eq!(again.uuids_for_label("BitTag-017"), ["U1", "U2"]);
        assert!(again.labels_for_uuid("U3").is_empty());
    }

    #[test]
    fn open_errors() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("x.csv");
        let mut log = RecordLog::new();
        assert_eq!(
            log.open(&path).unwrap_err(),
            format!("{} does not exist.", path.display())
        );
        std::fs::write(&path, "label,tag_type,uuid\n").unwrap();
        assert!(log.open(&path).unwrap_err().ends_with(
            "is not a Tag Programmer record: its first line should be the header \
             \"label,tag_type,uuid,firmware_version,git_hash,date,test_status,result,\
             image_sha256,image,erase,voltage_v,probe_serial,host_version\"."
        ));
        // The required seven columns are enough; later columns may differ.
        std::fs::write(
            &path,
            "label,tag_type,uuid,firmware_version,git_hash,date,test_status,other\r\nA,B,C\r\n",
        )
        .unwrap();
        log.open(&path).unwrap();
        assert_eq!(log.labels_for_uuid("C"), ["A"]);
    }
}
