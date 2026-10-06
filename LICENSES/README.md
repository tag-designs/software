---
type: readme
status: current
summary: The project's MIT license, which shipped programs and firmware are distributed under GPL-3.0 and why, the third-party inventory behind the notice files, and how to keep them current.
---

# Licenses

The project's own code, host and firmware alike, is licensed under the
[MIT License](../LICENSE), Copyright (c) 2018-2026 The Trustees of Indiana
University. Some of the programs built from it link GPL-licensed components and
are therefore distributed under the GNU GPL, version 3:

| Distributed program | GPL component | Its license |
| --- | --- | --- |
| every tag and base-board firmware image | ChibiOS/RT kernel, OS library and Cortex-M ports | GPL-3.0-only |
| `sensorviz`, `btviz` | QCustomPlot 2.1.1, statically linked | GPL-3.0-or-later |
| `qtcalibrate` | Qt Quick 3D and Qt Quick Timeline | GPL-3.0-only |

MIT is compatible with GPL-3.0, so the MIT code can be combined into those
programs. Each program as a whole is conveyed under GPL-3.0, and the MIT
grant still applies to the project's code taken on its own. The other host
programs and the external flash loaders contain no GPL code.

Where something still needs fixing before a release is fully compliant, it is
in [TODO.md](TODO.md).

## What is here

Each side has an index that lists every third-party component it covers, with
links to that component's license:

- [host/README.md](host/README.md) for the host tools and their user manual.
- [embedded/README.md](embedded/README.md) for the firmware and the external
  flash loaders.

| Path | Contents |
| --- | --- |
| `../LICENSE` | The MIT License for the whole repository |
| `host/`, `embedded/` | Each side's index (`README.md`), a copy of the MIT License (`LICENSE`), one notice file per third-party component (`third-party/`), and the full texts of the licenses those notices refer to (`texts/`) |
| `host/Qt-THIRD-PARTY-NOTICES.txt` | The third-party code inside the Qt libraries, generated from Qt's `qt_attribution.json` files |
| `tools/generate_qt_notices.py` | Regenerates `host/Qt-THIRD-PARTY-NOTICES.txt` |

Each `host/` and `embedded/` directory is self-contained, with relative links,
so it is installed into the packages as it stands.

Each component's notice file gives its copyright, where it is used and where
its source is. For MIT, BSD, Zlib, 0BSD and W3C code the file reproduces the
license in full, since those licenses require their notice to travel with
binaries. For GPL, LGPL, Apache, MPL and CC licenses it names the shared text
in `texts/`.

The texts in `texts/` are unmodified copies:
- **GPL-3.0, LGPL-3.0 and Apache-2.0:** `qtbase/LICENSES` in the Qt 6.8.2
  source.
- **LGPL-2.1:** libusb's vcpkg copyright file.
- **MPL-1.1:** lunr-languages' `LICENSE`.
- **CC-BY-4.0:** the SPDX license list.
- **`embedded/texts/GPL-3.0-only.txt`:** `ChibiOS/license.txt`.

## How the licenses reach users

- **Host packages:** `host/CMakeLists.txt` installs `LICENSES/host` as
  `tag_tools/licenses` in both the DMG and the ZIP.
- **Builds that install firmware or loaders:** `embedded/CMakeLists.txt`
  installs `LICENSES/embedded` beside them, as `licenses/`.
- **Firmware release archive:** `release-firmware.yml` copies
  `LICENSES/embedded` into it as `firmware/licenses`.

The inventory behind the indexes was taken on 2026-10-06. Each license was read
from the component's own license file or source header, not from package
metadata.

Generated code is the project's own:
- **Board files:** they carry the ChibiOS Apache-2.0 header from the
  templates.
- **nanopb and protoc output:** these belong to the owner of the `.proto`
  input.

These are build-time only and not shipped:
- the vcpkg build helpers (pkgconf and the vcpkg-cmake ports);
- the CMake modules in `cmake/` with third-party headers: DeployQt,
  SharedLibrary and Findlibusb;
- fmpp;
- the nanopb generator.

## Keeping this current

- **Adding a third-party component:**
  - Add a notice file under that side's `third-party/`, with its copyright line and, for MIT, BSD and similar licenses, the full notice.
  - Add a row linking to it in that side's `README.md`.
  - If its license text is not already in `texts/`, add it there.
- **Changing the Qt version, or deploying a new Qt module:** update `SHIPPED_MODULES` in `tools/generate_qt_notices.py` if needed, then regenerate:

  ```sh
  LICENSES/tools/generate_qt_notices.py ~/qt/<version>/Src \
      > LICENSES/host/Qt-THIRD-PARTY-NOTICES.txt
  ```

  Update the Qt version in `host/README.md` and `host/third-party/qt.txt` as well.
- **Upgrading a vcpkg baseline or the ChibiOS submodule:** check the versions and licenses above against the new `share/<port>/copyright` files and source headers.
- **A new GPL-only dependency in a program:** add the program to the GPL table at the top of this file and in `host/README.md` or `embedded/README.md`.
