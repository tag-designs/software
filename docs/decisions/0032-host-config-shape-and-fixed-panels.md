---
type: decision
status: accepted
summary: Which Config fields a tag supports comes from its default config, read at run time by tagcore's ConfigShape, which also gives Restore and tag-start --merge one merge rule; qtmonitor's configuration screen is fixed panels selected by tag type; the change was proven equivalent on the host by a probe and golden results; tag-reported presence through proto3 optional fields was deferred.
---

# 0032. Host: configuration shape from default configs, fixed panels per tag type

Date: 2026-10-23

## Context

proto3 does not record whether a zero-valued scalar was set, so a decoded
`Config` cannot say which fields a tag supports. qtmonitor answered that at
build time. A CMake script regex-scanned each tag's `default-config.json` into
a table of twenty booleans per tag type. Four configuration modules showed and
hid their widgets from it, field by field, and relabelled themselves by tag
type.

That arrangement had these costs:

- **One field was described in about seven places:** the proto; the struct;
  the generator's field list; the generator's regex; each module's
  `SetConfig()`, an unused value-guessing `SetConfig()`, and `GetConfig()`; and
  a hand-written restore merge.
- **The regex ignored nesting.** It matched `range`, `freq`, `filter`, `odr`
  and `channels` anywhere in a file.
- **Restore and `tag-start --merge` disagreed.** Restore skipped zeros and
  copied whole sub-messages; `MergeFrom` skipped zeros and copied fields the
  tag lacked. Neither could set a value to zero.
- **What Start sent depended on the selected sub-tab.** The accelerometer
  module wrote four fields only when their widgets `isVisible()`.

## Decision

**Shape.** tagcore's `ConfigShape` holds each tag type's supported fields as
dotted paths. They are read at run time from the default configs, whose text
tagcore's CMake embeds without interpreting. The walk is checked against the
`Config` descriptor at each level and keeps zero-valued keys.

**One merge.** `ConfigShape::merge()` copies a field only if the tag supports
it and the file speaks for it. Restore and `tag-start --merge` both use it.
`ConfigShape::forFile()` limits a file to its own tag type's fields, because
qtmonitor's Save pads files with zeros.

**Fixed panels.** qtmonitor's Configuration tab is one page of panels, each
built once with fixed labels and ranges. The tag type selects the panels shown,
from one table. Each panel reads and writes only the fields it lists, whatever
is on screen.

- The schedule panels are three variants of one class, because they share the
  "Logging Schedule" box.
- The accelerometer has one variant per meaning of the reused `adxl362`
  fields.
- `qtmonitor --check-config-panels` checks the table against every shape.

**Equivalence proven on the host.** A headless probe
(`qtmonitor --config-probe`) runs 686 generated cases through the real tab.
It was added to the old code first, and its results were committed as goldens.
Every later step was diffed against them, and each accepted difference is
listed with its reason.

**Deferred: tag-reported presence.** Making `Config`'s scalars proto3
`optional`, with a marker field, would let the tag report its own shape. It is
kept as a proposal:
[tag-reported configuration presence](../shared/design/proposals/tag-reported-config-presence.md).

The model as it now stands is described in
[Tag Configuration Model](../shared/design/configuration-model.md).

## Alternatives considered

- **Tag-reported presence now.** It is wire-compatible: `optional` changes no
  bytes, and old tags can be recognised by a missing marker. But it needs every
  nanopb output regenerated, firmware work in each family, and an audit of
  JSON consumers. Its only advantage over default configs is catching a
  firmware/default-config mismatch, which has not yet caused trouble.
- **A frozen, checked-in table of shapes.** It would cut the build's tie to
  `embedded/proto-c/`, but it would be a second copy of each tag's field list.
- **A form generated from the protobuf descriptors.** Most of what the panels
  do is tag-specific: limits that follow other fields, fields that mean
  different things on different tags, and the schedule's date handling. A
  generic form would still need all of that written by hand.
- **Generic binding tables (widget ↔ path) for load and store.** These were
  planned, but exact equivalence needed panel-specific code, so each panel's
  `load()` and `store()` are explicit instead:
  - the accelerometer's load order, and its clamping;
  - the filter's fallback to 0;
  - the BitTag log panel hiding itself;
  - BitTagNG's `accel_type`.
- **Separate start-delay and sampling-period panels.** They would have changed
  the screen. The three schedule variants keep it as it was.
- **Per-tag hardware testing.** The configuration screen is a function from
  `Config` to `Config`, and the firmware did not change. The probe proves the
  output identical for every tag type; one live start covers the USB path.

## Evidence

From `host/applications/qtmon/test/expected-differences.md` and two live runs
on a BitTag LE (UUID `2035374D303150190059002F`, firmware `56e5e6a0`):

**Sub-tab defect.** It was confirmed in 69 of 686 cases, and fixed by testing
`!isHidden()` in place of `isVisible()`. Only those 69 cases changed. Live: an
inactive threshold of 0.50 g, set on the Sensors tab with Start pressed from
the Schedule tab, was stored as 0.498 (it had been sent as the old 0.35).

**`ConfigShape` and the merge.** `ConfigShape` reproduced the generator's table
for all eight tag types (`config_shape_check`). The merge changed 68 restore
cases and nothing else:

- one-field files no longer wipe the rest of a message. Before, a one-field
  `adxl362` file switched a BitTagNG to ADXL362;
- stated zeros are copied;
- another tag's file contributes only shared fields.

Live: `tag-start --merge` with a padded BitTagNG file sent the LE 0.3 g and 6
samples, and kept its own 0.35 g and log format, with no `accel_type`. The tag
stored exactly that.

**Panels.** Nothing sent or shown changed, except hibernation capped at the
firmware's two intervals (7 cases). 130 restore cases differed only by stale
rows in the old goldens, an artefact of the old `deleteLater()`; each was
checked to be exactly the new rows plus the base configuration's.

Live, on the same BitTag LE:

- in the new panels, the inactive threshold was set to 0.40 g, the inactivity
  count to 5 samples, and the log format to bit per second;
- the configuration was saved, the active threshold changed as a decoy, the
  file restored, and the tag started;
- the saved file and the tag's stored configuration both held exactly those
  values, with the active threshold back at 0.50 g (stored as 0.498).

## Consequences

**Adding a field takes four places:** the proto, the default configs that list
it, the firmware conversion, and the panel that owns it. The cross-check fails
if the default config or the panel is missed.

**Adding a tag type** needs a default config and one row in the panel table.

**The default configs must list every supported field, zeros included.** A
mismatch with the firmware is not caught on the host: UIUCTag's firmware
reports `accel_type`, which its default config omits, so it passes through
unchanged.

**The configuration probe is the gate for any change to the configuration
screen.** Its goldens are regenerated only with an `expected-differences.md`
entry.

**Generated documentation screenshots** are one page per tag type.
