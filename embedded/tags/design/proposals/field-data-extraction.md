---
type: proposal
status: proposed
summary: Two unbuilt changes so a returned tag's data decodes and explains itself - a session superblock in the data region, and gaps closed in the flash marker log.
---

# Field Data Extraction

A returned tag's data can already be extracted. `tag-capture` reads its
registers, internal flash and external flash over SWD without running the
firmware ([Capturing a Tag](../../../../docs/bench/capturing-a-tag.md)), and
`tag-rebuild` decodes the capture. Two things are still proposed:

- **a session superblock** (Gap 1), so that the recorded data describes itself;
- **fixes to the flash marker log** (Gap 2), so that it records how a tag
  failed.

Why external flash is read by an SRAM loader and not a recovery firmware is
[a decision record](../../../../docs/decisions/0022-field-extraction-sram-loader-not-recovery-firmware.md);
what the first loader settled is [decision 0016](../../../../docs/decisions/0016-field-extraction-first-loader-settled.md).
The loaders themselves are in [`embedded/loaders`](../../../loaders/README.md).

## Purpose and scope

A tag comes back from a field site months after it was flashed. Two things
matter, in this order:

1. **Extract the recorded data**, whatever state the tag is in.
2. **Have enough recorded alongside it to understand what went wrong.**

The risk addressed here is not that data is unreachable. It is that data is
reachable and *silently misinterpreted*, because nothing in the bytes says which
firmware wrote them or how they are laid out.

Establishing which firmware an image is, and being able to rebuild it, is
covered separately in
[Tag Firmware Build Reproducibility](../../../../docs/build/firmware-reproducibility.md). The two
documents meet at the image hash: a capture that includes internal flash
contains the bytes of the image, so hashing them identifies the build without
trusting any embedded metadata.

## What exists today

**The internal-flash marker log is the field mechanism.** `recordState()` in
[`embedded/tags/common/core/src/persistent.c`](../../common/core/src/persistent.c)
writes a `t_StateMarker` into the `.persistent` section on every state
transition: epoch, state, internal and external page counts, supply voltage,
temperature, and the reason. It survives power loss and works on every tag
family. The `detail` word is STM32U3-only, taken from padding the 128-bit flash
row requires; the STM32L4 record has no slack.

**The image now identifies itself, but the data does not.** Every image built
since the tag identity record carries one directly after its interrupt vectors:
`tag_type`, git hash, the loader and decoder names, and the layout of each
stored region with a layout version
([decision 0021](../../../../docs/decisions/0021-offline-rebuild-tag-identity-record.md)).
Per-session facts are stored with the stored configuration
([decision 0020](../../../../docs/decisions/0020-offline-rebuild-session-facts-in-stored-config.md)).
A capture that includes internal flash can therefore be decoded. A dump of the
external flash on its own still cannot, and neither can a tag built before the
record.

**The retained scratchpad is not a field mechanism.** It is a bench debugging
tool: SRAM2 page 3 on STM32U375 targets only, enabled with `-DTAG_SCRATCHPAD=1`
([Debugging a Tag](../../../../docs/bench/debugging-a-tag.md#1-retained-sram2-scratchpad)).
It must not be load-bearing for anything a returned tag has to tell us. Any
diagnostic that matters in the field belongs in internal flash.

## Gap 1: the recorded data is not self-describing

This is the priority-1 problem.

Every page written by an IMUTag is prefixed by:

```c
typedef struct {
    int32_t epoch;
    uint16_t millis;
    int16_t rawtemp;
} t_ImuTagPageHeader;
```

Eight bytes. No magic, no format version, no tag-family identifier. Everything
needed to interpret the page body -- `IMUTAG_IMU_SAMPLES_PER_SUPERFRAME`,
`IMUTAG_SUPERFRAMES_PER_PAGE`, the layout of `t_ImuTagImuSample` and
`t_ImuTagAuxSample` -- is a compile-time constant in
[`include/imutag_log_format.h`](../../../../include/imutag_log_format.h), present nowhere
in the data.

Three consequences:

- A dump is only decodable by a host tool that still matches the firmware that
  wrote it. Change the superframe count, or add a field to the aux sample, and
  old and new dumps become indistinguishable by inspection. The host decodes
  both. One of them is wrong, with plausible values and wrong timestamps -- a
  worse outcome than failing to decode.
- Nothing in the bytes says whether a dump is an IMUTag, PresTag or UIUCTag.
- Correct decoding therefore depends on identifying the firmware, which makes
  build provenance a prerequisite for reading data. It should not be.

### Proposal: a session superblock

One self-describing record written where the data region begins, covering the
session that follows:

| Field | Purpose |
| --- | --- |
| `magic` | Identifies the block without external context |
| `format_version` | Lets a decoder reject or adapt, rather than guess |
| `tag_family` | Distinguishes IMUTag / PresTag / UIUCTag payloads |
| `header_size`, `sample_size`, `samples_per_frame`, `frames_per_page` | The layout constants that are currently compile-time only |
| `image_sha256` | Ties the session to an exact firmware image |
| `git_sha`, `dirty` | Human-readable provenance |
| `session_epoch` | When logging started |
| `config_digest` | Which configuration produced this session |

With this, a raw dump decodes standalone, indefinitely, with no reference to a
build -- which is the whole point, and which removes the need to identify a
build in order to read its data.

The identity record and the session facts now carry part of this table -- the
build identity and the layout of the internal regions -- but they live in
internal flash, not with the data, and only in images built since they were
added.

Two riders. It cannot help tags already deployed, so for those the mapping from
tag to image must be recorded externally before they fly. And while the format
is being versioned: `int32_t epoch` overflows in January 2038. A
`format_version` field is what makes widening it survivable later.

## Gap 2: the field failure record

The marker log is the right place and mostly does the job. Two gaps remain.

**The log stops silently when full.** `recordState()` returns without recording
once `offset >= sEPOCH_SIZE`, and the only notice is `tagScratchWord("ESLF",
...)`, which reaches the scratchpad, and the scratchpad is not present in the field. A tag
that filled its marker log and then had an interesting failure is
indistinguishable from a tag that simply stopped transitioning. Reserving the
final slot for an overflow marker, or carrying a dropped-transition count that a
later write can fold in, would make the distinction visible in a capture.

**Resets and faults leave no marker.** A hard fault, a watchdog bite and a
brownout are all invisible in the flash record; the tag simply reappears in an
earlier state. Latching the reset cause at boot from `RCC_CSR` and the `PWR`
flags, and recording it as a marker reason, separates those cases. This is
cross-family and cheap, but it costs a flash write, so it is worth recording
only when the cause is not an ordinary power-on -- an attach storm should not
burn the log.

**Build identity.** The identity record in the image, and the image hash a
capture can compute, now identify the build; the marker log itself does not need
to.

## Where to start

The data-format work -- the session superblock of Gap 1 -- is gated on the
first open question below: where the bulk of recorded data lives. That decides
whether the superblock belongs on external flash or internal. The marker-log
fixes of Gap 2 are independent of it, and cross-family.

## Open questions

- **Where does the superblock live?** If the bulk of recorded data is on external
  flash, the superblock belongs there, written through the same path that writes
  pages, and the internal-flash capture path is serving goal 2 rather than goal
  1. This decides which piece of work is actually urgent.
- **Which loaders are needed next?** The built loaders are listed in
  [`embedded/loaders`](../../../loaders/README.md). An MX25R or MX25L board would
  need a part driver.
- **Where is the per-deployment record** mapping a physical tag to the image hash
  it was flashed with, for tags deployed before a superblock exists?
- **Is a reset-cause marker worth its flash write**, given endurance and energy
  on a 12 mAh cell?
- **Fix `int32_t epoch` now**, while the format is being versioned, or accept the
  2038 boundary?
