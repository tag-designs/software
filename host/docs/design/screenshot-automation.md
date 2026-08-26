# Host User Guide Screenshot Automation

## Purpose

This document describes a repeatable workflow for generating host application
screenshots and annotated documentation images. It uses `qtcalibrate` as the
pilot application because its user guide is currently thin, its workflow is
visual, and its Qt widget tree already exposes stable object names for most
documentation targets.

The design is intended to drive a later implementation plan. It does not change
application behavior by itself.

## Problem

Host application screenshots are currently captured and annotated by hand. That
has several costs:

- screenshots drift as UI controls, labels, and workflows change;
- hardware-dependent states are difficult to reproduce;
- annotations must be manually redrawn when layout changes;
- the same work must be repeated for every host application;
- documentation reviews cannot easily verify that images match the current UI.

The documentation pipeline needs a deterministic way to put each application
into representative states, capture stable images, and generate annotations from
the real UI geometry.

## Goals

- Generate key screenshots for `qtcalibrate` without manual screen capture.
- Use realistic sample data while avoiding private device identifiers, serial
  numbers, or local paths.
- Support both clean screenshots and annotated derivatives.
- Keep screenshot definitions close to the application or host documentation
  tree so they can be reviewed with UI changes.
- Make the pilot reusable for other Qt host tools such as `qtmonitor`,
  `qtprogram`, `sensorviz`, `compviz`, and `btviz`.
- Allow focused local execution through CMake.

## Non-Goals

- Replacing ordinary GUI tests or adding full workflow coverage.
- Simulating USB transport with bit-for-bit fidelity.
- Generating prose documentation automatically.
- Requiring physical tag hardware for the documentation image set.
- Changing `qtcalibrate` runtime behavior in normal user mode.

## Current State

The user-facing Qt Calibrate guide lives at
`host/docs/src/apps/qtcalibrate.md` and currently contains a single screenshot
placeholder.

The application lives at `host/applications/qtcalibrate/`. Its main widget file
already gives useful object names to the controls that should appear in the
guide, including:

- `tabWidget`, `calTab`, `orientationTab`, and `logTab`;
- `graphWidget` for the magnetometer collection view;
- `qualityLabel` and calibration constant labels;
- `attachButton`, `detachButton`, `streamCheckBox`;
- `startButton`, `stopButton`, `clearButton`, `saveButton`, `loadButton`;
- `logTextEdit`, `logsaveButton`, `logclearButton`, `loglevelBox`;
- `tagWidget` and `attitudeWidget` for the shared QML orientation displays.

The main implementation currently calls `Attach()` during `MainWindow`
construction. When no compatible base is present, the application raises a
modal warning. This is correct for a user session but blocks deterministic
documentation capture unless the application has a hardware-free documentation
mode.

## Proposed Architecture

Add a Qt-native documentation screenshot pipeline with three layers:

1. A documentation/demo state provider for each application.
2. A screenshot runner that drives the application into named states.
3. An annotation renderer that draws callouts using Qt object geometry.

For `qtcalibrate`, the pilot should add a hardware-free documentation mode that
skips automatic USB attachment, enables representative UI states, and feeds
synthetic calibration/orientation data into the same display widgets used by
normal runtime code.

The first implementation may live as a dedicated executable target,
`qtcalibrate_docshots`, rather than as a user-visible mode in the app bundle.
That keeps production launch behavior unchanged and avoids exposing
documentation-only command-line flags to users. If sharing the normal binary is
more practical, the equivalent command-line shape should be hidden and explicit:

```sh
qtcalibrate --doc-mode --doc-screenshots host/docs/src/images
```

## State Provider

The state provider should be small and application-owned. For `qtcalibrate`,
it should provide methods equivalent to:

- initialize as disconnected;
- initialize as attached and idle;
- enable streaming;
- start active calibration collection;
- stop collection and show completed calibration constants;
- switch to the Orientation tab with representative compass and attitude data;
- switch to the Log tab with representative informational and warning text.

The provider should avoid mocking the entire `Tag` API unless the later
implementation needs that seam for broader tests. The documentation objective is
to set visible UI state, not to validate USB behavior.

Synthetic sample data should be deterministic. A fixed seed or checked-in data
fixture is preferred so screenshots are byte-stable except when rendering
changes. The magnetometer samples should form a plausible sphere with realistic
hard-iron offset, soft-iron distortion, and enough coverage gaps to make the
quality metrics meaningful.

Captured fixtures should be preferred for final documentation screenshots once
the capture path exists. `qtcalibrate` sample capture starts when the user
presses **Start** and ends when collection stops. The fixture should contain the
calibration-window stream only: received magnetometer samples, paired
accelerometer samples when present, capture timestamps, and fitted calibration
summary data. Orientation-window replay can be added as a separate fixture path.

The replay path should be runnable without a USB tag:

```sh
qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json
```

Milestone screenshots should use the same fixture and freeze the Calibrate tab
after feeding a fixed percentage of samples:

```sh
qtcalibrate --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json --replay-percent 25
```

The initial documentation set should include the start/ready state, 0 percent
collection, 25 percent collection, 50 percent collection, and 100 percent
results.

The app-level capture mode should generate the replay milestone images without
external desktop screenshot tools:

```sh
qtcalibrate --capture-startup-screenshot

qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json \
  --capture-replay-screenshots

qtcalibrate \
  --replay-capture host/docs/fixtures/qtcalibrate/good-sphere-v1.json \
  --capture-orientation-screenshot
```

The default output directory is `host/docs/src/images/`. Orientation
screenshots should use a fixed documentation pose by default rather than the
final pose from the replay fixture; use `--orientation-pose` when the
illustration needs a specific heading, pitch, or roll.

## Screenshot Runner

The runner should use Qt mechanisms instead of desktop screenshot tools:

- `QTest` or direct Qt calls to select tabs and invoke controls;
- `QObject::findChild()` with stable object names for locating targets;
- `QWidget::grab()` for whole-window or panel captures;
- fixed window geometry for repeatability;
- a fixed light theme and font policy where possible.

The runner should write images under `host/docs/src/images/` using the existing
user-guide naming convention. The first `qtcalibrate` image set should be:

| File | State | Purpose |
| --- | --- | --- |
| `qtcalibrate-startup.png` | App open, disconnected | Introduce the window and connection area. |
| `qtcalibrate-attached.png` | Attached and idle | Show the ready state before streaming. |
| `qtcalibrate-streaming.png` | Stream enabled | Show the controls available during live sample polling. |
| `qtcalibrate-collection-000.png` | Replay milestone 0 percent | Show collection just after pressing Start. |
| `qtcalibrate-collection-025.png` | Replay milestone 25 percent | Show early sphere coverage. |
| `qtcalibrate-collection-050.png` | Replay milestone 50 percent | Show partial sphere coverage and intermediate fit. |
| `qtcalibrate-collection.png` | Active calibration | Show the magnetometer point cloud and quality metrics. |
| `qtcalibrate-collection-100.png` | Replay milestone 100 percent | Show completed replayed collection. |
| `qtcalibrate-results.png` | Collection stopped | Show calibration constants and completed fit quality. |
| `qtcalibrate-orientation.png` | Orientation tab | Show compass and attitude preview after calibration. |
| `qtcalibrate-log.png` | Log tab | Show log level, save/clear controls, and example messages. |
| `qtcalibrate-file-menu.png` | File menu open | Show file actions if the guide documents them. |

The first pass should prioritize the collection, results, orientation, and log
screens because those carry the most explanatory value.

## Annotation Manifest

Annotations should be data-driven. A manifest file should identify the source
image state, target widget object names, labels, and optional placement hints.

Example:

```yaml
screens:
  - image: qtcalibrate-collection.png
    state: collection
    annotations:
      - target: graphWidget
        label: Magnetometer sample cloud
      - target: qualityLabel
        label: Coverage and fit quality
      - target: stopButton
        label: Stop collection when coverage is good
```

The annotation renderer should resolve each `target` with `findChild()`, map
the widget rectangle into screenshot coordinates, and draw a consistent callout
onto a copy of the clean screenshot. The renderer should preserve clean images
and write annotated derivatives with a stable suffix, for example:

- `qtcalibrate-collection.png`;
- `qtcalibrate-collection-annotated.png`.

This keeps guide authors free to choose whether a page needs a clean or
annotated figure.

## Documentation Integration

The generated images should support a rewrite of
`host/docs/src/apps/qtcalibrate.md` around the actual workflow:

1. Before You Start.
2. Connect To A Tag.
3. Stream Calibration Samples.
4. Collect A Full Magnetometer Sphere.
5. Check Fit Quality.
6. Save Or Read Calibration Constants.
7. Verify Orientation.
8. Troubleshooting.

The guide should reference generated images with ordinary Markdown paths, not
special build-time substitutions. Image generation should be an explicit
developer task, while MkDocs should continue to consume already-generated
checked-in images.

## CMake Integration

Add a focused CMake target for the pilot:

```sh
cmake --build <build-dir> --target qtcalibrate_docshots
```

The target should:

- build the app and screenshot runner;
- create the output directory if needed;
- run the screenshot runner with deterministic settings;
- fail if required screenshots cannot be produced;
- leave generated images in `host/docs/src/images/` for review.

The target should not be part of the default build. It is a maintainer tool,
not a package-build requirement.

## Headless And CI Notes

Pure offscreen Qt rendering may be enough for widget-only screenshots, but
`qtcalibrate` embeds `QQuickWidget` compass and attitude views. Those are more
sensitive to graphics backend differences.

The implementation should test these modes in order:

1. Native local rendering for developer updates.
2. Linux CI under Xvfb with a software Qt Quick backend.
3. `QT_QPA_PLATFORM=offscreen` only if the `QQuickWidget` captures are proven
   nonblank and stable.

The runner should include a simple blank-image guard for QML-heavy captures so
CI fails when the orientation widgets render as empty white or black panels.

## Review And Stability Rules

- Screenshot updates should be reviewed like source changes.
- Fixture data should be deterministic and free of private identifiers.
- The clean screenshot should always be generated before annotations.
- Object names used by annotation manifests become documentation contracts.
  Renaming those widgets should update the manifest in the same change.
- Documentation-only screenshot refreshes should not change application logic.

## Risks

| Risk | Mitigation |
| --- | --- |
| Demo state drifts from real tag behavior. | Keep demo state limited to visible UI state and use application-owned methods where practical. |
| QML orientation views render inconsistently in CI. | Use native or Xvfb rendering, software Qt Quick backend, and blank-image checks. |
| Annotations become visually noisy. | Generate clean and annotated variants; use annotations only where they clarify workflow. |
| Widget renames break screenshots. | Treat object names in manifests as reviewed contracts. |
| The pilot becomes too `qtcalibrate`-specific. | Keep runner and manifest concepts generic, with only the state provider app-specific. |

## Implementation Phases

### Phase 1: Pilot Design And Harness

- Add a `qtcalibrate_docshots` target.
- Add a documentation state provider for `qtcalibrate`.
- Capture clean screenshots for collection, results, orientation, and log tabs.
- Add deterministic sample data or replay a captured calibration sample
  fixture.

### Phase 2: Annotation Renderer

- Add a manifest for `qtcalibrate` annotations.
- Render annotated variants from real widget geometry.
- Add blank-image and missing-target validation.

### Phase 3: User Guide Rewrite

- Rewrite `host/docs/src/apps/qtcalibrate.md` around the generated image set.
- Add troubleshooting guidance for noisy data, failed fits, orientation
  mistakes, and connection loss.
- Build the host user guide with the generated images.

### Phase 4: Generalize

- Extract shared runner or manifest utilities if a second application needs
  them.
- Pilot the same pattern on `qtmonitor` or `qtprogram`.
- Decide whether screenshot targets should remain per-app or move into a shared
  host documentation tooling directory.

## Open Questions

- Should generated images be checked into the repository, or produced only in
  release/documentation jobs?
- Should the documentation state provider be compiled into the production
  application behind an internal flag, or live only in a separate docshots
  executable?
- Should annotations use a YAML manifest, JSON, or a small C++/Qt resource file
  to avoid adding another parser dependency?
- What minimum set of screenshots should block documentation review for a UI
  change?
- Should the pipeline eventually support animated captures for workflows that
  are hard to explain in still images?
