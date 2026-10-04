---
type: worklist
status: open
summary: Work list for the release after fw-v0.0.3: defect fixes, offline-reconstruction layout changes, release qualification and host counterparts, with per-item status.
---

# Tag Firmware: Next Release TODO

Status: open, written 2026-10-01 against `main` (89410ca). This is the work
list for the next tag firmware release after `fw-v0.0.3`: the defect fixes,
then the layout changes that let a tag's log be rebuilt from a raw SWD capture.
Each entry says which targets it touches, what to change, and how to show it
worked.

All the defects below were **found by reading code, not reproduced on a tag**.
Two were checked line by line against the source; their entries say which.

Background:
- [Offline Log Reconstruction](../../../docs/investigations/2026-10-offline-log-reconstruction.md),
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

A1 (CompassTag resumed logging overwrote earlier pages), A2 (CompassTag sub-zero temperatures), A3 (last partial page never downloaded): fixed. History: see [2026-10-next-release-fixes-verified](investigations/2026-10-next-release-fixes-verified.md).

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

### A6. A non-failure reset during RUNNING aborts the run (PresTag under 10 s, IMUTag always)

Fixed 2026-10-02. History: see [2026-10-reset-during-running-aborts](investigations/2026-10-reset-during-running-aborts.md).

### A7. PresTag and CompassTag: samples after a halt mid-page are timestamped early

Fixed. History: see [2026-10-next-release-fixes-verified](investigations/2026-10-next-release-fixes-verified.md).

**Still to do:**
- PresTag run and idle current against its flash log entry 0;
- CompassTag run and idle current against its entry 0;
- UIUCTag needs no fix: its samples go in time-indexed slots, and a 20 min
  bench run captured mid-run lost only the slot inside the halt.

## Part B: layout changes for offline log reconstruction

Specified in
[Offline Log Reconstruction, Decisions and plan](../../../docs/investigations/2026-10-offline-log-reconstruction.md#decisions-and-plan).
B1 and B2 change every image's layout, and B3 changes the package, so bundle
them into one release and qualify it once (Part C). Each needs a matching host-side change (Part D) and
a layout version, so the host keeps decoding fw-v0.0.3 images too.

B1 (tag identity record) and B2 (session facts in the stored configuration): implemented. History: see [2026-10-next-release-fixes-verified](investigations/2026-10-next-release-fixes-verified.md).

### B2. Session facts in the stored configuration: still to do

Still to do from the plan below:
- the effective sample settings as values;
- the nanopb-encoded `Config`.

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

History (status of the manual power checks and hardware checks, 2026-10-01 to 2026-10-03): see [2026-10-next-release-fixes-verified](investigations/2026-10-next-release-fixes-verified.md).

Every item above changes a tag image, and Part B changes all of them.
AGENTS.md is explicit that this needs measurement, not argument.

- **`tag_release_check.py` is a full gate for IMUTagNandBmp581 only.** It takes
  any `--target`, but its defaults were written for that tag:
  - the config is `power-configs/imutag-400.json`, and only IMUTag configs
    exist there;
  - `--run-max-ua 760` is sized for IMUTag at 400 Hz from a 3.7 V supply,
    and scales with supply voltage on the SMPS board;
  - the sample-count check reads only IMUTag's `lsm6.odr`;
  - the life-cycle run is a fixed 60 s.

  For the other tags, the build/flash, idle and attach-storm steps are still
  meaningful. The life-cycle step is not:
  - its run-current bound is too high for PresTag and BitTag to catch
    anything;
  - CompassTag (30 s samples, downloaded in blocks of 3) and UIUCTag (5-minute
    samples) probably produce no downloadable row in 60 s, and would fail on
    an empty table rather than on a fault.
- **For this release, the current checks are the gate.**
  - For each target being shipped, measure idle, running and finished current
    from a clean build of the release commit.
  - Record the numbers, so the next release has a baseline to compare against.
  - IMUTagNandBmp581 is still a prototype, with boards out for fab, so it is not
    gated on `tag_release_check.py` for this release. Run the release check
    when it ships.
- **Qualify the never-qualified targets first, then redo IMUTagNandBmp581.**
  Agreed 2026-10-03. BitTag, PresTag, CompassTagAT25 and UIUCTag have no power
  qualification at all, so they carry the most risk per hour spent.
  IMUTagNandBmp581 has a full `fw-v0.5`-era measurement, but of a *locally
  built* image rather than the released one, so it needs redoing against the
  release -- a correction to a known number, which is worth less than a first
  number for a target that has none. BitTag has a plan:
  [`BitTag/design/power-test-plan.md`](../BitTag/design/power-test-plan.md).
- **Each qualification updates the release notes**, per *Publishing the
  qualification* in `docs/release/release-procedure.md`. CI publishes
  every release stating the images are not bench-tested; that sentence is
  corrected per target as each is cleared, rather than once at the end.
- **Later:** make the release check cover every tag. This needs:
  - a config per tag in `power-configs/`;
  - per-target defaults selected by `--target`: a run-current bound taken
    from a known-good measurement, and a run long enough to fill at least
    one downloadable page;
  - a per-tag sample-count check in place of the IMU-only rate check.

  The hand measurements above provide the bounds.
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
  *Status 2026-10-03:* all five distributed targets have decoders, each
  validated on hardware against a live download (IMUTagNandBmp581, PresTag,
  CompassTagAT25, UIUCTag, BitTag). Earlier status: done for
  IMUTagNandBmp581 and PresTag with
  identity-record images (`tag-rebuild`, `recovery/capturesource`). Rebuilt
  against live downloads, every table is identical apart from three provenance
  rows in `info`, and a mid-run capture rebuilds to an exact prefix. Firmware
  asserts the struct offsets the decoders read, with no change to the images;
  `embedded/tools/tag_rebuild_check.py` checks the rest on hardware. Still to
  do: decoders for CompassTag, UIUCTag and BitTag, the fw-v0.0.3 layouts, and
  the GD5F logical block count in the identity record. See
  `docs/investigations/2026-10-offline-log-reconstruction.md`, "Implementation status" and
  "Keeping the rebuild in sync with the firmware".
- **D2.** `tagcore/recovery`: read the identity record at the per-MCU address
  (add it to `swdmcu`), check its magic, size and end entry (the record has no
  CRC), and use it to choose the loader and decoder; fall back to hash or string identification without it.
- **D3.** Capture additions:
  - EEOffset over I2C for tags already deployed;
  - NAND pages read through on-die ECC, with their status;
  - internal-flash ECC faults.
- **D4.** `legacy_bittag_rescue.cc`: take `vddHeader` from the ELF instead of
  the hard-coded `0x08007A68`, and accept `BITTAG_LE`.
- **D5.** Decoders: read CompassTag `temp10` as signed for fw-v0.0.3 offline
  decoding (A2), and the new `sconfig` members by layout version (B2).
