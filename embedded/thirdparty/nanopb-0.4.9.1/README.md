# Vendored nanopb runtime 0.4.9.1

These are the nanopb runtime sources compiled into every tag image. They are
vendored rather than referenced from a per-developer install so that the exact
bytes that went into a shipped image are recoverable from this repository's
history alone, without depending on an upstream archive still being available.

Only the runtime is here. The **generator** is a build-time tool and is not
vendored; it is pinned by version and checked at configure time. See
[Tag Firmware Build Reproducibility](../../../design/tag-build-reproducibility.md).

## Provenance

| | |
| --- | --- |
| Version | 0.4.9.1 |
| Upstream | <https://github.com/nanopb/nanopb> |
| Tag | `0.4.9.1` |
| Commit | `cad3c18ef15a663e30e3e43e3a752b66378adec1` ("Releasing 0.4.9.1 bugfix release", 2024-12-01) |
| Obtained | `git clone --depth 1 --branch 0.4.9.1 https://github.com/nanopb/nanopb.git` |
| Licence | zlib, see `LICENSE.txt` |

`CHECKSUMS.txt` records the SHA-256 of each file as vendored.

## Files

`pb.h`, `pb_common.{c,h}`, `pb_encode.{c,h}`, `pb_decode.{c,h}` — the complete
runtime. `embedded/tags/common/modules/protocol_nanopb.mk` lists the three `.c`
files; make resolves them through `VPATH`.

## Do not edit

Local changes here are compiled into shipped firmware while being invisible to
anyone reading upstream nanopb. To change the version, replace this directory
with a new `nanopb-<version>/`, update the pin, and regenerate every `.pb.*`
with the matching generator, in one commit.

## Relationship to what preceded it

Before this directory existed, the runtime came from a per-developer path
(`NANOPB_SRC_ROOT_FOLDER`). On the machine that built the current firmware that
path held a git clone at `nanopb-0.4.8-11-g1f0c2e1`, an untagged master
snapshot whose `pb.h` self-reports `0.4.9-dev`, while the generator producing
the `.pb.*` sources reported `0.4.9.1`. Adopting 0.4.9.1 here makes the runtime
and the generator agree for the first time.

Relative to that snapshot, 0.4.9.1 changes only: the version string; a
refactor introducing `PB_BYTE_T_OVERRIDE` and defining `pb_type_t` as
`pb_byte_t` (both resolve to `uint8_t` wherever `UINT8_MAX` is defined, so the
generated layout is unchanged on the STM32 targets); IAR detection added to the
`checkreturn` attribute macro, inert under GCC; and comment reflow. Field order
in `pb_ostream_s` and `pb_istream_s` is unchanged.
