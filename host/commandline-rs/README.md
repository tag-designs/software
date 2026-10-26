---
type: readme
status: current
summary: Rust ports of the tag command-line tools built on tagcore-rs - which tools exist, which are not ported yet and why, how they differ from the C++, and how they were checked.
---

# Tag command-line tools (Rust)

Rust ports of the tools in [`../commandline`](../commandline/README.md),
built on the Rust tag interface in
[`../libraries/tagcore-rs`](../libraries/tagcore-rs/README.md). Each tool
keeps its C++ name, options, messages and exit status, so scripts can use
either. The crate is not wired into the CMake build.

```sh
cargo build --release            # binaries in target/release/
cargo run --bin tag-info -- --json
```

Every tool that talks to a tag takes the shared options from
[`src/lib.rs`](src/lib.rs), the port of `options.cc`: `-d`/`--debug`, and
`-b`/`--base bus:device`, which is required when more than one base is
connected. **Attaching resets the tag**, as with the C++ tools.

## Ported

| Tool | C++ source | What it does |
| --- | --- | --- |
| `tag-info` | `tag-info.cc` | Information, configuration, status and state log; `--json` for recording |
| `tag-start` | `tag-start.cc` | Program a configuration (`--config`, `--merge`) and start an IDLE tag |
| `tag-stop` | `tag-stop.cc` | Stop a running tag and wait for a stopped state |
| `tag-reset` | `tag-reset.cc` | Stop if needed, erase, and wait for IDLE |
| `tag-test` | `test.cc` | Run a firmware self-test and report the result |
| `tag-test-example` | `test-example.cc` | Set the RTC and report the clock error |
| `tag-cal` | `tag-calibrate.cc` | Stream raw calibration data from a tag in calibration mode |
| `tag-cal-write` | `tag-cal-write.cc` | Write identity calibration constants so a reflashed compass tag will start |
| `tag-peek` | `tag-peek.cc` | Read words of tag memory between monitor RPCs |
| `tag-side-channel-check` | `tag-side-channel-check.cc` | Hardware check of memory access, keep-alive and threading |
| `tag-attach-cycle` | `tag-attach-cycle.cc` | Attach and detach repeatedly, for attach-path testing |
| `tag-monitor-test` | `tag-monitor-test.cc` | Drive the L4 monitor handshake directly through `LinkAdapt` |
| `qtmonitor-fixture-capture` | `qtmonitor-fixture-capture.cc` | Capture a tag's info, config and status as a qtmonitor test fixture |

## Not ported yet

These need a library that the Rust crate does not have yet:

- `tag-dwnld`: the data-log download and log writers.
- `tag-capture`, `tag-xflash`, `tag-sramcall` and `tag-rebuild`: the SWD
  capture and recovery library in `tagcore/recovery/`.
- `tag-log-import` and `tag-txt-import`: `sqlitelog` and `txtlogs`.
- `dataprocessing`: the sensoranalysis core and SQLite.

## Differences from the C++

- Option errors exit with status 2 (clap) rather than 1 (cxxopts).
- Text-format output omits the randomised `goo.gle/debugproto` line that
  recent C++ protobuf releases add to `DebugString()`.
- Log lines keep the `log.c` layout (`HH:MM:SS LEVEL file:line: message`),
  with Rust source paths.

Porting turned up a set of C++ faults, now fixed in both versions together
(`b2c907ef` on `main` for the C++):

- Failures that exited 0 now exit 1: `tag-start` when the tag refuses the
  start, `tag-cal` on a tag that is not IDLE, `tag-test` for an unknown test,
  a failed status read or a tag not IDLE, and `tag-test-example` when attach
  fails.
- `tag-reset` also stops a CONFIGURED tag, polls for the stop
  (`--stop-timeout`), and exits 1 unless the tag ends IDLE, so `--set-rtc`
  is sent only to an IDLE tag.
- `tag-monitor-test` follows `TagMonitor`'s L4 attach and call sequence,
  ends its session with MONITORSTOP, and attaches again.
- `tag-peek` rejects an address it cannot parse; `tag-side-channel-check`
  rejects `--idle` with `--interval 0`; `qtmonitor-fixture-capture` prints
  bus:address as numbers and logs with `--debug`; `tag-cal` waits 100 ms
  between calibration-log reads.

`qtmonitor-fixture-capture --capture-default-config` still does nothing: it
names the default, as its help says.

## How they were checked

On 2026-10-26, against a BitTag V6 (STM32L432, firmware `56e5e6a0`) on a tag
programmer base, each Rust tool was run immediately after its C++ build from
the same source tree, and the two outputs were compared:

- **Same output apart from live readings** (clock, voltage, temperature,
  timings and loop counts): `tag-info` (text and `--json`), `tag-peek`,
  `tag-test` (self-tests passed), `tag-test-example`, `tag-cal` (refused, as
  BitTag has no calibration mode), `tag-side-channel-check`,
  `tag-attach-cycle`, `qtmonitor-fixture-capture` (the fixture files too), and
  `tag-monitor-test`.
- **The state tools:** `tag-start --set-rtc`, `tag-stop` and `tag-reset` each
  took the tag through CONFIGURED, FINISHED and back to IDLE, with each step
  printing the same output as the C++ tool. After the fixes, `tag-cal`
  refused the started tag with exit 1, and `tag-reset` stopped it, waited for
  FINISHED and erased it to IDLE, in both versions; the argument checks and
  `tag-monitor-test` printed the same in both.

The same day, on an IMUTagNandBmp581 (STM32U375, the shared-block
transport, firmware `8f1350da`), each tool again matched its C++ build:
`tag-info`, `tag-peek`, `tag-side-channel-check` (including the
monitor_shared write guard and keep-alive across a 12 s idle),
`tag-attach-cycle`, `qtmonitor-fixture-capture`, the start, stop and reset
cycle, and `tag-cal`, which streamed samples and stopped on Ctrl-C.
`tag-monitor-test` refuses an STM32U3, as the C++ now does.

Not checked on hardware: `--config`/`--merge` (the merge itself is covered by
tagcore-rs unit tests), the opt-in write, start and unplug checks of
`tag-side-channel-check`, and `tag-test` on the IMUTag, whose
external-flash test can write the NAND bad-block map.
