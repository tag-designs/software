---
type: decision
status: accepted
summary: Tag images embed only commit-derived identity (GIT_DATE instead of __DATE__/__TIME__, clone properties pinned in version.cmake), so a commit determines its image byte for byte across machines.
---

# 0010. Build: an image is determined by its commit

Date: 2026-09-28 (widened at `fw-v0.0.3`, 2026-09-29)

Compile-time timestamps and clone-dependent git strings were removed from the
image so that the same commit builds the same bytes on any machine. Verified
within one machine, across Linux and macOS, and across all five distributed
tags. Extracted verbatim from the "What was not done" section of
[Tag Firmware Build Reproducibility](../build/firmware-reproducibility.md).

## Determinism, verified within and across machines

`monitor.c` used to embed `__DATE__ " : " __TIME__`, which made byte-identical
rebuilds impossible by construction: the same commit produced different bytes
every time, however carefully the inputs were pinned.

It now carries `GIT_DATE` instead -- the commit's author date, from
`version.cmake`, in `iso-strict` form so that it does not vary with the
builder's timezone. The field still reports a date, which the tags rely on; it
reports when the source was committed rather than when someone happened to
compile it, which is the more useful answer from a tag in hand anyway. It is
always exactly 25 characters against a 30-byte field.

**The measurement has been taken, and it passes.** A distributed tag built twice
from a clean tree, into two different build directories, produced a
byte-identical `.bin`. The differing build paths make that stronger than it
sounds: anything leaking a build location into the image -- `__FILE__`, a debug
path, a temporary name -- would have shown as a difference, and none did.
`__DATE__`/`__TIME__` now appear nowhere that compiles into a distributed tag,
not in ChibiOS' HAL, RT or common code, and only in `BitTag-legacy`, a prototype
that does not build.

This closes the loop the document opened with. A commit determines its image, so
the SHA-256 recorded in a board database can be re-derived from the commit alone
by anyone with the repository -- the forward question answering the backward
one, and no longer dependent on the archive surviving.

## Across machines: verified, after three clone properties were removed

That comparison has now been made, between a CI image from `fw-v0.0.1` and a
local rebuild of the same commit -- Linux against macOS, Arm's tarball against
Homebrew's build of the same 14.2.1. The images are the same size and **63 of
33416 bytes differ**, which is the useful part of the result: had the toolchains
disagreed, the sizes would have moved and the differences would be everywhere.
They are not. The compiled code is identical. Every differing byte is a string
git supplied, or a pointer displaced by one:

| | CI | local | cause |
| --- | --- | --- | --- |
| `GIT_REPO` | `https://github.com/...` | `git@github.com:...` | how the clone was made |
| `VERSION_HASH` | `a3f596d0` | `a3f596d` | `--short` picks the shortest unambiguous abbreviation, which depends on the object count |
| `GIT_DATE` | `...:30Z` | `...:30+00:00` | git renders UTC differently between versions, even under `iso-strict` |

All three are now pinned in `version.cmake`: `--short=8`, an explicit strftime
format under `TZ=UTC` rather than a format git chooses, and the remote reduced
to `host/owner/repo` so that SSH and HTTPS clones agree.

**That closed it.** At `fw-v0.0.2`, a CI image and a local rebuild of the same
commit are byte-identical: `24d009da...` on a Linux runner using Arm's tarball
and on macOS using Homebrew's build of 14.2.1, twenty-four seconds apart. The
two build manifests still record different remotes -- `https://github.com/...`
against `git@github.com:...` -- which is what proves they are genuinely
different clones on different machines rather than the same artifact compared
with itself.

That difference belonging in the manifest and not in the image is the whole
distinction: the manifest describes a build, the image describes a commit.

The lesson generalizes past these three. Anything derived from the *clone*
rather than the *commit* is a reproducibility hazard, and git's conveniences --
abbreviation, date rendering, remote URLs -- are all clone properties wearing
commit clothing.

That result has since been widened. All five distributed tags were rebuilt at
`fw-v0.0.3` and compared against the images that release published: every
`.bin` and `.hex` is byte-identical, and each matches the SHA-256 its own
manifest records. The two builds differ in almost every way a build can -- a
Linux CI runner against macOS, Arm's tarball against Homebrew's build of
14.2.1, and `REGENERATE_SOURCES=OFF` against `AUTO` -- so the comparison also
establishes something neither build alone could: **regenerating the committed
sources is a no-op.** One build consumed them untouched, the other rebuilt them
from their inputs, and the images agree.

The `.elf` files differ by 32 bytes, which is the DWARF path of a different
build directory and is not in the flashed bytes. The distinction is the same
one this document draws throughout: the manifest describes a build, the image
describes a commit.
