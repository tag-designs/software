---
type: readme
status: current
summary: The firmware and host build model; setup steps per platform are in the root README.
---

# Build

Per-platform setup and build commands are in the [root README](../../README.md).
This directory holds the build model: what is generated, what is pinned, and
what makes a firmware image reproducible.

| Document | What it gives you |
| --- | --- |
| [Tag Firmware Build Reproducibility](firmware-reproducibility.md) | Which tags are reproducible, what is committed and checked, what each image records, and how to promote a tag, bump ChibiOS, nanopb or the toolchain |
| [Build worklist](TODO.md) | Open gaps and questions in that model |
| [Embedded source layout](../../embedded/design/source-layout.md) | How boards, proto-c, tags, bases and loaders fit together in the firmware build |

Releasing what the build produces is in [docs/release](../release/README.md).
