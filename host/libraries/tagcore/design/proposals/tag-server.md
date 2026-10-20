---
type: proposal
status: proposed
summary: Two testing capabilities - Qt applications and command line tools running in-process against a simulated tag, and Python (or any gRPC language) driving a real tag through a standalone grpcxx-based tag-server that mirrors the Tag API and adds a memory read/write side channel; shipped applications gain no new dependency.
---

# Simulated Tags and a Tag Server for Testing

**Status:** phases 1 and 2 are built. The monitor side channel, keepalive and
`LastError` are in tagcore, verified on L4 and U3 tags
([TODO](../../TODO.md)). `tag-server` is in
[`host/tagserver`](../../../../tagserver/README.md) and its Python client in
[`host/python/tagclient`](../../../../python/tagclient/README.md). Phase 3, the
simulator, is not built.

This proposal delivers two outcomes:

1. **Applications tested against a simulated tag.** A new tagcore backend,
   `SimBackend`, stands in for the USB link inside the process. With
   `--simulate scenario.json`, `qtmonitor`, `qtcalibrate` and the command line
   tools run their real tag-handling code against a scripted tag. No server
   and no new dependency are involved.
2. **Python, or any other gRPC language, driving a real tag, with a memory
   side channel.** A standalone program, `tag-server`, attaches to one tag base
   and serves the whole `Tag` API over gRPC on a local socket. It adds
   read/write access to tag memory, which `TagMonitor` provides and keeps
   mutually exclusive with monitor RPCs. `tag-server --simulate` serves the
   simulated tag the same way, so the Python tests also run without hardware.

`tag-server` is built on [grpcxx](https://github.com/uatuko/grpcxx), a small
gRPC server library. That adds about 0.25 MB and half a minute of build time,
where the official gRPC C++ library adds 12 MB and over an hour of cold
dependency build ([measurements](../investigations/2026-10-grpc-cost.md)).
Clients see standard gRPC, so Python uses the ordinary `grpcio` package.
Shipped applications and tools keep talking to the tag directly over USB.

## Structure

```text
tagcore
  Tag                      public API, unchanged, plus ReadMemory / WriteMemory / KeepAlive
   │
  TagBackend               internal interface: Attach, Rpc, Voltage, GitSha, stats, memory, rescue helpers
   ├─ UsbBackend           TagMonitor over the ST-LINK; one lock covers RPCs, memory access and keepalive
   └─ SimBackend           simulated tag driven by a scenario file

qtmonitor, qtcalibrate, tag-*      ── Tag(UsbBackend)                    real tag, as today
          … --simulate scenario    ── Tag(SimBackend)                    outcome 1, in-process
Python ──grpcio──► tag-server      ── Tag(UsbBackend | SimBackend)       outcome 2
```

## The memory side channel

`TagMonitor` gains `ReadMem(addr, length, buffer)` and `WriteMem(addr, data)`,
built on the `ReadMem32`/`WriteMem32` it inherits from `LinkAdapt`. They
generalise today's single-word `ReadMemWord`, used by `tag-peek`.

- **Mutual exclusion lives in `TagMonitor`.** One mutex inside `TagMonitor`
  guards `Rpc`, `ReadMem`, `WriteMem`, the keepalive and the field-rescue
  helpers. A memory access therefore runs between monitor RPCs, never inside
  one, whoever calls it. `Tag`'s own mutex stays, to protect its reusable
  `Req` and `Ack`.
- **Access goes through the debug port while the core runs.** It never halts
  the core, so the monitor keeps working.
- **Only while attached.** The firmware keeps the debug clocks running only
  during a monitor session (see the `DBGMCU->CR` note in
  [`pwr-l432.c`](../../../../../embedded/tags/common/core/src/pwr-l432.c)), so
  a call made while detached fails rather than reading garbage.
- **Guarded writes, U3 only.** On STM32U3 a write that overlaps the
  `monitor_shared` block ([`monitor.h`](../../../../../include/monitor.h)) is
  refused unless the caller forces it, because such a write would end the
  session the caller depends on. STM32L4 has no shared block (its monitor uses
  the debug registers), and the same addresses are ordinary SRAM there, so
  nothing is guarded. No other range is guarded.
- **Word access.** The address and length must be multiples of 4, matching
  the ST-LINK's 32-bit memory access, and transfers are split into chunks of
  at most 1 KB.
- **Results.** Each call returns a `MemAccess`: `Ok`, `NotAttached`,
  `InvalidRange`, `Protected` or `LinkError`, so a caller can report each case
  differently.

`Tag::ReadMemory` and `Tag::WriteMemory` expose these to C++, and `tag-peek`
becomes a thin user of them. `ReadMemWord` is then retired.

## Keepalive

The monitor drops its session if the host goes quiet:

- **U3:** within 5-10 s. The firmware checks `host_activity` every
  `MONITOR_HEARTBEAT_PERIOD_S`.
- **L4:** after 60 s without a request.

The applications never notice, because they poll every 100-400 ms. A Python
script paused in a debugger would lose its session. `Tag::KeepAlive()` keeps
the session alive:

- **U3:** one SWD write of `host_activity`, which runs no code on the tag.
- **L4:** a `get_status` monitor request. A dedicated no-op request would
  need a firmware change and is not planned.

`tag-server` calls it from a timer every 2 s while attached. Both forms are
cheap enough that the server does not track other traffic to skip ticks.

## Outcome 1: the simulated tag

### SimBackend

`SimBackend` implements `TagBackend` in plain C++, with no new dependencies.
Everything above it runs its real code: `Tag`'s request building and reply
decoding, and the application or tool. It models the tag only as far as host
programs depend on it; it tests the host, never the firmware.

- **Answers** come from the existing qtmonitor fixtures in
  [`host/docs/fixtures/qtmonitor/`](../../../../docs/fixtures/qtmonitor/README.md):
  `info`, `config`, `voltage`, and a `Status` per state. They are extended with
  the optional sections the
  [fake tag proposal](fake-tag-for-command-line-tools.md) defines:
  `state`, `state_log`, `data_log`, `git_sha`, `settle`, `erase_polls`. One
  more section, `calibration_log`, holds the pages returned in the calibrate
  state, for `qtcalibrate`.
  Data-log fixtures are captured from real tags, so a simulated download writes
  a real log.
- **State** follows the firmware's per-request permission rules in
  [`monitor.c`](../../../../../embedded/tags/common/core/src/monitor.c). `Start`
  and `SetRtc` require idle, `Erase` requires finished or aborted, `Calibrate`
  requires idle. Anything else is refused with `Ack::PERM`. A rule that drifts
  from the firmware is a bug in the simulator.
- **Memory** is a sparse map the scenario can seed, so `ReadMemory` and
  `WriteMemory` work; unmapped addresses fail.
- **Behaviour over time** comes from the scenario's event list:

```json
{
  "fixture": "bittag.json",
  "events": [
    { "on": "Start", "fail": "PERM", "times": 1 },
    { "after_ms": 5000, "link": "drop", "for_ms": 3000 },
    { "after_ms": 9000, "state": "finished" },
    { "on": "GetDataLog", "delay_ms": 200 }
  ]
}
```

Events either fire on a call (`on`) or at a time after attach (`after_ms`).
They can make a call fail with a given `Ack::Err` or a transport failure,
delay a reply, drop and restore the link, or change state. A plain fixture
file, with no `events`, is also a valid scenario.

### Selecting it

`Tag::Simulate(path)` switches a `Tag` to `SimBackend` before attach.
`Available()` then lists one simulated base, and `Attach()` attaches to it.
The applications add `--simulate <scenario>`. The command line tools add it in
`parse_options()` ([`options.cc`](../../../../commandline/options.cc)), next to
`--base`, so every tool built on `Tag` gets it.

## Outcome 2: tag-server

`tag-server` is a small C++ program on tagcore and grpcxx, with no Qt:

```sh
tag-server [--attach | --base bus:device | --simulate scenario.json]
           [--listen unix:/tmp/tag-server-<uid>.sock | --listen 127.0.0.1:50061]
           [--enable-debug]
```

- **One tag per server.** The server owns the `Tag`. Clients may attach and
  detach it, or the server attaches at start-up with `--attach` or `--base`.
  Concurrent clients are serialised by the locks above; there are no sessions.
  Run two servers for two bases.
- **The keepalive** runs while attached.
- **Detach on exit.** The server sends `MONITORSTOP` when it shuts down,
  including on a signal, so the tag returns to low power.
- **Local by default.** On Linux and macOS it listens on a Unix domain socket,
  and on Windows on loopback TCP (see below). It uses TCP on other addresses
  only when `--listen` names one. No TLS.

### Server library: grpcxx

grpcxx is a C++20 gRPC server: HTTP/2 through nghttp2, I/O through libuv, and
protobuf for messages. It has no client, no streaming and no TLS. The revised
interface needs none of these, since every call is unary and clients come from
the standard gRPC packages. Its code generator, `protoc-gen-grpcxx`, writes only
the service header (`*.grpcxx.pb.h`). The messages come from the ordinary
`protoc --cpp_out`, so they are the same `libprotobuf` classes `Tag` already
uses. A handler is a template specialisation per call:

```cpp
template <>
rpcGetStatus::result_type ServiceImpl::call<rpcGetStatus>(grpcxx::context &, const Empty &) {
  Status status;
  if (!tag.GetStatus(status)) return {to_grpc_status(tag.LastError()), std::nullopt};
  return {grpcxx::status::code_t::ok, status};
}
```

What the measurements found when integrating it
([details](../investigations/2026-10-grpc-cost.md#grpcxx)):

- **The protobuf version check.** grpcxx asks `find_package(Protobuf
  3.15.0)`, but protobuf's CMake package accepts only requests with major
  version 6 (its C++ library version), so the project sets
  `PROTOBUF_MINVERSION=6.0`.
- **Building the code generator** needs `libprotoc`, which vcpkg installs only
  for the host triplet, so `protoc-gen-grpcxx` is built as a host tool.
- **Static linking** needs `BUILD_SHARED_LIBS=OFF`, since grpcxx otherwise
  builds a shared library.
- **Unix sockets.** grpcxx's `run(socket)` accepts a socket the caller has
  already bound. `tag-server` binds the Unix socket itself and passes it in;
  this worked with `grpcio` clients. On Windows that path checks the address
  family, so the server uses loopback TCP there.
- **The error message header.** grpcxx sends the status code and
  `grpc-status-details-bin`, but no `grpc-message`. See *Errors* below.

grpcxx is pre-1.0 and has one maintainer, which is acceptable for an optional
test tool. The server code it touches is small, and because clients see
standard gRPC, the official gRPC C++ library could replace it later without
changing any client.

### Interface: the Tag API, one call per method

`proto/tag_service.proto`, beside [`tag.proto`](../../../../../proto/tag.proto)
and reusing its messages, mirrors [`tagclass.h`](../../tagclass.h) one to one.
The server method for each call is a few lines around the `Tag` method, so the
mirror stays mechanical. Python and other languages get typed calls without
reimplementing `Tag`'s request building.

The mirror is cheap to keep because the `Tag` API rarely changes. The core
calls (attach, information, configuration, status, start, stop, erase, test,
clock and logs) are unchanged since the first commit in 2022. Later changes
were additions: calibration in 2025, then diagnostics in 2026 (the debug
message, statistics, rescue helpers and `ReadMemWord`). The memory side
channel and `--enable-debug` cover that kind of addition. The only
incompatible changes were two to `ReadCalibration`'s signature. What changes
is the protocol's messages, and the service carries those unchanged:

- **A new log type** (a new `Ack` payload and its message in
  [`tagdata.proto`](../../../../../proto/tagdata.proto)) needs no server
  change. `GetDataLog` returns the whole `Ack`, and Python sees the new type
  once its stubs are regenerated.
- **New fields in `Config`, `Status` or `TagInfo`** pass through the same way.
- **The simulator's fixtures** are protobuf JSON, so they can carry new fields
  and log types without changes to the reader.
- **Only a new `Tag` method** needs a new call in the service. A new `Req` is
  usable through `Call` before then.

| `Tag` method | gRPC call | Notes |
| --- | --- | --- |
| `Available` | `ListDevices` | |
| `Attach` / `Detach` / `IsAttached` | `Attach` / `Detach` / `IsAttached` | `Attach` takes a device from `ListDevices`, or none for the only one |
| `GetTagInfo` / `GetConfig` / `GetStatus` | `GetInfo` / `GetConfig` / `GetStatus` | |
| `Start` / `Stop` / `Erase` / `Calibrate` / `Test` | same names | |
| `SetRtc` | `SetRtc` | the server's clock, aligned to the next second |
| `GetStateLog` / `GetDataLog` / `GetCalibrationLog` | same names | paged by index, as in C++ |
| `ReadCalibration` / `WriteCalibration` | same names | |
| `Voltage` / `GitSha` | same names | |
| `GetLinkStats` / `GetMonitorStats` / `Reset…Stats` | same names | |
| `ReadMemory` / `WriteMemory` | `ReadMemory` / `WriteMemory` | only with `--enable-debug` |
| `ForceBackupState` / `CountBitTagLogHeaders` / `GetBitTagLogFromFlash` | same names | field rescue; only with `--enable-debug` |
| `DebugMessage` | — | replaced by status details on each call |
| — | `Call(Req) returns (Ack)` | raw pass-through, for requests that have no call yet |

The memory messages:

```proto
message MemRead  { uint32 address = 1; uint32 length = 2; }
message MemWrite { uint32 address = 1; bytes data = 2; bool force = 3; }
message MemData  { bytes data = 1; }
```

**Errors.** A failed call returns a gRPC status code. The details carry a
`google.rpc.Status` whose message is the tag's error message. That status in
turn holds a `TagError { Ack.Err err = 1; }` in its details. The codes:

| Status code | Cause |
| --- | --- |
| `FAILED_PRECONDITION` | `Ack::PERM`, or not attached |
| `INVALID_ARGUMENT` | `Ack::INVAL` |
| `RESOURCE_EXHAUSTED` | `Ack::NODATA` |
| `UNAVAILABLE` | USB or monitor failure; `MemAccess::LinkError` |
| `NOT_FOUND` | no device |
| `INVALID_ARGUMENT` | `MemAccess::InvalidRange` (unaligned or wrapping) |
| `PERMISSION_DENIED` | `MemAccess::Protected` |

`MemAccess::NotAttached` maps to `FAILED_PRECONDITION`, as for RPCs.

This needs one small addition to `Tag`: `LastError()`, returning the last
`Ack::Err`, its message, and whether the transport failed. Today that
information is folded into `bool` and `DebugMessage()`.

Because grpcxx sends no `grpc-message` header, the `grpcio-status` helper
(`rpc_status.from_call`) rejects these errors: it requires the message inside
the details to match the header. The Python wrapper instead reads
`grpc-status-details-bin` from the call's trailing metadata and parses the
`google.rpc.Status` itself, which takes three lines. A small grpcxx change to
send the header would let clients use the standard helper; see the open
questions. `google/rpc/status.proto` is vendored into `proto/`; it is
Apache-2.0 licensed.

### Python

A small pure-Python package: stubs generated by `grpcio-tools` from `proto/`,
plus a wrapper that turns status codes into exceptions (with the tag's message
and `Ack::Err` from the details) and detaches in a `with` block. It depends only on `grpcio` and `protobuf`, so it installs with
`pip` on any platform, with no native build. The script needs no USB access;
the server does.

## Scenarios

### 1. Shipped applications and tools on a real tag

Unchanged in behaviour: `Tag` over `UsbBackend`, the applications' own
polling, no gRPC and no size change.

*Cost:* the `TagBackend` extraction and the lock moving into `TagMonitor` are
changes to the shipped code path, verified on L4 and U3 hardware before
release.

### 2. A Qt application against a simulated tag

```sh
qtmonitor --simulate host/docs/fixtures/qtmonitor/scenarios/start-refused.json
```

`qtmonitor` attaches to the simulated base and runs its real code against it:
the status poll, the configuration tabs, start, stop, erase and download. The
scenario supplies the tag's side: a refused `Start`, a link drop while
running, a slow download.

The test drives the window through Qt Test, or through the existing
screenshot automation, and checks what the window shows. Nothing runs outside
the application process, so it works offscreen in CI
(`QT_QPA_PLATFORM=offscreen`). `qtcalibrate --simulate` works the same way,
with calibration samples taken from the fixture's calibration log.

Today's `--fake-fixture` and `--replay-capture` modes render fixture data
without the live `Tag` path. They can be retired once the screenshot
automation runs on `--simulate`.

### 3. Command line tools

Shipped tools still talk to USB directly. `--simulate` gives the
hardware-free checks the fake tag proposal wants, for exit status, output,
call order and error paths, with no separate fake build:

```sh
tag-info --simulate bittag.json --json        # the contract flash_release.py relies on
tag-reset --simulate settle-timeout.json      # expect exit 1, naming STATE_UNSPECIFIED
tag-dwnld --simulate bittag-with-log.json out.sqlite
```

A Python script runs these scenarios and checks the results, as that proposal
describes. Tools that work below `Tag` (`tag-capture`, `tag-rebuild`,
`tag-xflash`, `tag-sramcall`) drive SWD and the SRAM loaders directly and stay
out of scope.

### 4. A Python test program on a real tag

```sh
tag-server --base 1:4
```

```python
from tagclient import TagClient, pb       # client over runtime-compiled stubs

with TagClient() as tag:                    # tag-server's default socket
    print(tag.get_info().uuid, tag.git_sha())
    tag.start(make_config())
    while tag.get_status().state != pb.tag.RUNNING:
        time.sleep(0.5)
    ...
    tag.stop()
    page = tag.get_data_log(0)              # one raw page; index rules as tag-dwnld
```

The server's keepalive holds the session while the script sleeps or sits at a
breakpoint. A call from Python costs about 65 µs over the Unix socket (about
100 µs over loopback TCP) on top of the monitor RPC, which takes milliseconds.

### 5. Python with the memory side channel

```sh
tag-server --base 1:4 --enable-debug
```

```python
fw = Firmware.for_sha(tag.git_sha())         # ELF from the matching build; pyelftools
tag.start(cfg)
fifo = tag.read_memory(fw.symbol("imu_fifo_cfg"), 4)
tag.write_memory(fw.symbol("debug_trace_enable"), b"\x01\x00\x00\x00")
```

Reads and writes go between monitor RPCs, so the test can check firmware
variables or peripheral registers while it drives the tag through the normal
interface. Symbol lookup stays in Python; the server knows nothing about
firmware layouts. Repeated `tag-peek` runs, each attaching and detaching, are
no longer needed.

### 6. Python against a simulated tag

```sh
tag-server --simulate scenarios/start-refused.json
```

The same scripts as scenarios 4 and 5, with no hardware. A pytest fixture
starts a server per test on a temporary socket, with the scenario that test
needs. The scenario also seeds the simulated memory, so side-channel tests can
check their own logic, though not the firmware.

### 7. C++ test programs

Programs in the tree, such as `tag-test` or a future hardware check, call
`Tag::ReadMemory`/`WriteMemory` directly, with no server. They can also use
`Tag::Simulate` to run without hardware.

### 8. Continuous integration

A Linux runner with no hardware runs:

- the applications against `--simulate` scenarios (offscreen);
- the command line checks;
- the Python tests against `tag-server --simulate`.

Only the job that builds `tag-server` builds grpcxx and its dependencies,
which takes about half a minute.

### Not supported

- **An application and a script sharing one base.** A shipped application owns
  the USB link, and the applications have no gRPC client. Run the script alone
  through `tag-server`, or the application alone.
- **Driving a running application's simulated tag from outside.** The
  scenario file is fixed when the application starts. If a test ever needs to
  steer the simulator interactively while an application runs, the remedy is a
  gRPC client backend in development builds, rejected below for now.

## Build

| Item | Build option or setting |
| --- | --- |
| `SimBackend` | always built: no dependencies, small |
| `tag-server` | `BUILD_TAG_SERVER`, default `OFF` |
| grpcxx | pinned to a release, fetched only when `BUILD_TAG_SERVER` is on. It is built either by FetchContent with a URL hash, fetching its own libuv, nghttp2 and fmt, or by a vcpkg overlay port using vcpkg's `libuv`, `nghttp2` and `fmt` (`GRPCXX_HERMETIC_BUILD=OFF`). Release packages neither build nor link it. |
| `protoc-gen-grpcxx` | built for the host, with the host triplet's `libprotoc` |
| Python stubs | generated from `proto/`, which stays the only schema source |

`tag-server` is about 2 MB stripped, so it could later be packaged as an
optional tool, for labs that want Python access without building from source.

## Relationship to existing proposals

- **[Python interface](python-interface.md).** For test and automation use,
  this replaces its pybind11 extension and wheel packaging with a pure-Python
  gRPC client. The Python proposal's API shape (context manager, exceptions,
  protobuf messages as values) carries over to the wrapper. Its shared
  download service is still worth doing, so that `tag-dwnld` and a Python
  downloader share one loop. An in-process binding can be added later on the
  same `TagBackend` seam if a program must own the USB link without a server.
- **[Fake tag for the command line tools](fake-tag-for-command-line-tools.md).**
  Its fixture extensions and check scenarios carry over. Its link-time
  replacement of `Tag` is replaced by the runtime `SimBackend` that proposal
  pointed to as the better seam.

## Rejected alternatives

- **The applications and tools as gRPC clients of the server**, for the
  simulator or for sharing a base. The in-process `SimBackend` meets outcome 1
  with no dependency. grpcxx has no client, so the applications would need the
  official gRPC C++ client: 12 MB each, and about 200 MB across the command
  line tools.
- **The official gRPC C++ library for the server.** It works and is mature,
  but adds 12 MB (a third of it OpenSSL, which cannot be left out), more than
  an hour of cold dependency build, and a c-ares fix in the static triplets.
  It remains a drop-in replacement if grpcxx stalls, since clients would not
  change.
- **A sparse server exposing only `Call(Req) → Ack` and memory access.** Every
  client language would reimplement `Tag`'s request building and reply
  decoding. The full mirror is mechanical and keeps that code in one place.
- **Sessions, server-side status streaming and a streaming download.** Not
  needed for either outcome. Each can be added later without changing the
  calls above.
- **Qt GRPC**: client only, GPLv3 or commercial. **Length-prefixed
  `Req`/`Ack` over a socket**: no dependency, but no generated clients for
  other languages. Details in the
  [measurements](../investigations/2026-10-grpc-cost.md).

## Phases

The server comes first, so Python test programs can be written against real
tags before anything changes in how the applications reach a tag.

1. **Monitor additions.** The lock, `ReadMem`, `WriteMem` and `KeepAlive` in
   `TagMonitor`; `Tag::ReadMemory`, `WriteMemory`, `KeepAlive` and
   `LastError`; `tag-peek` moved onto them. These are additions; the existing
   calls behave as before. Verify on L4 and U3.
2. **Server.** `tag-server` on grpcxx, using `Tag` directly over USB: the full
   `Tag` mirror, keepalive, detach on exit and `--enable-debug`; the Python
   package. **Outcome 2 is complete on real tags.** No `TagBackend` is needed
   yet, and the applications are untouched.
3. **Simulator.** Extract `TagBackend`; add `SimBackend` with the scenario
   reader; `--simulate` in `qtmonitor`, `qtcalibrate`, the tools and
   `tag-server`; the first command line checks and scenarios; pytest fixtures
   that start a simulated server per test. **Outcome 1 is complete, and the
   Python tests also run without hardware.**

Phases 1 and 3 add no dependencies. Phase 1 is useful on its own to C++ tests
and `tag-peek`. Phase 3 is the only one that changes how the shipped
applications reach a tag, through the backend extraction.

## Open questions

1. **Whether shipped builds expose `--simulate`.** It costs nothing to keep,
   and it is useful for demonstrations and training, but it is one more option
   for users to meet.
2. **Windows.** grpcxx's own CI builds on Windows, but not yet with the
   project's static `x64-windows-static-md` configuration. Confirm that, and
   confirm loopback TCP as the Windows default.
3. **Upstream or carry.** Three small grpcxx changes would remove workarounds:
   - a protobuf version check that accepts the 6.x package;
   - sending `grpc-message`;
   - a documented way to listen on a Unix socket.

   Offer them upstream, or carry them as patches.
Settled:

- the write guard covers only the U3 `monitor_shared` block;
- the L4 keepalive uses `get_status` rather than a new firmware request;
- grpcxx is pinned with FetchContent and a hash, with its own pinned libuv,
  nghttp2 and fmt. A vcpkg overlay port remains an option if binary caching
  matters;
- the Python package is `host/python/tagclient`. It compiles the protos at
  import time rather than committing generated stubs.
