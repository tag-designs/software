---
type: readme
status: current
summary: A web mockup of qtmonitor's Configuration tab split the proposed way - the panels as a data spec, a stand-in engine owning the document and every rule, and a generic renderer that only draws field kinds and exchanges messages.
---

# qtmonitor Configuration Form Mockup

A mockup of how qtmonitor's Configuration tab would divide between UI and
business logic in a web (Tauri) front end with a C++ engine. Open
[`index.html`](index.html) in a browser; it needs no network and no tag.

## The split, as files

| File | Role | In the real design |
| --- | --- | --- |
| [`spec.js`](spec.js) | The panels as **data**: each panel's fields with kind, label, units, choices and tooltips; which panels each tag type gets; the tags' `default-config.json` | Sent by the engine as `FormSpec`; built in C++ beside `ConfigShape` |
| [`engine.js`](engine.js) | A **stand-in for the engine**: the document (config, source, baseline, unsaved changes), the bindings between `Config` and form values, rounding and clamping, constraints that follow other fields, whether editing is allowed, and the start checks | Qt-free C++ |
| [`ui.js`](ui.js) | A **generic renderer**: one control per field kind (`enum`, `number`, `whenTime`, `intervals`), dialogs, file pickers, the title. It holds no rules; every edit is a `setField` message, and the controls show the `FormState` that comes back | TypeScript in the webview |

The right-hand panes show the config the engine would send and every message
between UI and engine, as the sidecar would carry them.

## What it reproduces from qtmonitor

The panels, labels, ranges, choices and tooltips are those of
`host/applications/qtmon/tagconfiguration`, for all eight tag types in its
panel table, with the tags' own default configurations. The engine's rules are
ported from the panels' `load()` and `store()`:

- **Schedule:** start *Immediately* or *On* a time, run *Indefinitely* or
  *Until* a time; times in whole UTC hours, rounded up, a start never before
  now; immediate and indefinite stored as 0 and INT32_MAX.
- **Hibernation:** two rows with *In use*; a row first switched on starts at
  the logging start and lasts an hour; starts round down and ends up; an
  unused row is sent as 0 to 0.
- **Accelerometer:** thresholds range over range/64 to range for the chosen
  ±2/4/8 g, inactivity over 1 to 255 samples at the chosen rate, and changing
  the range or rate re-clamps them; the wake variants fix the range; values
  clamp and round to the shown decimals as the Qt spin boxes do.
- **Other panels:** PresTag's period (1–300 s), IMUTag's start delay (stored
  as minutes + 1), the BitTag log format, the LSM6 rates and ranges; fields no
  panel owns (`accel_type`) pass through.
- **Document:** defaults, open (the file merged onto its type's defaults),
  save, unsaved-changes marker; the tag's own configuration is read-only
  while the tag runs.
- **Start:** refused with the list of failed checks (clock offset, self-test,
  approval) and a *Start Anyway* choice.

**One rule is new:** an end time before the start, or a hibernation row that
ends before it starts, is reported as an error and blocks a plain Start.
qtmonitor does not check this today; it shows how validation would live in
the engine.

A *Simulated tag* box stands in for the attached tag's state, clock offset,
self-test result and approval. `?tag=IMUTAG` (or any tag type) opens with that
type's defaults.

## Checking it

```sh
node engine-test.cjs
```

[`engine-test.cjs`](engine-test.cjs) exercises the engine's rules: defaults
round-trip unmodified; thresholds clamp and round like the spin boxes and
follow the range; inactivity follows the rate; times round to the hour;
hibernation rows default and encode correctly; Start is refused, then
accepted; the running tag's configuration is read-only; a file merges onto its
defaults; IMUTag's start delay and BitTagNG's `accel_type` round-trip.

## Not covered

No tag, no engine process: `engine.js` is JavaScript standing in for C++. The
date and time inputs are the browser's own, in its locale's format, treated as
UTC; a finished UI would want a picker that shows UTC unambiguously.
