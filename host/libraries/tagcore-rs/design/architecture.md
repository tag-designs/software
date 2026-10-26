---
type: design
status: current
summary: How the Rust tagcore is put together - the three layers and who owns the USB link, the concurrency model, how failures are reported and logged, the generated protobuf types, and the rules for keeping the port in step with the C++ tagcore.
---

# tagcore-rs design

tagcore-rs has the same three layers as the C++ tagcore, with the same wire
behaviour:

- `LinkAdapt` speaks the ST-LINK USB protocol;
- `TagMonitor` runs the monitor protocol over it;
- `Tag` is the facade that tools call, one method per request.

The C++ is the reference. Where the two differ on the wire, the C++ is right
and the Rust is a bug. What the port changes is the API shape:

- `Result` instead of status flags;
- a mutex in `Tag` instead of a recursive mutex shared by every layer;
- `&mut self` on the lower layers, so the borrow checker guarantees one user
  of the link at a time.

The file-by-file map and the list of API differences are in the
[README](../README.md). The protocol is in
[the monitor interface](../../../../docs/shared/monitor-interface.md).

## Layers and ownership

```text
Tag (tag.rs)                 &self, Mutex<Inner>        thread-safe facade, last-RPC outcome
 └─ TagMonitor (monitor/)    &mut self                  attach sequences, calls, RPC framing
     └─ LinkAdapt (link/)    &mut self                  rusb handle, ST-LINK commands, nRST, voltage
```

Each layer owns the one below it by value. Nothing holds a second handle to
the USB device, so two exchanges can never interleave on the link. `Tag`
takes the lock once per request and holds it for the whole exchange: request
write, polling and reply read. A `Tag` can therefore be shared between
threads in an `Arc`. Requests from different threads are serialised, never
interleaved.

The lower layers are public, for tools that need them. `tag-monitor-test`
drives debug registers directly, and tagprobe's field-safe driver wraps a
`LinkAdapt` as a probe-rs probe. The C++ makes these members protected and
reaches them by subclassing. A user of `LinkAdapt` is outside the monitor's
rules: a register write can halt, reset or reconfigure the tag.

### Two families, one monitor

`TagMonitor::attach` identifies the part from `DBGMCU_IDCODE` and picks the
transport:

| Family | Transport | Where |
| --- | --- | --- |
| STM32L4 (L432) | DebugMonitor exception; `DCRDR` is the mailbox, `DEMCR.MON_PEND/MON_REQ` the doorbell; protobuf bytes in the firmware's RPC buffer | `monitor/l4.rs` |
| STM32U3 (U375) | A shared block at `MONITOR_SHARED_ADDR`, laid out by `include/monitor.h`, polled over SWD | `monitor/u3.rs`, `monitor/abi.rs` |

Above the transport, `monitor/rpc.rs` frames a `proto::Req` into the buffer
and parses the `proto::Ack` back. It is the same for both families.

### Attaching resets the tag

Both attach sequences go through nRST. The firmware notices the debugger
(`DEMCR.VC_CORERESET` on L4; on U3, a request written into the shared block
while the core is held at its reset vector) only at boot. So every
`Tag::attach` ends whatever the tag was doing. This is the reason for the
bench rule never to attach to a running tag just to watch it
([verifying firmware](../../../../docs/bench/verifying-firmware.md)). No API
here can observe a run in progress.

## Timeouts

Timeouts are the C++ values:

- 500 ms for a result read;
- 2.5 s for an ordinary monitor call;
- 65 s nominal for a protobuf call on L4. On U3 every call, protobuf
  included, has the 2.5 s limit, as in the C++.

The timeouts are counted in poll iterations, not wall time, so with USB
latency an unanswered L4 protobuf call takes about 120 s. During that time the `Tag`
lock is held. Every other caller, on any thread, waits behind it. An
application that calls the monitor from its UI or event thread will freeze
for that long when the firmware hangs. Tag Programmer's engine accepts this
([engine design](../../../applications/tagprogram-rs/design/engine.md#what-runs-on-the-engine-thread));
a new application should consider a worker.

## Failures

Each layer reports failure in the form its callers can use:

| Layer | Returns | The reason is |
| --- | --- | --- |
| `Tag` requests | `Result<T, TagError>` | in the error, and kept for `last_error()` / `debug_message()` as the C++ keeps it |
| `TagMonitor`, `LinkAdapt` | `Option` / `bool` | logged at the point of failure, as the C++ only logs it |

`TagError` separates a transport failure (`transport_failed`, the request may
never have reached the firmware) from a refusal (`err`, the firmware's Ack
code, and its message). A caller that retries should retry only the first.

### Logging

All messages go through the `log` facade, at the levels the C++ uses. Tools
install `env_logger`; Tag Programmer forwards them to its log pane.

Failed debug-register reads are rate-limited (`link/target.rs`):

1. the first failure in a run is an ERROR;
2. the rest are DEBUG;
3. the first success after two or more failures logs a WARN with the count.

A tag that is booting or held in reset fails every poll. After probe-rs
releases a field base, a burst of reads failing with ST-LINK status `0x81`
is normal. Without the limit, one reconnect wrote hundreds of identical errors.
The C++ does not rate-limit; this is a deliberate difference in logging only.
The calls and their results are the same.

### Instrumentation

The `instrumentation` feature (on by default) mirrors
`TAGCORE_ENABLE_INSTRUMENTATION`. It counts and times every USB transfer,
ST-LINK command and RPC, for `LinkAdaptStats` and `TagMonitorStats`. With it
off, the statistics read zero with `enabled == false`, and the counters
compile away.

## Generated protobuf types

`build.rs` compiles `proto/tag.proto` (which imports `tagdata.proto`) with
prost. Every message derives `prost_reflect::ReflectMessage` against a
descriptor pool written beside the generated code. Reflection is what
`textformat.rs` and the JSON functions in `proto.rs` use, to print and parse
exactly as the C++ `DebugString()` and `json_util` do, and what `ConfigShape`
uses to walk `Config`.

protoc comes from `PROTOC` if set, otherwise from the `protoc-bin-vendored`
crate. A machine without protoc, a CI runner included, still builds. libusb
is compiled in through `rusb`'s `vendored` feature, so no system libusb is
used either.

`build.rs` also embeds every `embedded/proto-c/*-proto-c/default-config.json`,
in directory-name order, as the C++ build's `defaultconfigs.cc` does.
`ConfigShape` derives from them which `Config` fields each tag type supports.
A new tag's default config is picked up on the next build, with no code
change.

## Keeping the port in step with the C++

A change to the C++ tagcore that alters behaviour (a timeout, an attach step,
a retry, a message the tools print) needs the same change here, in the file
the README's map names. The comparison that matters is on the wire and in the
tools' output:

- the unit tests check framing, the ABI layout, text and JSON spelling, and
  ConfigShape against the real default configs;
- `tests/hardware.rs` (ignored by default) attaches to a real tag, sends
  read-only requests and one calibration write the firmware must refuse, and
  checks the UID read over SWD against the firmware's;
- the command-line ports in `host/commandline-rs` were compared field for
  field with their C++ counterparts on a BitTag and an IMUTag.

The port found three bugs in the C++ (`c9967bc1`), fixed in both. When the two
disagree, check which one the firmware agrees with before porting either way.
