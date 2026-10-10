---
type: decision
status: accepted
summary: Host builds use Qt 6.10.3 on both platforms and the macOS deployment target rises to 13.0, because Qt 6.10's macOS frameworks are built for macOS 13; CI's Qt pin follows the presets rather than lagging them.
---

# 0026. Host: Qt 6.10.3 everywhere and a macOS 13 floor

Date: 2026-10-10

Amends the deployment-target finding in
[0024](0024-release-macos-package-built-on-ci-signed-locally.md), which set the
floor at 12.3. Everything else in 0024 and
[0025](0025-release-macos-package-notarized-locally.md) still stands.

## Context

The macOS presets named Qt 6.8.2 while the Windows presets named 6.10.2, and
CI pinned 6.8.2 for both. Three Qt versions across two platforms and one
pipeline is one more than anyone can keep straight, and it hid a real
divergence: a developer's local build and the released binary linked different
Qt.

Separately, an Xcode upgrade removed the versioned macOS SDK that the existing
build trees had cached in `CMAKE_OSX_SYSROOT`. The resulting failures looked
like Qt problems — a stray `-framework AGL` on every link line, then a
`Qt6::PrintSupport` include path that no longer existed — but were stale build
trees. That is recorded as a trap in
[the toolchain inventory](../build/toolchain-inventory.md#traps), with a guard
in the top-level `CMakeLists.txt` that fails configure with the remedy.

Moving macOS to Qt 6.10.3 surfaced the actual constraint. Qt 6.10's macOS
frameworks are built with a minimum of macOS 13.0, so linking them at a 12.3
deployment target produces a binary that claims to run on Monterey and will not
load Qt there. 0024 had recorded "no availability warnings" at 12.3, which was
true against Qt 6.8.2 and is not true now.

## Decision

- Both platforms' presets name **Qt 6.10.3**, and `QT_VERSION` in
  `release.yml` follows them rather than pinning independently. The comment
  there says so, and names the licence work a change entails.
- `CMAKE_OSX_DEPLOYMENT_TARGET` is **13.0**, matched by
  `VCPKG_OSX_DEPLOYMENT_TARGET` in `cmake/vcpkg-triplets/arm64-osx-static.cmake`
  as 0024 requires. macOS 12 is no longer supported.
- The written source offer and the Qt version row in
  [`LICENSES/host/README.md`](../../LICENSES/host/README.md) name 6.10.3, and
  `LICENSES/host/Qt-THIRD-PARTY-NOTICES.txt` is regenerated from the 6.10.3
  sources with `LICENSES/tools/generate_qt_notices.py`.

## Consequences

- Nobody on macOS 12 can run the host tools. This was accepted on the basis
  that the known users are on macOS 13 or later; it is a user-visible floor, so
  it belongs in release notes the first time a release ships under it.
- The macOS runner in `release.yml` is `macos-15`, well above the new floor, so
  CI is unaffected by the change itself.
- A Qt version bump is now a three-part change: the presets, `QT_VERSION`, and
  the licence files. Changing one alone reintroduces the divergence this record
  closes.
- The first release built on 6.10.3 needs the download-and-launch test of every
  app that 0024 already requires, since the deployed Qt differs from earlier
  releases.
