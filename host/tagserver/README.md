---
type: readme
status: current
summary: tag-server, the tagcore Tag API over gRPC on a local socket for test programs - building it, running it, its calls and error codes, and what it does not do.
---

# tag-server

`tag-server` attaches to one tag base and serves the whole tagcore `Tag` API
over gRPC, so test programs in Python, or any other gRPC language, can drive a
tag. It adds read/write access to tag memory (`--enable-debug`) and keeps the
monitor session alive while it holds the tag. The service is
`tagrpc.TagService` in [`proto/tag_service.proto`](../../proto/tag_service.proto);
the Python client is [`host/python/tagclient`](../python/tagclient/README.md).
It is a development and test tool, not part of the host packages. The design
and its rationale are in the
[tag server proposal](../libraries/tagcore/design/proposals/tag-server.md).

## Building

```bash
cmake -B <build-dir> -DBUILD_TAG_SERVER=ON <your usual options>
cmake --build <build-dir> --target tag-server
```

`BUILD_TAG_SERVER` (default `OFF`) also enables the `tag-server` vcpkg feature,
which adds protobuf's `libprotoc` for grpcxx's code generator. That rebuilds
protobuf once, about five minutes. The server is built on
[grpcxx](https://github.com/uatuko/grpcxx) v0.7.0, fetched at configure time and
pinned by hash in [`CMakeLists.txt`](CMakeLists.txt). grpcxx fetches and builds
its own libuv, nghttp2 and fmt, also pinned by hash, in about half a minute. The
executable lands in `<build-dir>/<config>/bin`.

## Running

```bash
tag-server --attach                      # the only base, on the default socket
tag-server --base 1:4 --enable-debug     # a given base, with memory access
tag-server --listen 127.0.0.1:50061      # TCP instead of a Unix socket
```

| Option | Meaning |
| --- | --- |
| `--listen unix:<path>` or `<ip>:<port>` | Where to listen. Default `unix:/tmp/tag-server-<uid>.sock`; `127.0.0.1:50061` on Windows. |
| `--attach` | Attach at start-up to the only base. Otherwise clients call `Attach`. |
| `--base <bus>:<device>` | Attach at start-up to that base. |
| `--enable-debug` | Serve `ReadMemory`, `WriteMemory` and the field-rescue calls. |
| `--keepalive <s>` | Seconds between keepalives while attached; default 2. |
| `--threads <n>` | gRPC worker threads; default 2. Calls on the tag are serialised regardless. |
| `-v`, `--verbose` | Log tagcore at DEBUG level. |

It prints `tag-server listening on ...` once ready. On `SIGINT` or `SIGTERM` it
stops serving, detaches, so the tag returns to low power, and removes its
socket. It refuses to start on a socket where another server is already
listening. The socket is created readable and writable by its owner only.

One server serves one tag. Concurrent clients share it, and their calls are
serialised; to use two bases, run two servers on two sockets.

## Calls

Every `Tag` method has a call of the same name (`GetTagInfo` is `GetInfo`);
see the comments in [`tag_service.proto`](../../proto/tag_service.proto).
Notes:

- `GetDataLog` and `GetCalibrationLog` return the tag's whole `Ack`, so new log
  types need no server change. At the end of a data log the `Ack`'s `err` is
  `NODATA`.
- `GetStateLog` returns an empty `StateLog` at the end; clients advance the
  index by the number of entries, as tagcore does.
- `Call` passes any `Req` through and returns the `Ack` as is, for requests
  without a dedicated call.
- `SetRtc` uses the server machine's clock.
- `ReadMemory`/`WriteMemory`: address and length must be multiples of 4, at
  most 16 MiB per call. On STM32U3, writes over `monitor_shared`
  (`0x20000000`-`0x2000003F`) need `force`. They run between monitor RPCs and
  never halt the core; see `TagMonitor::ReadMem` in
  [`tagmonitor.h`](../libraries/tagcore/tagmonitor.h).

## Errors

A failed call returns a gRPC status code. The `grpc-status-details-bin`
trailer holds a `google.rpc.Status` whose message is the reason, and whose
details hold a `tagrpc.TagError` with the firmware's `Ack.Err` and whether the
transport failed. The codes are listed at the top of
[`tag_service.proto`](../../proto/tag_service.proto).

grpcxx sends no `grpc-message` header, so a client sees an empty status
message and must read the details trailer. `tagclient` does this. Python's
`grpc_status.rpc_status.from_call` rejects such errors, because it requires
the two messages to match.

## Limitations

- **Unix sockets.** grpcxx listens on TCP only. `tag-server` binds the Unix
  socket itself and hands it to grpcxx's `run(socket)`, which works with
  standard gRPC clients but is not a documented grpcxx feature. On Windows that
  path checks the address family, so the server uses loopback TCP there.
- **Not yet built or tested on Windows or Linux**, only on macOS arm64.
- **No TLS and no authentication.** On TCP, listen on loopback only.
- **grpcxx** is pre-1.0 with one maintainer. Clients see standard gRPC, so the
  official gRPC C++ library could replace it without changing any client.
- **Not packaged**, so not in the [license inventory](../../LICENSES/README.md).
  Packaging it would add grpcxx, libuv, nghttp2 and fmt (all MIT), and the
  vendored `third_party/google/rpc/status.proto` (Apache-2.0, header kept).
