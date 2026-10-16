---
type: decision
status: accepted
summary: The host tools ship for Linux as an x86_64 tarball carrying its own Qt 6.10.3, built on Ubuntu 24.04 with Wayland and X11 support and tested in clean containers before release; the preferred Linux developer build is the same configuration, and a Qt other than HOST_QT_VERSION warns.
---

# 0029. Host: a Linux package, as a tarball carrying its own Qt

Date: 2026-10-16

## Context

Releases shipped the host tools for Windows and macOS only. Linux was a
supported build platform, against a distribution's own packages, but every
Linux user built from source, and against whatever Qt their distribution had:
6.8 on Debian 13, 6.4 on Ubuntu 24.04, where the code had never been built.
The installed tree was not relocatable -- each executable named the build
machine's Qt by absolute path -- and nothing granted an ordinary user access
to the tag base, so every tool needed root.

The plan is in
[the proposal](../../host/design/proposals/linux-release-package.md); what
was built is described in [Linux packaging](../../host/design/linux-package.md).

## Decision

- **A tarball with Qt bundled**, `UltralightTags-X.Y-linux-x86_64.tar.gz`,
  holding one relocatable `tag_tools` directory, like the Windows ZIP. Rejected:
  a `.deb` against the distribution's Qt (one package per distribution
  release, each on a Qt the release was not tested with, against the one-Qt
  rule of [0026](0026-host-qt-6-10-3-and-macos-13-floor.md)); an AppImage (one
  application per image, against six applications and seven tools); a Flatpak
  (its sandbox stands between the tools and the USB device).
- **x86_64 only, built on Ubuntu 24.04**, which sets the floor at glibc 2.39:
  Ubuntu 24.04 and Debian 13 or newer. Ubuntu 22.04's standard support ends
  in June 2027, and no one runs the tools on Arm Linux.
- **Wayland and X11 both native.** Wayland is the default session on current
  GNOME and KDE, so the package carries the wayland platform plugin and the
  client decorations GNOME needs, beside xcb.
- **Deployment of our own,** in `cmake/DeployQtLinux.cmake`: QML copied module
  by module from `qmlimportscanner`'s report, so nothing GPL-only that the
  applications do not import is ever copied; a named plugin list; libraries
  resolved with `file(GET_RUNTIME_DEPENDENCIES)`. Qt's
  `qt_deploy_qml_imports()` expects `qt_add_qml_module()` applications, which
  these are not.
- **A udev rule** in the package, `70-ultralight-tags.rules`, granting the
  seat user the probes the tools accept; numbered below 73 because systemd
  applies `uaccess` in `73-seat-late.rules`.
- **Tested before it is uploaded.** CI runs `linux-package-test.sh` in clean
  Ubuntu 24.04 and Debian 13 containers -- library resolution, the command
  line tools, and every Qt application's documentation capture against the
  repository's fixtures under Wayland (mutter) and X11 (Xvfb) -- and a failure
  stops the upload.
- **The developer build mirrors the release build.** A `linux-vcpkg` preset,
  like `macos-vcpkg`, builds with vcpkg's static libraries and installer Qt
  6.10.3, and the README leads with it; the distribution-package build remains,
  marked as not how releases are made. `HOST_QT_VERSION` (6.10.3) is compared
  with the Qt found on every platform: a difference warns, and is an error
  under `REPRODUCIBLE_BUILD`, which the release workflow now sets. A warning,
  not an error, by default, so the distribution build keeps working.

## Consequences

- `sensorviz` passes its messages to Qt's default handler as well as its log
  window, so automation errors and plugin failures reach stderr; and its menu
  captures fall back to grabbing the menu, since Wayland refuses screen grabs.
  Both were found by the package test.
- tagcore asks pkg-config for libusb's static dependencies with any vcpkg
  triplet, not only on macOS.
- A Linux `cmake --install` now writes the `tag_tools` layout. Against a
  distribution Qt it uses that Qt in place rather than copying it.

## Verification

On 2026-10-16, an x86_64 `ubuntu:24.04` container on OrbStack (Rosetta),
with aqt Qt 6.10.3 and vcpkg `x64-linux` at the manifest baseline, built the
tarball: 66 MB, 46 libraries, 17 plugins, 28 QML modules. The package test
passed in clean `ubuntu:24.04` and `debian:13` containers: 103 ELF files
resolving every library inside the package, every command line tool, and all
five Qt applications under both Wayland and X11, `qtcalibrate`'s 3D view
included. The first runs found three faults, all fixed before this record: a
`RUNPATH` naming the build machine's Qt in `dataprocessing` (a second
`CMAKE_INSTALL_RPATH_USE_LINK_PATH` in `host/commandline`), and the two
`sensorviz` faults above.

Not yet verified: the CI job itself, and, on a Linux desktop with a base
attached, the udev rule and the window decorations under GNOME.
