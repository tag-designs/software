---
type: worklist
status: current
summary: Open sensorviz work - validation, load warnings, transform registry, session files and GUI tests.
---

# sensorViz TODO

Current behavior and its known limitations are in [README.md](README.md).

## Near term

- Keep validating CompassTag and IMUTag derived streams against real logs and
  against specialized/reference tooling where available.
- Warn the user when a load succeeds but an expected stream or derived stream is
  skipped, for example when CompassTag data lacks calibration metadata needed
  for heading/orientation streams.
- Add new stream display defaults to `defaultDisplayForStream()` as new tag
  streams appear.
- Improve transform-specific configuration structure if more transforms are
  added.
- Consider a small shared helper for building mirrored menu-bar/context-menu
  sections if more actions are added.
- Decide whether metadata-box position should remain session-only or become a
  saved preference.
- Add richer IMUTag display transforms once the desired analysis views are
  clearer.
- Implement the dialog screenshot suite. The blocking dialogs in
  `stream_actions.cpp`, `controls.cpp`, and `transforms.cpp` first need a
  refactor into reusable dialog builders so they can be shown modelessly,
  captured, and closed without user input.
- Support files processed by `dataprocessing`; the open decisions are in
  [the command-line TODO](../../commandline/TODO.md#sensorviz-handling-of-processed-files).

## Larger refactors

- Introduce a transform registry that declares:
  - required input stream ids or record-set ids;
  - generated stream ids;
  - default display policy;
  - configuration UI factory.
- Generalize record-set transforms before adding more multi-column sensor
  families.
- Add optional session/project files if users need to persist graph title,
  ranges, sea-level pressure, declination, metadata-box position, or other
  current-view state.
- Add light GUI regression tests around menu enable/visibility rules and
  preference JSON round trips.
