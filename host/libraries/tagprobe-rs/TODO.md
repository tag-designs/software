---
type: worklist
status: current
summary: Open work in tagprobe - a virgin STM32U375 does not boot from flash; fix its boot option bytes, preferably from the tag firmware at first start, otherwise from the programmers.
---

# tagprobe TODO

## Fix the boot option bytes of a virgin STM32U375

A new STM32U375, straight from the factory, has its option bytes set so that
it does not boot from flash. A tag built on one can be programmed correctly
and still never run the image. Writing the option bytes fixes it, and no
manual step should be needed. There are two routes; the first needs no host
change.

### Route 1, preferred: the firmware fixes its own option bytes

Every U375 tag image runs a startup routine that sets `nSWBOOT0 = 0` and
`nBOOT0 = 1` in `FLASH->OPTR` when they are not already so, then launches an
option-byte load. This works on a virgin part because the programmers never
reset into the image: after programming, `program()` in
[`src/lib.rs`](src/lib.rs) loads SP and PC from the flash vector table and
runs, so the image starts whatever the boot selection says. The register and
bit names used below are those of the U375 header in this tree
(`FLASH->CR`, `SR`, `KEYR`, `OPTKEYR`, `OPTR`).

A sketch was given on 2026-10-31. Before adopting it:

- **Run it first.** Put it at the top of `main()`, before `pState`, clocks or
  devices are touched. `OBL_LAUNCH` resets the part, so anything done before
  it is thrown away. On the next boot the check finds the bits set and does
  nothing more, so the write happens once per board.
- **Add the checks the sketch leaves out.** Wait for `SR.BSY` to clear before
  starting. Clear stale error flags in `SR` before `OPTSTRT`. Read `OPTWERR`
  and the other error bits after `BSY` clears, and give up without
  `OBL_LAUNCH` if any is set. Bound the `BSY` wait.
- **The reset it causes.** The option-byte load is a system reset, so the
  backup registers, and with them `pState`, survive it. Check that the boot
  path's reset-cause handling treats the `OBLRSTF` flag as harmless.
- **TrustZone stays off.** The non-secure `CR` can make this change only while
  `TZEN = 0`, as the bench tags read today
  ([debugging a tag](../../../docs/bench/debugging-a-tag.md)).
- **U375 only.** The L4 tags have different boot rules and their own handling
  of `PEMPTY`
  ([release procedure](../../../docs/release/release-procedure.md)).
- **Verify on hardware.** Program a virgin U375 board, power-cycle it, and
  check it boots the image with `tag-test`. Then read `OPTR` back to confirm
  the write happened once and not on every boot.

### Route 2: the programmers write the option bytes

If route 1 is not taken, the host does it:

- **Detect it.** probe-rs can read the option bytes. Read them at connect,
  for a U375 target, and decide from them whether the boot selection needs
  changing. Leave a part that is already right untouched.
- **Write them.** Changing an option byte needs the flash and option unlock
  sequences and an option-byte load. That may need a small SRAM-resident
  program injected and run on the target, in the way
  [`embedded/loaders`](../../../embedded/loaders/README.md) runs its loaders.
- **Where it applies.** `tag-flash program`, and Tag Programmer's **Program**,
  which share this library. Say in each tool's output when the option bytes
  were changed.
- **Confirm the bits first.** Take the boot-selection fields and their factory
  values from the U3 reference manual and from a virgin part's read-back,
  not from the L4, whose `nSWBOOT0` and `nBOOT0` behave differently
  ([release procedure](../../../docs/release/release-procedure.md), on
  `PEMPTY`).
- **Verify on hardware.** Program a virgin U375 board, power-cycle it, and
  check it boots the image with `tag-test`.

Recorded 2026-10-31.
