---
type: decision
status: accepted
summary: In the macOS package the command line tools are plain executables in tag_tools/bin rather than app bundles; dataprocessing, which links Qt, stays a bundle with a link in bin, and the release checks fail any tool that links a library the package does not carry.
---

# 0028. Host: macOS command line tools ship as plain executables

Date: 2026-10-13

Amends what [0024](0024-release-macos-package-built-on-ci-signed-locally.md)
and [0025](0025-release-macos-package-notarized-locally.md) sign and notarize:
the tools in `tag_tools/bin` as well as the bundles. Everything else in both
still stands.

## Context

Up to v3.2 every command line tool was built `MACOSX_BUNDLE`, so the DMG held
`tag-info.app`, `tag-dwnld.app` and so on. A user ran one as
`tag_tools/tag-info.app/Contents/MacOS/tag-info`, which nothing in the user
documentation said; the build tree had grown a post-build symlink
(`host_cli_bundle_link`) so that developers did not have to.

The bundles carried nothing. In the v3.2 image each `tag-*` bundle held its
`Info.plist`, its signature and the executable, and the executable linked only
`/usr/lib` and `/System` libraries: vcpkg links protobuf, libusb, SQLite and
Abseil statically. A bundle earns its place by carrying frameworks, plugins
and resources; these had none.

`dataprocessing` is different. It links QtCore and QtGui through
`sensoranalysis`, Qt is dynamic on macOS, and `macdeployqt` deploys Qt only
into a bundle. Its v3.2 bundle is 35 MB, almost all Qt.

## Decision

- The seven shipped `tag-*` tools, and the developer tools built beside them,
  are plain executables on macOS. The package installs the shipped ones to
  `tag_tools/bin`, inside the folder the user drags out, so the DMG keeps one
  top-level folder and the tools one directory to put on `PATH`.
- `dataprocessing` stays a bundle in `tag_tools`, deployed as before, and the
  package adds `tag_tools/bin/dataprocessing`, a relative link to the
  executable inside it. dyld resolves `@executable_path` through the link, so
  the bundle's frameworks are found.
- Signing stays one implementation. `cmake/MacosCodesignBundle.cmake` gains
  `macos_codesign_executable()`, with the same hardened-runtime and timestamp
  rules as bundles; `install_macos_codesign_executable()` calls it at install
  time and `host/tools/sign-ci-macos.sh` calls it in script mode on CI's
  tools.
- `host/tools/verify-macos-dmg.sh` checks every tool in `bin`: signature,
  authority, notarization (`codesign -R=notarized --check-notarization`, since
  `spctl` rejects any bare executable as "not an app"), that the link
  resolves, and that a tool links nothing outside `/usr/lib` and `/System`.

## Verification

On 2026-10-13, from a local Developer ID build of `a66ec44e` plus this change:

- The DMG held six bundles and `tag_tools/bin` with seven tools and the
  `dataprocessing` link. Copied out, `tag-info --help` and `dataprocessing
  --help` ran, the latter through the link and from `PATH`.
- Apple accepted the image (submission `5f0a31cf`), the ticket stapled, and
  `verify-macos-dmg.sh --notarized` passed every bundle and every tool.
- Given an image with a tool linking Qt and a link into a missing bundle,
  `verify-macos-dmg.sh` failed both and exited 1.

The CI path was then exercised on `release.yml` run 38489181249, a manual
build of `667b8d8f`: `sign-ci-macos.sh --run 38489181249 --no-upload`
re-signed it, Apple accepted it (submission `a046b27f`), and the same checks
passed every bundle, tool and link.

## Consequences

- A tool that gains a dynamic dependency fails the release check rather than
  failing on a user's Mac. Making it work then means linking the dependency
  statically or making that tool a bundle like `dataprocessing`.
- A bare executable cannot hold a stapled ticket. Notarizing the image
  registers each tool's code hash, and Gatekeeper looks it up online the first
  time a quarantined copy runs, so the first run of each tool needs the
  network. The apps are unaffected; their tickets are stapled.
- Anyone who scripted the v3.2-and-earlier bundle paths has to change them to
  `tag_tools/bin/<tool>`. The README says so.
- Existing build trees keep stale `<tool>.app` directories beside the new
  executables in `<build>/<config>/bin`. They are inert; the linker replaces
  the old `bin/<tool>` link with the real executable.
- Windows and Linux are unchanged: `MACOSX_BUNDLE` was ignored there.
