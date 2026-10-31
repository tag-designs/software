---
type: proposal
status: accepted
summary: Plan for the release that makes the Rust tools and applications the host software - what ships, which C++ stays because it has no Rust equivalent, what moves behind a BUILD_LEGACY_HOST option, which dependencies that removes (Qt) and which it cannot (vcpkg's libusb, protobuf and SQLite, still inside tagcore_c and the kept tools), the gaps to close first, the work in phases down to the files, the checks, and the decisions taken (release v4.0).
---

# Switching the host release to the Rust tools and applications

**Proposed and accepted 2026-10-31.** The next host release, `v4.0`, ships the Rust applications
(Tag Programmer, Tag Monitor, Tag Calibrator, Tag View) and the Rust
command-line tools in place of the Qt applications and the C++ tools that
have Rust equivalents. The C++ that has no equivalent stays and ships:
`dataprocessing`, `tag-txt-import`, and the `tagcore_c` library the Rust
programs link. The C++ tools that exist only for the bench and have no port
(the recovery tools) stay in the default build in `bin/internal`. Everything
replaced moves behind one CMake option, `BUILD_LEGACY_HOST`, off by default,
so it can still be built for reference, for regenerating test oracles, and
for a fallback release.

The one dependency this removes is **Qt**, with everything attached to it
(aqt in CI, `macdeployqt` and `windeployqt`, the QML import checks, the Qt
notices and the GPL section of the licence index, QCustomPlot). The vcpkg
dependencies (`libusb`, `protobuf`, `sqlite3`) **cannot go**: `tagcore_c`
carries protobuf and SQLite inside it for Tag Monitor's downloads, Tag
Calibrator and Tag View; `dataprocessing` and `tag-txt-import` need protobuf
and SQLite; and the recovery tools need the C++ `tagcore`, which needs
libusb. The section [Dependencies](#dependencies) says what could still be
trimmed and at what cost.

## What ships after the switch

| Product | Today | After | Notes |
| --- | --- | --- | --- |
| Tag Programmer | `qtprogram.app` and `tagprogram.app` | `tagprogram` only | Programs through probe-rs; STM32CubeProgrammer is no longer needed on a station |
| Tag Monitor | `qtmonitor` and `tagmonitor` | `tagmonitor` only | Downloads through `tagcore_c` |
| Tag Calibrator | `qtcalibrate` and `tagcalibrate` | `tagcalibrate` only | Calibration is the same C++ session, through `tagcore_c` |
| Log viewer | `sensorviz` and `tagview` | `tagview` only | Same C++ core; the Qt-only activity filter is not carried in this release (decided, see gaps) |
| Bit Tag Visualizer | `btviz` | removed | Deprecated; the source in `host/applications/btdataviz` is deleted, its logs open in Tag View |
| `tag-info`, `tag-start`, `tag-stop`, `tag-reset`, `tag-test`, `tag-cal`, `tag-dwnld` | C++ | **Rust**, same names, options, output and exit status | `tag-dwnld` links `tagcore_c` |
| `tag-flash`, `base-flash` | Rust | Rust | unchanged |
| `dataprocessing`, `tag-txt-import` | C++ | C++ | no Rust equivalent; `tag-txt-import` needs the C++ text-log reader |
| `tagcore_c` | shared library on Linux and Windows, static on macOS | same | now linked by `tag-dwnld` as well |
| User manual | pages for both lines | the Rust applications' pages; the Qt pages kept for this one release under a "Legacy" heading | see [Documentation](#phase-4-documentation) |

Built but not installed, in `bin/internal`, unchanged: the C++ recovery tools
`tag-capture`, `tag-xflash`, `tag-sramcall`, `tag-rebuild`, and
`tag-log-import` (or its Rust replacement, below). The Rust bench tools
(`tag-peek`, `tag-attach-cycle`, `tag-monitor-test`, `tag-side-channel-check`,
`tag-test-example`, `tag-cal-write`, `qtmonitor-fixture-capture`) are built
into `bin/internal` too, so the bench scripts that run tools by path keep
working.

Behind `BUILD_LEGACY_HOST=ON` (default OFF): the five Qt applications,
`sensorui`, `qcustomplot`, sensoranalysis's Qt layer and its Qt-comparing
test programs, `host/common`, and the thirteen C++ command-line tools that
have Rust equivalents. The option requires Qt, exactly as `BUILD_QT_APPS`
does today.

## Dependencies

| Dependency | Needed by, after the switch | Removable? |
| --- | --- | --- |
| Qt 6.10.3 (and the aqt install, deployment tools, QML checks, Qt and QCustomPlot notices) | nothing in the default build | **Yes.** Only `BUILD_LEGACY_HOST` looks for it |
| vcpkg `protobuf` | `tagcore_c` (the SQLite writer parses the protobuf wire bytes; `sensoranalysis_core_json` reads calibration JSON), `dataprocessing`, `tag-txt-import`, the recovery tools | No |
| vcpkg `sqlite3` | `tagcore_c` (writer, `sensordata`), `dataprocessing`, `tag-txt-import`, `tag-rebuild` | No |
| vcpkg `libusb` | the C++ `tagcore` link layer: the recovery tools (`SwdSession`), `tag-txt-import` (links `tagcore` whole), and `tagcore_c` (links `tagcore` whole for `writeHeader(Tag &)`, which nothing calls) | Not while the recovery tools are C++. `tagcore_c` could stop carrying it by moving `writeHeader(Tag &)` and `readTagLogHeader()` out of the writer's files, which the `tagcore-c` design already lists; that shrinks the library but does not remove the vcpkg port |
| vcpkg `pkgconf` (host) | finding libusb off Windows | stays with libusb |
| WebKitGTK 4.1 (Linux), WebView2 (Windows) | the four Tauri applications | already required |
| Rust 1.85+ | everything Rust | already required; becomes mandatory for the default build rather than skipped when missing |

So the vcpkg manifest is unchanged. What the switch saves on a build machine
and in CI is Qt: the largest install, the deployment steps, and the licence
complications. Dropping libusb too would mean porting the recovery library
(the last large piece of C++ with no Rust equivalent); that is a separate
proposal and not part of this release.

## Gaps to close before the release

These are the items the Rust side has recorded as unfinished or unchecked.
Each is small; together they are the first phase.

1. **The Rust command-line tools are not installed by CMake.** Only
   `tag-flash` and `base-flash` are. The build must produce, sign and
   install the seven shipped tools from the `tag-tools` crate, with
   `tag-dwnld` linking `tagcore_c` (static on macOS; on Linux an rpath of
   `$ORIGIN/../lib`, which `commandline-rs/build.rs` does not yet set from
   `TAGCORE_C_RPATH` as the applications' build scripts do; on Windows the
   DLL beside the executable, which means the launcher arrangement the
   applications use).
2. **Hardware checks not yet run**, from the Rust READMEs: `tag-start
   --config` and `--merge` on a tag; `tag-test` on an IMUTag; the opt-in
   write, start and unplug checks of `tag-side-channel-check`; programming
   an STM32U375 through Tag Programmer's engine (Retest has passed, Program
   has not been run); Tag Calibrator and Tag View on Windows and Linux
   packages beyond "starts and stays up".
3. **Test oracles that need the Qt applications.** Tag Monitor's 710
   configuration-probe goldens are generated by `qtmonitor --config-probe`,
   Tag Calibrator's replay log is compared with qtcalibrate's, and Tag View
   is compared with the Qt `sensorviz` through `session_cli`. The committed
   goldens and logs keep the Rust tests running without Qt; regenerating
   them needs `BUILD_LEGACY_HOST=ON`. The test READMEs must say so, and
   the procedure in `tagmonitor-rs/design/adding-a-tag-type.md` becomes
   "build the legacy qtmonitor to regenerate goldens" until a Rust probe
   replaces it (not part of this release).
4. **Tag View's missing activity filter.** The Qt `sensorviz` has an
   activity filter that the toolkit-neutral core deliberately left in the
   Qt layer. Shipping Tag View alone drops the feature. **Decided
   2026-10-31: release without it.** The Tag View manual page says so, and
   porting it into the core (the proposal `toolkit-neutral-core.md`
   reserved it) goes to `host/applications/sensorviz-rs/TODO.md` for a
   later release.
5. **`tag-log-import`.** Promote `taglogwriter`'s `log-import` example to a
   `tag-tools` binary of that name (an afternoon), so the only C++ tools
   left outside the legacy option are `dataprocessing`, `tag-txt-import`
   and the recovery tools. It stays internal (decided 2026-10-31): built
   into `bin/internal`, not installed, no manual page. `tagsim` finds it
   through `TAG_LOG_IMPORT_BIN` or `PATH`, so nothing else changes.
6. **Screenshots for the manual.** The Rust applications' pictures come from
   their mocks (`ui/screenshots.sh`); the Qt pages' pictures came from the
   Qt applications' capture modes, which `linux-package-test.sh` also runs
   as a smoke test. Those captures go with the pages; the package test
   needs a Rust-side replacement for "the application draws its window"
   (the existing `run_stays_up` checks, plus one headless capture per
   application if wanted).
7. **The release procedure's counts and checks.** "Seven app lines" becomes
   four; `verify-macos-dmg.sh` applies its Qt TLS-plugin check only to
   bundles containing QtCore and so needs no change, but the bundle list it
   expects does; `linux-package-test.sh`'s TLS-plugin check (for qtprogram's
   GitHub listing) becomes "tagprogram lists releases", which Tag
   Programmer's `smoke` example can drive.

## The work, in phases

### Phase 1: close the gaps

- `host/commandline-rs/build.rs`: honour `TAGCORE_C_RPATH` for every binary
  of the crate (not only `tag-dwnld`), as the `app` build scripts do.
- `host/commandline-rs/src/bin/tag-log-import.rs`: the `log-import` example
  as a tool, built with the `sqlitelog` feature; retire the C++ one into the
  legacy option.
- Run the recorded hardware checks (item 2) on the bench, per
  `docs/bench/`, and record the results in the READMEs' status sections.
  The checklist is below.

**Progress, 2026-10-31.** The two code items are done. `build.rs` now gives
every `tag-tools` binary the rpath (`TAGCORE_C_RPATH`, else the link
directory). The Rust `tag-log-import` writes databases byte-identical to the
C++ tool's on a tagsim download, linked both ways, and tagsim's tests pass
with it; `taglogwriter`'s `log-import` example, which it supersedes, is
removed. Retiring the C++ tool waits for `BUILD_LEGACY_HOST` in phase 2, as
does building the Rust one into `bin/internal`. The activity filter is
recorded in `host/applications/sensorviz-rs/TODO.md` and the Tag View manual
page. Bench checks 1 to 4 below passed the same day on a BitTag V6 and the
IMUTagNandBmp581; the results are in `host/commandline-rs/README.md` and
Tag Programmer's README, and check 5 waits for the phase 2 packages.

Found on the bench, none blocking the release, to fix before or after it:

- An attach seconds after another tool's detach sometimes fails on its
  first debug read (status 0x81). **Fixed 2026-10-31:** the SWD enter sent
  just after nRST can reach a tag waking from Shutdown before its debug port
  is powered, and both libraries now retry a failed enter
  (`host/libraries/tagcore/design/investigations/2026-10-l4-attach-swd-enter.md`).
- An unplug during an L432 session logs about 2,500 identical USB errors in
  2.5 s, and a detach from a running U375 about 50; the rate limit added for
  the 0x81 reads does not cover them.
- `tag-test` labels the external flash size, which is in bytes, "kb" (the
  C++ tool does too).
- After Program, Tag Programmer's Tag row names the old firmware until the
  next attach.

#### Bench checklist

Each check follows the bench rules in the root `AGENTS.md`: the Joulescope
app and qtmonitor detached, the supply checked for the target (2.5 V for an
L432 tag, 3.7 V for an IMUTag) and recorded, the board UUID read and
recorded, and no re-attach to a running tag. Run the tools from a
`--features sqlitelog` release build of `tag-tools`.

1. **`tag-start --config` and `--merge`, on a BitTag** (IDLE): program a
   configuration file with `tag-start --config FILE`, read it back with
   `tag-info --json` and compare; repeat with `--merge` and a file holding
   fields the BitTag lacks, and confirm only its own fields changed. Stop
   and erase with `tag-reset` afterwards.
2. **`tag-test` on an IMUTag**, after a fresh `tag-info`: the self-tests,
   including the external-flash test, which can write the NAND bad-block
   map; record the result and the bad-block count before and after.
3. **`tag-side-channel-check`'s opt-in checks**, on one L432 tag and the
   IMUTag: `--write-test ADDR` with a word-aligned SRAM address outside
   monitor_shared (on the L432, below `0x2000_C000`, which the tool's range
   check does not enforce); `--unplug N`, unplugging the base when told; and
   `--start-running`, which needs a tag already RUNNING and so attaches to
   one. That is a permission test, one of the two reasons the bench rules
   allow; the attach ends the run, so use a run whose data is not wanted,
   then `tag-reset` to stop and erase.
4. **Programming a U375 through Tag Programmer's engine**:
   `cargo run -p tagprogram-core --example bench -- <scratch-dir> <release>
   <label>` on the IMUTagNandBmp581, then `--retest`; erase stored data
   first if the tag is FINISHED or ABORTED, and run `tag-test` after.
   Record the image in the IMUTag flash log.
5. **The packaged applications on Windows and Linux**: Tag Calibrator
   streaming from a CompassTag, and Tag View opening a CompassTag and an
   IMUTag log, beyond the "starts and stays up" check. This needs the
   phase 2 packages, so it moves to the phase 5 checklist if phase 2 is not
   done first.

### Phase 2: CMake

- `CMakeLists.txt` (top level): add `option(BUILD_LEGACY_HOST "Build the Qt
  applications and the C++ tools that the Rust programs replace" OFF)`.
  Make `BUILD_QT_APPS` follow it (keep the name as an alias for one release,
  with a warning, so presets and scripts that set it still work).
  `BUILD_HOST_DOCS` no longer defaults from `BUILD_QT_APPS`; default it ON.
  `BUILD_RUST_APPS` becomes ON regardless of whether cargo is found, with
  the error message pointing at rustup (a host build without Rust is now
  not a host build); keep an explicit `-DBUILD_RUST_APPS=OFF` for
  firmware-only machines.
- `host/CMakeLists.txt`:
  - `libraries/qcustomplot`, `libraries/sensorui`, the four remaining Qt
    applications and `host_common`'s Qt definitions under
    `BUILD_LEGACY_HOST`;
  - `applications/btdataviz` deleted outright (decided 2026-10-31), with its
    entry in `host_qt_install_targets`, its manual page `apps/btdataviz.md`
    and images, and its notices; `git log` keeps the source if it is ever
    wanted;
  - `host_qt_install_targets` empty unless legacy;
  - `host_cli_install_targets` reduced to `tag-txt-import` and
    `dataprocessing` (C++), and a new list of the Rust tools installed from
    the `tag-tools` build;
  - the macOS code-signing loop extended to the Rust tools (as
    `commandline-rs/CMakeLists.txt` already does for `tag-flash`), the
    Windows launcher arrangement for `tag-dwnld` (which needs
    `tagcore_c.dll` beside it in `tag_tools/lib`), the Linux install into
    `tag_tools/bin`.
- `host/commandline/CMakeLists.txt`: the thirteen replaced tools under
  `if(BUILD_LEGACY_HOST)`; `dataprocessing`, `tag-txt-import` and the
  recovery tools unconditional.
- `cmake/HostRust.cmake`: a `host_rust_tools()` function (or extend
  `rust_flash_tools()`) that builds the whole `tag-tools` crate with
  `--features sqlitelog` once, copies the shipped tools to `bin/` and the
  bench tools to `bin/internal/`, and depends on `tagcore_c_static` or
  `tagcore_c`. `flash_tools` folds into it; the embedded targets' dependency
  name is kept.
- `host/libraries/sensoranalysis/CMakeLists.txt`: the Qt layer and the
  Qt-comparing tests (`calibration_dump`, `compass_core_parity_check`,
  `capture_replay`) under the legacy option; `sensoranalysis_core` and
  `sensoranalysis_core_json` stay.
- `host/applications/sensorviz/CMakeLists.txt`: the Qt application under the
  option; `core/` stays (Tag View's).
- `cmake/CheckQmlImports.cmake`, `DeployQt*.cmake`, `PruneMacosQml.cmake`:
  unchanged, used only under the option.

**Progress, 2026-10-31.** Done as described, with these choices made on
the way:

- `BUILD_QT_APPS` is no longer an option. Set to ON it is treated as
  `BUILD_LEGACY_HOST=ON` with a warning; set to OFF it is ignored with a
  warning. The presets no longer set it.
- In a legacy build with the Rust programs, the replaced C++ tools build into
  `bin/legacy` and are not installed, since the Rust tools take their names;
  in a legacy build without them (`BUILD_RUST_APPS=OFF`) they build and
  install as in v3.5, which is the fallback release.
- The tools are one `cargo build -p tag-tools --features sqlitelog --bins`,
  target `rust_tools` (`host_rust_tools()` in `cmake/HostRust.cmake`), with
  the Linux run-time search path `$ORIGIN/../lib:$ORIGIN/../../lib` so the
  internal tools find `libtagcore_c.so` from `bin/internal` too.
  `flash_tools` depends on it in a host tree, and builds the two flash tools
  alone in a tree without the host tools.
- On Windows the ported tools install as the C++ ones did, the program in
  `tag_tools/lib` beside `tagcore_c.dll` and the console launcher under its
  name; `tag-flash` and `base-flash` stay where they were.
- The build report looks for Qt only in a legacy build.
- `btviz` is deleted with its manual page, images, the three notices only it
  needed (Kiss FFT, FastFIR, SOLPOS) and its rows in the licence indexes.

Checked on macOS: a fresh default configure looks for no Qt and builds the
four applications, the nine shipped tools, `dataprocessing` and
`tag-txt-import` into `bin/`, and the developer tools into `bin/internal`; the
install has the four application bundles and those eleven tools in
`tag_tools/bin`; the installed `tag-info` attached to a BitTag; the
CMake-built `tag-log-import` wrote a database identical to the C++ tool's; a
legacy configure through `-DBUILD_QT_APPS=ON` warned, put the replaced C++
tools in `bin/legacy`, and built and deployed `qtmonitor` and `qtcalibrate`.
Not yet checked: a Linux or Windows build (phase 3's CI runs both), and the
licence folder, which still installs the Qt notices (phase 3).

### Phase 3: CI and release tooling

- `.github/workflows/release.yml`: remove the Qt install step and
  `QT_VERSION` from the default matrix; configure with
  `-DBUILD_LEGACY_HOST=OFF`; keep one optional job (manual dispatch) that
  builds the legacy configuration so it does not rot. Remove the
  `REPRODUCIBLE_BUILD` Qt-version check from the default path (it stays in
  CMake for the legacy build).
- `host/tools/linux-package-test.sh`: the tool list adds nothing (same
  names); drop the Qt TLS-plugin check and the Qt application captures; add
  the Rust applications' headless checks (and a `tagprogram` release-listing
  check if its mock or `smoke` example can be driven in a container).
- `host/tools/verify-macos-dmg.sh`: expect four bundles; the per-bundle Qt
  checks stay conditional.
- `host/tools/release-macos.sh`, `sign-ci-macos.sh`: no change expected
  (they sign whatever is in the image); confirm.
- `docs/release/release-procedure.md`: the app count, the smoke-test list
  (launch the four applications, `tag-info --help`, `dataprocessing --help`),
  and the Qt pin table entries marked legacy.
- `LICENSES/host/README.md`: the GPL section goes (no QCustomPlot, no Qt
  Quick 3D in the default packages); the Qt and QCustomPlot rows move to a
  "legacy build only" note or go; `Qt-THIRD-PARTY-NOTICES.txt` is no longer
  installed by default. The Rust notices are regenerated as usual.

**Progress, 2026-10-31.** Done, with these choices:

- `release.yml` installs no Qt and configures with `BUILD_LEGACY_HOST=OFF`.
  Rather than a second job, `workflow_dispatch` has a `legacy` input that
  installs Qt (and, on Linux, Qt's build headers) and builds the legacy
  configuration; such a run never publishes, and its artifacts end in
  `-legacy`. The release job runs only on a tag push. `docs.yml` no longer
  passes the old option.
- `linux-package-test.sh` checks the nine Rust tools and the two C++ ones,
  starts the four Rust applications under Wayland and X11, and runs the Qt
  captures and the Qt TLS check only for the Qt programs a (legacy) package
  carries. `linux-build-check.sh` builds the Qt-free host against the
  distribution's libraries by default; `--legacy` adds the distribution's Qt
  and the QML import check, as before. The README's Linux section changed with
  it, since the script runs its commands.
- `verify-macos-dmg.sh` fails an image missing any of the four Rust
  application bundles. `release-macos.sh` and `sign-ci-macos.sh` sign
  whatever the image holds and needed only a comment.
- The licence folder: the index has no GPL programs, and a "Legacy Qt
  applications" section holds Qt, the Qt third-party notices, QCustomPlot
  and the QFlightInstruments artwork, which a default install leaves out. The
  Freescale calibration notice now names Tag Calibrator, which contains it
  through `tagcore_c`. The Rust tools, which had shipped without a notices
  file since `tag-flash` (v3.5), now have one,
  `tag-tools-THIRD-PARTY-NOTICES.txt` (300 crates); the four applications'
  files were regenerated and had not changed.
- The application directories' comment-only `Cargo.toml` files, left by the
  workspace change, made cargo fail inside an application; they are deleted.

Not yet run: the workflow itself and the Linux package test, which need a
push (a `workflow_dispatch` run, then one with `legacy`).

### Phase 4: documentation

User manual (`host/docs/`):

- `mkdocs.yml` nav and `src/index.md`: one entry per product, pointing at
  the Rust applications' pages (`tagprogram.md`, `tagmonitor.md`,
  `tagcalibrate.md`, `tagview.md`). **Decided 2026-10-31:** keep
  `qtprogram.md`, `qtmonitor.md`, `qtcalibrate.md` and `sensorviz.md` for
  this one release under a "Legacy" heading in the nav, each with a note at
  the top that the program is no longer in the packages and is built only
  with `BUILD_LEGACY_HOST`; remove them and their images (the `qtmonitor-*`,
  `qtcalibrate-*`, `qtprogram-*`, `sensorviz-*`, `TagMonitor*` files) in the
  release after. `btdataviz.md` and its images go now, with the program.
- `workflows/*.md`: name the Rust applications; the firmware-programming
  workflow drops STM32CubeProgrammer.
- `cli/*.md`: the pages describe behaviour the Rust tools share; add the
  Rust-only differences (option errors exit 2); `tag-dwnld.md` notes the
  file is written by the same writer. `tag-log-import` stays internal and
  gets no page.
- `reference/config-files.md` already names Tag Monitor; check the
  remaining pages for "qtmonitor".

Developer documentation:

- `host/README.md`, `host/applications/README.md`, `host/libraries/README.md`,
  `host/AGENTS.md`: "both ship" becomes "the Rust applications ship; the Qt
  applications build with `BUILD_LEGACY_HOST` for reference and oracles".
- The guide: chapter 1 (two implementations), 2 (the tree), 9 (which tools
  are installed), 11 (the Qt applications, briefly), 12 (Qt row), 13
  (options, packages, CI). One pass through each.
- `docs/architecture/overview.md` host paragraph; the root `README.md`
  prerequisites (Qt only for the legacy build; Rust required).
- Each Rust application's README: status "shipped as the product".
- `docs/decisions/0042-host-rust-default-release.md`: this plan as carried
  out, with the evidence.
- `docs/bench/*.md` and `embedded/tools/*.py` docstrings that say "detach
  qtmonitor": "detach Tag Monitor (tagmonitor)"; the `pgrep` pattern in
  `power-testing.md` gains `tagmonitor`.

### Phase 5: the release

Follow `docs/release/release-procedure.md` with the updated checks. The
version is `v4.0` (decided 2026-10-31): the package is at `v3.5`, and a
release that changes every application's implementation and drops Qt is a
major one. Steps particular to this release:

1. Tag firmware is unchanged; no firmware qualification is needed unless a
   firmware fix is folded in.
2. Build the three packages on CI with the default (Rust) configuration;
   run `linux-package-test.sh`; sign and notarise the macOS image locally.
3. On the bench, with the installed package: program a tag with Tag
   Programmer, configure and start it with Tag Monitor, stop and download
   it, erase it, view the download in Tag View, calibrate a CompassTag or
   IMUTag with Tag Calibrator and write the constants; run each shipped
   command-line tool once against a tag. Record the results in the release.
4. Release notes: the Rust applications replace the Qt ones; the names of
   the executables; STM32CubeProgrammer no longer required; the activity
   filter's status; the legacy build option for anyone who needs the Qt
   programs.

## Checks that say it is done

- [ ] A default configure on a machine without Qt succeeds and builds every
      shipped program and `bin/internal`.
- [ ] `BUILD_LEGACY_HOST=ON` builds the Qt applications and the replaced C++
      tools as before, and the oracle regeneration procedures work.
- [ ] `cmake --install` lays out `tag_tools` as the packaging tables say on
      all three platforms; `ldd`, `otool -L` and the DMG verifier are clean.
- [ ] `linux-package-test.sh` passes in clean containers.
- [ ] The bench checklist of phase 5 passes with the installed package.
- [ ] `docs.py check` passes; the manual builds; no page names a Qt
      application as current.
- [ ] The notices files match the lock; `LICENSES/host/README.md` describes
      the packages as built.

## Risks and the fallback

- **A regression in a Rust tool found in the field.** The legacy option
  builds the C++ tool of the same name; a fix can ship either as a Rust fix
  or, worst case, as a legacy build. Keep the option working for at least
  this release and the next.
- **Windows.** The Rust tools have been proven on Windows for `tag-flash`
  and the applications; `tag-dwnld` with `tagcore_c.dll` on Windows has not
  been exercised in a package. Phase 1 covers it; the package test on
  Windows is manual.
- **Users with scripts calling the C++ tools by path** are unaffected: names
  and output are the same, and `bin/` is the same place.
- **CI without Qt** is faster and simpler, but the legacy job must still
  run occasionally or the legacy build rots; a monthly or manual-dispatch
  job is enough.

## Decisions

Settled 2026-10-31:

- Tag View's activity filter: release without it; port it later.
- The Qt manual pages: keep one release under a "Legacy" heading, then
  remove.
- `btviz`: delete the source; its logs open in Tag View.
- `tag-log-import`: Rust, but internal (`bin/internal`, not installed).
- The version number: `v4.0`.

Nothing is open; the work can start with phase 1.

## Effort

| Phase | Estimate |
| --- | --- |
| 1. Gaps (without the activity filter) | 2 to 3 days, one of them on the bench |
| 2. CMake | 1 day |
| 3. CI and release tooling | 1 day, plus CI runs |
| 4. Documentation | 1 to 2 days |
| 5. Release | the procedure's usual day, plus the bench checklist |
