---
type: design
status: current
summary: How tagprobe programs tags through probe-rs - choosing a driver from the base's identity, how the field-safe driver maps probe-rs's DAP model onto the commands every base supports, one session per operation under reset, the program sequence (erase plan, verify, PEMPTY, start), and the threading and base-sharing rules callers must keep.
---

# tagprobe design

tagprobe does the four jobs the host tools used STM32_Programmer_CLI for:

- list the bases;
- check the connection;
- program an ELF with the right erase;
- read flash back.

It uses [probe-rs](https://probe.rs) 0.32 for all of them. A base that answers
the `F6 99` identify command has the ST-LINK fixes, and is driven by probe-rs's
own ST-LINK driver. A base that does not, which means every base already in
the field, is driven by `FieldSafeProbe`. That driver speaks only the ST-LINK
commands every base answers correctly. Each operation opens one probe-rs
session under reset, does its work, and closes it. tagprobe keeps no state
between calls.

Why probe-rs, and what was measured on the bench, is in the
[probe-rs evaluation](../../../../docs/investigations/2026-10-probe-rs-evaluation.md).
The API table is in the [README](../README.md).

## Choosing a driver

```text
identify(base) ──► Some("base=...;fw=...") ──► Driver::Stock     (probe-rs StLink)
               └─► None (no answer)        ──► Driver::FieldSafe (FieldSafeProbe)
```

`identify` sends `F6 99` through tagcore's `LinkAdapt`. It opens the base
without touching nRST or SWD, so asking does not reset the tag.
Older base firmware does not answer it. The two problems that rule out the
stock driver on those bases were fixed in the same base firmware that added
the command (`be0b151f`, `a473918f`):

- a 1-byte `READMEM_8BIT` gets a 1-byte reply, so probe-rs waits for a second
  byte that never comes;
- `READ_DAP_REG` returns stale data on bases flashed before `773db79a`.

So a reply to `F6 99` is a sound test for "has the fixes". Field bases cannot
be reflashed, so the field-safe driver is permanent, not a stopgap.

## Opening a probe

`open_probe(base, driver)` finds the base by vendor, product and ST-LINK
serial. With a single ST-LINK on USB, no serial is needed.

- **Stock**: through `probe_rs::probe::stlink::StLinkFactory`, never through
  `probe_rs::probe::list::Lister`. `Lister` asks every probe family for its
  devices, CMSIS-DAP included, and CMSIS-DAP enumeration goes through hidapi.
  On macOS hidapi creates one `IOHIDManager` bound to the run loop of the
  first thread that calls it, and calling it from another thread aborts the
  process. Tag Programmer runs each programming job on a new worker thread,
  so its second program in a session crashed (`EXC_BREAKPOINT` in
  `CFRunLoopAddSource`). The ST-LINK factory uses libusb only.
- **FieldSafe**: `FieldSafeProbe::open` opens the base with tagcore's
  `LinkAdapt` and wraps it as a probe-rs `Probe`.

## The field-safe driver

`FieldSafeProbe` implements probe-rs's `DebugProbe`, and `FieldSafeArm`
implements its ARM debug interface. probe-rs expects to drive the debug port
and the memory access port register by register. Old bases cannot be trusted
with that, so the driver presents probe-rs with a consistent view, and does
the real work with the base's own memory commands:

| probe-rs asks for | The driver does |
| --- | --- |
| DP/AP register reads | Answers from a table of the part's real values (DPIDR, AP0 IDR, BASE, CSW; "no AP" past AP0; 0 for anything else, CFG included). The part is told from `DBGMCU_IDCODE`, read with `READDEBUGREG`. |
| DP/AP register writes | Accepts and ignores them. Memory never goes through DRW/TAR, so there is nothing to set up. |
| 32-bit memory | `READMEM_32BIT` / `WRITEMEM_32BIT` with the last-R/W-status check, in chunks of at most 1024 bytes |
| 8/16-bit memory | Whole-word accesses: reads take the bytes needed; writes read, patch and write back |
| nRST, SWD entry | tagcore's order: nRST asserted before SWD is entered, because a sleeping tag answers SWD only from reset |
| SWO, JTAG, raw SWJ sequences | Not supported |

The tables were read from real parts through a fixed base (STM32L432 DPIDR
`0x2BA01477`, STM32U375 `0x0BE12477`). A new part needs its own table.

The word-wise byte write is safe for SRAM and flash-algorithm buffers, which
are all that probe-rs writes bytewise. It would not be safe for a peripheral
register whose read has side effects. Do not use this driver for general
debugging.

## One session per operation, under reset

Every operation that needs the core (`program`, `read_memory`) opens a
probe-rs `Session` with `attach_under_reset`, and closes it before returning.
`connect_check` uses `LinkAdapt` directly, under reset. **Every call therefore
resets the tag** and ends whatever it was doing. That is right for
programming, and wrong for watching a running tag.

The session's debug sequence is `TagSequence`: the ARM default sequence, not
probe-rs's STM32 one. The STM32 sequences set DBGMCU_CR's debug-in-stop bits
on attach. The default writes nothing to DBGMCU, and on detach clears
`DHCSR.C_DEBUGEN` and `DEMCR`, so the tag is left as a normal boot would leave
it. Joulescope measurements after each kind of attach found no difference in
resting current (see the evaluation).

### Choosing the part

`chip_for(device_id, flash_bytes)` maps the device ID (0x435 STM32L432, 0x454
STM32U375) and the flash size to a probe-rs target name. The flash size comes
from the tag's `TagInfo.intflashsz` when it answers (in KB: the firmware
copies the flash-size register, whatever the proto comment says), otherwise from the
image's `__flash0_size__`. The UIUCTag's STM32L432KB has 128 KB, the KC 256 KB.
Choosing the KC for a KB would make probe-rs's flash algorithm address flash
the part does not have.

## The program sequence

`program(base, driver, chip, elf, erase, on_progress)`:

1. Opens a session under reset.
2. Erases as the image's flash plan says:
   - `ImagePages`: probe-rs erases only the sectors the image covers;
   - `Range { start, end }`: erases that span first; Tag Programmer passes
     everything below the calibration page, so the calibration survives;
   - `All`: chip erase.
3. Downloads the ELF with `verify` on, in the same session, so no reset falls
   between erase and program.
4. On an STM32L4, reads `FLASH_SR` and clears `PEMPTY` only if it is set. The
   bit is write-1-to-toggle, so writing it when clear would set it. This is
   the rule of `embedded/tools/flash_release.py`. With PEMPTY set after a mass
   erase, the L432 boots its ROM bootloader instead of the image until it is
   powered off. The step fails rather than leave it set.
5. Loads SP and PC from the vector table at `0x08000000` and resumes the core,
   as STM32_Programmer_CLI's `-g 0x08000000` did. The new image is running
   when the call returns.

Progress reaches the caller as `Progress::Step` (erasing, programming,
verifying) and `Progress::Bytes { done, total }`. The callback runs on the
caller's thread, inside probe-rs's flashing loop. It must not block.

`read_memory` opens a session under reset, reads with the core held at its
reset vector, closes the session (which lets the firmware boot), and waits
50 ms. Tag Programmer's Retest uses it to compare internal flash with the
image's loaded bytes.

## Rules for callers

- **Never hold the base from tagcore and tagprobe at once.** Detach a
  tagcore `Tag` before any tagprobe call, and attach it again afterwards. Both
  open the same USB device, and interleaved commands corrupt each other.
- **Any thread may call tagprobe, one call at a time per base.** It keeps no
  state of its own. probe-rs sessions are not shared.
- **Expect the monitor to need a moment after a call.** After probe-rs
  releases a field base, tagcore's first debug-register reads can fail
  (`0x81`) while the tag boots. tagcore rate-limits those messages and
  retries.

## Tests

`cargo test` checks chip and driver selection without hardware. The hardware
results, through both base generations on both parts, are in the
evaluation. Tag Programmer's results on the bench are in its
[README](../../../applications/tagprogram-rs/README.md#status).
