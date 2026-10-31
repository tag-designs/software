---
type: design
status: current
summary: Chapter 6 of the Host Software Guide - the Rust tagcore crate that every tool and application uses to talk to a tag, its three layers and the ownership between them, the concurrency model, how failures are reported and logged, the code generated at build time, how to use it, and how it is kept in step with the C++ it ports.
---

# 6. tagcore: the tag interface

`libraries/tagcore-rs` (crate `tagcore`) is the Rust tag interface: the one
crate through which every Rust tool and application reaches a tag's
firmware. It is a port of three C++ classes in `libraries/tagcore`
(`Tag` in `tagclass.*`, `TagMonitor` in `tagmonitor.*`, `LinkAdapt` in
`linkadapt.*`) plus `ConfigShape` and the protobuf text and JSON output the
tools print. This chapter assumes chapter 3, which explained what the
layers do on the wire; here the subject is the code.

## Layers and ownership

```text
Tag (tag.rs)                 &self, Mutex<Inner>        thread-safe facade; one method per request; last RPC outcome
 └─ TagMonitor (monitor/)    &mut self                  attach sequences, the call dispatcher, RPC framing, keep-alive
     └─ LinkAdapt (link/)    &mut self                  the rusb handle, ST-LINK commands, memory, registers, nRST, voltage
```

Each layer owns the one below it **by value**: a `Tag` contains a
`TagMonitor`, which contains a `LinkAdapt`, which contains the libusb
handle. There is no second path to the USB device, so two exchanges cannot
interleave on the link. The lower layers take `&mut self` on every method
that touches the probe; the compiler therefore guarantees one user at a time
without a lock. `Tag` is the only layer meant to be shared: it wraps the
monitor in a `std::sync::Mutex`, takes the lock once per request and holds it
for the whole exchange (write, poll, read), so a `Tag` in an `Arc` can be
called from several threads and the requests are serialised, never
interleaved. The C++ uses a recursive mutex shared by all three classes for
the same effect.

The lower layers are public. `tag-monitor-test` drives debug registers
through `LinkAdapt` directly, and `tagprobe`'s field-safe driver wraps a
`LinkAdapt` as a probe-rs probe (chapter 7). A user of `LinkAdapt` is outside
the monitor's rules: a register write can halt, reset or reconfigure the tag.

## File map

| File | What it holds |
| --- | --- |
| `src/lib.rs` | The crate's front page (a `//!` block worth reading first) and the re-exports: `Tag`, `TagError`, `TagMonitor`, `LinkAdapt`, `UsbDev`, `TargetFamily`, `MemAccess`, the statistics types |
| `src/tag.rs` | `Tag` and `TagError`; `Inner` (the monitor plus the last `Ack`) behind the mutex; one method per request |
| `src/monitor/mod.rs` | `TagMonitor`: the struct, attach and detach, target identification from `DBGMCU_IDCODE`, the call dispatcher, live memory access, keep-alive, statistics |
| `src/monitor/l4.rs` | The STM32L4 transport: the DebugMonitor/`DCRDR` call, the attach sequence, the wait for an idle monitor, the timeout report |
| `src/monitor/u3.rs` | The STM32U3 transport: the shared-block call, the attach sequence, the timeout report |
| `src/monitor/rpc.rs` | Protobuf framing over either transport (chapter 3's five steps) |
| `src/monitor/abi.rs` | The constants and the shared-block layout from `include/monitor.h`, mirrored by hand, with a layout test |
| `src/monitor/cortex_m.rs` | Arm debug and system-control register addresses and bits; core halt and register reads for timeout reports |
| `src/monitor/rescue.rs` | Legacy BitTag V6 field-rescue helpers (rewriting one backup word, reading records straight from flash) |
| `src/link/mod.rs` | `LinkAdapt`: the struct, attach (nRST low, enter SWD) and detach, statistics |
| `src/link/discovery.rs` | `UsbDev` (bus, address, vendor, product, serial), listing ST-LINKs, decoding the serial, opening the chosen one |
| `src/link/stlink.rs` | The ST-LINK USB protocol: command codes, the 16-byte block, bulk transfers, mode changes |
| `src/link/target.rs` | Raw target access: `read_mem32`, `write_mem32`, `read_debug32`, `write_debug32`, nRST, voltage; the rate-limited failure logging |
| `src/proto.rs` | Includes the prost-generated types; `debug_string`, `short_debug_string`, `to_json`, `from_json` with the C++'s spelling |
| `src/textformat.rs` | Protobuf text format laid out as the C++ `DebugString()` prints it |
| `src/configshape.rs` | `ConfigShape`: which `Config` fields each tag type supports, from the embedded default configurations; `has()` and `merge()` |
| `build.rs` | Runs protoc on `proto/tag.proto`, derives reflection, embeds every `default-config.json` |
| `tests/hardware.rs` | The hardware test (`--ignored`): attaches, read-only requests, one write the firmware must refuse, the UID check |
| `examples/tag_info.rs` | A read-only report: the smallest complete program on the crate |

## Using it

```rust
use tagcore::{Tag, UsbDev};

let tag = Tag::new();                        // detached; its own libusb session
let bases = tag.available();                 // USB enumeration only; resets nothing
if tag.attach(&UsbDev::default()) {          // the first base; RESETS THE TAG
    let info = tag.tag_info()?;              // Result<TagInfo, TagError>
    let status = tag.status()?;
    println!("{} is {:?}", info.uuid, status.state());
    tag.detach();                            // MONITORSTOP; also done on drop
}
```

Every request is a method returning `Result<T, TagError>`, named after the
protobuf request: `tag_info`, `config`, `status`, `erase`, `start(&Config)`,
`stop`, `calibrate`, `set_rtc`, `test(TestReq)`, `state_log(index)`,
`data_log(index)`, `calibration_log`, `read_calibration(index)`,
`write_calibration(&constants)`, and `request(&Req)` for a raw exchange.
Link-level facts that send no request (`voltage`, `git_sha`,
`base_identity`, `target_family`) return `Option` or `bool`. `set_rtc` sleeps
up to a second, holding the lock, to send the time on a second boundary.

The protobuf types are in `tagcore::proto`: `Req`, `Ack`, `Config`, `Status`,
`TagInfo`, `TagState`, `TestReq`, and the rest, exactly as `proto/` defines
them. Enums arrive as `i32` fields (`status.state`) with a typed accessor
(`status.state()`) and `TagState::try_from(i32)`; chapter 14 explains why.

## Errors and logging

| Layer | Returns | Where the reason is |
| --- | --- | --- |
| `Tag` requests | `Result<T, TagError>` | In the error: `transport_failed` (the RPC did not complete; the request may never have arrived), `err` (the firmware's `Ack.Err` code), `message` (the firmware's text, or a description). Also kept for `last_error()` and `debug_message()`, as the C++ keeps it |
| `TagMonitor`, `LinkAdapt` | `Option` / `bool` | Logged at the point of failure, as the C++ only logs it |

A caller that retries retries only transport failures: a refusal is the
firmware's decision. A reply that completed but lacked the expected payload
is reported as "Tag reply carried no status" and so on.

All messages go through the `log` facade at the levels the C++ uses. Tools
install `env_logger` (`RUST_LOG=debug` shows the attach detail); the
applications forward the records to their log panes. One deliberate
difference from the C++: failed debug-register reads are rate-limited. A tag
that is booting or held in reset fails every poll, and after probe-rs
releases a field base a burst of ST-LINK status `0x81` failures is normal, so
the first failure in a run is an `ERROR`, the rest are `DEBUG`, and the first
success after two or more failures logs a `WARN` with the count.

The `instrumentation` feature (on by default) counts and times every USB
transfer, ST-LINK command and RPC for `LinkAdaptStats` and `TagMonitorStats`,
which `tag-dwnld --profile` prints. With it off, the counters compile away.

## Generated code

`build.rs` does three things at build time, which is why a fresh clone
builds with nothing installed:

1. **Compiles the protobuf schema with prost.** `tag.proto` imports
   `tagdata.proto`, so compiling it yields every message a monitor RPC can
   carry. `protoc` comes from the `PROTOC` environment variable if set,
   otherwise from the `protoc-bin-vendored` crate. The files declare no
   package, so prost writes them to `_.rs` under `OUT_DIR`, which
   `src/proto.rs` includes.
2. **Derives reflection.** Every message implements
   `prost_reflect::ReflectMessage` against a descriptor pool written beside
   the generated code. Reflection is how `textformat.rs` and the JSON
   functions print and parse exactly as the C++ `DebugString()` and
   `json_util` do, and how `ConfigShape` walks `Config` by field name.
3. **Embeds the default configurations.** Every
   `embedded/proto-c/*-proto-c/default-config.json`, in directory-name order,
   becomes a generated `defaultconfigs.rs`, the counterpart of the C++
   build's `defaultconfigs.cc`. A new family's default configuration is
   picked up on the next build with no code change.

libusb is compiled from source and linked statically through `rusb`'s
`vendored` feature, so the binaries depend on no system libusb (on Linux
they need `libudev`).

## Attach, step by step

For reading the code, this is the order `TagMonitor::attach` runs
(`monitor/mod.rs`, then the family's file):

1. `LinkAdapt::attach`: open the base by vendor, product and, when given,
   serial; drive nRST low; enter SWD. The tag is now held in reset.
2. Read `DBGMCU_IDCODE` at the L4 address, then the U3 address, to set
   `TargetFamily`. Unknown falls back to the L4 transport.
3. The family's attach sequence (`l4::attach_l4` or `u3::attach_u3`): arm the
   debug block, release reset, read the monitor metadata (protocol version,
   call buffer address and size, SHA string address), start the session.
4. Read the firmware's git SHA string from tag memory once and cache it.

From then on `rpc()` frames requests and the family's `call_*` delivers them.
`detach` sends `MONITORSTOP` and closes the probe; dropping a `Tag` detaches.

## Keeping it in step with the C++

The C++ is the reference: where the two differ on the wire, the C++ is right
and the Rust is a bug, unless the README's list of deliberate differences
says otherwise (Rust-shaped API, rate-limited logging, the omitted
`goo.gle/debugproto` marker in text output). A change to the C++ tagcore that
alters behaviour (a timeout, an attach step, a retry, a message) needs the
same change here, in the file the map above names. The checks are:

- the unit tests, which need no hardware: serial decoding, command and
  memory-range rules, the shared-block layout, legacy record decoding,
  error reporting, protobuf framing, text format and JSON spelling, and
  `ConfigShape` against the real default configurations;
- `tests/hardware.rs` (`cargo test --test hardware -- --ignored`), which
  attaches to a real tag, sends read-only requests and one calibration write
  the firmware must refuse, and checks the UID read over SWD against the
  firmware's;
- the command-line ports (chapter 9), compared field for field with their
  C++ counterparts on a BitTag and an IMUTag.

The port found three bugs in the C++, fixed in both; when the two disagree,
check which one the firmware agrees with before porting either way.

## Where to read next

- [`libraries/tagcore-rs/README.md`](../../libraries/tagcore-rs/README.md):
  the file map with the C++ names, the API differences, building and testing.
- [`libraries/tagcore-rs/design/architecture.md`](../../libraries/tagcore-rs/design/architecture.md):
  the design rationale this chapter condenses.
- The rustdoc (`cargo doc -p tagcore --no-deps --document-private-items`):
  every type and function, with the C++ name it follows.
- `libraries/tagcore/README.md` for the C++ library, which also holds what
  the Rust does not: the SQLite writer and the recovery library.
