---
type: decision
status: accepted
summary: dataprocessing no longer uses Qt -- it runs on the Qt-free sensoranalysis core, bit-identical to the Qt code -- so it ships as a plain executable in tag_tools/bin like the other command line tools, and builds in a host build without the Qt applications.
---

# 0031. Host: dataprocessing is a plain executable without Qt

Date: 2026-10-20

Amends [0028](0028-host-macos-command-line-tools-plain-executables.md): its
exception for `dataprocessing`, which stayed an app bundle with a link in
`tag_tools/bin` because it linked Qt, no longer applies. Everything else in
0028 stands.

## Context

`dataprocessing` linked QtCore and QtGui: QtCore for its command line, JSON,
hashing and file handling, QtGui through `sensoranalysis`, whose compass math
was written on `QVector3D` and `QQuaternion`. On macOS that made it the one
command line tool shipped as a 35 MB bundle, almost all Qt; on Linux and
Windows it was deployed as a Qt-runtime target; and it could not be built
without the Qt applications.

The obstacle to dropping Qt was the compass math. Euler angles were the
convention-dependent part, and [0030](0030-host-orientation-stored-as-quaternion-only.md)
moved them out to the displays. The remaining vector and quaternion code moved
onto `sensoranalysis_core`, a Qt-free target whose float types reproduce
QVector3D and QQuaternion expression for expression, with the Qt classes kept
as adapters over it, and stored calibration gained a Qt-free reader
(`calibrationFromJson()`).

## Decision

- `dataprocessing` uses no Qt. It links `sensoranalysis_core_json` (the core
  plus protobuf), SQLite and `tag_monitor_interface`, and compiles in two
  self-contained files from tagcore by path -- the vendored cxxopts header and
  `recovery/sha256.cc` -- rather than linking tagcore and libusb.
- It is a plain executable. `host_cli_bundle_link()` is gone, it is no longer
  a Qt-runtime install target, and the package installs and signs it like the
  other tools in `tag_tools/bin`.
- `sensoranalysis` is always added to the build; its Qt library and checks are
  built only with `BUILD_QT_APPS`, its core always.
- Its output, provenance JSON format and command line are unchanged. Help is
  now cxxopts's, and an unknown option exits 2, like the tool's other usage
  errors, rather than Qt's 1.

## Alternatives considered

- **Keep Qt Core for the plumbing and drop only Qt Gui.** Still a Qt-runtime
  target and a bundle on macOS, for the sake of a command line parser, a JSON
  writer and a hash.
- **A GUI application instead.** Raised earlier as better for users who avoid
  the command line. Not ruled out: a GUI would call the same core. This
  decision keeps the command line tool usable without Qt either way.

## Evidence

On 2026-10-20, the Qt build of `883578cd` and this build, run on the same
inputs -- a CompassTag log with the compass-calibrated and compass-orientation
processors, the compasstag fixture with both, and an IMUTag log with
imu-compass-orientation:

- Every output database was identical by `.dump` with the ProcessingRun
  timestamp masked, provenance JSON and input hash included, and every REAL
  column compared equal in SQL: 50 calibrated rows, 50 orientation rows and
  1005 IMUTag orientation rows.
- Eighteen other invocations -- list, describe, dry run, every usage error, a
  missing input, the same path for input and output, and `--if-exists`
  fail/keep against an existing output file and existing output tables --
  produced the same output and exit status.
- The executable links only CoreFoundation, libc++ and libSystem.
- A configuration with `BUILD_QT_APPS=OFF` built `dataprocessing` from the
  core, the JSON reader and `sha256.cc` alone.

## Consequences

- One fewer bundle on macOS; the release check now expects five app lines and
  eight tool lines, with no link
  ([release procedure](../release/release-procedure.md)).
- A host-only macOS configuration still needs Qt on the search path, because
  `cmake/DeployQt.cmake` -- which also defines the signing helper for plain
  executables -- requires Qt unconditionally. That predates this change.
- On Windows, as for the other tools, a path given on the command line must
  be representable in the system code page; Qt's command line handled any
  Unicode path.
