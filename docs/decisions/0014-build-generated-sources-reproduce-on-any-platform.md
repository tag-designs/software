---
type: decision
status: accepted
summary: Committed generated sources may be regenerated on any platform, and a separate weekly CI job regenerates them with pinned tools and fails on any difference.
---

# 0014. Build: generated sources reproduce on any platform, checked by a separate job

Date: 2026-09-30

No platform is authoritative for regenerating the committed nanopb and board
sources: measurement showed nanopb's output platform-independent (2026-09-28),
and `generated-sources-reproduce.yml` now shows the same for the board files
every week. That job is deliberately separate from the freshness check.
Extracted verbatim from [Tag Firmware Build Reproducibility](../build/firmware-reproducibility.md).

## Evidence: generator output does not depend on the platform

For nanopb 0.4.9.1, the macOS x86 release binary and the Linux PyPI wheel produce
**byte-identical** output: all 24 files across the six variants tested compared
equal, `.pb.c` and `.pb.h` alike, line endings included. The two runs also used
different include-path spellings, so the output is not sensitive to that either.

A regeneration on a Mac can therefore be committed directly; there is no need to
make Linux authoritative or to route protocol changes through a particular
machine. This is what makes the CI freshness check meaningful -- had it not held,
the check would have failed for everyone not building on the blessed platform.

## Evidence: the board files do not depend on the machine that rendered them

This was the last open question about `fmpp`, and it now has a standing answer
rather than a one-off measurement.

`generated-sources-reproduce.yml` installs the pinned generators on a Linux
runner, regenerates every committed source, and compares. It passes: all five
proto variants and all five boards come back byte-identical to files rendered
on macOS, with a different JVM underneath fmpp and a different platform under
everything.

nanopb's output had already been shown platform-independent -- the macOS
release binary and the Linux wheel producing identical output across 24 files.
fmpp's never had been, and it was the weaker case of the two, since a template
renderer on a JVM has more ways to inherit machine state than a Python code
generator does. It reproduces.

The result is worth more as a job than as a measurement. A measurement says the
files matched on the day someone checked; the job says they still match every
week, and says so loudly on the day they stop.

## Decision: the freshness check cannot detect a divergent generator -- a second job does

`CheckGeneratedSourcesFresh.cmake` compares the recorded input hashes against
the tree. It does not re-render the output and diff it. The difference matters
for any generator whose own version is not recorded -- `fmpp` above all.

If a different `fmpp` rendered `board.c` differently, the developer would
commit that output alongside a perfectly correct `inputs.sha256` -- the inputs
really did not change, only the renderer did -- and every check thereafter
would pass. `AUTO` then seals it: no other machine regenerates, because the
recorded inputs still match, so the competing output that would expose the
disagreement is never produced. The failure is self-certifying and stable.

Pinning the renderer prevents this; it does not detect it. Detection is what
`generated-sources-reproduce.yml` adds: it installs the pinned generators,
regenerates every committed source, and fails if the generated code differs
from what is in the tree. `inputs.sha256` is excluded from that comparison,
since it records the tool versions the runner had and those may legitimately
differ; the generated code may not.

It is a separate workflow rather than an extension of
`embedded-reproducibility.yml`, and deliberately so. That check was built to
need nothing but CMake and a checkout, on the reasoning that a check proving
the generators unnecessary must not itself require them. Detection needs the
opposite -- every generator installed -- so the two cannot be the same job
without destroying the property the first one demonstrates. It runs weekly and
on demand rather than per pull request, for the same reason.
