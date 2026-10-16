---
type: proposal
status: proposed
summary: Add a Linux x86_64 host tools package to the release -- a relocatable tarball built on CI against the same aqt Qt and static vcpkg libraries as Windows and macOS, with udev rules for the ST-LINK base.
---

# A Linux Release Package for the Host Tools

A `vX.Y` release ships the host tools for Windows (a ZIP) and macOS (a DMG).
This proposal adds a third package, for Linux on x86_64: a tarball, built by
the same release workflow, that a user unpacks anywhere and runs without
installing Qt or building anything.

Linux is already a supported *build* platform: the README's Linux instructions
build every host tool against a distribution's own packages, and
[`linux-build-check.sh`](../../tools/linux-build-check.sh) checks that they do,
on Debian 13, from a Mac. What is missing is a package, and the three things a
package needs that a build tree does not: an install tree that can be moved,
the Qt it runs against, and permission to open the USB device.

## What ships

```
UltralightTags-vX.Y-linux-x86_64.tar.gz
  tag_tools/
    bin/        the command line tools and the Qt applications
    lib/        the Qt libraries the applications link, and the ICU Qt ships
    plugins/    platforms (wayland, xcb), wayland-decoration-client,
                wayland-graphics-integration-client, wayland-shell-integration,
                xcbglintegrations, imageformats, iconengines
    qml/        the QML modules the applications import, and nothing else
    udev/       99-ultralight-tags.rules
    docs/       the host user guide
    licenses/   LICENSES/host, as in the other packages
```

The shape follows the Windows ZIP: one `tag_tools` directory holding
everything. Every executable carries an `RPATH` of `$ORIGIN/../lib`, so the
directory works wherever it is unpacked. The command line tools other than
`dataprocessing` link no Qt, as on the other platforms.

From the user's system the package takes the C library and the compiler
runtime, `libudev` (which vcpkg's static `libusb` still loads on Linux, and
which every systemd distribution has), the display stack -- the Wayland
client libraries and `libxkbcommon`, and for X11 `libxcb`, `libxkbcommon-x11`
and `libxcb-cursor` -- OpenGL and EGL, and fontconfig. A distribution with a
desktop has all of these except, commonly, `libxcb-cursor0`, which Qt's xcb
platform plugin has required since Qt 6.5 and which only an X11 session
needs; the install notes name it.

Expected size: 60-80 MB compressed, against 56 MB for the v3.2 Windows ZIP,
the difference being mostly ICU.

## Why a tarball

Three forms were considered.

- **A `.deb` (or `.rpm`) against the distribution's Qt.** Small, and installs
  like any other package, but ties each package to one distribution release:
  Debian 13 ships Qt 6.8, Ubuntu 24.04 Qt 6.4, and the release would need a
  package per distribution and version, each against a Qt the release was not
  tested with. It also departs from the rule that one Qt version
  ([0026](../../../docs/decisions/0026-host-qt-6-10-3-and-macos-13-floor.md))
  is behind every package.
- **An AppImage.** One AppImage is one application; the package has six
  applications and a dozen command line tools that users put on `PATH`.
- **A tarball with Qt bundled.** The same Qt 6.10.3 and the same static vcpkg
  dependencies as Windows and macOS, built by the same workflow. Chosen.

A Flatpak is out of scope: its sandbox stands between the tools and the USB
device, which is the one thing every tool needs.

## Building it on CI

A third entry in the `build` matrix of
[`release.yml`](../../../.github/workflows/release.yml):

| Setting | Value |
| --- | --- |
| Runner | `ubuntu-24.04` (see "Supported distributions", below) |
| Qt | aqt `linux_gcc_64`, the same `modules:` as the other entries |
| vcpkg triplet | `x64-linux`, which is static |
| Build packages (apt) | autotools and `libudev-dev` for vcpkg's `libusb` port; `libgl1-mesa-dev` and `libxkbcommon-dev` for Qt's headers |
| CPack generator | `TGZ` |
| Artifact | `host-tools-linux-x64`, with a build provenance attestation |

The `release` job uploads the tarball to the draft release beside the Windows
ZIP. Linux packages are not signed; the attestation is what ties the tarball
to the workflow run, as it does for the DMG before it is re-signed.

The QML import check (`qml_import_check_<app>`) runs in this build as in the
others and fails it if the aqt install lacks a module an application imports.

### Supported distributions

A binary runs only on a glibc at least as new as the one it was built against,
so the runner sets the oldest distribution the package supports. Building on
Ubuntu 24.04 sets that floor at glibc 2.39: Ubuntu 24.04 and Debian 13 and
later, and other distributions of the same age. Ubuntu 22.04, the one older
LTS release, reaches the end of its standard support in June 2027, and the
package is not built for it. Debian 12 and RHEL 9 are below the floor too.

Qt documents its own Linux binaries as built on Ubuntu 24.04, so the aqt Qt
should need nothing newer than the runner; the package test below confirms it,
since an unmet glibc version shows in `ldd` like a missing library.

The package is x86_64 only. Qt supports Linux arm64 desktops and an
`ubuntu-24.04-arm` runner exists, but no one runs the tools on Arm Linux, and
a second package is a second thing to test at every release.

### Wayland and X11

Wayland is the default session on current GNOME and KDE desktops, Ubuntu
24.04 and Debian 13 included, so the package ships Qt's wayland platform
plugin and runs natively there. Qt chooses it at startup on a Wayland session
and the xcb plugin on an X11 one; both ship. `QT_QPA_PLATFORM=xcb` makes an
application use X11 through XWayland instead, the escape hatch if a Wayland
compositor misbehaves, and the install notes say so.

The wayland platform plugin alone is not enough. GNOME draws no window
decorations for its clients, so without the `wayland-decoration-client`
plugins an application's windows have no title bar there; the 3D attitude view
renders through EGL, which needs `wayland-graphics-integration-client`; and
the shell protocols are plugins of their own. Those plugin types are named
explicitly in the deployment (step 3 below) rather than left to the link
graph, which never reaches them. Whether aqt's base Linux install carries the
Qt Wayland module or it needs adding to `modules:` is to be checked when the
matrix entry is written.

## Changes to the install

The Linux install today is a developer install, not a package: executables go
to `bin` with `CMAKE_INSTALL_RPATH_USE_LINK_PATH`, so each one points at the
build machine's Qt by absolute path, and support files go to
`share/UltralightTags`. Under `if(UNIX AND NOT APPLE)` in
[`host/CMakeLists.txt`](../../CMakeLists.txt):

1. **Layout.** Install executables to `tag_tools/bin`, support files to
   `tag_tools`, matching `HOST_BUNDLE_INSTALL_DIR` on macOS.
2. **RPATH.** `INSTALL_RPATH` of `$ORIGIN/../lib` on every installed
   executable, and no link-path RPATH.
3. **Qt libraries and plugins.** Qt's own `qt_deploy_runtime_dependencies()`
   supports Linux: it walks the link graph with `file(GET_RUNTIME_DEPENDENCIES)`,
   copies non-system libraries to `LIB_DIR`, and, since Qt 6.10, takes
   `INCLUDE_PLUGIN_TYPES`/`EXCLUDE_PLUGIN_TYPES` to choose the plugin set. One
   generated deploy script per Qt application, run at install time, as
   `install_windeployqt()` does on Windows. It writes a `qt.conf` beside the
   executables pointing at `../plugins` and `../qml`. The Wayland plugin types
   are listed in `INCLUDE_PLUGIN_TYPES` (see "Wayland and X11", above).
4. **QML.** `qt_deploy_qml_imports()` expects applications built with
   `qt_add_qml_module()`; these applications load QML from resources and
   declare their source directories in `QT_DEPLOY_QML_DIRS` instead. So the
   deployment is ours: run `qmlimportscanner` over those directories, as
   [`CheckQmlImports.cmake`](../../../cmake/CheckQmlImports.cmake) already
   does, and copy exactly the modules it reports. Copying only what is imported
   makes the macOS pruning step (`MACOS_PRUNE_UNUSED_QML`) unnecessary here: the
   GPL-only modules a full Qt carries are never copied in the first place. The
   QML plugins' own library dependencies go through step 3 as
   `ADDITIONAL_MODULES`.
5. **udev rules.** Install `udev/99-ultralight-tags.rules` (next section).
6. **CPack.** `CPACK_GENERATOR TGZ` and a file name of
   `UltralightTags-${tag}-linux-${arch}`, beside the existing `ZIP` and
   `DragNDrop` branches in the top-level
   [`CMakeLists.txt`](../../../CMakeLists.txt).

None of this changes Windows or macOS. A plain `cmake --install` on Linux
changes shape (a `tag_tools` tree instead of `bin` and `share`), which affects
nobody now: no documented procedure installs on Linux.

## USB access

On Linux an ordinary user cannot open a USB device until a udev rule grants it,
so without one every tool fails with a permission error unless run as root.
Nothing in the repository provides one today.

The rule grants the logged-in user access to every probe
[`linkadapt.cc`](../../libraries/tagcore/linkadapt.cc) accepts -- vendor
`0483`, products `3748` (the tag programmer base), `374a`, `374b`, `3752`,
`374e`, `374f` and `3757` -- with `TAG+="uaccess"`, which needs no group and
follows whoever is at the seat. Installing it is one root step:

```sh
sudo cp tag_tools/udev/99-ultralight-tags.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The rule is the same file whether the tools come from this package or from a
source build, so it lives in the source tree (beside the host tools) and the
README's Linux build section points to it too.

## Documentation

- The README gains "Installing a Linux Release": unpack, add `tag_tools/bin`
  to `PATH`, install the udev rule, and `libxcb-cursor0` if an application
  reports that the xcb plugin could not load.
- [The release procedure](../../../docs/release/release-procedure.md) lists
  the Linux tarball among what CI builds and the draft release carries.
- A decision record when it is built, as for the macOS package.

## Testing

Before the first release that carries it:

- **On CI, every build.** The QML import check, as now.
- **From the Mac, before tagging.** A mode of `linux-build-check.sh` that takes
  the CI tarball instead of building, unpacks it into clean `debian:13` and
  `ubuntu:24.04` containers (x86_64, which OrbStack runs under Rosetta), and
  fails if `ldd` reports any library of any executable or plugin as not found,
  or if any command line tool fails `--help`. This is the check that the
  package is self-contained, and that no build-machine path leaked into it.
  Each application is then started under a headless Wayland compositor in the
  same container (`weston --backend=headless`, or `sway` with the headless
  wlroots backend) with `QT_QPA_PLATFORM=wayland`, and under `Xvfb` with
  `QT_QPA_PLATFORM=xcb`. It must stay up, with no plugin or QML errors, until
  it is closed; a screenshot of each (`grim` under sway) records that windows
  were mapped and drawn. This tests both display paths, the decoration and EGL
  plugins included, without a display.
- **On a Linux machine with a base attached, once.** Install the udev rule as
  an ordinary user, then `tag-info` against a tag, and open each application
  in a Wayland session and, from the login screen's session menu, an X11 one.
  Neither the device nor a real compositor can be had in a container.

## Phases

1. The relocatable install (steps 1-4 and 6 of "Changes to the install"),
   tested locally with an aqt Qt in an x86_64 `ubuntu:24.04` container.
2. The udev rule, tested on a Linux machine with a base.
3. The CI matrix entry and the release job upload, run with
   `workflow_dispatch` before any tag.
4. The package test in `linux-build-check.sh`, the README and release
   procedure changes, and the decision record.
