---
type: decision
status: accepted
summary: UIUCTag opens a log block lazily at its first sample, anchors the 5-minute sample grid at the run's first minute boundary, and permits hibernation at any sample boundary.
---

# 0004. UIUCTag: run-anchored sample grid, lazy block open

Date: 2026-08-31

Five changes made to the UIUCTag data-collection plan while building its
firmware stage (S3), cut verbatim from
[`embedded/tags/UIUCTag/design/data-collection.md`](../../embedded/tags/UIUCTag/design/data-collection.md).
The date is that of the commits adding UIUCTag data collection and anchoring
the sample grid at the run start.

## Decision

Five decisions were changed while building S3.

1. **Blocks open lazily, and the checkpoint is written after the sample it
   anchors.** The plan opened a block on entry and at each rollover, before
   writing samples. Instead the first sample of a block writes its pressure and
   temperature first, then appends the checkpoint. A reset in between now costs
   one block of samples rather than leaving a checkpoint that points at data
   which was never stored, and every checkpoint describes a block holding at
   least one sample.
2. **No sample at RUNNING entry.** The first sample lands on the first sample
   boundary after entry, so entry does not have to special-case a pressure
   conversion alongside accelerometer setup. At most one slot is lost at run
   start, and it is recorded as an ordinary gap.
3. **Hibernation is permitted at any sample boundary, not only a block
   boundary.** A resumed run anchors a fresh block at its own first sample, so
   there is no reason to defer hibernation by up to two hours. The wake that
   enters hibernation still stores its sample first.
4. **Activity spanning a reset is left unwritten rather than reconstructed.**
   `lastwrite` is cleared on entry, so the sample written before a reset keeps
   an erased activity word. Its accumulation is genuinely lost, and the log says
   so instead of recording a partial count as if it were complete.
5. **The sample grid is anchored at the run's first minute boundary, not at
   absolute epoch multiples.** See the note at the end of
   [Time mapping](../../embedded/tags/UIUCTag/design/data-collection.md#time-mapping-the-invariant-the-host-decoder-depends-on). It
   removed the host's normalization step entirely.

## Alternatives considered

An earlier revision of this plan anchored the grid to absolute epoch multiples
of 7200 instead, and had the host round a header epoch down to its window. That
worked, but it cost more than it bought: the first block of a run wasted the
slots before the run started, a header epoch and its slot 0 were different
instants, and two blocks could share a normalized start after a restart inside
one window. Anchoring at the run's first minute boundary removes all three, and
reduces the host's time reconstruction to one addition.

## Consequences

The resulting record layout, time mapping and firmware/host contract are in
[`data-collection.md`](../../embedded/tags/UIUCTag/design/data-collection.md).
