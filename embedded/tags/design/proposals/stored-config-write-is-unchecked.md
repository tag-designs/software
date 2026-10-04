---
type: proposal
status: proposed
summary: writeStoredConfig() ignores whether the flash write succeeded on every tag but IMUTag, so a refused program leaves a stale configuration running while the host reports the new one. A cheap fix catches it; the full fix needs a page per target.
---

# The stored-configuration write is unchecked on every tag but IMUTag

`writeStoredConfig()` programs the tag's configuration into internal flash and
**ignores the result**. On an STM32L4 a program into a double word that has not
been erased is refused, and flash programming can only clear bits, so writing
over a populated configuration yields the bitwise AND of old and new. The tag
then runs a configuration nobody asked for while `tag-start` prints the one it
sent.

This is PresTag item **F3**, and it is already recorded as the reason the
2026-09 PresTag campaign was not treated as a release qualification. It is not
a PresTag bug: it is in seven of the eight implementations.

## Scope

| Target | Erase before program | Program result | Verify after |
| --- | --- | --- | --- |
| `families/IMUTag` | yes, `if (!storedConfigErased())` | — | yes, word by word, reports a mismatch |
| `families/PresTag` | no | ignored | no |
| `families/CompassTag` | no | ignored | no |
| `families/BitPresTag` | no | ignored | no |
| `families/BitTagNG` | no | ignored | no |
| `UIUCTag` | no | ignored | no |
| `BitTag` | no | ignored | no |
| `BitTag-legacy` | no | ignored | no |

## Why it has not simply been copied from IMUTag

IMUTag erases the region first, which is what makes its write reliable. It can
only do that because the linker gives the stored configuration **a page of its
own**: `TAG_STORED_CONFIG_OWN_PAGE` is defined as 1 by `IMUTagNand` and
`IMUTagNandBmp581` and by nothing else, defaulting to 0 in
`common/core/inc/flash_internal.h`.

On every other target the configuration shares a page with other persistent
records, so an erase would take `sEpoch` and the checkpoint headers with it.
That is why the other tags do not erase, and it is why this has lingered: the
obvious fix is unsafe where the obvious fix is needed.

## What it looks like when it bites

- PresTag: `tag-start` printed `period: 10` while the tag ran at 9 s.
- IMUTag, before its fix: `S400` (0x190) programmed over `S100` (0x64) gave
  0x000, an unspecified ODR that made `get_lsm_config()` fail and aborted
  collection at start.

Both are silent. The host reports success, and the failure surfaces later as
wrong data or an unexplained abort.

**It is reachable by ordinary bench use.** The attach-from-sleep workaround in
use on this rig issues every command twice, so `tag-start` programs the
configuration, and then programs it again into a region that is no longer
erased. Identical content makes that harmless today, but it is the exact
sequence the defect needs.

## Proposed fix, in two parts

**1. Verify and report, everywhere, now.** After programming, read the region
back and compare. On a mismatch, record it where a failure can be seen without
a debugger -- the scratchpad and the marker log both survive a tag that cannot
talk -- and fail the start rather than running on a configuration that was
never written. This needs no linker change, is safe on a shared page, and
converts a silent wrong configuration into a loud refusal. It does not make
the write succeed.

**2. Give the stored configuration its own page, per target.** Set
`TAG_STORED_CONFIG_OWN_PAGE 1` and place `sconfig` in its own section, then
erase before programming as IMUTag does. This is what actually fixes the write.
It is a linker change per target and wants measuring afterwards, since it
moves regions in internal flash -- see the calibration relocation in
[the CompassTag worklist](../../families/CompassTag/TODO.md) for the same class
of change and what it costs.

Part 1 is worth doing on its own and should land first: it is small, it is
safe, and it tells us whether part 2's absence is actually biting in the field
rather than only on the bench.

## Sequencing

Both parts change what ships, so they belong **before** the next release and
its qualification runs, not after. A qualification taken against an unverified
configuration write measures a tag that may not be running the configuration
the report records -- which is a worse failure than a qualification that fails,
because it looks like a pass.
