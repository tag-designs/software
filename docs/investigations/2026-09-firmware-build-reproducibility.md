---
type: investigation
status: closed
summary: How the nanopb runtime and generator were found disagreeing, and the first end-to-end release-firmware CI run, its toolchain-checksum failure and its 15-of-15 hash verification.
---

# Firmware build reproducibility: findings (2026-09)

Two findings from making the tag firmware build reproducible, September 2026,
extracted verbatim from [Tag Firmware Build Reproducibility](../build/firmware-reproducibility.md).
Closed: the nanopb mismatch led to the
[nanopb pin](../decisions/0011-build-pin-nanopb-0-4-9-1.md), and the CI run
completed with every recorded hash verified.

## How nanopb was found disagreeing with itself

Worth keeping, because it is the concrete failure this document was written
against.

`NANOPB_SRC_ROOT_FOLDER` originally supplied both the include path for the
*runtime* -- `pb_encode.c`, `pb_decode.c`, `pb_common.c`, `pb.h`, compiled into
every shipped image -- and the `generator-bin` hint for the *generator* that
produces the `.pb.*` sources. The tree was listed in `.gitignore` and supplied
per developer, so neither role had a recorded version.

The tree actually in use was a git clone at `nanopb-0.4.8-11-g1f0c2e1`, an
untagged master snapshot whose `pb.h` self-reports `0.4.9-dev`, while the
generator beside it reported `0.4.9.1`. Runtime and generated code disagreed in
every image built on that machine. `PB_PROTO_HEADER_VERSION` is 40 for both, so
the one safety net nanopb provides could not see it: it catches 0.3 against 0.4,
not 0.4.7 against 0.4.9.1.

## The first release-firmware CI run

`release-firmware.yml` has been run to completion on a runner via
`workflow_dispatch`. It produced all five distributed tags, seven files each,
with `reproducible_mode: true`, `regenerate_sources: OFF`, a clean tree, the
pinned toolchain at 14.2.1 and the expected ChibiOS commit. Every SHA-256 and
size a manifest records was re-checked against the bytes actually shipped: 15 of
15 matched. That is the backward question working -- bytes recovered from a tag
can be matched to an archived build with nothing to trust but the bytes.

Getting there took one real failure, worth recording because it is the failure
mode the pin exists for. The first run rejected the toolchain: the pinned hash
belonged to a different file in the same release, because Arm ships `x86_64`,
`aarch64` and `darwin-arm64` builds of 14.2.rel1 under near-identical names and
the checksum taken was the one a Mac is offered. The download was fine and the
pin was wrong. The workflow now prints the downloaded file's size, type and
computed hash alongside the pinned one, and the checksum Arm publishes beside
the file -- printed for comparison and never acted on, since a checksum served
by the same host as the file proves nothing about the file.
