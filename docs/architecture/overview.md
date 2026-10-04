---
type: design
status: current
summary: One-page map of the system -- tag firmware, the SWD monitor through a base, the shared protobuf schema, flash logs, download to SQLite, and the host applications -- with links to where each part is documented.
---

# System Overview

Tags are small STM32 data loggers (STM32L432 and STM32U375) that record sensor
data into flash. A host talks to a tag over **SWD**, through a base board that presents itself as an ST-LINK, using a
**monitor** protocol carried in protobuf messages. Host tools configure and
start a tag, read its status, and download its logs into a **SQLite** file,
which the visualization applications read.

```text
 host apps / CLI ──tagcore── USB ── base (ST-LINK) ── SWD ── tag firmware
   (qtmonitor, sensorviz,     │                                 │
    tag-* tools)              │ protobuf (proto/)               ├─ internal flash: config, marker log, page headers
                              │                                 ├─ external flash (most tags): data pages
   SQLite log  <── download ──┘                                 └─ RTC backup registers: pState
```

## Tag firmware

Firmware lives under `embedded/`. Each tag target combines one generated board
description (`embedded/boards`), one nanopb protocol binding
(`embedded/proto-c`), shared code from `embedded/tags/common`, often a family
directory under `embedded/tags/families`, and ChibiOS. See
[Embedded Source Layout](../../embedded/design/source-layout.md) and the
[tags README](../../embedded/tags/README.md). Runtime state that must survive
standby is kept in `pState`, a mirror of the RTC backup registers.

## Bases

A base is a USB SWD programmer that emulates an ST-LINK. The host uses it both
to program firmware and to run the monitor. See the
[bases README](../../embedded/bases/README.md).

## The monitor

The monitor is the SWD path host tools use to inspect a tag, exchange protobuf
RPC packets, run tests, download logs and manage state. STM32L4 tags use the
DebugMonitor exception and `DCRDR`; STM32U3 tags use a shared-memory mailbox.
The host side is `host/libraries/tagcore/tagmonitor.cc`. See
[Tag Monitor Interface](../shared/monitor-interface.md).

## Shared contracts

The host and the firmware compile the same definitions: the protobuf schema in
[`proto/`](../../proto/README.md), the packed binary log structs in `include/`
([Shared Binary Log Formats](../shared/binary-datalogs.md)), and the loader
service block. They are listed in [Shared Contracts](../shared/README.md).

## Logs and download

A tag stores its configuration and state-marker log in internal flash. Tags
with an external SPI NOR or NAND part keep their sample data there, indexed by
page headers in internal flash; BitTag and BitTag-legacy have no external flash part. `tagcore` downloads the log over
the monitor and writes it with `sqlitelog`. The tables are documented for users
in the [SQLite log reference](../../host/docs/src/reference/sqlite-logs.md),
and the writer in [`sqlitelog/README.md`](../../host/libraries/tagcore/sqlitelog/README.md).

When a tag cannot run its firmware, its flash can still be read without
changing it. SRAM-resident [external flash loaders](../../embedded/loaders/README.md)
read the external flash, `tag-capture` stores the tag's state, and
`tag-rebuild` reconstructs the SQLite download from that capture
([SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md)).

## Host tools

`host/libraries/tagcore` is the low-level host interface (USB, monitor, log
writers, recovery). The Qt applications in `host/applications` are
`qtmonitor` (monitor, configure, download), `qtprogram`, `qtcalibrate`,
`sensorviz` (SQLite log visualization) and the legacy `btviz`
([applications README](../../host/applications/README.md)). The command-line
tools in `host/commandline` (`tag-info`, `tag-start`, `tag-stop`, `tag-reset`,
`tag-capture`, `tag-xflash`, `tag-rebuild`, and others) script the same
operations. User manuals are in `host/docs/`.

## Building and releasing

One CMake tree builds host tools and firmware; see the
[root README](../../README.md). Firmware for distributed tags builds
reproducibly from committed generated sources
([Tag Firmware Build Reproducibility](../build/firmware-reproducibility.md)),
and a release image is qualified on hardware for power before it is flashed
onto a field tag ([release procedure](../release/release-procedure.md)).
