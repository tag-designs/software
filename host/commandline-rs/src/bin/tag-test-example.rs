//! `tag-test-example`: set the RTC and report the clock error.
//!
//! Port of `host/commandline/test-example.cc`. Syncs the tag clock, waits
//! two seconds, reads the tag's time and prints its offset from the host
//! clock in seconds. As in the C++, a failed attach exits 0 silently.

use std::process::ExitCode;
use std::thread::sleep;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base};
use tagcore::Tag;
use tagcore::textformat::format_g;

/// sets the RTC
#[derive(Parser)]
#[command(name = "tag-test-example")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);

    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);
    if tag.attach(&dev) {
        if tag.set_rtc().is_err() {
            eprintln!("SetRtc failed: {}", tag.debug_message());
            return ExitCode::FAILURE;
        }
        sleep(Duration::from_millis(2000));
        // millis defaults to 0, which would report a clock error of about
        // -1.8e9 seconds as though it were a measurement.
        let status = match tag.status() {
            Ok(status) => status,
            Err(_) => {
                eprintln!("GetStatus failed: {}", tag.debug_message());
                return ExitCode::FAILURE;
            }
        };
        let now_ms = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis() as i64;
        // std::ostream's default double format: %g, six significant digits.
        let error = (status.millis - now_ms) as f64 / 1000.0;
        println!("Clock Error: {}", format_g(error, 6, 6, |_| true));
    }
    ExitCode::SUCCESS
}
