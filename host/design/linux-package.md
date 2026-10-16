---
type: design
status: current
summary: How the Linux x86_64 host tools package is laid out, built, deployed and tested -- a relocatable tag_tools tarball carrying Qt 6.10.3, made on CI from the linux-vcpkg configuration and tested in clean containers under Wayland and X11.
---

# Linux Packaging

A release's Linux package is a tarball holding one relocatable `tag_tools`
directory, built by CI on Ubuntu 24.04 x86_64 against aqt Qt 6.10.3 and
vcpkg's static libraries -- the same configuration as the `linux-vcpkg`
preset -- and tested in clean Ubuntu 24.04 and Debian 13 containers before it
is uploaded. Why this form and not a `.deb`, an AppImage or a Flatpak is in
[decision 0029](../../docs/decisions/0029-host-linux-package-tarball-with-bundled-qt.md).

## What is in it

```
UltralightTags-X.Y-linux-x86_64/
  tag_tools/
    bin/        every shipped executable, and qt.conf
    lib/        the Qt libraries and the ICU Qt ships
    plugins/    the Qt plugins listed in cmake/DeployQtLinux.cmake
    qml/        the QML modules the applications import
    udev/       70-ultralight-tags.rules
    docs/       the user guide (when built with BUILD_HOST_DOCS, as CI does)
    licenses/   build-report.json  build-report.txt
```

Every executable has an `RPATH` of `$ORIGIN/../lib`, and `bin/qt.conf` points
Qt at `../plugins` and `../qml`. The tree mirrors a Qt installation's own
layout, so the `RUNPATH`s Qt's libraries and plugins carry (`$ORIGIN`,
`$ORIGIN/../../lib`) resolve inside it unchanged. Nothing in it names the
build machine; the package test fails any `RUNPATH` that does.

The command line tools link no Qt; vcpkg links libusb, protobuf, SQLite and
Abseil into every executable. `dataprocessing` links QtCore and QtGui.

The package relies on the system for glibc 2.39 or newer (the build runner's;
Qt's own libraries need 2.34), the C++ runtime, GLib, `libudev`, the display
stack (Wayland client libraries, `libxkbcommon`, and for X11 `libxcb` and
`libxcb-cursor`), OpenGL and EGL, fontconfig, D-Bus and CUPS. A desktop
installation has all of them, except sometimes `libxcb-cursor0`, which the
README names.

## How the install is made relocatable

[`host/CMakeLists.txt`](../CMakeLists.txt) installs on Linux to `tag_tools`,
sets `INSTALL_RPATH` and leaves `CMAKE_INSTALL_RPATH_USE_LINK_PATH` off, so
the build machine's library directories never reach an installed binary. An
install-time step then runs
[`cmake/DeployQtLinux.cmake`](../../cmake/DeployQtLinux.cmake), Qt having no
Linux counterpart of `windeployqt` or `macdeployqt`. It

1. asks `qmlimportscanner` which QML modules the applications' QML imports,
   and copies each module's own files -- stopping at any subdirectory that is
   a module in its own right, so the GPL-only modules a full Qt keeps under
   `QtQuick` are never copied and nothing needs pruning afterwards;
2. copies a fixed list of plugins: the `wayland` and `xcb` platforms; the
   Wayland client decorations (GNOME leaves decorations to the client), the
   `wayland-egl` integration the 3D view needs, and `xdg-shell`; the xcb GL
   integrations; compose and IBus input; the desktop-portal platform theme;
   CUPS print support; image formats and icon engines. Nothing links a
   plugin, so they are named rather than discovered;
3. copies every library the executables, plugins and QML plugins need that is
   not under `/lib` or `/usr/lib`, resolved against Qt's library directory,
   failing the install if a Qt or ICU library cannot be found;
4. writes `qt.conf`.

Qt's directories come from the layout it records for itself
(`host_find_qt_layout()`), so a distribution Qt, which splits its tree, is
found as reliably as an installer one. When Qt is a distribution's -- its
libraries under `/usr` -- the install does not deploy it: the applications use
the system Qt in place, keeping the plugins the distribution adds. Only an
install against an installer or aqt Qt carries its own.

## How it is built

`cmake --preset linux-vcpkg` configures it: vcpkg at `~/Software/vcpkg` with
the `x64-linux` triplet, Qt at `~/Qt/6.10.3/gcc_64`. `HOST_QT_VERSION`
(6.10.3) is compared with the Qt found; a difference warns, and is an error
under `REPRODUCIBLE_BUILD`, which the release workflow sets. CPack's `TGZ`
generator makes the tarball (`cmake --build --preset linux-vcpkg-package`).
[`release.yml`](../../.github/workflows/release.yml) builds the same
configuration on `ubuntu-24.04`, attests the tarball's provenance, and
attaches it to the draft release beside the Windows ZIP. Linux packages are
not signed.

## How it is tested

[`host/tools/linux-package-test.sh`](../tools/linux-package-test.sh) runs in
a clean container holding only what a desktop has, and

- fails any ELF file in the package with a library `ldd` cannot find (which
  catches a too-old glibc as well) or a `RUNPATH` outside the package;
- runs each command line tool's `--help`, `tag-info` with no base (it must
  exit 1), and `dataprocessing` deriving streams from a fixture log;
- runs each Qt application's documentation capture against
  `host/docs/fixtures` -- every `qtmonitor` fake tag, a recorded `qtcalibrate`
  session including its 3D orientation view, `sensorviz` logs -- under
  Wayland (`mutter --headless`, GNOME's compositor) and X11 (`Xvfb`). Each
  must exit 0, write screenshots that are not blank, and print no plugin,
  library or QML loading failure. `btviz` and `qtprogram` have no capture
  mode and must start and stay up.

[`linux-build-check.sh --package`](../tools/linux-build-check.sh) starts it in
a container of a chosen image; CI runs it on `ubuntu:24.04` and `debian:13`
for every package and uploads only one that passes. From a Mac, OrbStack runs
the x86_64 containers under Rosetta.

Under Wayland the captures grab the application's own widgets, since
compositors let no client read the screen, so the screenshots do not show
window decorations; the test proves the decoration plugins load, not how they
look. USB access through the udev rule cannot be tested in a container either.
Both are checked once on a Linux desktop with a base attached.

## The udev rule

[`host/linux/70-ultralight-tags.rules`](../linux/70-ultralight-tags.rules)
gives the user at the seat access to the tag base and the other ST-LINK
probes `isSTlink()` accepts, with `TAG+="uaccess"`. The number matters: the
tag is applied by systemd's `73-seat-late.rules`, so the file must sort before
it. The package carries it in `udev/`; the README's install section and a
source build both point to it.
