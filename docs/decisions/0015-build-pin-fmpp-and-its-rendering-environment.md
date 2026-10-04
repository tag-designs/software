---
type: decision
status: accepted
summary: fmpp is pinned by a configure-time version check (0.9.16) and board.fmpp.in pins locale and encodings; the fmpp version stays out of the board manifests.
---

# 0015. Build: pin fmpp and its rendering environment

Date: 2026-09-30

The board-file renderer is pinned in two parts, without adding its version to
the board manifests. Extracted verbatim from the "What was not done" section of
[Tag Firmware Build Reproducibility](../build/firmware-reproducibility.md).

## Decision, alternatives and consequences

The board manifests hash the ChibiOS templates but not the renderer, so a
different `fmpp` could in principle render them differently and the manifests
would not notice. Two changes closed that without touching the manifests.

**`FMPP_VERSION` pins 0.9.16** and configure compares what `fmpp` reports
against it, routing a disagreement through `reproducibility_problem` like the
ARM toolchain check. fmpp's last release was September 2018, so there is
effectively one renderer in circulation and this is an assertion rather than a
constraint. It is a check and not a manifest entry, which is what avoids the
problem described next.

**`board.fmpp.in` pins the rendering environment** -- locale, number format and
both encodings -- which FreeMarker was otherwise taking from the machine. That
mattered more than the version: the templates interpolate numbers in 54 places
via `?number` and 156 more via `?index`, `?size` and `?counter`, none guarded
with `?c`, and `board.c` is C source, so a locale that groups digits
differently produces a file that does not compile. `board.fmpp.in` is itself a
hashed input of every board manifest, so this pin is covered by the existing
freshness check with nothing added to it.

The original reasoning for leaving the version out of the manifests still
stands and is why it stayed out: Pinning it was rejected because a version string reported on
one machine and not another would cause false staleness across the group, which
would be worse than the risk. The per-image build manifest does not record it
either -- that would be a small, easy addition.

That reasoning was sound while the renderer was an uncontrolled desktop
install, and `config-gen` has since shown what removes it. Its protobuf is
pinned in a `requirements.txt` and installed into a virtualenv the build makes
itself, so every machine reports the same version because every machine runs
the same bytes, and the version becomes recordable rather than a source of
false staleness. The same pattern would work for `fmpp` and the JRE under it,
with one difference worth weighing: a Java runtime is not a pip install, so it
would mean either a documented download or a container.

Whichever way that goes, **the pin and the manifest entry have to land
together.** Recording `fmpp` in `inputs.sha256` while desktop regeneration is
still permitted reintroduces exactly the false staleness that was avoided, and
the commit that adds it invalidates every board manifest and requires a
regeneration alongside.
