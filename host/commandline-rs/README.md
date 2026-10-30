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
| `tag-dwnld` | `dwnld.cc` | Download a stopped tag's log to SQLite, through the C++ writer (below) |

### tag-dwnld and the C++ writer

`tag-dwnld` reads the tag with tagcore-rs but writes the database with the
C++ `SqliteTagLogWriter`, through
[`taglogwriter`](../libraries/taglogwriter-rs/README.md) and the `tagcore_c`
library, so its file is the one the C++ `tag-dwnld` writes. It is built only
with the `sqlitelog` feature, and needs that library built by CMake first:

```sh
cmake -DBUILD_TAGCORE_C=ON <build-dir>
cmake --build <build-dir> --target tagcore_c_static    # tagcore_c off macOS
TAGCORE_C_LIB_DIR=<build-dir>/Release/lib \
    cargo build --release --features sqlitelog --bin tag-dwnld
```

On macOS it links `libtagcore_c.a`, and the executable needs nothing at run
time. On Linux and Windows (and with `TAGCORE_C_LINK=dylib`) it links the
shared library, one copy of which serves every tool in the install
directory, and finds it through an rpath to that directory. Messages and exit
status follow the C++; the format check that names an unsupported tag type
reports the writer's own message ("Could not open output: SQLite log output
does not support tag type ...").

## Programming without STM32CubeProgrammer

`tag-flash` has no C++ counterpart. It programs, erases and reads a tag's
internal flash through a base, on [`tagprobe`](../libraries/tagprobe-rs/README.md),
the probe-rs library Tag Programmer uses, and is meant to replace
STM32_Programmer_CLI in the CMake `-download` and `-erase` targets and the
bench scripts. The plan, and the bench checks still to run, are in
[the proposal](design/proposals/programming-without-cubeprogrammer.md).

```sh
tag-flash list                                  # bases: serial, bus:device, identity
tag-flash -b <serial> info                      # device, flash size, voltage
tag-flash -b <serial> program BitTag.elf        # image pages only, as -download
tag-flash -b <serial> program BitTag.elf --erase plan   # Tag Programmer's erase plan
tag-flash -b <serial> erase                     # mass erase
tag-flash -b <serial> read 0x2003E000 8192 -o scratch.bin
```

Unlike the other tools, `-b` takes the base's USB serial, not `bus:device`.
Every command but `list` resets the tag.

`base-flash` programs a **base** through its MCU's ROM bootloader over USB
DFU, replacing the CMake `-dfu` targets. Plug the base in with its button
held, then:

```sh
base-flash list                                 # devices in DFU mode
base-flash program bittag-base-v7.elf           # write, leave DFU, confirm
base-flash program --wait 120 bittag-base-v7.elf  # start first, then plug in
```

It succeeds only when the base comes back as an ST-LINK reporting the
identity string the image carries. On Linux it needs the `0483:df11` line of
`host/linux/70-ultralight-tags.rules`.

## Not ported yet

These need a library that the Rust crate does not have yet:

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
