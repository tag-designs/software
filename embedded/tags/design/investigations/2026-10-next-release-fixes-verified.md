---
type: investigation
status: closed
summary: How the post-fw-v0.0.3 defect fixes (A1-A3, A7) and the identity record and session facts (B1, B2) were specified and verified, on hardware and in host simulation, October 2026.
---

# Next-Release Fixes: Specification and Verification

2026-10-01 to 2026-10-03. Items A1, A2, A3, B1 and B2 were merged to `main`
from the `firmware-fix` branch; A7 landed as f3b2cf75 (PresTag) and fcc57d73
(CompassTag). Cut verbatim from
[Next Release TODO](../../TODO.md), which keeps the items still open.

### A1. CompassTag family: resumed logging overwrites earlier pages

**Severity: data loss.** Checked against the source.

**Status: fixed, and verified on hardware on 2026-10-01.** On a CompassTagAT25
(UID `203633324B4250060022005E`, firmware `a7a69b17`), with the flash read by
`tag-xflash` and the new `AT25XE_CompassTagv1` loader:
- **Hibernation resume, the path that corrupted fw-v0.0.3 logs:**
  - The tag logged a full page and hibernated at the page boundary; a reset
    then woke it after the window.
  - `Running(T_INIT, ENDHIB)` put the cursor at 380 words, page 2, and wrote
    header 2.
  - Page 0 was byte-identical before and after.
  - The new samples began at byte 760.
  - The pre-fix code would have resumed at 60 words, on top of page 0.
- **Backup-domain loss:**
  - The valid word was cleared under reset to simulate a battery swap.
  - `restoreLog()` rebuilt the cursor as 190 words, the next page, where the
    pre-fix code gave 30.
  - The boot then classified the reset as a power failure and ended the run
    ABORTED, so nothing was written there.

Both agree with `families/CompassTag/test/datalog_sim.c`.

Two things learned on the way, both by design:
- Hibernation is re-checked only by the hourly alarm (`ALARM_HOUR` in
  `Hibernating()`), so a window shorter than an hour ends at the next hourly
  wake or at a reset, not at its `end_epoch`.
- A plain NRST reset while RUNNING keeps the retained cursor and does not call
  `restoreLog()` at all.

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

### A7. PresTag and CompassTag: samples after a halt mid-page are timestamped early

**Status, 2026-10-02: fixed in the working tree, uncommitted. The host
simulation and the bench PresTag pass; current is not yet measured** (flash
log entry 1, `captures/2026-10-02-prestag-d1/flash-log.md`).

On hardware, with `tag_rebuild_check.py run`, both runs passed every check:
- **10 s, 300 s run.** The capture's halt now shows as a 56 s gap, and the
  last sample is 20:00:05 against a stop at 20:00:11, which is real time.
  The boot after the capture took one sample off the 10 s phase (19:57:41)
  before the old phase resumed (19:57:45). The check therefore opened a page
  for that single sample: correct, but a page of space per such reset.
- **1 s, 180 s run.** The uninterrupted pages are full (60 samples, 60 s
  apart), so the 1 s jitter tolerance holds, and the capture starts a new
  page as before.

PresTag samples carry no timestamps. The host places sample *j* of a page at
the header epoch plus *j* × `lps_period` (`sqlitelog/pressure.cc`), which is
right only if no sample in the page was missed. A gap shorter than a page used
to start a new page only by luck of the reset path.

The case found: a 10 s run captured mid-run with `tag-capture`, on the bench
PresTag.
- The capture held the core for about 55 s.
- Its reset arrived during Shutdown, where `getResetCause()` returns
  `resetShutdown` for any reset, because of the shutdown wake marker. The
  run therefore resumed as an ordinary wakeup, `Running(T_CONT, OK)`, mid-page.
- The page then held 31 samples spaced 10 s from its header, where the run
  had lasted 356 s. Every sample after the capture was placed about 50 s
  early.
- The 1 s runs did not show it: their resets were classified differently and
  went through `restoreLog()`, which rounds the cursor to whole pages.

**The fix** is in `Running()`'s sample path and does not depend on reset
classification. If a sample is more than max(1 s, period / 2) from where its
slot puts it, the cursor moves to a new page, as T_INIT does. The rest of the
old page stays erased and the download ends that page at its first erased
sample. That covers a capture halt, a lost wakeup and a clock set alike. The
cost is one internal-flash read of the current header per sample.

`families/PresTag/test/datalog_sim.c` gained three cases:
- a 55 s halt at a 1 s period;
- a 55 s halt at a 10 s period;
- 1 s wake jitter, which must not split a page.

All pass with the fix, in both builds. Against the previous `state_run.c` the
gap case fails with "wrong sample count". The simulation's stub `tag.pb.h`
also lacked `State_EVENT_POWERFAIL`, used since `a406eda`, so the simulation
had not built since then; the stub is fixed.

**CompassTag had the same fault.** On the bench CompassTagAT25
(`captures/2026-10-02-compasstag-d1`), a 400 s run captured mid-run held 12
samples evenly spaced from its header, with every sample after the capture
about 60 s early. The same check in `families/CompassTag/src/state_run.c`
fixes it, adapted to its layout:
- samples come in blocks of three closed by an activity word, and the host
  puts sample *n* at the header epoch plus (*n* + 1) × 30 s;
- on a gap the new page's header is therefore the current time minus 30 s,
  with the current sample first;
- an unfinished block on the old page has no activity word, so the download
  skips it, losing at most two samples per interruption.

The CompassTag simulation gained the gap case, which fails on the previous
`state_run.c`. On hardware, after the fix (flash log entry 2 there):
- the first page ends at 20:58:29;
- the second page's header is 20:59:14, so its first sample is 20:59:44,
  when the capture ended;
- the samples then follow real time to 21:02:14, against a stop at 21:03:05.

The rebuild and prefix checks passed throughout.

Flash entry 1 there was the wrong target. `CompassTag` is built for the
MX25R part, and an AT25XE board takes `CompassTagAT25`. The identity record's
loader name showed it, when `tag-capture` could not find
`MX25R_CompassTagv1`.


### B1. Tag identity record after the interrupt vectors

**Status: implemented; verified on a bench PresTag on 2026-10-01 (see B2).**
Other families not yet checked on hardware.

A follow-up from that check: the record's build-options digest does not cover
the toolchain. The `5060aa03` image on the bench tag, built on another
machine, had the same digest as a local build of the same source. But it was
460 bytes smaller, with every code address shifted. A decoder that needs
symbol addresses must take them from the ELF of the exact build. Consider
adding the compiler version to the record.
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

**Status: implemented for the EEOffset, and verified on a bench PresTag on
2026-10-01 after a fix (uncommitted at the time of writing; see below).**
- **The facts:** `common/core/inc/session_facts.h` defines `t_sessionFacts`:
  version, EEOffset steps, a valid flag, ppm, and a reserved word, 16 bytes in
  all.
- **Opt-in:** PresTag, CompassTag, IMUTag, BitTag and BitPresTag (which covers
  UIUCTag) each add it to `t_storedconfig` and define
  `TAG_STORED_CONFIG_HAS_SESSION`.
- **Written at start:** `state_machine.c` fills it just before the start
  command writes the stored configuration. `tagSessionFactsCapture()` reads
  the RV3028 offset over I2C at that moment.
- **Reported:** `infoAck()` returns the stored ppm when it is valid, and reads
  it live otherwise, as before.
- **Described:** the identity record gains a `TAG_ID_SESSION_FACTS` entry and
  reports the stored-config layout as version 2.
  `embedded/tools/decode_tag_identity.py` decodes the stored facts from a
  capture.

**The hardware check found a bug, now fixed.** As first committed
(`5060aa0`), the facts were taken from the driver's cached correction, so that
start added no I2C traffic. That cache is RAM, filled only by `tagRtcInit()` on
a power-on boot or a `set_rtc` request. A start normally arrives on a later
boot that did neither. That is how `tag-reset` then `tag-start` works, and
`tag-start` sets the clock only with `--set-rtc`.

On a bench PresTag (UID `20333050364150040063005F`), a start therefore stored
`rtc_offset_valid = false`. The fix is to refresh the cache with a live read in
`tagSessionFactsCapture()`, as `infoAck()` already does. With it, the same tag
started without `--set-rtc` stored `rtc_offset_valid = true` at 0 steps
(0.0 ppm). This unit's EEOffset really is zero, which agrees with `tag-info`'s
live read. A CompassTagAT25 (`203633324B4250060022005E`) also stored a valid
offset of 0 steps.

A third unit, a never-programmed PresTag, read −2 steps (−1.907 ppm), and a
UIUCTag read +1 step (+0.954 ppm) through `RV3028_UIUCTag`. It was
read before any firmware ran, by the read-only SRAM probe
`embedded/loaders/RV3028_PresTagv3` through `tag-sramcall`. Offsets of a step
or two are evidently typical for these oscillators, so zero on two of three
units is plausible.

On that blank unit, the normal workflow left the offset unchanged:
- flashing, and the first boot, which rewrote CLKOUT from `C0` to `C2`;
- `tag-info`, which then reported −1.907 ppm;
- `tag-reset` and `tag-start --set-rtc`.

A cold power-up was not tested, deliberately: it would risk the factory value,
which cannot be recovered once lost. To record each tag's factory EEOffset,
probe boards before they are first programmed. The fix adds one I2C
transaction at start, outside the sleep path.
Measure idle current anyway, as for any image change.

The hardware check also confirmed B1 on that tag. `decode_tag_identity.py` on
a `tag-capture` directory decoded every field correctly: the record at
`0x080001A0`, the region table, loader, decoder, JEDEC ID and git SHA. Those
captures are in
`/Users/geobrown/Research/tag-designs/captures/2026-10-01-prestag-5060aa0/`,
outside the repo.



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

## Part C status, 2026-10-01 to 2026-10-03

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
- The currents were checked by eye, not recorded. Recording them for each
  shipped target, from the release commit, is the gate for this release (see
  below).
- B1 and B2 were checked on a bench PresTag on 2026-10-01 (see B2), and B2
  needed a fix. Both were then checked on a CompassTagAT25: the record decoded
  with its calibration region and 380-byte pages, and the session facts were
  valid. They were then checked on a UIUCTag (`2036354B3032500800520028`,
  firmware `b9f37a45`), whose RV3028 holds +1 step:
  - the record decoded with its block-field mapping and 288-byte blocks;
  - after `tag-reset`, a later `tag-start` without `--set-rtc` stored
    `rtc_offset_steps = 1`, `rtc_offset_ppm = 0.953674`, valid. That is the
    path the B2 fix addresses, now shown with a nonzero offset.

  They were then checked on an IMUTagNandBmp581 (STM32U375, UID
  `00303143433650090059002E`, firmware `bce3fe38`):
  - the record at `0x08000240` decoded with its NAND map, calibration
    region, and the U375 backup-state map (magic `TAGB`);
  - the session facts stored a valid offset of 0 steps after a start in a
    later session than the clock set, agreeing with `tag-info`.

  BitTag is not yet checked on hardware. Images flashed on the IMUTag are
  logged in `captures/2026-10-02-imutag-nand-bmp581/flash-log.md` for power
  root-causing.
- On UIUCTag, `tag-start` used to report failure for a start that succeeds.
  After the start request the tag goes to sleep, and must give up its debug
  interface to do so, so the follow-up status read fails. That is the nature
  of the debug link, not a firmware fault, and it predates the branch.
  `tag-start` now treats a lost link after an accepted start as expected: it
  prints `State: not confirmed` and exits 0. Checked on the UIUCTag; a
  following `tag-stop` found the tag RUNNING.
- A1 was checked on a CompassTagAT25 (see A1). A3 has been checked by
  simulation only.
