---
title: Toolchain inventory
type: procedure
status: current
summary: Every tool the tag-designs work depends on across all five repositories, with version, install path and what it serves; written for rebuilding a macOS workstation.
---

# Toolchain inventory

As of 2026-10-06. Compiled from the repositories themselves and from the
machine: `build-host/CMakeCache.txt` records the exact path and version of
every tool the last host build resolved, so most of what follows is measured
rather than remembered. It covers all five tag-designs repositories, not only
this one.

## What lives outside the home directory

A backup of `~` carries most of this work, including `~/Research`,
`~/Software`, `~/bin`, `~/Qt` and the login keychain that holds the Developer
ID identity. The following are the pieces it does **not** carry, and so the
real reinstall list.

| Outside `~` | What it is | How it comes back |
| --- | --- | --- |
| `/opt/homebrew` | The whole Homebrew prefix: `arm-none-eabi-gcc`, `protobuf`/`protoc-29.3.0`, `libusb` 1.0.30, `pkg-config`, `fmpp` 0.9.16, `graphviz`, `nanopb`, `mkdocs`, `python@3.13` | Reinstall Homebrew, then the formulae below. Nothing here is restorable by copying. |
| `/Applications/Xcode.app` | Apple toolchain, SDK MacOSX14.4, clang 15.0.0, and the SQLite3 headers the host build links against | App Store or developer.apple.com. Install before configuring anything. |
| `/Applications/CMake.app` | CMake 3.28.3 — the one that configured the current build trees | cmake.org disk image |
| `/Applications/Doxygen.app` | Doxygen | doxygen.nl, or use the Homebrew formula instead |
| `/Applications/KiCad/KiCad.app` | KiCad, including `kicad-cli` | kicad.org |
| `/usr/local/bin/openocd` | OpenOCD, used as the `embedded-debugger` backend for U375 | Not a Homebrew path — it was installed by hand and will not return with `brew install` |
| `/usr/local/arm/bin/arm-none-eabi-gdb` | Referenced by `.vscode/launch.json` | Already stale; the working GDB is in the Homebrew Arm toolchain |
| `/usr/bin/{git,make,python3,codesign,install_name_tool}` | Provided by macOS and the Xcode command-line tools | `xcode-select --install`, or Xcode itself |

Two home-directory items are worth calling out even though a backup carries
them, because losing them is expensive: the **login keychain**
(`~/Library/Keychains`) holding the `Developer ID Application: Indiana
University (5J69S77A7G)` identity, and `~/.ssh`, since all five git remotes
are SSH.

## Save before you wipe

The five repos are on GitHub and come back with a clone. These will not.

| What | Where | Why it matters |
| --- | --- | --- |
| Developer ID signing key | Login keychain, `Developer ID Application: Indiana University (5J69S77A7G)` | Every signed macOS release is cut on this Mac because CI deliberately has no access to this key. Export the identity and its private key as a `.p12` first. |
| SSH key for GitHub | `~/.ssh` | All five remotes are `git@github.com:tag-designs/…`. |
| `~/Software` | local | Hand-placed: ChibiOS copies, nanopb distributions, vcpkg checkout, STM32CubeProgrammer, OpenSCAD 2021.01, freecad-cli, freecad-mcp. |
| `~/bin` | local | `hugo` binary, `chibios-*-migrate` scripts. |
| Uncommitted work | all five repos | Most firmware build manifests in `build-host` record a `-dirty` describe; the newest, UIUCTag at 3 Oct, reads `fw-v0.0.3-55-g8a055a8-dirty`. Check `git status` in each repo. |
| `captures/`, `Claude outputs/` | `~/Research/tag-designs` | Not git repos. Nine capture sessions, 30 Sep – 3 Oct 2026. |
| Joulescope venv | `~/opt/joulescope-mcp/.venv` or `~/.venvs/joulescope` | `pip freeze` it before it goes. |
| Qt account credentials | Qt Maintenance Tool | Reinstalling Qt 6.8.2 needs the Qt account login. |

## Homebrew

Homebrew at `/opt/homebrew` (Apple Silicon). The first group is confirmed by
the recorded path in `build-host/CMakeCache.txt`.

| Formula | Version seen | Serves |
| --- | --- | --- |
| `arm-none-eabi-gcc` | 14.2.1 | All tag firmware; version is pinned. |
| `protobuf` | protoc 29.3.0 (`/opt/homebrew/bin/protoc-29.3.0`) | Host protobuf; pulls `abseil`, `utf8_range`. |
| `python@3.13` | 3.13.2 | What `find_package(Python3)` resolved to, and what builds the `config-gen` virtualenv. |
| `libusb` | 1.0.30 | Host tools' USB transport. |
| `pkg-config` | — | Locates `libusb-1.0`. |
| `fmpp` | 0.9.16 | Board file generation; needs a Java runtime. |
| `mkdocs`, `mkdocs-material` | binary at `/opt/homebrew/bin/mkdocs`; the repo pins `mkdocs>=1.6,<2.0` and `mkdocs-material>=9.5` as pip ranges | Host user documentation. The READMEs install them with `python3 -m pip install -r host/docs/requirements.txt`. |
| `graphviz` | `dot` | Doxygen graphs. |
| `nanopb` | symlinked into `~/Software/nanopb/generator-bin` | See Traps. |

Named in the READMEs, install alongside:

| Formula | Needed for |
| --- | --- |
| `autoconf`, `autoconf-archive`, `automake`, `libtool` | vcpkg's `libusb` port on macOS (build-only). |
| `ninja` | CI's macOS generator; local presets use Unix Makefiles. |
| `go`, `node`, `hugo`, `pdf2svg`, `rsync` | The Hugo documentation site. |
| `mactex-no-gui` (cask) | Only to regenerate TikZ-derived SVGs. |
| `ripgrep` | `rg`, the search tool AGENTS.md specifies. |
| `gh` | GitHub CLI; release workflows upload through it. |
| `doxygen` | A `Doxygen.app` is installed instead here; the formula is simpler. |
| `ngspice` (or LTspice / Xyce) | The `spice` skill treats simulation as required during a board review when any simulator is installed. None is currently present. |

Two prerequisites the READMEs name that come from macOS rather than Homebrew:
**Git** (used by the version-generation helpers in every repo) and
**`codesign`** (required when `MACOS_SIGN_APPS=ON`).

## Applications and standalone installs

| Tool | Version | Path |
| --- | --- | --- |
| Xcode | SDK MacOSX14.4, clang 15.0.0 | `/Applications/Xcode.app` — `cc`, `c++`, `ar`, `ld`, `objdump`, and the SQLite3 headers |
| CMake | 3.28.3 installed; the project's floor is **3.20** | `/Applications/CMake.app/Contents/bin/cmake`, not Homebrew's |
| Doxygen | — | `/Applications/Doxygen.app/Contents/Resources/doxygen` |
| KiCad | — | `/Applications/KiCad/KiCad.app`; `kicad-cli` is **not** on `PATH` |
| OpenOCD | — | `/usr/local/bin/openocd`, installed by hand |
| STM32CubeProgrammer | 2.4.0 | `~/Software/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI` |
| OpenSCAD | 2021.01 | `~/Software/OpenSCAD-2021.01.app` — the default in the Mechanical makefiles |
| FreeCAD | — | `freecadcmd` drives the parametric cases; a `freecad` MCP server is registered |
| Joulescope desktop app | — | Confirming supply voltage; detach before scripted measurement |
| DB Browser for SQLite | — | Inspecting tag logs |
| Zotero | — | Present in the home directory; `docs/docs/resources/My Library.json` is the exported library the Hugo bibliography draws on |

Also in `~/Software`, decide rather than restore blindly: `ChibiOS` (a symlink
to `Chibios-21.11`), `ChibiOS-20.3.x`, `ChibiOS-back` — all superseded by the
software repo's submodule — `nanopb-0.4.8-macosx-x86`, `protobuf`,
`protobuf-29.3`, `protobuf-build`, `qt6`, `libusb`, `STM32CubeProgrammer-old`,
`Doxygen.app`, `MotionCal`, `NXP-ISSDK-SensorFusion`, `QFlightinstruments`,
`condortools`.

## Qt

**Qt 6.8.2 for macOS** at `~/Qt/6.8.2/macos`, installed by the Qt Maintenance
Tool. The `macos-vcpkg` preset hard-codes that path, and `macdeployqt` comes
from its `bin`, which must be on `PATH` for packaging.

Also installed: Qt 6.5.3, Qt Creator, Qt Design Studio, Maintenance Tool. Only
6.8.2 is referenced. Windows presets name 6.10.2 for MSVC; not applicable here.

Modules required: Core, Gui, Widgets, Concurrent, PrintSupport, Svg,
SvgWidgets, Qml, Quick, QuickWidgets. `-DBUILD_QT_APPS=OFF` avoids all of it.

## Embedded toolchain and generators

Four versions are pinned because generated sources are committed. By default a
mismatch is a **warning**; `-DREPRODUCIBLE_BUILD=ON` turns the checks that go
through `reproducibility_problem()` into errors. The nanopb *generator* version
check is never fatal.

| Tool | Pin | Source |
| --- | --- | --- |
| Arm GNU Toolchain | **14.2.1** (14.2.rel1) | `/opt/homebrew/bin/arm-none-eabi-gcc` |
| nanopb generator + runtime | **0.4.9.1** | `docs/decisions/0011-build-pin-nanopb-0-4-9-1.md` |
| Python `protobuf` for `config-gen` | **5.28.1** | `embedded/proto-c/requirements.txt` — the protobuf the pinned nanopb bundles |
| ChibiOS | branch `stable_21.11.x` | Submodule; last built at `e209b17c`, `ver21.11.1@15221-412` |

Also required:

- `fmpp` 0.9.16 and a **Java runtime** — prototype boards do not commit
  generated files, so a default configure fails without it.
- `protoc` — normally the one beside `nanopb_generator`, deliberately not PyPI.
- Python 3. `find_package(Python3)` resolved to Homebrew's
  `python@3.13` (3.13.2); the legacy `PYTHON_EXECUTABLE` cache entry points at
  `/usr/bin/python3`. The first configure builds the pinned `config-gen`
  virtualenv and needs network once.
- `make` (`/usr/bin/make`), required by ChibiOS firmware builds.
- A native C/C++ compiler, even for an embedded-only configure.

Escape hatch before the generators are back: `-DREGENERATE_SOURCES=OFF` with
the `distributed_firmware` target needs only Arm GCC, `make`, CMake, a native
compiler and the ChibiOS submodule.

## Bench and instrumentation

| Piece | Detail |
| --- | --- |
| Joulescope JS220 | `pyjoulescope_driver` must be importable — a virtualenv, not system Python. |
| Measurement interpreter | `power_experiment.py` searches `~/opt/joulescope-mcp/.venv/bin/python`, `~/.venvs/joulescope/bin/python`, `<repo>/.venv/bin/python`; override with `--measure-python` or `JOULESCOPE_PYTHON`. |
| Joulescope desktop app | Setting and confirming supply voltage. Detach before scripted runs. |
| `joulescope-js220` MCP server | Not to be used; it holds the device. Kill stray `joulescope-mcp` processes. |
| ST-LINK probes | Tag programmer base `0483:3748` (preferred), STLINK-V3PWR `0483:3757`. Select with `STM32_PROGRAMMER_PROBE`. |
| STM32CubeProgrammer CLI | SWD with `mode=UR`, and DFU. 2.4.0 here. |
| `tag-capture` | Host tool from this repo; halts the core before firmware runs. |
| `embedded-debugger` MCP | Probe inspection, core control, memory reads, breakpoints, RTT, over probe-rs or OpenOCD. **probe-rs has no STM32U3 target**, so U375 work needs the OpenOCD backend at `/usr/local/bin/openocd`. |
| GDB over SWD | `arm-none-eabi-gdb` from the Arm toolchain; the VS Code configs use `cortex-debug`. |

The bench scripts themselves live in `software/embedded/tools` and come back
with the repo. What does not come back is the interpreter that can run them.

## Hardware and mechanical

| Tool | Role |
| --- | --- |
| KiCad | 72 `.kicad_pro` files under `hardware/BoardDesigns`, 52 outside `Archive/` and `Obsolete/`. `kicad-cli pcb drc --format json`; remember `--refill-zones`. |
| `kicad-happy` review suite | `kicad`, `emc`, `spice`, `datasheets`, `bom`, distributor and fab skills. A copy of the `kicad` skill is vendored at `hardware/.agents/skills/kicad`; the rest is installed outside the repo. |
| SPICE simulator | `ngspice`, LTspice or Xyce. The `kicad` skill runs `which ngspice ltspice xyce` and treats simulation as required when one is found. |
| Distributor API credentials | `DIGIKEY_CLIENT_ID`/`SECRET`, `MOUSER_SEARCH_API_KEY`, `ELEMENT14_API_KEY` enable datasheet sync and BOM pricing; LCSC needs none. None are currently configured. |
| `kicad-helpers` | In-repo Python probes over analyzer JSON. Plain Python 3. |
| `STM32_open_pin_data` | Hardware repo submodule; ST's machine-readable pin data behind the AF tables. |
| KiBot | Fab outputs; configs at `BoardDesigns/Kibot-config/`. |
| FreeCAD + `freecadcmd` | Parametric tag cases via `tagcase_param.py`. |
| `freecad-cli`, `freecad-mcp` 0.1.22 | `uv`-managed Python projects, Python >= 3.12 — so **`uv`** is a tool to reinstall. |
| OpenSCAD 2021.01 | Older `.scad` case models and the acrylic charger DXF. |
| Slicer | Implied by the `.stl` output; not referenced by any file here. |

## Documentation stacks

Two stacks, one per repo, sharing nothing.

**MkDocs** — the `software` repo. MkDocs with Material, pinned in
`host/docs/requirements.txt`. Builds the packaged end-user manual
(`BUILD_HOST_DOCS`) and the developer portal (`BUILD_DEVELOPER_DOCS`).
`docs/tools/docs.py index` and `docs.py check` regenerate the index from front
matter. Doxygen with Graphviz covers the API reference.

**Hugo** — the `tag-designs.github.io` repo:

| Requirement | Minimum |
| --- | --- |
| Hugo Extended | 0.155.3 (a `hugo` binary also sits in `~/bin`) |
| Go | 1.22, for Hugo Modules (Docsy, hugo-cite) |
| Node.js + npm | 22 LTS; `npm ci` in `docs/` installs the PostCSS toolchain |
| CMake | 3.10 |
| `pdflatex` + `pdf2svg` | Only to regenerate TikZ-derived SVGs |
| `rsync` | Only for the `sync-gh-pages` target |

The first site build needs network to populate the Hugo module cache. The
separate `docs` repo carries the themes as submodules: Docsy, hugo-cite,
font-awesome.

## Repos, submodules and identity

All five remotes are SSH to `github.com:tag-designs`.

| Repo | Submodules |
| --- | --- |
| `software` | `ChibiOS` (branch `stable_21.11.x`) |
| `hardware` | `STM32_open_pin_data` |
| `docs` | `docsy`, `hugo-cite`, `font-awesome` |
| `protobuf` | — |
| `tag-designs.github.io` | — |

Not repos: `captures/` and `Claude outputs/`.

- **vcpkg** at `~/Software/vcpkg`, checkout `8defa4b8` (13 May 2026). The
  manifest baseline the project pins is
  `99a97de2cb371449d4fb9dc970f2ac562d689ec2`. Manifest installs `libusb`,
  `protobuf`, `sqlite3`, host `pkgconf`.
- **`MACOS_CODE_SIGN_IDENTITY`** defaults to
  `Developer ID Application: Indiana University (5J69S77A7G)`. Without it,
  `sign-latest` and `host/tools/release-macos.sh` cannot cut a release;
  `-DMACOS_SIGN_APPS=OFF` builds unsigned.

## Traps

**`~/Software/nanopb` is not a nanopb install.** CMake recorded
`NANOPB_SRC_ROOT_FOLDER=~/Software/nanopb` and the generator at
`~/Software/nanopb/generator-bin/nanopb_generator`, but that `generator-bin`
holds exactly one entry: a symlink to `/opt/homebrew/bin/nanopb_generator`. The
directory itself is a git checkout at `nanopb-0.4.8-11-g1f0c2e1`, two versions
behind the 0.4.9.1 pin. The build is using the Homebrew generator through a
hand-made shim. A full `nanopb-0.4.9.1-macosx-x86` distribution also sits in
`~/Software`, unused by that cache entry. On the rebuilt machine, point
`NANOPB_ROOT` at the real 0.4.9.1 distribution rather than recreating the shim,
and check that `protoc` sits beside the generator.

**Path casing.** The presets reference `$HOME/qt/6.8.2/macos`; the directory is
`~/Qt`. This works only on a case-insensitive volume.

**Absolute paths baked into the tree.**
`hardware/Mechanical/{BitPresTag,IMUTag}/Makefile` default to
`$(HOME)/Software/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD` — overridable
with `OPENSCAD=`, but wrong out of the box if OpenSCAD lands elsewhere.
`hardware/CLAUDE.md` names
`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`.
`software/.vscode/launch.json` names
`/usr/local/arm/bin/arm-none-eabi-gdb`, which is already stale — the installed
Arm toolchain is Homebrew's.

**CMake is the app bundle, not Homebrew's.** `/Applications/CMake.app` 3.28.3
configured the current build trees; the project's stated floor is 3.20.

**SQLite comes from the macOS SDK.** `SQLite3_LIBRARY` resolved to
`libsqlite3.tbd` inside the Xcode SDK. Install Xcode before configuring.

**Old build trees are stale the moment the machine changes.**
`software/build-host`, `build-docs` and `~/Build/tag-designs` embed absolute
paths — delete and reconfigure, but read `build-host/CMakeCache.txt` once more
first, since it is the best record of what was installed.
