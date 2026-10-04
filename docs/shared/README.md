---
type: readme
status: current
summary: The contracts host tools and tag firmware both compile against, and where each is documented.
---

# Shared Contracts

Host tools and tag firmware meet at a small set of contracts. A change to any of
them changes both sides, and usually stored data as well, so each has one home
here or a single link from here. The code stays where it is: `proto/` and
`include/` are not moved.

| Contract | Code | Documented in |
| --- | --- | --- |
| Monitor protocol (configuration, status, download RPC over SWD) | `proto/tag.proto`, [`include/monitor.h`](../../include/monitor.h) | [monitor-interface.md](monitor-interface.md) |
| Protobuf schema and nanopb generation | [`proto/`](../../proto/README.md) | [proto README](../../proto/README.md) |
| Packed binary log records | [`include/imutag_log_format.h`](../../include/imutag_log_format.h), [`prestag_log_format.h`](../../include/prestag_log_format.h), [`uiuctag_log_format.h`](../../include/uiuctag_log_format.h) | [binary-datalogs.md](binary-datalogs.md) and the Doxygen in each header |
| External-flash loader service block | [`include/loader_service.h`](../../include/loader_service.h) | [loader runtime](../../embedded/loaders/design/loader-runtime.md), [SWD capture library](../../host/libraries/tagcore/design/swd-recovery.md) |
| SQLite download schema | `host/libraries/tagcore/sqlitelog` | [user reference](../../host/docs/src/reference/sqlite-logs.md) (authoritative), [writer internals](../../host/libraries/tagcore/sqlitelog/README.md) |

The offline rebuild (`tag-rebuild`) decodes captured tag memory on the host, so
firmware structs are a contract too: the firmware `_Static_assert`s every struct
offset the decoders in `host/libraries/tagcore/recovery/capturesource.cc` read.
When one fires, see [Capturing a tag](../bench/capturing-a-tag.md).

Open work on these contracts is in the [shared contracts worklist](TODO.md).
