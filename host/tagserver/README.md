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

## Building and testing

Build `tag-server` in its own build tree, beside your usual one. Turning
`BUILD_TAG_SERVER` on in a release tree would rebuild its protobuf and fetch
grpcxx into it.

`BUILD_TAG_SERVER` (default `OFF`) also enables the `tag-server` vcpkg feature,
which adds protobuf's `libprotoc` for grpcxx's code generator. That rebuilds
protobuf once, about five minutes. The server is built on
[grpcxx](https://github.com/uatuko/grpcxx) v0.7.0, fetched at configure time and
pinned by hash in [`CMakeLists.txt`](CMakeLists.txt). grpcxx fetches and builds
its own libuv, nghttp2 and fmt, also pinned by hash, in about half a minute, so
configuring needs network access to GitHub. It needs CMake 3.28 or newer.

The tests are in [`host/python/tagclient`](../python/tagclient/README.md). Each
starts its own server. With `TAG_SERVER_HARDWARE=1` and one base connected,
they also read information, status, the state log and memory from the tag,
and hold an idle session on the server's keepalive. They change no tag state.

### Linux

Prerequisites are those of the
[Linux build](../../README.md#linux-prerequisites). To let an ordinary user open
the tag base, install the udev rule, as for the other host tools:

```bash
sudo cp host/linux/70-ultralight-tags.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Build, from the repository root:

```bash
cmake --preset linux-vcpkg -B ~/Build/tag-designs/tag-server-linux -DBUILD_TAG_SERVER=ON -DBUILD_QT_APPS=OFF
cmake --build ~/Build/tag-designs/tag-server-linux --target tag-server -j
```

Test, with Python 3.10 or newer:

```bash
python3 -m venv ~/Build/tag-designs/tag-server-venv
~/Build/tag-designs/tag-server-venv/bin/pip install -e "host/python/tagclient[test]"
TAG_SERVER_BIN=~/Build/tag-designs/tag-server-linux/Release/bin/tag-server ~/Build/tag-designs/tag-server-venv/bin/pytest -v host/python/tagclient
```

Add `TAG_SERVER_HARDWARE=1` to the last command with a base connected.

### Windows

Prerequisites are those of the [Windows build](../../README.md#windows-build):
Visual Studio 2026 with the C++ workload, `VCPKG_ROOT` set, and the ST-LINK
driver the other host tools use. The server listens on loopback TCP
(`127.0.0.1:50061`), because grpcxx cannot take a Unix socket there. In
PowerShell, from the repository root:

```powershell
cmake --preset windows-vcpkg-static -B c:/software-build-tag-server -DBUILD_TAG_SERVER=ON -DBUILD_QT_APPS=OFF
cmake --build c:/software-build-tag-server --config Release --target tag-server
```

Test, with Python 3.10 or newer from python.org:

```powershell
py -m venv c:\tag-server-venv
c:\tag-server-venv\Scripts\pip install -e "host/python/tagclient[test]"
$env:TAG_SERVER_BIN = "c:\software-build-tag-server\Release\bin\tag-server.exe"
c:\tag-server-venv\Scripts\pytest -v host/python/tagclient
```

Then `$env:TAG_SERVER_HARDWARE = "1"` and run `pytest` again with a base
connected. The tests stop each server with Ctrl-Break, which it handles like
Ctrl-C: it detaches and exits 0. One test, a second server on a busy Unix
socket, is skipped on Windows.

Not yet verified on Windows:

- the `NGHTTP2_NO_SSIZE_T` definition in [`CMakeLists.txt`](CMakeLists.txt),
  taken from grpcxx's own Windows CI;
- grpcxx with the static `x64-windows-static-md` triplet;
- Ctrl-Break handling.

A build error from nghttp2 or libuv is the place to look first.

### Recording the results

Record a platform that passes in
[tagcore's TODO](../libraries/tagcore/TODO.md#verify-tag-server-on-hardware-and-other-platforms):
the OS and compiler, the test counts, and the tag used for the hardware run.
When both platforms pass, delete the item. For a failure, note the first
error in the same place.

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

It prints `tag-server listening on ...` once ready. On `SIGINT` or `SIGTERM`
(Ctrl-C or Ctrl-Break on Windows) it stops serving, detaches, so the tag returns to low power, and removes its
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
- **Not yet built or tested on Windows**, only on macOS arm64 and Linux
  x86_64.
- **No TLS and no authentication.** On TCP, listen on loopback only.
- **grpcxx** is pre-1.0 with one maintainer. Clients see standard gRPC, so the
  official gRPC C++ library could replace it without changing any client.
- **Not packaged**, so not in the [license inventory](../../LICENSES/README.md).
  Packaging it would add grpcxx, libuv, nghttp2 and fmt (all MIT), and the
  vendored `third_party/google/rpc/status.proto` (Apache-2.0, header kept).
