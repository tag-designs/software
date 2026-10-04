---
type: readme
status: current
summary: The end-user host manual - how to preview, build and package it, how its sources are laid out, and how its screenshots are generated from fixtures.
---

# Host User Guide

This directory holds the **Ultralight Tags User Guide**, the end-user manual for
the host applications and command-line tools. It is an MkDocs Material site
(`mkdocs.yml`, sources in `src/`) that is built by the CMake `docs` target and
installed with the host packages. It lives under `host/` because it ships with
the host tools; developer documentation is separate (see
[docs/documentation-guide.md](../../docs/documentation-guide.md)) and does not go
here.

## Preview Locally

Install the documentation dependencies:

```sh
python3 -m pip install -r host/docs/requirements.txt
```

Run the local preview server:

```sh
python -m mkdocs serve -f host/docs/mkdocs.yml
```

Build static HTML:

```sh
python -m mkdocs build -f host/docs/mkdocs.yml
```

The generated site is written to `host/docs/site/`.

You can also build the same site through CMake from any top-level build
directory:

```sh
cmake --build build --target docs
```

With Makefile generators, this is equivalent to running `make docs` from the
build directory. The generated CMake-built site is written under the build tree
at `host/docs/site/`.

## Package Build

The CMake `docs` target is available in top-level builds. The
`BUILD_HOST_DOCS` option controls whether that target is part of the default
build and whether generated docs are installed into packages. By default, that
option follows `BUILD_QT_APPS`.

CMake first looks for `mkdocs` on `PATH`, then falls back to `Python3 -m
mkdocs`. If your Python install works as a module but CMake cannot auto-detect
it, configure with a semicolon-separated command list:

```sh
cmake -S . -B build -DHOST_MKDOCS_COMMAND="python;-m;mkdocs"
```

Generated package docs are installed alongside the host tools:

- Windows: `tag_tools/docs`
- macOS: `tag_tools/docs`
- Linux: `share/UltralightTags/docs`

## Source Layout

| Path | Holds |
| --- | --- |
| `src/` | The manual: `apps/` (one page per Qt application), `cli/` (one page per distributed command-line tool), `workflows/`, `reference/`, plus `index.md` and `imutag-overview.md`. The sidebar is the `nav` list in `mkdocs.yml`. |
| `src/images/` | Screenshots and figures, checked in. |
| `src/reference/documentation-guidelines.md` | Writing, image placement and screenshot naming conventions. |
| `fixtures/` | Maintainer data for regenerating screenshots; not rendered into the guide. |
| `design/proposals/` | The original screenshot-automation plans, kept as history. |

## Screenshots

Screenshots of `qtmonitor`, `sensorviz` and `qtcalibrate` are mostly generated
rather than hand-captured: those three applications have maintainer-only
command-line options that load fixture data, put the window into a known state,
and write PNGs into `src/images/` (override with `--screenshot-dir`). These images
in `src/images/` are hand-made, and are redone by hand:

- every `btdataviz-*.png`, because `btdataviz` has no capture options;
- the `sensorviz` dialog and interaction images no hook produces
  (`sensorviz-cursors`, `-derived-views`, `-graph-title-dialog`,
  `-print-preview`, `-range-dialog`, `-utc-offset-dialog`,
  `-visible-streams-dialog`);
- `imutag-render.png` and `TagMonitor1-3.png`.

The guide references the images with ordinary Markdown paths; the `docs`
build only consumes the checked-in images and never runs the capture. Regenerating screenshots is an explicit maintainer task, and the
results are reviewed and committed like source.

| Application | Fixture data | Capture options documented in |
| --- | --- | --- |
| `qtmonitor` | fake-tag JSON in [`fixtures/qtmonitor/`](fixtures/qtmonitor/README.md), captured from real tags with `qtmonitor-fixture-capture` | [qtmon README, "Screenshot Commands"](../applications/qtmon/README.md#screenshot-commands) |
| `sensorviz` | SQLite logs in [`fixtures/sensorviz/`](fixtures/sensorviz/README.md) | [sensorviz README, "Documentation Capture Hooks"](../applications/sensorviz/README.md#documentation-capture-hooks) |
| `qtcalibrate` | a saved calibration sample capture in [`fixtures/qtcalibrate/`](fixtures/qtcalibrate/README.md), replayed as a fake tag | [qtcalibrate README, "Sample Replay"](../applications/qtcalibrate/README.md#sample-replay) |

There is no annotation renderer and no aggregate screenshot target; each
application is captured by running it directly. The original plans
([screenshot-automation.md](design/proposals/screenshot-automation.md),
[qtmonitor-screenshot-automation.md](design/proposals/qtmonitor-screenshot-automation.md),
and sensorviz's
[screenshot-capture-plan.md](../applications/sensorviz/design/proposals/screenshot-capture-plan.md))
are historical.
