# Firmware Programming

Use this page for the end-to-end firmware programming workflow.

## Choose Firmware

Describe how users identify the correct firmware image for their tag model.

## Program

1. Connect the base station.
2. Open [Tag Programmer](../apps/tagprogram.md). It programs through the
   base; STM32CubeProgrammer is not needed.
3. Choose the firmware and check the tag type.
4. Put a tag in the base, press **Program** and enter its label.
5. Confirm that the tag reaches IDLE and the record gains a line.

To program from a script, use [tag-flash](../cli/tag-flash.md).

## Verify

Describe the expected post-programming status, version output, or smoke test.

