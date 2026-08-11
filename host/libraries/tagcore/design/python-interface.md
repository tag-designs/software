# TagCore Python Interface Design

## Status

Proposed. This document defines the intended architecture and public API for a
Python interface to the Qt-free `tagcore` host library. It does not commit the
project to a particular release date or package registry.

## Motivation

`tagcore` already provides the host-side primitives needed to discover tag
bases, attach to a tag, exchange protobuf requests, control tag state, and
write downloaded logs. A Python interface would make those capabilities
available to laboratory automation, scripted configuration, data acquisition,
manufacturing tests, and interactive analysis without duplicating the USB and
monitor protocols.

The interface should feel like a Python library rather than a mechanical
translation of the C++ API. In particular, Python callers should receive
values instead of output parameters, use exceptions instead of checking
Boolean return values, and work with the standard Python protobuf runtime.

## Goals

- Provide discovery, connection, information, configuration, status, control,
  calibration, and log-download operations from Python.
- Reuse the same transport, protocol, recovery, and log-writing behavior as the
  C++ command-line and Qt applications.
- Present generated Python protobuf messages for all protocol-bearing values.
- Keep `tagcore` and its Python binding Qt-free.
- Support deterministic connection cleanup through a context manager.
- Permit long-running native calls without blocking unrelated Python threads.
- Provide source builds first and a path to distributable Linux, macOS, and
  Windows wheels.
- Make the interface testable without requiring physical hardware for every
  test.

## Non-Goals

- Reimplementing the monitor or USB protocol in Python.
- Replacing the C++ `tagcore` API used by existing applications.
- Exposing arbitrary debug-memory operations as a supported public API.
- Providing an asynchronous API in the first version. Callers can initially
  use Python threads or executors around the synchronous API.
- Loading or plotting downloaded sensor data. The interface produces the
  existing text or SQLite formats; analysis remains a separate concern.
- Publishing wheels for every Python and platform combination in the first
  milestone.

## Existing Constraints

The current `Tag` class is a non-copyable façade over `TagMonitor`. It owns a
mutex, reusable request and acknowledgment protobufs, and the monitor instance.
Most operations are synchronous and report failure as `false`; successful
queries write into caller-provided output parameters. `DebugMessage()` exposes
the error string from the most recent acknowledgment, but the API does not
consistently distinguish transport, protocol, tag-state, and payload failures.

The public log-writer abstraction and its text and SQLite implementations
already belong to `tagcore`. However, the complete download workflow—including
state checks, optional stop, rescue behavior, progress accounting, and handling
of `Ack::NODATA`—currently belongs to `tag-dwnld`. A Python implementation must
not create a third, independently maintained version of that workflow.

The native build depends on C++20, libusb, protobuf, SQLite, and the shared tag
monitor interface. Those native dependencies must be resolved when building a
Python extension and, for binary wheels, either linked statically or packaged
with the extension as permitted by each dependency and platform.

## Architecture

The interface consists of three layers:

```text
Python application
        |
        v
tagcore public Python package
  - Pythonic Tag and Device APIs
  - protobuf conversion
  - exceptions and result dataclasses
        |
        v
tagcore._native extension
  - pybind11 binding adapter
  - GIL release around blocking calls
  - byte-oriented protobuf boundary
        |
        v
C++ tagcore
  - Tag / TagMonitor / LinkAdapt
  - shared download service
  - text and SQLite log writers
```

The public package owns Python policy. The private extension should be small
and should not become a second public API. `tagcore` remains the source of
truth for device behavior and download semantics.

### Binding Technology

The initial implementation should use pybind11. It integrates directly with
CMake, supports the required lifetime and GIL controls, and is mature on the
three target desktop platforms. The binding should wrap a deliberately small
adapter rather than automatically expose every C++ class and method.

A C ABI plus `ctypes` or CFFI would require a new lifetime, buffer, and error
contract while still needing a compiled native library. A subprocess wrapper
around the existing command-line tools would be useful for simple automation
but could not provide configuration objects, raw log iteration, structured
errors, or reliable connection ownership. Neither is the preferred library
interface.

### Protobuf Boundary

Python and C++ protobuf objects must not be bound to each other directly. The
native extension accepts and returns serialized protobuf bytes:

```text
Python protobuf --SerializeToString()--> bytes
bytes --ParseFromArray()--> C++ protobuf

C++ protobuf --SerializeToString()--> bytes
bytes --ParseFromString()--> Python protobuf
```

The public Python layer performs this conversion and returns the generated
Python classes from `tag_pb2` and `tagdata_pb2`. This design:

- preserves protobuf field-presence and enum behavior;
- avoids coupling callers to the C++ protobuf ABI;
- avoids exposing C++ message lifetimes to Python;
- works even when the Python protobuf runtime uses a different internal
  implementation from the extension; and
- keeps JSON and dictionary conversion in the standard Python protobuf API.

The Python package should expose the generated modules under
`tagcore.proto`. Generated files are package artifacts rather than canonical
schema sources; `proto/tag.proto` and `proto/tagdata.proto` remain authoritative.

## Public Python API

Python names use `snake_case`. Native acronyms such as RTC and USB remain
uppercase in prose but not in method names. The examples below specify the
intended shape, not an exact implementation language for every annotation.

### Device Discovery

```python
from dataclasses import dataclass

@dataclass(frozen=True)
class Device:
    vid: int
    pid: int
    bus: int
    address: int

def discover() -> list[Device]: ...
```

`discover()` returns all compatible tag bases visible to libusb. An empty list
is a normal result. Discovery failures, such as failure to initialize libusb,
raise `TransportError` rather than being reported as no devices.

`Device` is a stable selection token only for the current enumeration. Bus and
address can change after disconnect or reboot, so applications must rediscover
devices instead of persisting them as durable identities.

### Connection Lifetime

```python
class Tag:
    @classmethod
    def open(cls, device: Device | None = None) -> "Tag": ...

    @property
    def attached(self) -> bool: ...

    def close(self) -> None: ...

    def __enter__(self) -> "Tag": ...
    def __exit__(self, exc_type, exc_value, traceback) -> None: ...
```

When `device` is omitted, `open()` selects the first discovered device only if
exactly one is available. It raises `DeviceNotFoundError` when none are
available and `DeviceSelectionError` when several devices require an explicit
choice. This is safer than silently selecting one base in a multi-tag setup.

`close()` is idempotent. Exiting the context manager always detaches, including
when the body raises. Destruction provides a last-resort detach but callers
must not depend on garbage-collection timing for hardware cleanup.

### Information and Control

```python
class Tag:
    @property
    def voltage(self) -> float: ...

    @property
    def git_sha(self) -> str: ...

    def get_info(self) -> TagInfo: ...
    def get_status(self) -> Status: ...
    def get_config(self) -> Config: ...

    def start(self, config: Config) -> None: ...
    def stop(self) -> None: ...
    def erase(self) -> None: ...
    def calibrate(self) -> None: ...
    def set_rtc(self) -> None: ...
    def run_test(self, test: TestReq | int) -> None: ...

    def read_calibration(self, index: int) -> CalibrationConstants: ...
    def write_calibration(self, constants: CalibrationConstants) -> None: ...
```

Query methods return populated Python protobuf messages. Commands return
`None` on success and raise on failure. The wrapper validates that callers pass
the expected protobuf class before serialization; the native adapter validates
that the bytes parse completely before invoking `Tag`.

`set_rtc()` retains the native behavior of synchronizing the request to the
next whole second. Its documentation must state that it can block for up to
approximately one second in addition to transport latency.

### Raw Log Access

```python
class Tag:
    def iter_state_logs(self, start: int = 0) -> Iterator[StateLog]: ...
    def iter_data_logs(self, start: int = 0) -> Iterator[Ack]: ...
    def get_calibration_log(self) -> Ack: ...
```

These operations support specialized inspection and custom decoders. The
iterators are synchronous, retain exclusive use of their `Tag` object for each
request, and stop on the protocol's normal end-of-data response. They raise on
transport failure, malformed payloads, or explicit tag errors.

The exact advancement rule for `iter_data_logs()` depends on the number of
records represented by each tag-specific acknowledgment. Before this method is
implemented, the shared C++ download layer must expose one authoritative way
to calculate the next index. Python must not reproduce tag-specific record
counting rules.

### Download API

```python
from dataclasses import dataclass
from os import PathLike
from pathlib import Path
from typing import Callable, Literal

@dataclass(frozen=True)
class DownloadResult:
    path: Path
    format: Literal["sqlite", "text"]
    records: int
    requests: int
    link_stats: LinkStats
    monitor_stats: MonitorStats

class Tag:
    def download(
        self,
        path: str | PathLike[str],
        *,
        format: Literal["default", "sqlite", "text"] = "default",
        stop_if_running: bool = False,
        rescue_exception: bool = False,
        progress: Callable[[int, int], None] | None = None,
    ) -> DownloadResult: ...
```

`format="default"` uses `defaultTagLogStorageFormat()` for the connected tag.
An explicitly requested unsupported format raises `UnsupportedFormatError`
before the output file is modified.

The initial API does not silently overwrite existing output. The eventual
implementation must choose and document one of these policies before release:
require a separate `replace=True` option, or fail whenever the path exists.
The current SQLite writer's replacement default must not leak through as an
undocumented Python behavior.

The progress callback receives completed and expected record counts. It runs
on the calling Python thread, may be invoked many times, and must not call
another method on the same `Tag`. If it raises, the download is cancelled,
the active writer transaction is rolled back or closed, and the original
callback exception is propagated after native cleanup.

### Statistics

`LinkStats` and `MonitorStats` are immutable Python dataclasses mirroring the
instrumentation values exported by `tagcore`. Duration fields retain their
native nanosecond units in explicitly suffixed names such as `usb_in_ns`.
Instrumentation-disabled builds return the structures with `enabled=False`;
they do not return `None`.

## Error Model

All public exceptions derive from `TagCoreError`:

```text
TagCoreError
|- DeviceNotFoundError
|- DeviceSelectionError
|- AttachError
|- NotAttachedError
|- TransportError
|- ProtocolError
|- InvalidStateError
|- UnsupportedFormatError
`- LogWriteError
```

Exceptions should carry a human-readable message plus structured context when
available, including the operation name, selected device, acknowledgment error
code, and firmware error message. Python callers must not need to parse an
English message to distinguish the categories above.

The current C++ Boolean-return API cannot always produce that distinction.
The first prototype may map failures to a coarse `TagCoreError`, but a stable
release requires a structured native result or error facility shared by the
C++ applications and the binding. Adding structured errors is preferable to
guessing failure categories in the binding from `DebugMessage()`.

## Shared Download Service

The download loop should be extracted from `tag-dwnld` into `tagcore` before
the public Python download API is declared stable. A possible C++ shape is:

```cpp
struct DownloadOptions;
struct DownloadResult;

DownloadResult downloadTagLog(
    Tag &tag,
    const DownloadOptions &options,
    DownloadProgressCallback progress);
```

The service owns these policies:

- determining whether the current tag state is downloadable;
- optionally stopping a running tag;
- choosing and validating the storage format;
- creating the writer and writing metadata;
- beginning, committing, or rolling back the log transaction;
- advancing download indices and handling `Ack::NODATA`;
- applying the legacy BitTag exception-rescue path;
- recording transfer statistics; and
- converting writer and protocol failures into structured errors.

`tag-dwnld`, Qt download code, and the Python binding should call this service.
CLI formatting, signals, UI progress presentation, and Python callbacks remain
in their respective adapters.

## Threading and Reentrancy

One `Tag` instance represents one connection and is not concurrently
reentrant. The native mutex serializes most existing operations, but the Python
contract should be stricter: callers must not issue overlapping operations on
the same object. Separate `Tag` objects may be used for separate devices.

The binding releases the Python global interpreter lock while waiting for USB,
monitor RPCs, RTC synchronization, and log I/O. It reacquires the lock only to
construct Python results, raise exceptions, or invoke a progress callback.
Native state and borrowed Python references must not be accessed while the GIL
is released unless their lifetime is otherwise guaranteed.

An asynchronous API can later wrap the synchronous implementation with
`asyncio.to_thread()` or an executor. The first version should not introduce a
second native concurrency model.

## Logging

The first version should not bind the C logging callback directly to Python,
because a callback can arrive while the GIL is released and introduces
lifetime and shutdown hazards. Native errors needed for control flow belong in
structured exceptions. Optional integration with Python's `logging` module can
be added later through an explicitly installed, GIL-safe callback adapter.

## Build and Package Layout

The intended source layout is:

```text
host/python/
|-- CMakeLists.txt
|-- pyproject.toml
|-- bindings/
|   `-- module.cpp
|-- src/tagcore/
|   |-- __init__.py
|   |-- api.py
|   |-- errors.py
|   |-- models.py
|   `-- proto/
`-- tests/
```

The top-level build adds `BUILD_PYTHON_BINDINGS`, defaulting to `OFF`, so the
existing host packages do not acquire a Python build dependency. When enabled,
CMake finds the Python interpreter and development module plus pybind11, builds
`tagcore._native`, and stages the Python sources and generated protobuf files.

Because the extension links the existing static `tagcore` and `proto` targets,
those targets and any relevant static dependencies must be compiled as
position-independent code on platforms that require it. The binding continues
to use C++20.

`pyproject.toml` should use `scikit-build-core` so both editable source builds
and wheels invoke the existing CMake graph. The Python package declares a
compatible `protobuf` runtime dependency. pybind11 may be a build dependency
or supplied by the native dependency manager, but the project should select
one authoritative version per build to avoid ambiguous discovery.

### Binary Wheels

Wheel production is a separate milestone after source builds work reliably.
`cibuildwheel` should drive release builds for the supported CPython versions.

- Linux wheels must be repaired for the selected manylinux baseline and must
  document the required udev/device permissions. A successful import does not
  imply permission to open a USB device.
- Windows wheels must account for the libusb driver used by supported tag bases
  and either statically link or bundle all non-system runtime libraries.
- macOS wheels must cover the supported architectures and ensure bundled
  library paths remain valid after wheel installation.

Protobuf, SQLite, and libusb licensing and redistribution terms must be
reviewed before publishing bundled wheels. Import tests should run in a clean
environment so an undeclared system library cannot mask a packaging defect.

## Test Strategy

### Unit Tests

- Device and statistics conversion.
- Serialization and parsing for every public protobuf-bearing method.
- Rejection of the wrong protobuf type or malformed serialized input.
- Exception-category and context mapping.
- Context-manager cleanup and idempotent `close()`.
- Download option validation and output replacement policy.
- Progress callback success, cancellation, and exception propagation.

### Hardware-Free Native Tests

`Tag` currently owns a concrete `TagMonitor`, so deterministic transport tests
require an internal abstraction or injected backend. A fake backend should be
able to provide acknowledgments, transport failures, malformed payloads,
timeouts, state transitions, and log pages. This seam is internal and must not
expose arbitrary custom transports as part of the public Python API.

The shared download service should be tested against the fake backend and
temporary text/SQLite outputs. Tests must cover normal completion,
`Ack::NODATA`, unsupported formats, writer failure, callback cancellation, and
legacy rescue behavior.

### Hardware Integration Tests

An opt-in suite should cover discovery, explicit device selection, attach and
detach, information/status/config reads, and a small safe download against
known test hardware. Destructive commands such as erase and calibration must
require separately designated fixtures and must never run merely because a
tag is present.

### Package Tests

Each wheel is installed into a clean environment and tested for import,
protobuf construction, discovery with no device, and native dependency
resolution. Supported hardware runners can add end-to-end smoke tests, but
ordinary package validation must not require USB hardware.

## Implementation Plan

### Milestone 1: Native Prototype

- Add the optional pybind11 extension build.
- Generate and package the Python protobuf modules.
- Implement discovery, explicit attach/detach, information, status,
  configuration, voltage, Git SHA, and basic control methods.
- Release the GIL around blocking calls.
- Provide context-manager cleanup and coarse `TagCoreError` failures.
- Validate a Linux source build against real hardware.

### Milestone 2: Stable Core API

- Add structured native error reporting.
- Add the injectable monitor/backend seam and hardware-free tests.
- Expose calibration and carefully defined raw-log iteration.
- Finalize API naming, type annotations, docstrings, and output replacement
  policy.

### Milestone 3: Shared Downloads

- Extract the download workflow into `tagcore`.
- Convert `tag-dwnld` and Qt download code to the shared service.
- Bind `Tag.download()` with progress, cancellation, result statistics, text,
  SQLite, and supported rescue behavior.

### Milestone 4: Distribution

- Add clean-environment wheel builds and tests.
- Produce Linux wheels first, followed by macOS and Windows.
- Document OS-specific USB permissions and driver prerequisites.
- Establish the supported Python/version matrix and release process.

## Compatibility and Versioning

The Python package version should initially follow the repository release
version. Public Python names, exception categories, protobuf return types, and
download semantics are compatibility commitments once a stable package is
released. The private `_native` module is not a compatibility boundary and may
change with the public wrapper.

Protocol evolution continues to follow protobuf compatibility rules. Returning
generated Python messages allows new optional fields and enum values to appear
without redesigning the binding, although applications must still tolerate
unknown enum values and fields introduced by newer firmware.

## Open Decisions

The following must be resolved before the affected API is declared stable:

1. Whether existing download paths fail by default with an explicit
   `replace=True` escape hatch, or always fail without a separate replacement
   operation.
2. Which CPython versions and platform architectures are supported by the
   first binary release.
3. Whether Python source generation happens during every package build or in a
   separate release-generation step, while keeping `.proto` files canonical.
4. The exact structured native error type shared by C++ and Python adapters.
5. Whether raw data-log iteration is needed in the first stable release or can
   remain an advanced follow-up after the high-level downloader is available.

## Acceptance Criteria

The design is implemented when:

- a clean Python environment can build and import the package through
  `pyproject.toml`;
- discovery and explicit device selection behave deterministically;
- a context-managed `Tag` can read information, status, and configuration and
  issue supported control commands using Python protobuf messages;
- blocking native calls release the GIL and all failures raise documented
  exceptions;
- Python, CLI, and Qt downloads use one shared native workflow;
- text and SQLite downloads match the existing command-line output semantics;
- hardware-free tests exercise protocol, error, download, and cancellation
  paths; and
- at least one supported platform passes an end-to-end hardware test and a
  clean-environment package test.

