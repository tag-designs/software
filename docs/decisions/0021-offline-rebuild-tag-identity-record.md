---
type: decision
status: accepted
summary: Every image carries a const, versioned tag identity record directly after the interrupt vectors, read by the host at a fixed per-MCU address to choose its loader and decoder.
---

# 0021. A tag identity record immediately after the interrupt vectors

Date: 2026-10-01

Item 4 of the plan agreed on 2026-10-01 for rebuilding a download from an SWD capture, one of the items that make future releases self-describing. The record shipped as format version 1 in `common/core/src/tag_identity.c`; the text notes where the build differs from the first draft. Cut verbatim from [Offline Log Reconstruction](../investigations/2026-10-offline-log-reconstruction.md), "Decisions and plan", which holds the fw-v0.0.3 analysis behind it.

## Decision

A const, versioned record holding the key compile-time constants that
identify a tag's type, hardware and software: everything the tag-info call
reports, plus what a downloader needs to choose its loader and decoder. It is
the first thing a capture reads after identifying the processor.

**Where it is.** It goes in its own linker section placed directly after
`.vectors`. The SWD session already identifies the processor from
`DBGMCU_IDCODE`, and the vector table has a fixed size per MCU. In every
fw-v0.0.3 image it is:
- `0x1A0` bytes on the STM32L432 tags, so the record is at `0x080001A0`;
- `0x240` bytes on the STM32U375, so the record is at `0x08000240`.

So the host reads the record at a known address for that MCU: one read, no
search. The address belongs in the per-MCU table (`swdmcu`). If the magic is
not found there -- every image released before the record exists -- the host
falls back to identification by image hash or strings, as for fw-v0.0.3.

**Contents.** It starts with a magic word, a format version and its size, so
it can grow without breaking older readers. Then:

- **What the tag is:**
  - `tag_type`;
  - the target name (e.g. `PresTag`) and family;
  - `board_desc` and a board hardware revision;
  - the external flash part (JEDEC ID) and its geometry;
  - the RTC part;
  - the name of the loader that reads its external flash
    (e.g. `AT25XE_PresTagv3`);
  - the name of the data decoder, with its layout version.
- **What software it runs:**
  - `firmware` (`FIRMWARE_STRING`);
  - `gitrepo`, `githash`, the commit date (`build_time`) and `source_path`;
  - the version of the record format itself.
- **The rest of what the tag-info call reports:** `qtmonitor_min_version`,
  `accelconstant` and `magconstant`. `infoAck()` reads all of these from the
  record rather than from scattered literals, so live and offline report the
  same values from one source.
- **A region table and decoding constants**, described in the next
  subsection.

#### Region table and decoding constants

Every address and size below is already available from the `.map` or the ELF,
and every layout fact from the source. Putting them in the record means a
host needs neither: one read at a fixed address tells it where everything is
and how big each record is. That holds even for an image whose package has
been lost.

**Format.** Entries are `{u16 id; u16 length; value}` after an 8-byte header
(magic, format version, total size), ending with an end entry. Readers skip ids
they do not know. So a family can add entries, and the format can grow, without
breaking older hosts. Values are little-endian `u32`s, or NUL-terminated strings
for the identity fields.

As built (format version 1, `common/core/src/tag_identity.c`), two details
differ from the first draft:
- **There is no CRC.** A C initializer cannot compute one. The magic, the size
  field and the end entry identify a well-formed record, and the image's
  SHA-256 covers its bytes.
- **Regions are given as start and end addresses, not sizes.** Each address is
  a link-time constant, while the difference of two symbols is not. The data
  headers' end is given as 0, meaning "to the end of the persistent region".

The reader is `embedded/tools/decode_tag_identity.py`.

**Regions.** Each region entry gives `{address, size, record_size,
record_count, layout_version}`:

| Region | What it is (fw-v0.0.3 name) | Why a decoder wants it |
| --- | --- | --- |
| Persistent region | `__persistent_start__`..`__persistent_end__` | Bounds everything the log scan may read; the erase unit is the MCU page size |
| State markers | `sEpoch[]`, `t_StateMarker` | Start of the states; the record size differs between L4 (24 B) and U3 (32 B) |
| Stored configuration | `sconfig`, `t_storedconfig` | Location of the config; whether it has its own page; its layout version, which captures the short-enum ABI |
| Default configuration | `tag_default_config`, nanopb blob and length | `tag_type` and defaults, decodable with the proto schema alone |
| Data headers | `vddHeader[]` | Start, record size, and the real end: the array runs on to `__persistent_end__`, not to its declared length |
| Calibration | `calConstants[]` | Slot size and count, which differ between L4 (56 B x 36) and U3 (64 B x 32) |
| NAND map | `gd5fLogicalBlockMap` | Address, entry size, logical and physical block counts |
| Scratchpad | `0x2003E000`, 8 KB (U375 builds with `TAG_SCRATCHPAD`) | Where retained diagnostics are, and whether this image has them |
| U3 monitor mailbox | the shared-memory block | Lets a host recognise the monitor path without trying it |

**Backup registers.**
- The base address: `RTC_BKP0R` or `TAMP_BKP0R`.
- The word count.
- The word index of each `BackupState` field the decoders use: `valid`,
  `state`, `pages`, `external_blocks`, `resetCause`.
- `BACKUP_STATE_VALID_MAGIC`.

This replaces reading the per-family `persistent.h`, the source of the
UIUCTag/BitPresTag surprise.

**External flash.**
- The part's JEDEC ID and total size.
- The program-page size, the erase-unit size, and the spare-area size (NAND).
- The data region's base, and how a header maps to it: the page stride for the
  fixed-stride tags (240 B PresTag, 288 B UIUCTag, 380 B CompassTag), or
  "via checkpoint" for IMUTag.

**Data-format constants** for the tag's own decoder:
- the header record size and the sample record size;
- samples per page or block;
- the nominal sample period, or where it is read from in the stored
  configuration;
- the sub-second tick rate (1024 Hz on IMUTag);
- the scale factors, as IEEE floats (0.01 for `vdd100`, 1/16 for LPS27
  pressure, 0.976 mg and 0.04 uT for CompassTag);
- the erased-value conventions (epoch `-1`, `0xFFFF` activity, NaN samples).

These let a generic decoder handle the simple tags, and let a tag-specific
decoder check that it is reading the layout it was written for.

**Validation limits:**
- `_TagState_MAX` and `_State_Event_MAX`, which bound a valid state marker;
- the proto schema version, or a hash of `tag.proto` and `tagdata.proto`, so
  the host can pick matching `.proto` files.

**Build identity beyond the commit:**
- a digest of the compile-time option set (`UDEFS`, or the tag's `project.mk`
  and `custom.h`);
- named flags for the options that change what is stored or retained, such as
  `TAG_SCRATCHPAD` and `TAG_RETAINED_RUN_DIAGNOSTICS`.

AGENTS.md notes that a test image differs from a shipping one "by a `-D` that
leaves no trace in the git hash"; this is that trace.

**Keeping it correct.** The record is a C initializer built from the same
symbols and types the firmware uses: `&sEpoch`, `sizeof(t_StateMarker)`,
`offsetof`, the enum maxima. So it cannot drift from the image, and
`_Static_assert`s catch anything that does not fit its field. The build-time
layout descriptor of item 3 is generated from the same definitions, so the
in-flash record and the package agree by construction. The package descriptor
then carries what is too large for flash: full field tables for every struct,
and the `.proto` files.

**Cost.** A few hundred bytes. Images use 33-59 KB of their 256 KB or 1 MB
(fw-v0.0.3), and the record comes out of the persistent region's capacity, a
few dozen data headers at most.

**How the downloader uses it.**
1. Attach halted.
2. Identify the MCU over SWD (`DBGMCU_IDCODE`).
3. Read the identity record at that MCU's fixed address.
4. Select the loader, and the decoder that turns the capture into the SQLite
   file, by name, confirming with the loader's JEDEC check.
5. Capture.

The processor determines where to look; the record determines everything else.
No board or tag-type argument is needed.

The record changes every image's layout, and on the STM32U375 layout alone has
moved idle current. So it ships only as a qualified release
(`tag_release_check.py`).
