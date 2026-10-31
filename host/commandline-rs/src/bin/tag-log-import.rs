//! `tag-log-import`: write a JSONL download as a SQLite log, without a tag.
//!
//! Port of `host/commandline/tag-log-import.cc`. The input is the download
//! format written by `host/python/tagclient/examples/tag_dwnld.py`, and the
//! database is written by the C++ writer (`SqliteTagLogWriter`) through the
//! `taglogwriter` crate, the same writer `tag-dwnld` uses, so the file is
//! the one the C++ tool writes from the same input: byte for byte, given
//! the same input path.
//!
//! ```text
//! tag-log-import download.jsonl -o tag.db3 [--info KEY=VALUE ...]
//! ```
//!
//! The first line is a header object. Its `config` and `info` members are
//! required; `states` (a list of State) and `calibration` (a list of
//! CalibrationConstants, oldest first) are optional, and any other member
//! (`tag_sha`, `status`) is ignored. Every later line is one Ack in
//! protobuf JSON, written with one `write_log` call each, in order. Blank
//! lines are skipped. Unknown fields in any message are an error, as a
//! misspelt field would otherwise be dropped silently.
//!
//! The tool is tag-agnostic: any tag type the writer supports is accepted.
//! It is how tagsim (`host/python/tagsim`) turns simulated pages into a
//! genuine log. The info table records `source = import` and the input
//! path, then any `--info` pairs.
//!
//! A developer tool: CMake builds it into `bin/internal`, and packages do
//! not install it. Built only with the `sqlitelog` feature, which needs
//! `libtagcore_c`; see `host/libraries/taglogwriter-rs/README.md`.
//!
//! Exits 0 when the database was written, 1 when it was not (the partial
//! file is removed, so it cannot pass for an import), 2 on an option error
//! (clap). It touches no hardware.
//!
//! # Differences from the C++
//!
//! - Option errors, including a missing input or `-o`, exit 2 (clap) rather
//!   than printing the help and exiting 1, as for every Rust tool.
//! - A malformed JSON line is reported in prost-reflect's words rather than
//!   protobuf's (`unrecognized field name 'x'` for `no such field: 'x'`).
//!
//! # Maintaining
//!
//! Keep the writer calls, their order and the messages in step with
//! `tag-log-import.cc` while both exist. To check a change, import the same
//! file with both tools and `cmp` the databases.

use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::ExitCode;

use clap::Parser;
use serde_json::Value;
use tagcore::proto::{self, Ack, TagType};
use taglogwriter::{LogHeader, LogWriter};

/// Write a tagclient JSONL download as a SQLite log
#[derive(Parser)]
#[command(name = "tag-log-import")]
struct Cli {
    /// JSONL download (tag_dwnld.py format)
    #[arg(value_name = "download.jsonl")]
    input: PathBuf,

    /// SQLite file to write (replaced)
    #[arg(short, long)]
    output: PathBuf,

    /// Extra info-table row KEY=VALUE (repeatable)
    #[arg(long, value_name = "KEY=VALUE")]
    info: Vec<String>,
}

/// Parses one header member as message `M`, re-serialising it so the
/// message is read on its own, as the C++ `ParseMember()` does.
///
/// # Errors
///
/// When the member is not valid JSON for `M`, with the member's name.
fn member<M: prost_reflect::ReflectMessage + Default>(
    value: &Value,
    what: &str,
) -> Result<M, String> {
    proto::from_json(&value.to_string()).map_err(|e| format!("header member {what}: {e}"))
}

/// Parses an optional list member of the header; an absent member is an
/// empty list.
///
/// # Errors
///
/// When the member is not a list, or an element does not parse.
fn list<M: prost_reflect::ReflectMessage + Default>(
    root: &Value,
    key: &str,
) -> Result<Vec<M>, String> {
    match root.get(key) {
        None => Ok(Vec::new()),
        Some(Value::Array(items)) => items.iter().map(|item| member(item, key)).collect(),
        Some(_) => Err(format!("header member {key} is not a list")),
    }
}

/// Parses the header line, as the C++ `ParseHeader()`.
///
/// # Errors
///
/// When the line is not a JSON object, `config` or `info` is missing, or a
/// member does not parse.
fn header(line: &str) -> Result<LogHeader, String> {
    let root: Value = serde_json::from_str(line).map_err(|e| format!("header line: {e}"))?;
    if !root.is_object() {
        return Err("header line: not a JSON object".into());
    }
    for required in ["config", "info"] {
        if root.get(required).is_none() {
            return Err(format!("header has no {required}"));
        }
    }
    Ok(LogHeader {
        config: member(&root["config"], "config")?,
        info: member(&root["info"], "info")?,
        calibration: list(&root, "calibration")?,
        states: list(&root, "states")?,
    })
}

/// Writes the import into `cli.output`, returning (records, pages). The
/// caller removes the file on failure.
///
/// The calls and their order are the C++ `WriteImport()`'s: create
/// (replacing), header, the `source` and `import_file` rows, the `--info`
/// rows, then one `write_log` per Ack inside one log transaction, which is
/// opened only when there is an Ack (as `tag-dwnld` writes only the header
/// when there is nothing to download).
///
/// # Errors
///
/// On a malformed `--info` pair or line, a writer error, or an Ack the
/// writer consumes nothing from: `tag_dwnld.py` writes only pages that
/// carried records, so that is a mismatch with the header's tag type.
fn import(
    lines: impl Iterator<Item = std::io::Result<String>>,
    header: &LogHeader,
    cli: &Cli,
) -> Result<(u64, u64), String> {
    let text = |e: taglogwriter::WriterError| e.to_string();
    let mut writer = LogWriter::create(&cli.output, &header.config, true).map_err(text)?;
    writer.write_header(header).map_err(text)?;
    writer.write_info("source", "import").map_err(text)?;
    writer
        .write_info("import_file", &cli.input.to_string_lossy())
        .map_err(text)?;
    for pair in &cli.info {
        match pair.split_once('=') {
            Some((key, value)) if !key.is_empty() => {
                writer.write_info(key, value).map_err(text)?;
            }
            _ => return Err(format!("--info expects KEY=VALUE, got {pair}")),
        }
    }

    let tag_type = TagType::try_from(header.config.tag_type).map_or_else(
        |_| header.config.tag_type.to_string(),
        |t| t.as_str_name().to_owned(),
    );
    let (mut begun, mut records, mut pages) = (false, 0u64, 0u64);
    // The header was line 1.
    for (line, number) in lines.zip(2u64..) {
        let line = line.map_err(|e| format!("line {number}: {e}"))?;
        if line.trim_matches([' ', '\t', '\r']).is_empty() {
            continue;
        }
        let ack: Ack = proto::from_json(&line).map_err(|e| format!("line {number}: {e}"))?;
        if !begun {
            writer.begin_log().map_err(text)?;
            begun = true;
        }
        let consumed = writer
            .write_log(&ack)
            .map_err(|e| format!("line {number}: {e}"))?;
        if consumed == 0 {
            return Err(format!(
                "line {number}: Ack carries no log payload for tag type {tag_type}"
            ));
        }
        records += consumed as u64;
        pages += 1;
    }
    if begun {
        writer.end_log().map_err(text)?;
    }
    Ok((records, pages))
}

/// Parses the options, reads the header and imports, reporting as the C++
/// tool does.
fn main() -> ExitCode {
    let cli = Cli::parse();
    let fail = |error: String| {
        eprintln!("tag-log-import: {error}");
        ExitCode::FAILURE
    };

    let Ok(file) = std::fs::File::open(&cli.input) else {
        return fail(format!("cannot read {}", cli.input.display()));
    };
    let mut lines = BufReader::new(file).lines();
    let Some(Ok(first)) = lines.next() else {
        return fail(format!("cannot read {}", cli.input.display()));
    };
    let header = match header(&first) {
        Ok(header) => header,
        Err(error) => return fail(error),
    };

    match import(lines, &header, &cli) {
        Ok((records, pages)) => {
            println!(
                "{records} records ({pages} pages) written to {}",
                cli.output.display()
            );
            ExitCode::SUCCESS
        }
        Err(error) => {
            let _ = std::fs::remove_file(&cli.output);
            fail(error)
        }
    }
}
