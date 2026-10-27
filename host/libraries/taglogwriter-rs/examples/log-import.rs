//! `log-import`: `tag-log-import` through [`taglogwriter`], to check the C
//! interface against the C++ tool.
//!
//! Reads the same JSONL download (a header object with `config`, `info`
//! and optional `calibration` and `states` members, then one Ack per line)
//! and makes the same calls in the same order as
//! `host/commandline/tag-log-import.cc`, including its `source` and
//! `import_file` info rows. The two databases should therefore be
//! identical; compare them with `sqlite3 X .dump`.
//!
//! Usage: `cargo run --example log-import -- download.jsonl out.db3`

use std::io::{BufRead, BufReader};
use std::process::ExitCode;

use serde_json::Value;
use tagcore::proto::{self, Ack};
use taglogwriter::{LogHeader, LogWriter};

/// Parses header member `key` as message `M`, re-serialising it so the
/// C++-compatible JSON reader sees one message at a time.
///
/// # Errors
///
/// When the member is not valid JSON for `M`.
fn member<M: prost_reflect::ReflectMessage + Default>(value: &Value) -> Result<M, String> {
    proto::from_json(&value.to_string())
}

/// Parses an optional list member of the header.
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
        Some(Value::Array(items)) => items.iter().map(member).collect(),
        Some(_) => Err(format!("header member {key} is not a list")),
    }
}

/// Parses the header line, as tag-log-import's `ParseHeader()`.
///
/// # Errors
///
/// When the line is not an object, `config` or `info` is missing, or a
/// member does not parse.
fn header(line: &str) -> Result<LogHeader, String> {
    let root: Value = serde_json::from_str(line).map_err(|e| format!("header line: {e}"))?;
    let get = |key: &str| root.get(key).ok_or(format!("header has no {key}"));
    Ok(LogHeader {
        config: member(get("config")?)?,
        info: member(get("info")?)?,
        calibration: list(&root, "calibration")?,
        states: list(&root, "states")?,
    })
}

/// Imports `input` into `output`, returning (records, acks).
///
/// # Errors
///
/// With the reason, as tag-log-import reports it.
fn import(input: &str, output: &str) -> Result<(usize, usize), String> {
    let file = std::fs::File::open(input).map_err(|e| format!("{input}: {e}"))?;
    let mut lines = BufReader::new(file).lines();
    let first = lines
        .next()
        .ok_or("empty input")?
        .map_err(|e| e.to_string())?;
    let header = header(&first)?;
    let mut writer =
        LogWriter::create(output.as_ref(), &header.config, true).map_err(|e| e.to_string())?;
    writer.write_header(&header).map_err(|e| e.to_string())?;
    writer
        .write_info("source", "import")
        .map_err(|e| e.to_string())?;
    writer
        .write_info("import_file", input)
        .map_err(|e| e.to_string())?;

    let (mut begun, mut records, mut acks) = (false, 0, 0);
    for (number, line) in lines.enumerate() {
        let line = line.map_err(|e| e.to_string())?;
        if line.trim().is_empty() {
            continue;
        }
        let ack: Ack = proto::from_json(&line).map_err(|e| format!("line {}: {e}", number + 2))?;
        if !begun {
            writer.begin_log().map_err(|e| e.to_string())?;
            begun = true;
        }
        let consumed = writer
            .write_log(&ack)
            .map_err(|e| format!("line {}: {e}", number + 2))?;
        if consumed == 0 {
            return Err(format!("line {}: Ack carries no log payload", number + 2));
        }
        records += consumed;
        acks += 1;
    }
    if begun {
        writer.end_log().map_err(|e| e.to_string())?;
    }
    Ok((records, acks))
}

/// Runs the import and reports as tag-log-import does.
fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let [input, output] = args.as_slice() else {
        eprintln!("usage: log-import download.jsonl out.db3");
        return ExitCode::from(2);
    };
    match import(input, output) {
        Ok((records, acks)) => {
            println!("Wrote {records} records from {acks} pages to {output}");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("{error}");
            let _ = std::fs::remove_file(output);
            ExitCode::FAILURE
        }
    }
}
