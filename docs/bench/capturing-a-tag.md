---
type: procedure
status: current
summary: Capturing a tag's state over SWD before anything resets it, rebuilding a download from the capture, and keeping that rebuild in step with the firmware.
---

# Capturing a Tag

Capture a tag **before anything else touches it**. Use `tag-capture`, which
holds the core at its reset vector so the firmware never runs. Any monitor
attach (`tag-info`, `tag-reset`, qtmonitor) lets the firmware boot, and that
boot clears the reset flags, can rewrite `pState`, and can append to the flash
marker log. `tag-rebuild` then turns a capture into the SQLite file
`tag-dwnld -f sqlite` would have written, with no tag attached.

Usage of `tag-capture`, `tag-xflash`, `tag-sramcall` and `tag-rebuild` is in
the [command-line tools README](../../host/commandline/README.md#tag-capture).
How the library behind them works is in
[SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md).

## Which tool

| Tool | How it attaches | Reads | Use it for |
| --- | --- | --- | --- |
| `tag-capture` | Halts the core at the reset vector; nothing runs | Registers, backup registers, internal flash, OTP, option bytes; SRAM with `--sram`; external flash through the loader the identity record names | A returned tag, a mid-run capture, the input to `tag-rebuild`. Both MCUs. |
| `embedded/tools/tag_capture_state.py` | STM32CubeProgrammer `mode=UR`, which lets the firmware start booting before it halts | SRAM, the writable part of internal flash, backup registers | A bench failure on an STM32U375 tag, especially one built with `TAG_SCRATCHPAD`; it stores the ELF and build files with the capture |

`tag_capture_state.py` does not work on STM32L432 tags. Its SRAM and
backup-register steps use U375 constants; see
[Loader Runtime Design, open issues](../../embedded/loaders/design/loader-runtime.md#open-issues).
Its captures also describe a tag that has started to boot. Two consecutive
CubeProgrammer `mode=UR` reads of SRAM1 on the same PresTag differed in about
14,900 of 49,152 bytes.

## Capturing after a bench failure

Run the capture **before** anything resets or erases the tag.
`tag_attach_storm.py --stop-on-failure` exists so that you can.

```sh
embedded/tools/tag_capture_state.py --reason "storm round 2 aborted" \
    --elf build-embedded/embedded/tags/<Tag>/build/<Tag>.elf \
    --extra embedded/tags/<Tag>/project.mk
```

- **Always pass `--elf`.** The script reads the region bounds from it,
  because flash size and the persistent floor (`__tag_code_limit__`,
  `__tag_config_start__`, `__tag_nand_map_start__`, `__flash0_end__`) are
  per-target linker symbols, not constants. The ELF is also the only reliable
  record of what was running: a test image differs from a shipping one by a
  `-D` that leaves no trace in the git hash. A capture with no image stored
  says so in its manifest.
- **Pass the target's `project.mk` as `--extra`**, for the same reason.
  `--extra` is repeatable, so a `.map` can go too.
- **The persistent-flash sweep stops at the first wholly erased page.** That
  cut one real capture from 909,312 bytes to 8,192. The config and NAND-map
  pages sit above it, so the script captures them by address. Otherwise the
  sweep would never reach them. `--keep-blank` stores the whole region.
- **The backup registers read as all zeros unless `RCC_APB1ENR1_RTCAPBEN` is
  set first.** Holding the core in reset also resets RCC. A zero read looks
  exactly like a backup domain that was really lost, and telling those two
  apart is what the capture is for. Both tools set the bit before reading.
- **SRAM1 is not retained through Standby, and cannot be.** The part offers
  `PWR_CR1_RRSB1..RRSB3`, which retain SRAM2 pages. The `SRAMxPDS` bits in
  `PWR_CR2` are Stop-mode controls and do nothing for Standby. Ordinary
  `.data` and `.bss` live in SRAM1 (the tag uses 17.9 KB there, ending at
  `0x200047A8`), so anything you want to read back after
  the tag has slept must be in the [retained scratchpad](debugging-a-tag.md#1-retained-sram2-scratchpad),
  which is SRAM2 page 3. Building with `-DTAG_RETAINED_RUN_DIAGNOSTICS=1`
  copies the boot decision there.
- **SRAM can capture as noise.** If the tag is caught mid-Standby, SRAM is
  powered down, and `mode=UR` holds the core in reset so nothing repopulates
  it. The dump then decodes as plausible garbage instead of failing. A live
  image has a large zeroed `.bss` and readable strings, and noise has neither.
  The backup registers do not have this problem.

`embedded/tools/decode_capture.py <capture-dir>` decodes the named globals and
`pState` using the stored ELF.

## Capturing a returned tag

Run `tag-capture` first, with no other tool before it. It writes one timestamped
directory, holding one file per region and a `manifest.json`. A region that
fails is recorded and the capture carries on.

Know what the capture costs:

- **A capture interrupts a running tag, and the run resumes.** The session
  ends with a plain NRST, and the firmware records that as an external reset
  with valid retained state. Recovery treats an external reset as a reattach,
  not a failure, so the run continues with `Running(T_CONT, POWERFAIL)`.
  Samples due while the core was halted are lost, and on the STM32L4 the
  reattach starts a new page. See
  [restart recovery](../../embedded/tags/common/core/design/restart-recovery.md#reattach-versus-failure).
  Firmware older than `a406eda7` (2026-10-02), including fw-v0.0.3, treats that
  reset as a power failure instead. On those images, a capture of a run that
  is awake or in Stop 1 or Stop 2 ends it as ABORTED/POWERFAIL. That covers
  every IMUTag run (IMUTagNandBmp581 sleeps in Stop 2, IMUTagNand in Stop 1),
  and PresTag below a 10 s period. The data recorded up to that point stays
  downloadable.
- **Downloading a loader overwrites the start of SRAM1.** `tag-capture` reads
  SRAM (with `--sram`) before the external flash. If you use `tag-xflash` on
  its own, run it after any capture that needs SRAM.
- **On a tag whose option bytes erase SRAM on reset, the SRAM is already
  gone.** The attach is a system reset. The manifest records the option and
  marks the SRAM as untrustworthy.

The capture leaves `DHCSR.C_MASKINTS` clear. A capture that left it set once
stalled every later monitor attach on a running tag: `tag-info` timed out
with the tag in its idle thread. The monitor attach now clears a stale bit too.
After changing the capture path, attach with `tag-info` straight after a
mid-run capture. `tag_rebuild_check.py run` does this for you. The mechanism is
in [SWD Capture and Recovery Library](../../host/libraries/tagcore/design/swd-recovery.md#attaching-without-booting-the-firmware).

To read the identity record of a capture, run
`embedded/tools/decode_tag_identity.py <capture-dir>`.

## Rebuilding a download

```sh
build-host/bin/tag-rebuild <capture-dir> -o out.db3
```

The rebuild is "as captured". It shows the tag as found, with no stop marker
and none of the reset recovery a live attach would run. Its `info` table adds
three provenance rows: `source` = `capture`, `capture_dir` and `captured_at`.
A capture that fails its manifest SHA-256 checks is refused, and so is one
whose region layout version the decoder does not know. A rebuild that fails
part way removes its output. How the rebuild is built is in
[decision 0018](../decisions/0018-offline-rebuild-capture-backed-source.md).

## Keeping the rebuild in step with the firmware

The decoders in `host/libraries/tagcore/recovery/capturesource.cc` are a second
implementation of each family's monitor handlers. Nothing makes them follow
the firmware automatically, so each kind of drift has its own guard.

| What can change | Guard | Catches it |
| --- | --- | --- |
| A struct the decoder reads: stored config, state marker, data header or checkpoint, calibration slot, session facts | `_Static_assert` on every offset and size the decoder uses, next to the type, naming `capturesource.cc` | at firmware build time; the images are byte-identical with or without the asserts (`.list` compared, PresTag and IMUTagNandBmp581) |
| The same change, made deliberately | bump the region's `layout_version` in the identity record; the host refuses a version or record size it does not know | at rebuild time, as a refusal instead of a misread |
| The download logic in `data_logAck()`: checkpoint search, flag masking, conversions, page termination, holes | `tag_rebuild_check.py run` on hardware | at release qualification |
| A host decoder change | `tag_rebuild_check.py compare` over the stored reference pairs | before committing the host change |
| The SQLite writer | none needed: live downloads and rebuilds use the same writer and the same `TagLogHeader` | by construction |
| A family with no decoder | `tag-rebuild` refuses it by name | always |

The rule for a firmware change:

- **A struct listed above:** when a `_Static_assert` fires, update the decoder
  and bump that region's `layout_version` in the identity record.
- **`data_logAck()`, `readConfig()` or `system_logAck()`:** update the decoder
  and run the hardware check. The asserts cannot see logic.
- **A host decoder:** run `compare` on every reference pair.

### The hardware check

```sh
embedded/tools/tag_rebuild_check.py run --config <config.json>
```

The script starts the tag with the given configuration and captures it
mid-run with `tag-capture`. It then attaches with `tag-info`, which must
succeed, stops the tag and downloads it with `tag-dwnld --stop -f sqlite`,
and captures it again. Both captures are rebuilt. The final rebuild must equal
the download table by table, apart from the provenance rows in `info`. The
mid-run rebuild must be an exact prefix of the download. Every file is kept
under `--out-dir` (default `rebuild-checks`). Detach qtmonitor first.

`tag_rebuild_check.py compare <capture> <download.db3>` rebuilds one stored
capture and checks it against its download offline. Keep the reference pairs
from `run` and re-run `compare` on them after any host decoder change. The
pairs are kept outside the repository, in `captures/`, at 1 to 5 MB each.

A difference anywhere else is one of three things: a decoder bug, a firmware
change the decoder has not followed, or a capture problem. The tool does not
guess which. One field is reported as a note rather than a failure:
`ppm_clock_error`. Live, `infoAck()` falls back to reading the RV3028 over I2C
when the stored session facts hold no valid offset. A capture has no such
fallback.

### What remains unguarded

- **Constants the identity record does not carry.** The GD5F logical block
  count, which `externalFlashSize()` multiplies by, is a constant in the
  decoder: 2008, the figure for IMUTagNandBmp581's 2 Gbit GD5F2GM7RE
  (`flash_gd5f2gm7re.mk`). IMUTagNand's 1 Gbit GD5F1GQ5RE has 1004.
- **Algorithm drift between hardware checks.** A change to `data_logAck()`
  that keeps every struct is caught only when the hardware check runs. A
  differential test is not built yet. It would compile the real `data_logAck()`
  against stubs, as `families/PresTag/test/datalog_sim.c` already does, and
  require `CaptureSource` to produce the same Acks byte for byte.
