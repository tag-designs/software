---
type: worklist
status: current
summary: Open work on the SWD capture and loader code in tagcore-rs's recovery module, and on the C++ offline rebuild (CaptureSource) it feeds.
---

# tagcore-rs recovery TODO

The design is [SWD Capture and Recovery Library](design/swd-recovery.md). The
capture, loader and SRAM-call code is Rust, in `src/recovery`; the offline
rebuild, `CaptureSource`, is still C++ in `host/libraries/tagcore/recovery`.
The unbuilt pieces (Python binding, identification without an identity record,
rescue) are in
[their proposal](../tagcore/design/proposals/recovery-identification-rescue-python.md).
The capture additions still to build are D3 in
[Tag Firmware: Next Release TODO](../../../embedded/tags/TODO.md):
- the RV3028 EEOffset for tags already deployed;
- internal-flash ECC faults.

## Checks never run

- **Program through `Serve()`, and the read-only image's own refusal.** Erase
  through `Serve()` has run (the rescue erase, 2026-11-06), but no tool has
  programmed through it, and `rescue-erase` refuses a read-only image on the
  host before the loader is asked. The loaders README bench sequence (pattern,
  overwrite, refuse, restore) must pass through `ExternalFlash` with the `-RW`
  and read-only images; it is the Python binding's check.

## Open work

- **A NAND rescue.** `rescue-erase` is NOR only. The IMUTag's GD5F has no
  writable loader, and a NAND erase must skip factory-marked bad blocks (and
  leave the firmware's bad-block map, which lives in internal flash, to be
  rebuilt by `tag-test`).
- **The GD5F logical block count (2008) is a constant in the IMUTag capture
  decoder.** Move it into the identity record's numbers, so the record carries
  every constant the decoder multiplies by.
- **A differential test for `data_logAck()` drift.** Compile the real
  `data_logAck()` against stubs, as `families/PresTag/test/datalog_sim.c`
  already does. Dump the simulated flash as a capture directory with the
  encoded Acks, and require `CaptureSource` to produce the same Acks byte for
  byte. Today a logic change that keeps every struct is caught only by
  `tag_rebuild_check.py run` on hardware.
- **SRAM erased by the attach.** If a tag's option bytes clear SRAM on system
  reset, no capture can recover its SRAM, because this rig must attach under
  reset. Setting the "not erased" options on every tag at provisioning would
  close this. That is a policy decision, still to be made.
