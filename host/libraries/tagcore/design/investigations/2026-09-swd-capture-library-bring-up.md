---
type: investigation
status: open
summary: Dated build log of the SWD capture library, 2026-09-30 to 2026-10-02: what each implementation step measured on bench tags, the C_MASKINTS stall, and how the resetCause question was settled.
---

# SWD Capture Library Bring-up

The dated record of building `tagcore/recovery` and the `tag-capture` and `tag-xflash` tools on bench tags, from 2026-09-30 to 2026-10-02: the status as last recorded, each implementation step with its original plan and what its check found, and two findings made along the way. Steps 0-5 and 9 are built; some of their checks were still open when this was last updated. Cut verbatim from [SWD Capture and Recovery Library](../swd-recovery.md), which describes the design and keeps the steps not yet built.

## Status as last recorded

Partly implemented. Steps 0-2 of the
[implementation sequence](#implementation-sequence) are built and were run on a
PresTag (STM32L432) on 2026-09-30: `tagcore/recovery/` and the `tag-capture`
tool capture registers, option bytes, OTP and internal flash, halted before the
firmware runs.

Steps 3 and 4 are built (2026-10-01). On 2026-10-02 they were extended to the
STM32U375 and SPI NAND, through `GD5F2GM7RE_IMUTagNandv2`, `Serve()`
version 2 (`READ_PAGE`) and `tag-xflash nand`; see
`embedded/loaders/design/proposals/u375-nand-loader-plan.md`. `TargetImage`, `SramCall` and `tag-xflash dump`
call a loader's `Init` and `Read` from the host and stream the external flash
to a file; see step 3 for what has been checked. Step 9, the identity record,
shipped in the firmware ahead of steps 4-8 (`next-release-todo.md` B1). It was
checked on a bench PresTag with `tag-capture` and
`embedded/tools/decode_tag_identity.py`. Everything else here is still a
proposal.

**Resume here.** Step 3's checks passed, and step 4's read path is built and
checked (see each step). The next steps are:
- step 4's remaining checks, which need an erase path through `Serve()`;
- step 5's AT25 check (a PresTag capture with external flash);
- then the offline SQLite shim (`next-release-todo.md` D1) on PresTag. Capture
  and dump a tag, rebuild its SQLite file, and compare it with a normal
  `tag-dwnld` of the same tag.

## Implementation sequence

**0. Baseline throughput.** *Done.* 32-bit memory reads through a base run at
101-104 KB/s at 2-4 KB per transfer (internal flash 256 KB in 2.59 s, SRAM1
48 KB in 0.47 s). A 4 MB external-flash dump is therefore bounded by SWD at
about 40 s, against CubeProgrammer's 70 s, and double buffering is dropped.

**1. Core control and attach-without-boot.** *Built, except the idle-current
check.* Halting uses `DHCSR` writes and core registers use `DCRSR`/`DCRDR`
through `LinkAdapt`'s existing debug-register access, so the base's
`FORCEDEBUG` handler is not needed. On the bench the core halted with PC equal
to the reset vector on every attach. The idle-current measurement after detach
is still to do.
Original plan: add halt, run, wait-for-halt,
core-register access and vector catch to `LinkAdapt`. Implement `SwdSession`
attach and detach as above. Check first whether the base's `FORCEDEBUG` handler
halts reliably; it carries a "this isn't working yet" comment in
`embedded/bases/common/src/stlink.c`, although CubeProgrammer halted the core
through a base on 2026-09-30.
*Check:*
- after attach, PC equals the reset vector;
- `RCC_CSR` still holds the flags from before the attach;
- after detach, `DEMCR` reads zero and the tag reaches its idle current. That
  last point is a measurement, per AGENTS.md: a leftover vector catch reads as a
  tag that never sleeps.

**2. Registers, internal flash and SRAM.** *Built and checked on STM32L432.*
Internal flash, OTP and option bytes were byte-identical across two captures
and a CubeProgrammer read. SRAM could not be cross-checked, because
CubeProgrammer lets the firmware run first. The U375 table was checked on
2026-10-02 on an IMUTagNandBmp581:
- the core halted at its reset vector;
- every region read;
- 1 MiB of internal flash, at about 218 KB/s, was byte-identical to a
  CubeProgrammer read;
- the backup registers read correctly, with `RTCAPBEN` set by the capture.

A first failure there was a tag with no power (configured to take it from an
absent Joulescope). Every tool, CubeProgrammer included, then reports
`enter swd mode` / `Unable to get core ID`, with the base sensing 1.80 V.
Original plan: `SwdSession` region reads and the MCU
tables of [MCU reference](../swd-recovery.md#mcu-reference), with the capture directory and
manifest for steps 1-3 of a capture.
*Check:*
- the regions are byte-identical to CubeProgrammer reads of the same tag at the
  same moment, on an STM32L432 tag (where `tag_capture_state.py` fails today)
  and on an STM32U375 tag;
- the manifest decodes `FLASH_OPTR` correctly on both parts, including which
  SRAMs the attach erased and the watchdog selection;
- the ECC registers are captured before the flash read.

**3. ELF reader and generic ST-style call.** *Built.*
- `recovery/targetimage.*` is a minimal ELF32 reader.
- `recovery/sramcall.*` implements the traced convention. It downloads the
  image's SRAM segments with a `BKPT` trap word at the start of SRAM1, sets LR
  to the trap, MSP 1 KB past the image, R0-R3 to the arguments and xPSR to
  Thumb, then runs with `DHCSR.C_MASKINTS` so the tag's interrupts are never
  taken.
- `SwdSession` gained `Write`, `WriteCoreRegister`, `Run`, `Halt` and
  `WaitHalt`.
- `tag-xflash dump --loader <.stldr> -o <file>` calls `Init` once, then `Read`
  per 32 KB buffer, taking the part's base and size from the loader's
  `StorageInfo`.

Checked on a bench PresTag on 2026-10-01 (UID `20333050364150040063005F`):
- **Identical to CubeProgrammer.** A 4 MiB dump holding a logged run was
  byte-identical to `STM32_Programmer_CLI -el ... -u 0x90000000 0x400000` of
  the same part (SHA-256 `b51b10ef...`). It took 56.1 s at about 75 KB/s,
  against 57.4 s for CubeProgrammer and 101 KB/s for raw SWD reads (step 0).
  The gap to raw SWD is the per-call register setup and the loader's 2 ms
  flash wake, which `Serve()` removes.
- **The `resetCause` question is settled: it is CubeProgrammer's exit, not
  the loader.**
  - Backup registers captured before and after a `tag-xflash` session were
    identical.
  - Across a CubeProgrammer read of the same FINISHED tag, `resetCause` went
    from 2 to 1 and `external_blocks` from 14 to 60, rounded up to a page.
  - CubeProgrammer lets the firmware boot on exit, and that boot's recovery
    path rewrites `pState`. A `tag-xflash` session halts at the reset vector
    and ends in a plain reset, so the firmware runs only after the session.
- **A capture ends a short-period run.** On PresTag at sample periods under
  10 s (run-mode Stop 2), the reset that ends any SWD session is classified
  `EVENT_POWERFAIL` and the run goes to ABORTED. At 10 s and above (Standby
  between samples) the run carries on through the same reset. See
  `embedded/tags/common/core/design/restart-recovery.md`. For PresTag, periods under
  10 s are a bench convenience, not deployed. **IMUTag always runs in Stop 2**,
  and a capture of a RUNNING IMUTagNandBmp581 ended its run as ABORTED/
  POWERFAIL (2026-10-02). Its data up to that point remained downloadable.
  A capture tool that must not end a run needs a different exit. Options are
  to leave the core halted for a power cycle, or to make the firmware
  recognise a debugger reset as it does a monitor reset.
Original plan: `TargetImage` and `SramCall`, then
call the existing `AT25XE_PresTagv3.stldr` `Init` and `Read` from the host.
*Check:*
- a 4 MB dump is identical to a CubeProgrammer dump of the same part;
- backup registers compared before and after a session that ends with
  `hardware_reset`. This settles whether the `resetCause` change comes from
  CubeProgrammer's exit or from something the loader session itself does.

**4. `Serve()` and `ExternalFlash`.** *Built; read path checked (2026-10-01).*
- **Command block:** `include/loader_service.h`, shared by loader and host.
- **Loader side:** `Serve(buffer, size)` in `embedded/loaders/common/src/loader_entry.c`,
  with `loaderFlashIdentity()` in the part driver. The ST entry points are
  unchanged.
- **Host side:** `recovery/externalflash.*` provides probe at open, `Read`,
  `EraseSector`, `Program` and `Close`.
- **Tool:** `tag-xflash dump` uses `Serve()` when the loader has it; `--st`
  forces the ST path.

Two departures from the plan above:
- The transfer buffer is passed to `Serve()` as an argument, not found by
  symbol, so the host chooses where it goes.
- The host zeroes the block before starting `Serve()`. The block is `.bss`,
  which is not downloaded, and SRAM survives a reset, so a magic left by an
  earlier session would otherwise read as ready.

Checked on the bench PresTag:
- A 4 MiB `Serve()` dump of a logged run was byte-identical to an ST-path dump
  of the same state, taking 53.8 s against 56.1 s. That is about 78 KB/s
  against step 0's 101 KB/s for raw SWD; SWD itself is the bottleneck.
- `detail[]` reported JEDEC `0x1F4708` and SR1 `0x00`, no block protection.
  The identity record (B1) stores the ID as `manufacturer << 16 | device1` =
  `0x1F0047`, because the firmware knows only those two bytes. Identification
  must compare those two bytes, not the whole word.
- Not yet checked: the read-only image refusing erase through `Serve()` (no
  tool issues an erase yet), and the loaders README bench sequence through
  `ExternalFlash` (the `-RW` image).

Original plan: add `Serve()` and the service block to
`embedded/loaders/common`, keeping the ST entry points unchanged, and implement
`ExternalFlash` on it.
*Check:*
- the dump matches step 3's;
- `detail[]` reports the JEDEC ID and SR1;
- the read-only image refuses erase through `Serve()`;
- the throughput is compared with step 0. The loaders README bench sequence
  (pattern, overwrite, refuse, restore) passes through `ExternalFlash`.

**5. Complete capture.** *Built 2026-10-02.* `tag-capture` reads the
external flash last, through the loader the identity record names; `--loader`
overrides it. The shared code is `recovery/externalcapture.*`, and the record
parser is `recovery/identityrecord.*`. Checked with SPI NAND on an
IMUTagNandBmp581; see `embedded/loaders/design/proposals/u375-nand-loader-plan.md`,
step 5. The AT25 (linear) path is still to be run on a PresTag.
Original plan: add external flash to the capture, with the loader
named by argument. Retire `tag_capture_state.py`, or leave it as a thin wrapper
around `tag-capture`.
*Check:* a full capture of a PresTag, and a tag that is then booted and
downloaded normally, with its data intact.

**9. Identity record (firmware).** *Done in the firmware, ahead of steps 4-8*
(`embedded/tags/design/next-release-todo.md` B1). It is in every family.
It was checked on hardware on a PresTag. What remains here is the host side:
identification (step 7) should read the record first.
Original plan: specify it with the session superblock, add
it to one family, and qualify that release. This is a separate firmware change,
gated on a firmware release.
*Check:* `tag_release_check.py` passes, and step 7 identifies the tag from the
record.

## `DHCSR.C_MASKINTS` left set by `Close()`

Found on 2026-10-02 on the bench PresTag (`20333050364150040063005F`). After
a `tag-capture` during a 1 s run, each `tag-info` timed out with
`dhcsr=0x3010009`, the tag in `__idle_thread` and the monitor request pending.
The A/B used the RV3028 register probe through `tag-sramcall`, with the tag
RUNNING at 1 s:
- the old `Close()` left the bit set in 2 of 2 trials;
- the fixed one left it clear in 3 of 3, counting a full capture.

In FINISHED the old `Close()` was harmless, because the tag drops to Standby
or Shutdown and its debug registers reset. That is why every check before the
first mid-run capture passed. The run itself survived: its only gaps were the
capture's 57 s halt and about 8 s per failed attach.

## `resetCause` after an SWD session

This is also the experiment for the open
`resetCause` issue: CubeProgrammer loader sessions leave `resetCause` =
`resetStandby` where a plain connection leaves `resetShutdown`, and a session
whose exit is known lets that be tested directly. Measured on 2026-09-30: after
a session ending in `hardware_reset`, the next capture read `resetCause` =
`resetShutdown`, as after a plain connection. So an SWD session as such does not
cause the change; what remains is something particular to CubeProgrammer's
loader sessions.
