# Tag Firmware: Next Release TODO

Status: open, written 2026-10-01 against `main` (89410ca). This is the work
list for the next tag firmware release after `fw-v0.0.3`: the defect fixes,
then the layout changes that let a tag's log be rebuilt from a raw SWD capture.
Each entry says which targets it touches, what to change, and how to show it
worked.

All the defects below were **found by reading code, not reproduced on a tag**.
Two were checked line by line against the source; their entries say which.

Background:
- [Offline Log Reconstruction](../../../design/offline-log-reconstruction.md),
  where these were found and where the layout changes are specified.
- [Open Issues](open-issues.md), the standing defect tracker. It carries a
  one-line entry for each defect here.

The distributed targets affected: **CompassTagAT25**, **PresTag**, **UIUCTag**,
**IMUTagNandBmp581** and **BitTag**. Non-distributed family members
(CompassTag, CompassTagAT25Breakout, PresTagRaw) pick up the family fixes
automatically.

## Part A: defect fixes

These are small, local changes. Do them first: they stand on their own if the
layout work slips.

### A1. CompassTag family: resumed logging overwrites earlier pages

**Severity: data loss.** Checked against the source.

- **Targets:** CompassTagAT25, plus CompassTag and CompassTagAT25Breakout
  (all family members).
- **Defect.** `pState->external_blocks` is the external write cursor in 16-bit
  words: the write address is `external_blocks * 2`
  (`families/CompassTag/src/datalog.c:257`), and a page is
  `sizeof(t_DataLog) / 2` = 190 words. Two places reset it to `pages * 30`,
  which counts samples, not words:
  - `families/CompassTag/src/state_run.c:62`, in `Running(T_INIT)`;
  - `families/CompassTag/src/datalog.c:231`, in `restoreLog()`, the path taken
    when no FINISHED marker is found.

  Every re-entry into `Running(T_INIT)` with pages already logged therefore
  resumes writing inside an earlier page. Re-entries happen at the end of
  hibernation, on a brown-out restart, and in restore-based recovery. On NOR
  flash the overwrite ANDs new data into old, corrupting both.

  The FINISHED-marker path (`datalog.c:224`) restores the cursor that
  `recordState()` saved (`persistent.c:351`), in the same units, and is
  correct.
- **Fix.** At both sites, `pState->external_blocks = pState->pages *
  (sizeof(t_DataLog) / 2);`. Better, one helper used by both, so the unit
  cannot diverge again.
- **Verify.**
  - Configure a run with a hibernation window, or force a restart mid-run.
  - Download after logging resumes and confirm every page's samples are
    plausible and continuous.
  - Compare a page written before the resume with the same page read after;
    they must be byte-identical.
  - Without hardware: `families/CompassTag/test/datalog_sim.c` compiles the
    real `state_run.c` and `datalog.c` against stubs. It resumes after a reset
    and from a stale cursor, and asserts the cursor and a byte-exact download
    of every page. A fake NOR rejects reprogramming. It passes on the branch
    and fails on the pre-fix source; see that directory's `README.md`.
- **Note for deployed tags.** fw-v0.0.3 CompassTagAT25 logs that crossed a
  hibernation or restart are already corrupted from that point on, and no
  decoder can undo it.

### A2. CompassTag family: sub-zero temperatures read as about 6553 °C

Checked against the source.

- **Targets:** as A1.
- **Defect.** `t_DataHeader.temp10` is `uint16_t`
  (`families/CompassTag/inc/datalog.h:47`). It is assigned the signed
  `int16_t` running average (`state_run.c:37, 69, 144, 196`), and
  `data_logAck()` scales it as unsigned (`datalog.c:339`).
- **Fix.** Make the field `int16_t`. The size and offset are unchanged, so the
  on-flash format is the same and existing data reads correctly once
  interpreted as signed.
- **Verify.** Below 0 °C (or with a forced negative value) the downloaded
  `CoreTemperature` is negative and correct.

### A3. PresTag, CompassTag and UIUCTag: the last partial page is never downloaded

- **Targets:** PresTag (and PresTagRaw), CompassTagAT25 (family), UIUCTag.
- **Defect.** Each tag writes until external flash is truly full:
  - PresTag `datalog.c:253` checks word by word;
  - CompassTag `datalog.c:251` checks word by word;
  - UIUCTag `dataLogWriteField()` checks sample by sample (`datalog.c:331`).

  But `data_logAck()` serves a page only if a *whole* page fits:
  - PresTag `datalog.c:374` (240-byte pages);
  - CompassTag `datalog.c:327` (380-byte pages);
  - UIUCTag `datalog.c:431` (288-byte blocks).

  4 MiB is not a multiple of any of these, so the final page of a full tag is
  written and never downloaded. On the 4 MiB AT25XE that is the last 64 bytes
  for PresTag (16 samples), 244 bytes for CompassTag (6 blocks, 18 samples) and
  160 bytes for UIUCTag (13 slots, plus the pressure of a 14th).
- **Fix.** Serve the final page when its *start* is inside the flash. Read only
  the bytes that exist, and fill the remainder of the buffer with `0xFF`. Each
  tag's existing erased-data rule then ends the page where the data does:
  - PresTag stops at the first `-1` pressure;
  - CompassTag skips blocks with a `0xFFFF` activity word;
  - UIUCTag trims trailing erased slots.
- **Verify.** Fill external flash, either with a short sample period or by
  programming a full image through the `-RW` loader. Confirm the last header's
  samples appear in the download, and that the sample count equals what was
  written.
  - Without hardware: each family has a host harness that fills a fake 4 MiB
    NOR through the real `Running()` and downloads every page through the
    real `data_logAck()`:
    - `families/PresTag/test/datalog_sim.c`, converted and raw;
    - `families/CompassTag/test/datalog_sim.c`, which also fills 8 MiB;
    - `UIUCTag/test/datalog_sim.c`, which also fills 8 MiB.
  - All of them pass on the branch and fail on the pre-fix source. Each
    directory's `README.md` has the build line.

### A4. All tags: the state-marker log stops silently when full

Optional for this release.

- **Defect.** `recordState()` (`common/core/src/persistent.c`) stops writing
  once `sEpoch[]` is full: 24 entries on L4, 20 on U3. The only notice is a
  scratchpad `ESLF` record, and the scratchpad is not present in field builds.
  A tag that filled its marker log and then failed is indistinguishable from
  one that simply stopped transitioning. See Field Data Extraction, Gap 2.
- **Fix.** Reserve the final slot for an overflow marker, or keep a
  dropped-transition count that a later write folds in.
- **Verify.** Drive more than `sEPOCH_SIZE` transitions on the bench, then
  confirm the state log shows the overflow.

### A5. All tags: resets and faults leave no marker

Optional for this release.

- **Defect.** A hard fault, a watchdog reset or a brown-out leaves nothing in
  the flash record: the tag simply reappears in an earlier state.
- **Fix.** Latch the reset cause from `RCC_CSR` at boot, and record a marker
  only when it is not an ordinary power-on or a monitor attach, so attach
  storms do not burn the log. Weigh the flash write against endurance on a
  12 mAh cell first; this is an open question in Field Data Extraction.

## Part B: layout changes for offline log reconstruction

Specified in
[Offline Log Reconstruction, Decisions and plan](../../../design/offline-log-reconstruction.md#decisions-and-plan).
B1 and B2 change every image's layout, and B3 changes the package, so bundle
them into one release and qualify it once (Part C). Each needs a matching host-side change (Part D) and
a layout version, so the host keeps decoding fw-v0.0.3 images too.

### B1. Tag identity record after the interrupt vectors

**Status: implemented on `firmware-fix`; hardware verification outstanding.**
- **Record:** `common/core/src/tag_identity.c`, with the format in
  `common/core/inc/tag_identity.h`.
- **Family facts:** `inc/tag_identity_family.h` in each of PresTag,
  CompassTag, IMUTag, BitTag and UIUCTag.
- **Placement:** `common/tag_rules_code.ld`, the project copy of ChibiOS's
  `rules_code.ld`.
- **Reader:** `embedded/tools/decode_tag_identity.py`.

On the build machine, each of the eight affected targets' records matched its
ELF's symbols and its `.bin` length. Two notes for the test machine:
- **Build from a fresh tree**, or remove each target's `build/` and `dep/`. The
  new family headers shadow a common default, and `make` does not notice a new
  header shadowing an old one.
- **`tag-info` should report exactly what it did before.** Compare its output
  field by field.

What follows is the specification as planned.

- **Targets:** every tag; shared code.
- **Changes.**
  - **Linker scripts** (`common/STM32L432xC.ld`, `common/STM32U375xG.ld`):
    add a `KEEP`ed `.tag_identity` output section directly after `.vectors`.
    In fw-v0.0.3 that puts it at `0x080001A0` on L4 and `0x08000240` on U3.
    Assert the address in the linker script, or with a host check against the
    `.map`, so it cannot move silently.
  - **A new common source**, `common/core/src/tag_identity.c`, defining the
    record as a const C initializer. Contents:
    - the magic, format version and size;
    - the identity strings (everything `infoAck()` reports);
    - the board and hardware revision, external flash part, RTC part, and
      loader and decoder names;
    - the TLV region table (state markers, stored and default config, data
      headers with their real end, calibration, NAND map, scratchpad, U3
      monitor mailbox).

      The NAND map needs no header of its own. Offline decoding follows the
      checkpoints, which record physical pages, so it never reads the map. A
      map found in a capture can be checked by its own rule: live entries are
      strictly increasing physical block numbers.
    - the per-family `BackupState` word map and valid magic;
    - external geometry, the data-format constants and scale factors, the
      validation limits, the proto schema version, the compile-option digest;
    - a CRC.

    Build every value from the firmware's own symbols, `sizeof`s and
    `offsetof`s, with `_Static_assert`s.
  - **Per-family values** (stride, samples per page, scales, decoder name)
    from each family's headers, through macros the family defines and the
    common record consumes.
  - **`common/core/src/monitor.c` `infoAck()`** reads its strings and constants
    from the record, and `InfoStrings` goes away. Live and offline then report
    the same values.
  - **New board-level macros** for the board hardware revision and RTC part,
    if `board.h` does not already carry them.
- **Verify.**
  - Every target builds, and its record sits at the expected address
    (check the `.map`).
  - `tag-info` reports exactly what it did before the change.
  - A host-side reader decodes the record from a `tag-capture` image and
    checks the CRC (D2).
  - For a family the decoder supports, the region table matches `nm`.

### B2. Session facts in the stored configuration

**Status: implemented on `firmware-fix` for the EEOffset; hardware verification
outstanding.**
- **The facts:** `common/core/inc/session_facts.h` defines `t_sessionFacts`:
  version, EEOffset steps, a valid flag, ppm, and a reserved word, 16 bytes in
  all.
- **Opt-in:** PresTag, CompassTag, IMUTag, BitTag and BitPresTag (which covers
  UIUCTag) each add it to `t_storedconfig` and define
  `TAG_STORED_CONFIG_HAS_SESSION`.
- **Written at start:** `state_machine.c` fills it just before the start
  command writes the stored configuration, from the RV3028 correction cached at
  RTC initialisation, so start adds no I2C traffic.
- **Reported:** `infoAck()` returns the stored ppm when it is valid, and reads
  it live otherwise, as before.
- **Described:** the identity record gains a `TAG_ID_SESSION_FACTS` entry and
  reports the stored-config layout as version 2.
  `embedded/tools/decode_tag_identity.py` decodes the stored facts from a
  capture.

Still to do from the plan below:
- the effective sample settings as values;
- the nanopb-encoded `Config`.

The facts are 16 bytes so that every L4 stored configuration that opts in stays
a whole number of flash double-words, which `persistent.c` now asserts. On
STM32L4, `FLASH_Program_Array()` programs one word past the struct when given
an odd word count. BitTagNG's 28-byte `t_storedconfig` already does this; it
lands in alignment padding, but it writes stray RAM into flash and should be
fixed in that family.

What follows is the specification as planned.

- **Targets:** every tag (`t_storedconfig` per family); RV3028 driver.
- **Changes.**
  - Add to each family's `t_storedconfig`:
    - the RV3028 factory EEOffset (raw 9-bit steps, and the ppm derived from
      it);
    - the effective sample settings as values, not enums: sample period, ODR,
      ranges;
    - a layout version.
  - Write them when the tag is started, where `sconfig` is written now.
    `sconfig` is erased with the data, so the facts never outlive their
    session.
  - Read EEOffset from the RV3028 when writing `sconfig`
    (`rtc_rv3028.c rv3028ReadClockCorrection()`). `infoAck()` reports the
    stored value instead of reading it live; it falls back to a live read when
    `sconfig` is erased.
  - Optionally store the nanopb-encoded `Config` beside the raw struct, which
    removes the per-family `readConfig()` mapping from the offline path.
  - Mind the short-enum ABI: give new members fixed-width integer types.
  - Check that the larger struct still fits its page or region, especially on
    L4, where `sconfig` shares `.persistent`.
- **Verify.**
  - After start, `tag-info`'s `ppm_clock_error` equals the live RV3028 value.
  - A capture's `sconfig` decodes to the same value.
  - IMUTag `ElapsedUs` from a download is unchanged.

### B3. Build-time layout descriptor in the firmware package

- **Targets:** the build (`embedded/CMakeLists.txt`, `add_embedded_target`)
  and release packaging. The image itself does not change.
- **Changes.**
  - A small C program compiled for each target with that target's flags and
    `-fshort-enums`, so it matches the ARM ABI. It emits a JSON layout
    descriptor: `sizeof` and member offsets for every stored struct, the same
    constants as B1, and the symbol addresses read from the linked ELF.
  - Install it beside the ELF, and record its hash in the build manifest.
  - Copy the `.proto` files, or a `FileDescriptorSet`, into the package.
  - Generate B1's record and the descriptor from the same definitions, so they
    cannot disagree.
- **Verify.**
  - For each fw-v0.0.3 target, the descriptor agrees with what the analysis
    established by hand (the struct sizes and offsets in Offline Log
    Reconstruction).
  - The descriptor is regenerated byte-identically by a reproducible build.

## Part C: release qualification

**Status, 2026-10-01 (branch head `5060aa0`):** a manual power check on
PresTag, BitTag, UIUCTag, IMUTagNandBmp581 and CompassTagAT25 found idle,
running and finished current at their previous levels. It looked for
noticeable deviations only; no currents were recorded.

The data downloaded from those runs was examined and looked right. Those runs
did not exercise the specific A1 and A3 scenarios: a CompassTag resume, and a
tag logging until its flash is full.

Those two scenarios are covered instead by host simulations that compile the
real `state_run.c` and `datalog.c` (see A1 and A3):
- `families/CompassTag/test/`, `families/PresTag/test/` and `UIUCTag/test/`;
- each passes on the branch and fails on the source from before its fix.

This is **not yet a release qualification**:
- `tag_release_check.py` has not been run on any target. It adds four idle
  measurements, the life-cycle walk with the clock set, the bound on run
  current and the attach storms.
- A1 and A3 have been checked by simulation only, not on a tag. B1 and B2 have
  not been checked on a tag.

Every item above changes a tag image, and Part B changes all of them.
AGENTS.md is explicit that this needs measurement, not argument.

- Run `embedded/tools/tag_release_check.py --target <Tag>` for every
  distributed target, from a clean tree. It builds and flashes, measures idle
  four times, walks the life cycle (bounding run current), and runs attach
  storms.
- On the STM32U375 (IMUTagNandBmp581), layout alone has moved both idle and
  run current. Treat any change in either as a layout effect first, and bisect
  against the previous image before reasoning about logic.
- Functional checks beyond the release gate: A1's resume test, A3's full-flash
  test, and a `tag-capture` of each target to confirm B1's record and B2's
  facts are readable from the capture.
- Tag the release and publish the package as before.

## Testing the `firmware-fix` branch on another machine

The branch is pushed to `origin/firmware-fix`, with one commit per item, so a
failure can be pinned to one change and reverted on its own:

| Commit | Item | What to check on a tag |
| --- | --- | --- |
| `3139005` | A1 | CompassTagAT25: resume after hibernation or a forced restart does not overwrite earlier pages |
| `053c2a3` | A2 | CompassTagAT25: a negative core temperature downloads as negative |
| `e417238` | A3 | PresTag, CompassTagAT25, UIUCTag: a full tag downloads its last partial page |
| `b2f3986` | B1 | `tag-info` output unchanged; `decode_tag_identity.py` on a `tag-capture` directory decodes the record |
| "tags: store the RV3028 clock offset with the stored configuration" | B2 | After a start, `tag-info` `ppm_clock_error` is unchanged, and a `tag-capture` shows it stored |

```sh
git fetch origin
git switch firmware-fix          # first time: git switch -c firmware-fix origin/firmware-fix
git pull --ff-only               # later updates; the branch is never rewritten
```

- **Build from a fresh tree**, or remove each target's `build/` and `dep/`
  first. Two of these commits add headers that shadow common ones by basename,
  and `make` does not notice a new header shadowing an old one.
- **Run the release qualification (Part C) on the branch head**, and on any
  commit that changes current: bisect by commit.
  `git checkout <commit>` then rebuild and flash, or `git revert <commit>` to
  test without one item. On the STM32U375, B1 alone moves every image's
  layout, so treat an idle or run-current change there as a layout effect
  first.
- **Report results against commit hashes.** Fixes made in response are new
  commits on the branch; nothing already pushed is rewritten.
- **Merge to `main` after qualification**, then tag the release.

## Part D: host-side counterparts

These are not in the firmware release, but the firmware changes are only
useful once they exist. They are listed so neither half ships alone.

- **D1.** Offline shim: a capture-backed source for the unchanged SQLite
  writer, rebuilding info, Config, Calibration, State and the data rows. It
  decodes fw-v0.0.3 layouts from the release source and later layouts from B1
  and B3.
- **D2.** `tagcore/recovery`: read the identity record at the per-MCU address
  (add it to `swdmcu`), verify its CRC, and use it to choose the loader and
  decoder; fall back to hash or string identification without it.
- **D3.** Capture additions:
  - EEOffset over I2C for tags already deployed;
  - NAND pages read through on-die ECC, with their status;
  - internal-flash ECC faults.
- **D4.** `legacy_bittag_rescue.cc`: take `vddHeader` from the ELF instead of
  the hard-coded `0x08007A68`, and accept `BITTAG_LE`.
- **D5.** Decoders: read CompassTag `temp10` as signed for fw-v0.0.3 offline
  decoding (A2), and the new `sconfig` members by layout version (B2).
