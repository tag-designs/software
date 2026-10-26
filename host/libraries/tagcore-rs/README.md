---
type: readme
status: current
summary: Rust port of tagcore's tag interface (Tag, TagMonitor, LinkAdapt) and ConfigShape - layout, how it maps to the C++, where it deliberately differs, and how to build and test it.
---

# tagcore (Rust)

A Rust crate, `tagcore`, that talks to a tag's firmware monitor through an
ST-LINK. It is a port of the C++ tag interface in
[`../tagcore`](../tagcore/README.md): the `Tag` class (`tagclass.*`) and the
two layers under it, so it is usable on its own, plus `ConfigShape` and the
protobuf text/JSON output the command-line tools need. The Rust tools in
[`../../commandline-rs`](../../commandline-rs/README.md) are built on it. It
is not wired into the CMake build.

| Rust | C++ | Role |
| --- | --- | --- |
| `tag.rs` — `Tag`, `TagError` | `tagclass.*` | Thread-safe facade: one method per request, last RPC outcome |
| `monitor/` — `TagMonitor` | `tagmonitor.*` | The monitor protocol, one file per concern (below) |
| `link/` — `LinkAdapt`, `UsbDev` | `linkadapt.*` | ST-LINK discovery and USB commands (rusb/libusb), one file per concern (below) |
| `proto.rs` | `tag.pb.h`, `json_util` | prost types generated from `proto/` at build time, with reflection; JSON in and out as the C++ prints and parses it |
| `textformat.rs` | `TextFormat`, `DebugString()` | Protobuf text format laid out as the C++ prints it |
| `configshape.rs` — `ConfigShape` | `configshape.*` | Which Config fields each tag type supports, from the embedded `default-config.json` files, and the `--merge` overlay |

`TagMonitor` and `LinkAdapt` are each one type whose methods are spread over
a folder, one file per concern, so each question has one place to look:

| File | What it holds |
| --- | --- |
| `monitor/mod.rs` | The struct and its public API: attach, detach, memory access, keep-alive; target identification; the call dispatcher |
| `monitor/l4.rs` | STM32L4 transport: DebugMonitor/DCRDR call, attach sequence, timeout report |
| `monitor/u3.rs` | STM32U3 transport: shared-block call, attach sequence, timeout report |
| `monitor/rpc.rs` | Protobuf framing over either transport |
| `monitor/cortex_m.rs` | Arm debug and system-control registers; core halt and register reads for timeout reports |
| `monitor/abi.rs` | The host/firmware monitor ABI, mirrored from `include/monitor.h` |
| `monitor/rescue.rs` | Legacy BitTag V6 field-rescue helpers (`legacy_bittag_rescue.cc`) |
| `link/mod.rs` | The struct, its statistics, attach and detach |
| `link/discovery.rs` | Finding ST-LINKs (`UsbDev`), reading serials, opening the selected probe |
| `link/stlink.rs` | The ST-LINK USB protocol: command codes and blocks, bulk transfers, mode changes |
| `link/target.rs` | Raw target access: memory, debug registers, nRST, voltage |

The attach sequences, timeouts, retry and recovery paths, and log messages
follow the C++ code. The protocol is described in
[`docs/shared/monitor-interface.md`](../../../docs/shared/monitor-interface.md).

## Using it

```rust
use tagcore::{Tag, UsbDev};

let tag = Tag::new();
if tag.attach(&UsbDev::default()) {          // resets the tag
    let info = tag.tag_info()?;
    let status = tag.status()?;
    tag.detach();
}
```

**Attaching resets the tag**, as with every tool in this tree; never attach to
a tag mid-run just to see what it is doing. See
[`docs/bench/verifying-firmware.md`](../../../docs/bench/verifying-firmware.md).

`examples/tag_info.rs` is a read-only report: it lists probes, then prints the
voltage, firmware, info, status, config and state log.

## Differences from the C++

The wire behaviour is the same. The API is Rust-shaped:

- Requests sent to the firmware return `Result<_, TagError>`, and the outcome
  is also kept for `last_error()` and `debug_message()`. Link-level calls
  (`voltage`, `git_sha`, rescue helpers) return `Option`/`bool`, with the
  reason in the log, as the C++ only logs it.
- `Tag` takes `&self` and holds a mutex; `TagMonitor` takes `&mut self`, so
  the borrow checker serialises the link instead of a recursive mutex.
- `Tag::target_family()` is new: it exposes the family detected at attach.
- `LinkAdapt`'s debug-register and memory calls are public; the C++ makes
  them protected, and `tag-monitor-test` subclasses it to reach them.
- Text format omits the randomised `goo.gle/debugproto` marker that recent
  C++ protobuf releases insert into `DebugString()`.
- `git_sha()` stops at the first NUL; the C++ returns all 48 bytes.
- `read_calibration` takes an `i32`, as the protobuf field does, so "last
  written" (`< 0`) can be asked for directly.

The port also prompted three fixes to the C++ (`c9967bc1`), which both
versions now share: `WriteCalibration` requires the firmware's OK rather than
just a completed RPC; an L4 call whose result reads all fail now fails rather
than succeeding with an unset result; and a reply length larger than the
monitor buffer is rejected rather than truncated to 16 bits.

Logging uses the [`log`](https://docs.rs/log) facade; install any logger
(the example uses `env_logger`, `RUST_LOG=debug` for the attach detail).

## Building and testing

Needs a Rust toolchain, `protoc` on `PATH` (or `PROTOC` set) and libusb-1.0.

```sh
cargo build
cargo test                                             # unit tests, no hardware
cargo test --test hardware -- --ignored --test-threads=1   # needs a tag on a base
RUST_LOG=info cargo run --example tag_info [-- <ST-LINK serial>]
```

The unit tests cover the parts that need no hardware: serial decoding,
command and memory-range rules, the shared-block layout, legacy record
decoding, error reporting, protobuf framing, text format and JSON spelling,
and ConfigShape against the real default configs. The hardware test attaches
(resetting the tag), sends only read-only requests and one calibration write
the firmware must refuse, checks the chip UID read
over SWD against the UUID the firmware reports, and detaches. Quit qtmonitor,
qtprogram and anything else that polls the base first.

Checked on 2026-10-26 against a BitTag V6 (STM32L432, firmware `56e5e6a0`) on
a tag programmer base: the report matched the C++ `tag-info` field for field.
The STM32U3 transport has not yet been run on hardware.
