# AGENTS.md

This file is for agents working under `host/`. It summarizes host-side
ownership, dependency direction, and common checks. The orientation for a
human newcomer is the [Host Software Guide](design/guide/README.md); the
directory map is [README.md](README.md).

## Directory Ownership

C++:

- `libraries/tagcore/`: low-level tag access, protocol-facing helpers, download
  support, text/SQLite log writing, the SWD recovery library. Keep it Qt-free.
- `libraries/tagcore-c/`: the C interface (`tlw_*`, `tcs_*`, `svz_*`) through
  which Rust reaches the C++ libraries. Every entry point catches every
  exception; a signature change raises the interface version in the header.
- `libraries/sensoranalysis/`: UI-free sensor math; the arithmetic is in the
  Qt-free `core/`, which must stay bit-identical to the Qt-typed layer.
- `libraries/sensordata/`: Qt-free decimating log reader (sqlite3 only).
- `libraries/sensorui/`: reusable Qt/QML widgets and dialogs.
- `libraries/qcustomplot/`: vendored plotting support.
- `common/`: small Qt helper headers shared by the Qt applications.
- `applications/{qtmon,qtprogram,qtcalibrate,sensorviz,btdataviz}/`: the Qt
  applications.
- `commandline/`: the C++ CLI tools, mostly on `libraries/tagcore`.
- `tagserver/`, `python/tagclient/`: the gRPC test server and its client
  (`BUILD_TAG_SERVER=ON` only).

Rust (one workspace, `host/Cargo.toml`):

- `libraries/tagcore-rs/` (crate `tagcore`): the tag interface. A change to
  the wire behaviour of the C++ `tagcore` is made here too, in the file the
  README's map names.
- `libraries/tagprobe-rs/` (`tagprobe`): programming through probe-rs and DFU.
- `libraries/tagcore-c-sys/`, `taglogwriter-rs/`, `calibsession-rs/`,
  `sensorviz-session-rs/`: the link to `tagcore_c` and the safe wrappers over
  its three interfaces. All `unsafe` lives in the wrappers.
- `commandline-rs/` (`tag-tools`): the Rust CLI tools; same names, options,
  messages and exit status as the C++ ones.
- `applications/*-rs/`: `core/` holds every rule and all state; `ui/` draws
  only; `app/` is the Tauri shell. Each names the C++ code it ports.
- `ui-kit/`: the shared web front-end kit (CSS, Preact components, engine
  connection). Classic scripts, no build step, vendored libraries.

## Dependency Direction

Dependencies flow from applications toward libraries, and from Rust toward
C++ (through `tagcore_c`), never the other way. The diagram is in
[README.md](README.md#dependency-direction). Do not add Qt dependencies to
`libraries/tagcore`, `libraries/tagcore-c` or `libraries/sensordata`. Do not
port C++ logic that `tagcore_c` can expose; the rule is one implementation of
each format and algorithm.

## Application Notes

- A Qt application and its Rust port are held to the same behaviour by shared
  oracles in the Qt application's `test/` directory (qtmonitor's
  `action-states.txt`, `start-checks.txt`, `menus.txt`, the probe `cases/` and
  `golden/`; qtcalibrate's replay fixtures; sensorViz's state dump). A
  behaviour change updates the oracle in the same commit, and the Rust tests
  then fail until the port matches. Deliberate differences are listed in each
  Rust application's `design/ui-engine-contract.md`.
- In a Rust application, a new dialog is a prompt in the view state answered
  by id; a new rule goes in `core/`, never in `ui/app.js`. Keep
  `ui/mock-engine.js` and the contract in step with the view state.
- `applications/qtprogram`, `btdataviz`, and related apps keep one-off
  behavior local until a second app genuinely needs it.

## sensorViz Conventions

- The Qt-free core (`applications/sensorviz/core/`) owns the log reader,
  transforms, plot spec, view model and preferences; `tagview` runs the same
  core through `sensorviz-session`. A change to display policy goes in the
  core, so both front ends show it.
- Stream visibility, colors, axis sides, and ranges are view concerns, not
  SQLite-log schema concerns.
- Menu-bar structure and plot context-menu structure should mirror each other
  when practical, in both front ends.
- Tag-specific controls should be hidden unless the loaded log contains the
  stream/record set/calibration that makes them useful.

## Build And Verification

Use a focused target when possible:

```sh
cmake --build <build-dir> --target sensorviz
cmake --build <build-dir> --target qtmonitor
cmake --build <build-dir> --target tagmonitor        # a Rust app, through cargo
cmake --build <build-dir> --target tagcore_c_static  # the C interface (macOS; tagcore_c elsewhere)
cmake --build <build-dir> --target docs
```

For Rust, from `host/` (the workspace root):

```sh
cargo test -p <crate>                      # crates without tagcore_c need nothing else
TAGCORE_C_LIB_DIR=<build-dir>/Release/lib cargo test -p calibsession   # those that link it
cargo clippy --all-targets -p <crate> 2>&1 | grep -c "missing documentation"   # expect 0
RUSTDOCFLAGS="-D warnings" cargo doc --no-deps --document-private-items -p <crate>
```

Every crate warns on `missing_docs` and `clippy::missing_docs_in_private_items`;
keep both at zero. `cargo build --release --locked` is what CMake runs, so a
dependency change must update `host/Cargo.lock` and regenerate the four
notices files (`LICENSES/tools/generate_rust_notices.py`).

Run `git diff --check` before finishing. If a change touches packaging or
install rules, also verify the relevant package/install target when practical.

## Documentation

- Update app-level READMEs when changing structure or user-visible workflows.
- Update `host/docs/src/apps/*.md` for user-facing guide changes.
- MkDocs dependencies live in `host/docs/requirements.txt`.
- The [guide](design/guide/README.md) describes the architecture of the whole
  host tree; when a chapter's subject changes (a layer, a contract, a thread
  model, a dependency), change the chapter.
