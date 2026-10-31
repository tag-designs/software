---
type: worklist
status: current
summary: Open Tag View work - features of the Qt sensorViz not carried into the v4.0 release, starting with the activity filter.
---

# Tag View TODO

Current behaviour and how it differs from the Qt sensorViz are in
[README.md](README.md).

## Not in v4.0

- **The activity filter.** The Qt sensorViz's **Configuration ▸ Activity
  Filter** was left in its Qt layer when the core was made toolkit-neutral
  ([proposal](../sensorviz/design/proposals/toolkit-neutral-core.md), revised
  2026-10-28), so Tag View does not have it. v4.0 ships Tag View alone and
  releases without it (decided 2026-10-31,
  [release plan](../../design/proposals/rust-default-release.md)). If it is
  wanted back, port it into the core as a transform in the registry, with
  its time constant as a declared parameter, and check it against the Qt
  app with `session_cli`, which the legacy build (`BUILD_LEGACY_HOST`)
  still provides.
