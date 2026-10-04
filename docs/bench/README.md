---
type: readme
status: current
summary: Hardware procedures for verifying firmware changes, measuring power, debugging, and capturing a tag's state.
---

# Bench Procedures

Procedures that need a tag on the bench. Read the relevant one before any
change to the boot path, the state machine, power sequencing or the download
path: a clean build and passing functional tests have repeatedly failed to show
faults these procedures catch.

| Procedure | Use it when |
| --- | --- |
| [Verifying a firmware change](verifying-firmware.md) | After any firmware change: confirm what compiled, and measure idle after a power-path change |
| [Power testing](power-testing.md) | Taking any current measurement: rig, Joulescope server, flashing, windows, supply voltage, the qualification tools, what to record |
| [Debugging a tag](debugging-a-tag.md) | Seeing inside a tag: scratchpad, GDB, GPIO markers, the monitor |
| [Capturing a tag](capturing-a-tag.md) | A tag has failed: capture its SRAM, flash and backup registers over SWD before anything resets it, and rebuild a download from the capture |

Per-target power plans and results are linked from
[power testing](power-testing.md).
