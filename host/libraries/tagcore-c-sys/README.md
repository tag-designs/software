---
type: readme
status: current
summary: tagcore-c-sys, the one Rust crate that links the tagcore_c library and declares its C functions (tlw_*, tcs_*, svz_*), so the safe wrappers over it - taglogwriter, calibsession, sensorviz-session - can be used together in one program.
---

# tagcore-c-sys (Rust)

Links [`tagcore_c`](../tagcore-c/README.md), the C interface to the C++ host
libraries, and declares its functions: one module of raw declarations per
header, written by hand.

| Module | Header | Safe wrapper |
| --- | --- | --- |
| `tlw` | [`taglogwriter_c.h`](../tagcore-c/taglogwriter_c.h) | [`taglogwriter`](../taglogwriter-rs/README.md) |
| `tcs` | [`calibsession_c.h`](../tagcore-c/calibsession_c.h) | [`calibsession`](../calibsession-rs/README.md) |
| `svz` | [`sensorviz_c.h`](../tagcore-c/sensorviz_c.h) | [`sensorviz-session`](../sensorviz-session-rs/README.md) |

Programs use the wrappers, not this crate. It exists because Cargo allows
only one crate in a build to declare `links = "tagcore_c"`: while each wrapper
linked the library itself, a program could not use two of them.

## Building

```sh
cmake -DBUILD_TAGCORE_C=ON <build-dir>
cmake --build <build-dir> --target tagcore_c_static tagcore_c
export TAGCORE_C_LIB_DIR=<build-dir>/Release/lib
cargo build                          # macOS: libtagcore_c.a; Linux, Windows: shared
TAGCORE_C_LINK=dylib cargo build     # force the shared library
```

The link follows the platform's packaging: static on macOS, where each
application is its own bundle; shared on Linux and Windows, where one copy
serves every tool in the install directory. `build.rs` says how each is
linked. A dependent's build script that sets a run-time search path reads
`DEP_TAGCORE_C_LIB_DIR`, which Cargo gives only to direct dependents of this
crate (see `host/commandline-rs/build.rs`).

## Maintaining

When a header changes, change its module here and the safe wrapper over it,
together; each wrapper's "Maintaining" section has the steps, including the
interface version each must match. A new interface in `tagcore_c` gets a new
module here and its own wrapper crate.
