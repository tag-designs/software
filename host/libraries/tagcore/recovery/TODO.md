---
type: worklist
status: current
summary: Open work on the SWD capture, loader and offline-rebuild code in tagcore/recovery.
---

# tagcore/recovery TODO

The design is [SWD Capture and Recovery Library](../design/swd-recovery.md).
The unbuilt pieces (Python binding, identification without an identity record,
rescue) are in
[their proposal](../design/proposals/recovery-identification-rescue-python.md).
The capture additions still to build are D3 in
[Tag Firmware: Next Release TODO](../../../../embedded/tags/TODO.md):
- the RV3028 EEOffset for tags already deployed;
- internal-flash ECC faults.

## Checks never run

- **Idle current after a recovery session detaches.** A leftover vector catch
  reads as a tag that never sleeps, so this needs a measurement. See
  [Power Testing](../../../../docs/bench/power-testing.md).
- **Erase through `Serve()`.** The read-only image must refuse erase through
  `Serve()`, and the loaders README bench sequence (pattern, overwrite, refuse,
  restore) must pass through `ExternalFlash` with the `-RW` image. No tool
  issues an erase yet.

## Open work

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
