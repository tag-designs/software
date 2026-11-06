//! `tag-rebuild`: rebuild a SQLite download from a `tag-capture` directory,
//! without the tag.
//!
//! Ported from the deleted C++ `host/commandline/tag-rebuild.cc`. The rebuild itself stays C++:
//! the family decoders in `tagcore/recovery/capturesource` turn the capture's
//! internal flash, backup registers and external flash back into the messages
//! the firmware would have served, and the same `SqliteTagLogWriter` as
//! `tag-dwnld` writes them, reached through `taglogwriter`'s
//! [`rebuild_from_capture`] (the `rbd_*` interface of `tagcore_c`). So the file
//! is the one the C++ tool wrote, from a capture taken by either `tag-capture`.
//!
//! ```text
//! tag-rebuild captures/capture-20261106-162304 -o tag.db3
//! ```
//!
//! The result is "as captured": the tag as found, before any reset recovery a
//! live attach would run. Its `info` table records `source` = `capture`, the
//! capture directory and the capture time. Families whose decoder is not
//! implemented are refused.
//!
//! # Exit status
//!
//! 0 written, 1 not. Option errors exit 2 (clap).
//!
//! See `docs/decisions/0018-offline-rebuild-capture-backed-source.md`.

use std::path::PathBuf;
use std::process::ExitCode;

use clap::Parser;
use taglogwriter::rebuild_from_capture;

/// Rebuild a SQLite download from a tag-capture directory
#[derive(Parser)]
#[command(name = "tag-rebuild")]
struct Cli {
    /// tag-capture directory
    capture: PathBuf,

    /// SQLite file to write (replaced)
    #[arg(short, long)]
    output: PathBuf,

    /// Log the writer's progress at DEBUG; otherwise only errors
    #[arg(short, long)]
    debug: bool,
}

/// Parses the options, rebuilds, and reports as the C++ tool does.
fn main() -> ExitCode {
    tag_tools::restore_sigpipe();
    let cli = Cli::parse();
    taglogwriter::set_debug(cli.debug);
    match rebuild_from_capture(&cli.capture, &cli.output) {
        Ok(records) => {
            println!("{records} records written to {}", cli.output.display());
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("tag-rebuild: {e}");
            ExitCode::FAILURE
        }
    }
}
