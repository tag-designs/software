# AGENTS.md

This file gives coding agents a quick orientation to the software repository.
It is intentionally shorter than the READMEs: use it for working rules and
where-to-look guidance, then read the local README for details.

## Repository Shape

- `host/`: desktop host tools, command-line utilities, Qt applications, shared
  host libraries, and MkDocs user documentation.
- `embedded/`: ChibiOS firmware targets for tags and base/programmer boards.
- `proto/`: shared protobuf definitions used by host tools and embedded nanopb
  generation.
- `cmake/`: shared CMake helpers, presets support, vcpkg triplets, and package
  helpers.
- `ChibiOS/`: ChibiOS submodule. Do not edit it as project source.
- `archive/` directories contain retired or reference code. Ignore archived
  code when searching, refactoring, building, or reviewing unless the task
  explicitly asks about an archive.

## General Working Rules

- Prefer small, focused changes that match the existing directory ownership.
- Use `rg` for searches.
- Do not commit generated build products, package outputs, or local build trees.
- Do not edit `ChibiOS/` unless the task is explicitly about submodule
  management.
- Keep shared behavior in the lowest appropriate layer:
  - protobuf schema in `proto/`;
  - host protocol/log/download code in `host/libraries/tagcore`;
  - host sensor math in `host/libraries/sensoranalysis`;
  - reusable Qt/QML widgets in `host/libraries/sensorui`;
  - firmware board pin/signal ownership in `embedded/boards`;
  - firmware runtime behavior in `embedded/tags` or `embedded/bases`.
- Preserve user changes in the worktree. If unrelated files are dirty, leave
  them alone.
- Update nearby documentation when changing architecture, build behavior,
  packaging, or user-visible workflows.

## Build Orientation

The top-level CMake options are:

- `BUILD_HOST`: build host libraries/tools.
- `BUILD_QT_APPS`: build Qt GUI applications.
- `BUILD_EMBEDDED`: build embedded firmware targets.
- `BUILD_HOST_DOCS`: include MkDocs output in the default/package build.

Prefer focused verification:

```sh
cmake --build <build-dir> --target sensorviz
cmake --build <build-dir> --target qtmonitor
cmake --build <build-dir> --target docs
cmake --build <build-dir> --target PresTag
```

Use the target that matches the files changed. For documentation-only changes,
`git diff --check` is often enough unless CMake/docs build files changed.

## Cross-Cutting Notes

- Host SQLite logs are written by `host/libraries/tagcore` and viewed primarily
  by `host/applications/sensorviz`.
- SQLite log schema metadata should describe data: table names, columns, stream
  ids, labels, and units. Viewer policy such as colors, initial visibility, and
  axis side belongs in sensorViz display preferences.
- Protocol changes in `proto/` can affect host code, embedded nanopb code, and
  stored/default configuration JSON. Scope those changes carefully.
- If CMake source lists change for embedded firmware, update the relevant
  `BUILD_SOURCES.md` under `embedded/tags` or `embedded/bases`.
