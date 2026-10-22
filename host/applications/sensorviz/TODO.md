---
type: worklist
status: current
summary: Open sensorviz work - validation, load warnings, transform registry, session files and GUI tests.
---

# sensorViz TODO

Current behavior and its known limitations are in [README.md](README.md).

## Near term

- **One y axis per sensor group, with a legend.** Today every visible stream
  gets its own y axis, so accel x/y/z plus magnitude means four accel axes,
  and likewise for gyro and magnetometer. Linked groups share a *range*
  (`linkedAxisGroupId()` in `core/plot_spec.cpp`) but still draw separate
  axes, which clutters the plot.
  - *Group membership comes from data, not code.*
    - Use the `streams` table's `group_id` (`imu_accel`, `imu_gyro`, `imu_mag`;
      CompassTag record-set groups likewise) instead of the hard-coded ids in
      `linkedAxisGroupId()`.
    - Each derived magnitude declares the group of its components when it is
      registered, so `|a|` shares the accel axis.
    - A stream with no group keeps its own axis, as now.
  - *Axis policy* (`buildPlotSpec()`):
    - one `PlotAxisSpec` per visible group;
    - traces in a group point at it, and its range covers every visible member;
    - the axis label is the group name and units ("Acceleration, mg");
    - a group's axis side, and any fixed or custom range, is set per group, not
      per stream. Preferences migrate: a per-stream side or range on a member
      becomes the group's, with the first member winning.
  - *Legend:* show which coloured trace is which member.
    - Either a compact legend per group beside its axis (QCustomPlot legend
      items grouped by axis), or member names in the axis colour on the axis
      label, e.g. "Acceleration, mg: x y z |a|".
    - Pick one with a mock-up first.
    - The legend belongs in `PlotSpec` (traces already carry name and colour)
      so a webview renderer gets it too.
  - *Colours:* members of a group need distinct, consistent colours, for
    example x/y/z/|a| in a fixed order. Today's per-stream defaults in
    `defaultDisplayForStream()` were chosen for separate axes.
  - *Checks:*
    - extend `plotspec_check` with group assignment, the shared range,
      ungrouped streams, and preference migration;
    - refresh the screenshot suites, where only the axis and legend layout
      should change;
    - update the user manual's sensorviz page.
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

- Move what is still Qt-bound out of the front end, so a non-Qt UI needs
  only `sensorviz_core`. View state and plot policy are already in the core
  (`ViewModel`, `buildPlotSpec()`); still in the Qt layer are:
  - the transforms (`transforms.cpp`, `compass_transforms.cpp`): the
    magnitudes, altitude, activity filter and compass orientation, and their
    parameter dialogs; this is the transform registry below;
  - loading (`sqlite_loader.cpp`): `SensorLog`, record sets, event markers,
    collection start and calibration constants, still `QString`/`QMap`/
    `QVector` types, and the calibration correction of the IMU axes;
  - the hover readout's formatting (`streamValueAt()`, `formatPlotTime()`)
    and the compass panel;
  - preference file I/O (`QJsonDocument`), which converts to and from the
    core's `TagPreferences`;
  - menus and the PlotView renderer, which are the front end by design.
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
