---
type: proposal
status: proposed
summary: Deferred. Let each tag report which Config fields it supports, through proto3 optional fields and a config_schema marker, instead of the host reading it from the tag's default config; tags already in the field keep working by falling back to the default config.
---

# Tag-Reported Configuration Presence

**Deferred.** Today the host reads which `Config` fields a tag supports from the
tag's default configuration, through `ConfigShape`
([Tag Configuration Model](../configuration-model.md)). This proposal would let
the tag report it instead. It was the second stage of the work recorded in
[decision 0032](../../../decisions/0032-host-config-shape-and-fixed-panels.md),
deferred because default configs serve well and this stage costs firmware work
in every family. Take it up if a mismatch between a firmware and its default
config causes trouble, or if tags of one type start to support different
fields. One mismatch exists today and is harmless: UIUCTag's firmware reports
`adxl362.accel_type`, which its default config omits.

## Mechanism

Mark the scalar and enum fields of `Config`, `Adxl362` and `Lsm6dsv` as proto3
`optional`, and add a marker:

```proto
message Config {
  ...
  uint32 config_schema = 10;  // 0: presence not reported (older firmware);
                              // 1: every supported field is present
}
```

Field 5 is unused but skipped, in case it was retired. Firmware that sets
`config_schema = 1` promises that every field it supports is present (`has_x`),
zeros included.

The vendored nanopb 0.4.9.1 supports this. `TagInfo.ppm_clock_error` is already
`optional` and generates a `has_` flag, and `pb_encode.c` writes an optional
field whenever its flag is set, even when the value is 0.

On the host, `ConfigShape` gains a second source. For a `Config` with
`config_schema >= 1` the shape is its present fields; otherwise it is
`ConfigShape::forTag()`, as now. Panel selection stays by tag type, and the
tag's own shape can be checked against the selected panels when it attaches.

## Why tags already in the field keep working

`optional` changes no bytes on the wire. It changes two things, both in code
generated from the new `.proto`:

- whether the sender writes a zero;
- whether the receiver keeps a "was this on the wire" flag.

An existing tag, built from today's `.proto`, keeps omitting zeros, and every
nonzero value it sends is byte for byte what it sends now. On its config,
`has_x()` therefore means "nonzero", not "supported". So the marker is
essential: without it the host cannot tell "older firmware with a zero" from
"newer firmware without the field". A missing marker reads as 0, and the host
falls back to the default config.

| | Existing firmware | Firmware with the marker |
| --- | --- | --- |
| **Existing host** | Today's behaviour. | The host uses default configs. It keeps the unknown field 10 and echoes it back on Start; the firmware ignores it there. |
| **New host** | Shape from the default config. | Shape from the tag. |

## Firmware rules for adopters

- `readConfig()` sets the marker and every supported field's `has_` flag, in
  every state it reports from. BitTag and BitTagNG report their compiled-in
  default in IDLE. For that path the change is data:
  `"config_schema": 1` in the default config, which `config-gen.py` then
  encodes with presence. Their stored-configuration path, and the other
  families' hand-built `readConfig()`, set the flags in code.
- `writeConfig()` treats an absent field as 0, never as "keep" or "reject":
  an older host, or any client built from today's `.proto`, never sends a zero.
- Unsupported fields are still ignored on write, not rejected.

Each family would adopt the marker at its own release, or never.

## Costs

- **Regeneration:** every nanopb output under `embedded/proto-c/` is
  regenerated, and each `Config` struct grows by one `bool` per field, so
  every target needs a `.list` check.
- **Firmware:** the work is per family, under the flashing protocol.
- **JSON output:** protobuf stops printing absent `optional` fields, even with
  `always_print_fields_with_no_presence`. That affects `tag-info` records and
  qtmonitor's Save, and the Python readers of `tag-info` output have not been
  audited.
- **Merge semantics:** `MergeFrom` in the Python `tagclient` example would
  start copying stated zeros.
