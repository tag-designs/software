---
type: decision
status: accepted
summary: The shipped macOS DMG is the one CI builds, attested by the workflow and re-signed with the Developer ID identity on a developer's Mac by the sign-latest target; the signing key still never reaches CI.
---

# 0024. Release: the macOS package is built on CI and signed locally

Date: 2026-10-06

Supersedes [0012](0012-release-macos-package-signed-off-ci.md). The reason for
0012 stands: the Developer ID key is not put into a public repository's
secrets. What changes is which build ships.

## Context

Under 0012 the macOS DMG that shipped was built entirely on the developer's
Mac by `host/tools/release-macos.sh`. CI built the same package ad-hoc signed,
as a check only. So the Windows package came from a clean, pinned CI
environment and the macOS one from whatever state the developer's machine was
in. The developer's Mac also had to hold the full vcpkg, Qt and docs toolchain
for every release.

## Decision

The macOS DMG CI builds is the one that ships, signed locally:

1. `release.yml` builds the DMG ad-hoc signed, as before, and attaches a build
   provenance attestation (`actions/attest-build-provenance`) recording the
   workflow and commit that built it.
2. On a Mac holding the identity, `cmake --build <build-dir> --target
   sign-latest` runs `host/tools/sign-ci-macos.sh`, which takes the newest
   successful tag-push run, checks the tag still names the commit it built,
   downloads the DMG, and refuses it unless the attestation verifies against
   this repository's `release.yml`, that commit and that tag.
3. It copies the image out, re-signs every bundle with
   `cmake/MacosCodesignBundle.cmake`, makes a new HFS+/UDZO image, verifies it
   with `host/tools/verify-macos-dmg.sh`, and uploads it to the draft release.
   It never changes a published release.

`MacosCodesignBundle.cmake` is the only signing code. `install_macos_codesign`
in `cmake/DeployQt.cmake` calls it for local packages, so a local build and a
re-signed CI build are signed by the same commands. `release-macos.sh` stays
as the fallback and shares the verifier.

## Alternatives considered

- **Sign in CI, with the key in an environment secret behind a required
  reviewer.** It removes the local step, but the key would sit on a runner
  that has just run vcpkg ports, Homebrew, pip and third-party actions, and
  anyone with write access could reach it through a workflow. Rejected for
  the reason 0012 gave.
- **A separate CI signing job that runs no build code.** It narrows that
  exposure but still puts the Indiana University key into GitHub. Rejected for
  the same reason.
- **Keep building locally (0012).** It works, but the shipped macOS package
  then comes from a less controlled environment than the Windows one.

## Evidence

Measured on 2026-10-06 with the v3.0.1 CI artifact (run 36490656846), which
predates the attestation and so was signed with `--no-attestation`:

- Moving the signing into `MacosCodesignBundle.cmake` changed no signature. A
  local install signed before and after the change gave identical
  `codesign -dvvv` output, apart from signing times, for all 622 signed items
  (bundles, frameworks, dylibs, plugins, executables), including CDHashes.
- All 13 re-signed bundles in the new image passed `codesign --verify
  --strict`, with the Developer ID leaf authority.
- Compared with the v3.0.1 DMG signed locally and published, the 241 items
  both images contain have identical identifiers, flags, authority chains,
  team identifier and sealed-resource state.

The same comparison showed the two *builds* differed, in ways that would have
made a CI-built package unshippable. Both were fixed in the same change:

- The CI `qtcalibrate.app` had no `Qt3D` or `QtQuick3D` QML modules and none of
  the Qt 3D, Quick 3D, Shader Tools or Timeline frameworks the local one
  bundles. `sensorui`, which `qtcalibrate` uses, imports `QtQuick3D`,
  `QtQuick3D.Helpers`, `QtQuick3D.AssetUtils` and `QtQuick.Scene3D`, and the
  CI job installed Qt with `jurplel/install-qt-action` and no `modules:`, so
  only base Qt was present. The v3.0.1 Windows ZIP, built the same way and
  shipped, has the same gap. Nothing links these modules, so nothing failed.
  Against a base aqt install, `qmlimportscanner` leaves those four imports
  unresolved; with `qt3d` and `qtquick3d` alone they resolve, but Quick 3D's
  plugins link `QtShaderTools` and `QtQuickTimeline`, which aqt does not
  install with them.
  - **Fix:** `release.yml` installs `qt3d qtquick3d qtshadertools
    qtquicktimeline`. A `qml_import_check_<app>` target
    (`cmake/CheckQmlImports.cmake`), part of the default build, fails the
    build when an app's QML imports a module the Qt install lacks, or a
    plugin of one links a missing Qt library. Checked against a base aqt
    install (fails, naming the four imports), `qt3d` and `qtquick3d` alone
    (fails, naming the two libraries), the four modules (passes) and a full
    install (passes).
- The CI executables declared a minimum of macOS 15.0 (SDK 15.5), the local
  ones 14.4. Neither honoured the 12.3 in `CMakeLists.txt`, which was set after
  `project()`, where the cache entry `project()` had already created empty made
  it a no-op; an empty deployment target lets the compiler default to the
  build machine's SDK.
  - **Fix:** it is set before `project()`, and an empty entry left in an
    existing tree is replaced. A local rebuild then gave `minos 12.3` on every
    shipped executable, with no availability warnings.

## Consequences

- The first release that ships a CI-built DMG needs the download-and-launch
  test of every app in
  [the release procedure](../release/release-procedure.md#3-releasing-the-host-tools),
  including `qtcalibrate`'s 3D view, because its contents differ from earlier
  releases. The module list and the check were verified on macOS only; the
  first CI run is their first test on Windows.
- Artifacts are kept 14 days (`retention-days` in `release.yml`), so a tag
  must be signed within that window or rebuilt.
- Runs from before the attestation step cannot be signed by `sign-latest`;
  the script's `--no-attestation` exists for them and says what it gives up.
