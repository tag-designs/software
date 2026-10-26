---
type: readme
status: current
summary: Rust port of tagcore's tag interface (Tag, TagMonitor, LinkAdapt) - layout, how it maps to the C++, where it deliberately differs, and how to build and test it.
---

# tagcore (Rust)

A Rust crate, `tagcore`, that talks to a tag's firmware monitor through an
ST-LINK. It is a port of the C++ tag interface in
[`../tagcore`](../tagcore/README.md): the `Tag` class (`tagclass.*`) and the
two layers under it, so it is usable on its own. It is not wired into the
CMake build.

| Rust | C++ | Role |
| --- | --- | --- |
| `tag.rs` — `Tag`, `TagError` | `tagclass.*` | Thread-safe facade: one method per request, last RPC outcome |
| `tagmonitor.rs` — `TagMonitor` | `tagmonitor.*` | L4 DebugMonitor and U3 shared-block transports, attach/detach, RPC framing, timeout diagnostics |
| `rescue.rs` | `legacy_bittag_rescue.cc` | Old BitTag V6 field-rescue helpers |
| `linkadapt.rs` — `LinkAdapt`, `UsbDev` | `linkadapt.*` | ST-LINK discovery and USB commands (rusb/libusb) |
| `monitor_abi.rs` | `include/monitor.h` | Monitor ABI constants and the U3 shared block |
| `proto.rs` | `tag.pb.h` | prost types generated from `proto/` at build time |

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
decoding, error reporting and protobuf framing. The hardware test attaches
(resetting the tag), sends only read-only requests (and one calibration write the firmware must refuse), checks the chip UID read
over SWD against the UUID the firmware reports, and detaches. Quit qtmonitor,
qtprogram and anything else that polls the base first.

Checked on 2026-10-26 against a BitTag V6 (STM32L432, firmware `56e5e6a0`) on
a tag programmer base: the report matched the C++ `tag-info` field for field.
The STM32U3 transport has not yet been run on hardware.
