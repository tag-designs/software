# SensorViz Documentation Screenshot Capture Plan

## Purpose

SensorViz documentation needs real screenshots of data-dependent views,
menus, dialogs, and plot customizations. Unlike simpler host applications,
SensorViz exposes much of its workflow through top-level menus, plot context
menus, and modal dialogs that change according to the loaded SQLite log. The
documentation pipeline therefore needs representative sample logs plus
maintainer-only UI automation hooks that can load those logs, put the viewer in
known display states, and capture the same widgets users interact with.

This plan covers the fixture data, application hooks, screenshot inventory, and
validation workflow needed to replace the current SensorViz placeholder images
in the user guide.

## Goals

- Capture deterministic SensorViz screenshots from representative SQLite logs.
- Document top-level menus, plot context menus, and data-dependent dialogs.
- Show stream visibility, colors, axis side placement, y-axis ranges, and
  saved preference behavior.
- Show tag-specific views for IMUTag, CompassTag, and BitPresTag.
- Keep screenshot automation as a maintainer path that does not affect normal
  interactive SensorViz behavior.

## Non-Goals

- Do not invent static mock screenshots. Captures should come from the real
  SensorViz UI and real SQLite logs.
- Do not capture live hardware directly. SensorViz consumes downloaded SQLite
  logs, so its fixtures should be curated `.db3` files.
- Do not save analysis/session parameters such as y-axis ranges, sea-level
  pressure, declination, UTC offset, or battery direction as preferences.
  Those remain session settings.
- Do not repeat the same main, File Info, and Error/Help screenshots for every
  tag when one representative screenshot explains the shared workflow.

## Fixture Data

Store sample data under:

```text
host/docs/fixtures/sensorviz/
```

Recommended files:

```text
host/docs/fixtures/sensorviz/imutag.db3
host/docs/fixtures/sensorviz/compasstag.db3
host/docs/fixtures/sensorviz/bitprestag.db3
host/docs/fixtures/sensorviz/preferences-demo.json
host/docs/fixtures/sensorviz/README.md
```

The fixture README should record how each `.db3` file was produced, the tag
type, firmware/build identity when available, a short description of the data,
and the documentation screenshots that depend on it.

### Required Logs

#### IMUTag

Use IMUTag because it demonstrates the highest stream variety and elapsed-time
plotting. The log should include:

- pressure and pressure-sensor temperature when available;
- accelerometer x/y/z;
- gyroscope x/y/z;
- magnetometer x/y/z;
- enough samples for derived magnitude streams to be visually meaningful;
- elapsed-time metadata, including collection start time when available.

Screenshots from this log should show the elapsed x-axis, generated IMU
magnitudes, range controls for visible streams, and axis-side customization.

#### CompassTag

Use CompassTag because it exercises SensorViz features no other tag has:

- raw compass record set;
- compass calibration constants;
- derived heading/orientation streams;
- declination and battery-forward controls;
- QML orientation panel beside the plot.

Screenshots from this log should show the compass/orientation panel,
calibration constants dialog, declination menu text, battery-forward toggle,
and context menu entries that appear only when compass data is available.

#### BitPresTag

Use BitPresTag because it combines pressure and activity data:

- pressure stream;
- pressure-sensor temperature if present;
- activity stream;
- voltage/core temperature if present;
- sufficient duration to make altitude and low-pass activity meaningful.

Screenshots from this log should show pressure, altitude, activity, activity
filter, sea-level pressure, linked range behavior, and preference-driven
stream display customization.

## Application Hooks

Add maintainer-only command-line options to SensorViz:

```text
--load-log <path>
--load-preferences <path>
--capture-screenshot <name>
--capture-suite <startup|menus|dialogs|imutag|compasstag|bitprestag|all>
--screenshot-dir <dir>
--no-user-prompts
```

Expected behavior:

- With no capture options, SensorViz behavior is unchanged.
- `--load-log` loads a SQLite fixture without opening `File > Load`.
- `--load-preferences` applies a JSON preference file after the log has loaded.
- `--capture-screenshot` captures one named screen and exits.
- `--capture-suite` runs a curated set of named captures and exits.
- `--screenshot-dir` defaults to `host/docs/src/images`.
- `--no-user-prompts` makes automation fail with diagnostics instead of
  opening blocking file dialogs or warning dialogs.

Loading is currently asynchronous through `QFutureWatcher`. The capture path
must wait until `applyLoadedLog()` has completed, menus have been refreshed,
and the plot has rendered before grabbing images.

## UI State Helpers

Add private helpers that are compiled into SensorViz but used only by capture
options:

```text
loadLogForDocumentation(path)
applyDocumentationPreferences(path)
selectDocumentationTab(plot|file-info)
setDocumentationVisibleStreams(ids)
setDocumentationAxisSide(stream_id, left|right)
setDocumentationStreamColor(stream_id, color)
setDocumentationRange(stream_id, lower, upper)
setDocumentationGraphTitle(text, visible)
setDocumentationSeaLevelPressure(mbar)
setDocumentationActivityFilter(seconds, enabled)
setDocumentationDeclination(degrees)
setDocumentationBatteryForward(enabled)
captureMainWindow(name)
captureOpenMenu(name, menu_path)
captureContextMenu(name, plot_position)
captureDialog(name, dialog_kind, setup)
```

Where possible, use the same `QAction` objects as the live UI. SensorViz already
centralizes most menu state in persistent actions, which is a useful contract:
documentation setup should change those action states rather than duplicate UI
policy.

## Menu Capture Strategy

SensorViz has two menu surfaces:

- top-level menu bar menus;
- a plot context menu built dynamically in `showPlotContextMenu()`.

Both should be documented because users are likely to work directly in the
plot. Capture menus with explicit helper functions rather than external mouse
automation:

1. Load the fixture log that makes the desired data-dependent actions visible.
2. Apply any setup state, such as enabling the activity filter.
3. Open the menu at a deterministic screen position.
4. Let Qt process paint/layout events.
5. Grab the native frame or screen region containing the open menu.
6. Close the menu and continue to the next capture.

Required menu images:

```text
sensorviz-file-menu.png
sensorviz-preferences-menu.png
sensorviz-view-menu.png
sensorviz-ranges-menu.png
sensorviz-configuration-menu.png
sensorviz-help-menu.png
sensorviz-popup-menu.png
```

Use BitPresTag for File/View/Ranges/Preferences examples because it shows
pressure, altitude, activity, and activity filter controls without the extra
CompassTag panel. Use CompassTag for Configuration and popup examples when the
goal is to show declination, battery-forward, or calibration constants.

## Dialog Capture Strategy

Several SensorViz dialogs are currently created inside slots and shown with
blocking `exec()` or static `QInputDialog` helpers. Documentation capture
should factor dialog construction into reusable helpers or add capture-only
entry points that show the dialog modelessly, grab it, and close it.

Required dialog images:

```text
sensorviz-visible-streams-dialog.png
sensorviz-axis-sides-dialog.png
sensorviz-colors-dialog.png
sensorviz-range-dialog.png
sensorviz-graph-title-dialog.png
sensorviz-utc-offset-dialog.png
sensorviz-sea-level-pressure-dialog.png
sensorviz-calibration-constants-dialog.png
sensorviz-print-preview.png
```

Dialog setup should prefer real loaded data:

- Visible Streams: IMUTag or CompassTag, because the list is rich.
- Axis Sides: BitPresTag with pressure/altitude/activity visible.
- Colors: BitPresTag or IMUTag with several visible streams.
- Range: pressure or activity from BitPresTag.
- Graph Title: any loaded log.
- UTC Offset: BitPresTag or CompassTag, not elapsed-time IMUTag.
- Sea-level Pressure: BitPresTag or IMUTag when pressure is present.
- Calibration Constants: CompassTag.
- Print Preview: a representative BitPresTag or CompassTag plot.

## Screenshot Inventory

### Shared Workflow

```text
sensorviz-startup.png
sensorviz-main-window.png
sensorviz-file-info.png
sensorviz-file-menu.png
sensorviz-preferences-menu.png
sensorviz-help-menu.png
sensorviz-popup-menu.png
sensorviz-cursors.png
sensorviz-print-preview.png
```

### Display Customization

```text
sensorviz-visible-streams-dialog.png
sensorviz-axis-sides-dialog.png
sensorviz-colors-dialog.png
sensorviz-ranges-menu.png
sensorviz-range-dialog.png
sensorviz-customized-plot.png
sensorviz-preferences-demo.png
```

This sequence should demonstrate the lifecycle of user display customization:
choose visible streams, assign axes, choose colors, set a range, view the
resulting plot, then store/load preferences.

### Configuration And Derived Views

```text
sensorviz-configuration-menu.png
sensorviz-graph-title-dialog.png
sensorviz-utc-offset-dialog.png
sensorviz-sea-level-pressure-dialog.png
sensorviz-derived-views.png
```

### Tag-Specific Examples

```text
sensorviz-bitprestag-plot.png
sensorviz-bitprestag-file-info.png
sensorviz-imutag-plot.png
sensorviz-imutag-file-info.png
sensorviz-compasstag-plot.png
sensorviz-compass-view.png
sensorviz-calibration-constants-dialog.png
```

## Preferences Fixture

Add a small preferences JSON file for documentation customization examples.
It should include only data that SensorViz preferences intentionally persist:

```json
{
  "version": 1,
  "tags": {
    "BITPRESTAG": {
      "visible_streams": ["pressure", "altitude", "activity"],
      "colors": {
        "pressure": "#1f77b4",
        "altitude": "#6e46b4",
        "activity": "#2ca02c"
      },
      "axis_sides": {
        "pressure": "left",
        "altitude": "left",
        "activity": "right"
      }
    }
  }
}
```

Do not store manual y-axis ranges, sea-level pressure, declination, UTC offset,
battery-forward state, cursor positions, or metadata-box position in this file.
Those are session or analysis settings.

## User Guide Organization

Keep `host/docs/src/apps/sensorviz.md` user-focused:

1. Purpose and supported SQLite log files.
2. Opening a file and reading the File Info tab.
3. Plot basics: visible streams, axes, ranges, cursors, zoom.
4. Menus and plot context menu.
5. Display customization and preferences.
6. Derived views and configuration parameters.
7. Tag-specific examples:
   - IMUTag for high-rate IMU streams and elapsed time;
   - CompassTag for orientation panel and calibration;
   - BitPresTag for pressure/activity/altitude.
8. Troubleshooting.

Keep capture commands, fixture requirements, and automation details in
developer/maintainer docs, not in the user narrative.

## Implementation Plan

1. Add `host/docs/fixtures/sensorviz/README.md` and collect the three sample
   `.db3` logs.
2. Add `MainWindowOptions` or equivalent startup options to SensorViz.
3. Add `--load-log`, `--load-preferences`, `--capture-screenshot`,
   `--capture-suite`, `--screenshot-dir`, and `--no-user-prompts`.
4. Refactor asynchronous loading so capture code can be notified when
   `applyLoadedLog()` and the first plot render are complete.
5. Add capture helpers for the main window and selected tabs.
6. Add top-level menu and context-menu capture helpers.
7. Refactor blocking dialogs into captureable dialog builders or add
   capture-only wrappers.
8. Add deterministic setup helpers for visible streams, axis sides, colors,
   ranges, title, sea-level pressure, activity filter, declination, and
   battery direction.
9. Generate screenshots into `host/docs/src/images`.
10. Replace placeholder images in `host/docs/src/apps/sensorviz.md`.
11. Document maintainer commands in `host/applications/sensorviz/README.md`
    and the fixture README.

## Validation

Run focused checks:

```sh
cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target sensorviz
cmake --build /Users/geobrown/Build/tag-designs/software/build-host --target docs
git diff --check
```

Visual review should confirm:

- menus are not clipped and show the expected data-dependent entries;
- dialogs contain realistic stream names and current values;
- CompassTag captures include the orientation panel when expected;
- IMUTag captures show elapsed-time labeling;
- BitPresTag captures show pressure/activity/altitude relationships;
- preference captures demonstrate visibility, colors, and axis sides only;
- screenshots do not expose unintended local paths or private data beyond the
  curated fixture metadata.

## Open Questions

- Should screenshot capture include mouse tooltip images, or are those too
  transient for stable documentation?
- Should print preview be captured from the platform dialog, or should the
  documentation use a rendered plot export instead?
- Should a future inventory file list all SensorViz screenshot fixtures and
  capture names, similar to the planned `qtmonitor` inventory?
- Should sample logs be hand-curated small files or generated from raw captured
  logs by a reduction tool that preserves stream metadata and representative
  data ranges?
