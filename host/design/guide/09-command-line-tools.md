---
type: design
status: current
summary: Chapter 9 of the Host Software Guide - the Rust command-line tools in commandline-rs (the shape every tool follows, the shared crate behind them, what each tool does, a walk through tag-dwnld, how they are held to the C++ tools, and how to add one) and the C++ tools that have no port.
---

# 9. The command-line tools

The command-line tools are the smallest complete programs in the tree, and
the best place to see the tag interface used end to end. This chapter
assumes chapters 3, 5 and 6. The tools live in `host/commandline-rs` (crate
`tag-tools`, one binary per file in `src/bin/`) and are ports of the C++
tools in `host/commandline`, keeping each tool's name, options, messages and
exit status so scripts can use either.

## The shape of a tool

Every tool that talks to a tag has the same skeleton, and `src/lib.rs` is
what they share (a port of the C++ `options.cc` and the parts of `log.c` the
tools rely on):

```rust
use clap::Parser;
use tag_tools::{BaseArgs, init_logging, select_base, exit_on_interrupt};
use tagcore::Tag;

/// what the tool does, as `--help` prints it
#[derive(Parser)]
#[command(name = "tag-info")]
struct Cli {
    #[command(flatten)]
    base: BaseArgs,          // -d/--debug and -b/--base BUS:DEVICE, shared by every tool
    #[arg(long)]
    json: bool,              // the tool's own options
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    init_logging(&cli.base);                   // stderr, log.c's "HH:MM:SS LEVEL file:line: message"
    let tag = Tag::new();
    let dev = select_base::<Cli>(&tag, &cli.base);   // one base: use it; several: --base is required
    if !tag.attach(&dev) { return fail("Attach failed"); }   // RESETS THE TAG
    exit_on_interrupt();                       // Ctrl-C exits 1, as the C++ SIGINT handler did
    // ... settle (poll status until it is not STATE_UNSPECIFIED), do the work, print, exit 0 or 1
}
```

The conventions that every tool keeps, because scripts and the bench
procedures depend on them:

- **Check the options before attaching.** Attaching resets the tag, so a
  mistyped option must not cost a running tag its run. `tag-dwnld` checks
  `--format` first for exactly this reason.
- **Settle after attach.** The first status after a reset can legitimately
  be `STATE_UNSPECIFIED` while the tag boots; tools poll once a second, up
  to `--settle-timeout` (10 s), for a definite state.
- **Poll for the state you asked for.** `Stop`, `Start` and `Erase` are
  accepted, not completed; the tools poll `Status` with a timeout and report
  the state reached. `tag-start` exits 0 for RUNNING or CONFIGURED, or
  "not confirmed" if the tag stopped answering after accepting (a tag that
  sleeps at once drops the debug link); `tag-reset` exits 1 unless the tag
  ends IDLE.
- **Exit status.** 0 for success, 1 for any failure, 2 for an option error
  (clap's convention; the C++ cxxopts exits 1 there, the one standing
  difference).
- **Output.** Protobuf messages print in text format laid out as the C++
  `DebugString()` prints them (`tagcore::proto::debug_string`), or as JSON
  with `--json` where a tool offers it.

## The tools

| Tool | Does | Changes the tag? |
| --- | --- | --- |
| `tag-info` | Information, configuration, status and state log; `--json` for recording | the reset only |
| `tag-start` | Program a configuration (`--config FILE`, `--merge` to overlay only the fields the tag supports) and start an IDLE tag; `--set-rtc` first; `--start-now` | writes the configuration, starts a run |
| `tag-stop` | Stop a running tag and wait for a stopped state | ends the run |
| `tag-reset` | Stop if needed, erase, wait for IDLE; `--set-rtc` once IDLE | erases the log |
| `tag-dwnld` | Download a stopped tag's log to SQLite (below) | no (`--stop` ends a run first; `--rescue-exception` rewrites one backup word) |
| `tag-test` | Run a firmware self test and report the result; exits 1 on a failed test | the test's side effects (the IMUTag's external-flash test can write the NAND bad-block map) |
| `tag-test-example` | Set the RTC, wait, report the clock error: a worked example of the API | sets the clock |
| `tag-cal` | Stream raw calibration samples from a tag in CALIBRATE | enters and leaves CALIBRATE |
| `tag-cal-write` | Write identity calibration constants, so a reflashed compass tag will start | appends a calibration record |
| `tag-peek` | Read words of tag memory between RPCs | no |
| `tag-side-channel-check` | Bench check of live memory access, keep-alive and threading | opt-in writes |
| `tag-attach-cycle` | Attach and detach repeatedly, for attach-path testing | resets each time |
| `tag-monitor-test` | Drive the L4 monitor handshake directly through `LinkAdapt`; refuses an STM32U3 | resets |
| `qtmonitor-fixture-capture` | Capture a tag's info, config and status as a qtmonitor test fixture | no |
| `tag-flash`, `base-flash` | Program, erase and read a tag; program a base over DFU (chapter 7) | rewrites flash |

Only `tag-flash` and `base-flash` are installed; the ports are built for
development and checking (the C++ tools of the same names still ship). The
user manual's `cli/` pages describe the shipped tools. In a CMake build tree
the shipped C++ tools land in `<build>/<Config>/bin` and the developer and
bench tools in `bin/internal`, so the listing of `bin/` is the list of what a
release carries.

### Not ported

`tag-capture`, `tag-xflash`, `tag-sramcall` and `tag-rebuild` need the C++
SWD capture and recovery library; `tag-log-import` and `tag-txt-import` need
the writer's import paths; `dataprocessing` needs `sensoranalysis_core` and
SQLite. All stay C++ in `host/commandline`, whose README describes them.

## tag-dwnld, line by line

`src/bin/tag-dwnld.rs` is the one tool that crosses the C++ bridge, and
it is the clearest expression of chapter 5's download loop. Its structure:

1. **Options**, then `check_format()`: SQLite is the only format; the retired
   text format is refused with a message naming `tag-txt-import`. Done
   before the attach.
2. **Attach and settle**: `Tag::attach`, then `settle()` polls for a definite
   state.
3. **Rescue and stop**: with `--rescue-exception`, an old BitTag's EXCEPTION
   is rewritten to ABORTED in its backup register
   (`Tag::force_backup_state`); with `--stop`, a RUNNING tag is stopped and
   polled every 2 s until FINISHED or ABORTED.
4. **Refuse** unless FINISHED, ABORTED or EXCEPTION.
5. **Create the writer**: `LogWriter::create(path, &config, replace)`, which
   the C++ side refuses for a tag type it has no schema for, before touching
   the file.
6. **Header**: `read_header()` makes the same requests in the same order as
   the C++ `readTagLogHeader()`: `Config`, `TagInfo`, calibration constants
   from index 0 until one is refused, the state log in chunks until one is
   refused or empty. Then `write_header`.
7. **Pages**: `begin_log()`; then the loop of chapter 5, with the progress
   bar and the `NODATA`-below-count skip; `end_log()`.
8. **Report** the records and requests, and with `--profile` the monitor and
   link statistics from `tagcore`'s instrumentation.

It is built only with the `sqlitelog` feature (`cargo build --features
sqlitelog --bin tag-dwnld`), because it links `libtagcore_c`; the other
tools build with cargo alone.

## How the ports are held to the C++

Each tool was run immediately after its C++ build from the same tree, on a
BitTag (STM32L432) and an IMUTag (STM32U375), and the outputs compared: the
same apart from live readings (clock, voltage, temperature, timings). The
state tools took the tag through CONFIGURED, FINISHED and back to IDLE in
both languages with the same output at each step. Porting also found a set
of C++ faults (exit codes, `tag-reset` on a CONFIGURED tag, an argument
check), fixed in both together; the README lists them, and the handful of
things not checked on hardware.

The unit tests in `src/lib.rs` cover what needs no tag: `bus:device` parsing
with `sscanf`'s leniency, state naming. Everything else is checked by the
comparison above and by `tagcore`'s own tests.

## Adding a tool

1. Copy the skeleton above into `src/bin/<tool>.rs`; cargo finds binaries
   in `src/bin/` without a manifest entry (`tag-dwnld` has one because of
   its `required-features`).
2. Start the file with a `//!` block saying what the tool does, its options,
   its exit status, its **hardware side effects** (every tool has this
   section), and, for a port, the C++ file it follows and how it differs.
3. Use the shared options; add the tool's own with clap's derive.
4. Check options before attaching; settle; poll for the state you asked for.
5. Document every item (the crate warns otherwise), and add a row to the
   README's table and, if it ships, a page under `host/docs/src/cli/`.

## Where to read next

- [`commandline-rs/README.md`](../../commandline-rs/README.md): the tables,
  the differences from the C++, how the ports were checked.
- `commandline-rs/src/lib.rs`: the shared code, which is short and fully
  commented.
- [`commandline/README.md`](../../commandline/README.md): the C++ tools,
  including the recovery tools and `dataprocessing`.
- The user manual's `cli/` pages for what a user sees.
