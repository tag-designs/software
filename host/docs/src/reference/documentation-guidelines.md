# Documentation Guidelines

Use these guidelines when adding or updating pages in the host/user guide. The
guide is built from `host/docs/src/` and is intended for people using the host
tools, not for firmware or host-library implementation notes.

## Page Structure

Write task-focused pages. Start each page with what the user is trying to do,
then give the shortest successful path, then add troubleshooting and reference
details after the main workflow.

Prefer this shape for application and workflow pages:

1. A short purpose paragraph.
2. Preconditions or required hardware/software.
3. The main workflow as numbered steps.
4. Screenshots only where they clarify a decision or state.
5. Troubleshooting and reference details after the workflow.

Reference pages can be more table-oriented. Keep tables compact and put the
most commonly used fields first.

## Source Layout

Markdown source files for the published host/user guide live in
`host/docs/src/`.

| Content | Location |
| --- | --- |
| Application guides | `host/docs/src/apps/` |
| Command-line references | `host/docs/src/cli/` |
| Workflow guides | `host/docs/src/workflows/` |
| Reference pages | `host/docs/src/reference/` |
| Published guide images | `host/docs/src/images/` |

Developer design notes for the host documentation workflow live in
`host/docs/design/`. Fixture data for generated or repeatable screenshots lives
under `host/docs/fixtures/`. Design notes and fixtures are maintainer sources;
they are not ordinary end-user guide pages unless they are linked from
`host/docs/mkdocs.yml`.

## Writing Style

- Write for a user who understands the tag workflow but may not know this
  specific tool.
- Use menu paths such as **File > Load** when describing UI actions.
- Use literal command, file, option, and table names in backticks.
- Avoid implementation details unless they affect a user's decision.
- Keep placeholder text out of published pages.
- When a page describes generated data, include enough context for users to
  inspect the file with ordinary tools.

## Screenshots And Images

Store published guide images in `host/docs/src/images/`. Reference them from
Markdown with relative paths from the page location:

```md
![Qt Program main window](../images/qtprogram-main-window.png)
```

Capture only the relevant application window or panel. Use realistic sample
data when possible, but avoid personal file paths, serial numbers, real UUIDs,
or private device identifiers. Keep image width reasonable for documentation
pages.

### Image Naming

Use short, stable names that identify the app and screen:

- `qtprogram-main-window.png`
- `qtcalibrate-startup.png`
- `qtcalibrate-collection.png`
- `qtcalibrate-collection-000.png`
- `qtcalibrate-collection-025.png`
- `qtcalibrate-collection-050.png`
- `qtcalibrate-collection-100.png`
- `qtcalibrate-orientation-forward.png`
- `qtcalibrate-orientation-backward.png`
- `qtmonitor-startup.png`
- `qtmonitor-main-idle.png`
- `qtmonitor-main-running.png`
- `qtmonitor-main-finished.png`
- `qtmonitor-config-bittag-schedule.png`
- `qtmonitor-config-bittag-sensors.png`
- `qtmonitor-config-bittag-le-schedule.png`
- `qtmonitor-config-bittag-le-sensors.png`
- `qtmonitor-config-bitprestag-schedule.png`
- `qtmonitor-config-bitprestag-sensors.png`
- `qtmonitor-config-compasstag-schedule.png`
- `qtmonitor-config-imutag-schedule.png`
- `qtmonitor-config-imutag-sensors.png`
- `qtmonitor-config-prestag-schedule.png`
- `qtmonitor-error-log.png`
- `btdataviz-startup.png`
- `btdataviz-plot-view.png`
- `btdataviz-temperature.png`
- `btdataviz-zoom-week.png`
- `btdataviz-cursors.png`
- `btdataviz-filtering.png`
- `btdataviz-export.png`
- `btdataviz-actogram.png`
- `btdataviz-sun-elevation.png`
- `btdataviz-tag-info.png`
- `sensorviz-compass-view.png`

When adding a new screenshot, add a name that remains correct if minor wording
or layout changes. Prefer `app-area-state.png` over names tied to transient
implementation details.

## Preview And Checks

Preview the guide locally with MkDocs:

```sh
python -m mkdocs serve -f host/docs/mkdocs.yml
```

Build the generated site through CMake:

```sh
cmake --build build-docs --target docs
```

For documentation-only changes, also run:

```sh
git diff --check
```
