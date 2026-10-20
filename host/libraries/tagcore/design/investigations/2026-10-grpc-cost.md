---
type: investigation
status: closed
summary: Measured cost of a gRPC server in the host build on macOS arm64 - official gRPC C++ (+12 MB per executable, over an hour of cold dependency build, a c-ares failure on the static triplet) against grpcxx (+0.24 MB, half a minute), with a Python grpcio client working against grpcxx.
---

# Cost of a gRPC Server in the Host Build (2026-10)

Adding the official gRPC C++ library adds about **12 MB to every statically
linked executable**, about 4 MB of which is OpenSSL even when no channel uses
TLS. It costs about **70 minutes of cold vcpkg build time** on an M2 (the host
code generator plus the static library), which vcpkg's binary cache reduces to
once per gRPC version. Call latency is 20 µs in-process and 81 µs over a Unix
socket, negligible next to an SWD monitor RPC. c-ares, a required gRPC
dependency, does not currently build for `arm64-osx-static`.

[grpcxx](#grpcxx), a server-only gRPC library, adds **0.24 MB** and builds,
dependencies included, in **about half a minute**. The standard Python
`grpcio` client works against it for typed calls, the raw `Call`, memory
read/write and error codes, over TCP and over a Unix socket. These numbers
led the [tag server proposal](../proposals/tag-server.md) to choose grpcxx.

## Setup

- macOS 27, Apple M2 (8 cores), Xcode clang. The machine was heavily loaded
  during the build (load average 60-190), so treat build times as an upper
  bound.
- vcpkg at the project baseline `99a97de` (gRPC 1.76.0, protobuf and abseil as
  the project already uses), triplet `arm64-osx-static` from
  [`cmake/vcpkg-triplets`](../../../../../cmake/vcpkg-triplets/), host triplet
  `arm64-osx`.
- Manifest: `protobuf` and `grpc` only. The gRPC port has no default features,
  so `"default-features": false` changes nothing; OpenSSL, c-ares, re2, zlib and
  abseil are unconditional dependencies of the port.
- A throwaway CMake project, outside the tree, generating C++ from
  [`proto/tag.proto`](../../../../../proto/tag.proto) plus a two-method service
  (`Call(Req) returns (Ack)`, `Voltage`), and four executables:
  - `baseline`: protobuf only; builds a `Req`, parses an `Ack`;
  - `client`: gRPC client over a Unix socket;
  - `server`: gRPC server on a Unix socket, answering with a fake `Ack`;
  - `inproc`: server and client in one process over
    `grpc::Server::InProcessChannel()`.
- Release, `-Wl,-dead_strip`, sizes measured after `strip`.

## Build time (cold, no binary cache)

| Package | Triplet | Time |
| --- | --- | --- |
| grpc (host, for `grpc_cpp_plugin`) | `arm64-osx` | 30 min |
| grpc (debug + release) | `arm64-osx-static` | 39 min |
| openssl | each triplet | 2.6-3.2 min |
| protobuf / abseil | `arm64-osx-static` | 2.1 / 1.9 min |
| c-ares, re2, zlib, utf8-range | each | under 2 min |

Editing a triplet file changes the vcpkg ABI hash of every package built with
it, so the static protobuf, abseil and OpenSSL were rebuilt rather than
restored from the binary cache after the c-ares workaround below.

Compiling project code that includes `<grpcpp/grpcpp.h>` added about one
second to a single-file target (4.5 s against 3.5 s, including the link).

## c-ares fails on `arm64-osx-static`

c-ares 1.34.6 detects `pipe2()` in the macOS 27 SDK, but the triplet's
deployment target is 13.0, where it is unavailable:

```
src/lib/event/ares_event_wake_pipe.c:70:7: error: 'pipe2' is only available
on macOS 27.0 or newer [-Werror,-Wunguarded-availability-new]
```

Pre-seeding the configure check in the triplet works around it:

```cmake
if(PORT STREQUAL "c-ares")
  set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DHAVE_PIPE2=0)
endif()
```

## Binary size (stripped)

| Executable | Size |
| --- | --- |
| `baseline` (protobuf only) | 1.39 MB |
| `client` | 13.15 MB |
| `server` | 13.34 MB |
| `inproc` | 13.47 MB |
| for reference, the shipped `tag-info` | 2.54 MB |

Linked code in `inproc` by library, from the linker map (12.1 MB total):

| Library | MB | Share |
| --- | --- | --- |
| grpc (core) | 5.69 | 47% |
| OpenSSL libcrypto | 3.51 | 29% |
| protobuf | 0.97 | 8% |
| OpenSSL libssl | 0.53 | 4% |
| abseil | 0.44 | 4% |
| re2, grpc++, c-ares, zlib, upb, gpr | about 0.6 | 5% |

gRPC registers its security code at start-up, so dead-stripping does not
remove OpenSSL even when every channel uses insecure credentials. gRPC's build
offers BoringSSL or an external OpenSSL, never no TLS.

## Latency

A unary `Call` returning an `Ack` with an empty `Status`:

| Channel | Per call |
| --- | --- |
| In-process | 20 µs |
| Unix domain socket | 81 µs |

## grpcxx

grpcxx v0.7.0 (HTTP/2 through nghttp2, I/O through libuv, C++20) has no
client, no streaming and no TLS. It was measured once the proposal needed only
a unary server, with Python clients.

### Setup

- The same machine, vcpkg protobuf and triplet as above, fetched with
  FetchContent and `GRPCXX_HERMETIC_BUILD=ON`, which fetches and builds libuv,
  nghttp2 and fmt.
- A service with `GetStatus`, `Start`, `Stop`, `Voltage`, `Call(Req)
  returns (Ack)`, `ReadMemory` and `WriteMemory`, backed by a fake tag with a
  state machine, a sparse memory map and a refused write to the
  `monitor_shared` range.
- A Python client on `grpcio` 1.84.0 and `protobuf` 7.36.2, with stubs from
  `grpcio-tools`.

### Results

| Measure | grpcxx | Official gRPC (above) |
| --- | --- | --- |
| Cold build of the library and its dependencies | 33 s (configure 21 s, build 12 s) | about 70 min |
| Server, stripped, static | 1.63 MB | 13.34 MB |
| Over protobuf alone (1.39 MB) | +0.24 MB | +12 MB |
| Python `GetStatus` round trip, loopback TCP | 103 µs | — |
| Python `GetStatus` round trip, Unix socket | 65 µs | — |

Every call worked from Python. `FAILED_PRECONDITION` and
`PERMISSION_DENIED` arrived with their `google.rpc.Status` details intact.

### Integration findings

- **Protobuf version check.** grpcxx calls `find_package(Protobuf 3.15.0)`.
  Protobuf's `protobuf-config-version.cmake` (release 33.4, C++ library 6.33.4)
  rejects any requested major version other than 6, so the request fails even
  though the installed version is newer. `-DPROTOBUF_MINVERSION=6.0` passes.
- **`libprotoc`** is installed by vcpkg only for the host triplet, and
  `protoc-gen-grpcxx` links it. The experiment borrowed the host library,
  which works only because host and target are the same architecture. An
  integration builds the plugin as a host tool.
- **Shared by default.** grpcxx built `libgrpcxx.dylib` until
  `BUILD_SHARED_LIBS=OFF` was set. An earlier 1.45 MB reading left grpcxx out
  of the binary for that reason.
- **No `grpc-message` header.** grpcxx sends `grpc-status` and
  `grpc-status-details-bin` only. `grpc_status.rpc_status.from_call` then
  raises, because the message inside the details does not match the empty
  header. Reading `grpc-status-details-bin` from the trailing metadata and
  parsing `google.rpc.Status` directly works.
- **Unix sockets.** grpcxx listens on TCP only, but `server::run(uv_os_sock_t&&)`
  accepts a socket the caller has bound. A bound `AF_UNIX` socket passed in this
  way served `grpcio` clients at `unix:///tmp/...`. On Windows, libuv checks
  the address family in that path, so loopback TCP is the expectation there.
- **Project maturity.** One maintainer, pre-1.0, not in vcpkg. Its CI builds
  on Linux, macOS and Windows; the Windows job uses vcpkg's protobuf.

## Alternatives checked
- **Qt GRPC**: client only (Qt 6.12), and licensed GPLv3 or commercial, not
  LGPL, so it cannot go into distributed MIT-licensed applications built on
  LGPL Qt. It also generates QtProtobuf message classes, not the `libprotobuf`
  classes the applications use.
- **A gRPC submodule** instead of vcpkg: the same libraries and size, no
  binary cache, and a second copy of protobuf and abseil to keep in step. A
  vcpkg overlay port gives the same control over gRPC's CMake options.
