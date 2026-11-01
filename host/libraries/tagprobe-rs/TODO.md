---
type: worklist
status: current
summary: Open work in tagprobe - setting the boot option bytes of a virgin STM32U375 so it boots from flash after programming.
---

# tagprobe TODO

## Fix the boot option bytes of a virgin STM32U375

A new STM32U375, straight from the factory, has its option bytes set so that
it does not boot from flash. A tag built on one can be programmed correctly
and still never run the image. Writing the option bytes fixes it. The
programmers should do this themselves rather than leave it to a manual step.

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
