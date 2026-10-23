---
type: readme
status: current
summary: qtprogram (Tag Programmer) source layout, how it chooses the internal-flash erase from the image's linker symbols, its program sequence, and its remembered settings.
---

# qtprogram

`qtprogram` (Tag Programmer) writes a firmware image to a tag through
STM32CubeProgrammer, tests it, and leaves it erased and IDLE. The user guide is
`host/docs/src/apps/qtprogram.md`.

| File | What it is |
| --- | --- |
| `mainwindow.*`, `mainwindow.ui` | Base discovery, attach, the program state machine, the confirmation. |
| `flashplan.*` | Reads the image's ELF symbols and chooses the erase. |
| `custommessagebox.*` | The progress dialog shown while programming. |
| `logwindow.*` | The log pane, which also shows the programmer's output. |

## Choosing the erase

`planFlash()` follows the release procedure
([docs/release/release-procedure.md](../../../docs/release/release-procedure.md),
"When a mass erase is needed") and `embedded/tools/flash_release.py`, reading
the **new** image's symbols, since its layout governs from now on:

| `__tag_flash_page_size__` | Calibration region | Erase |
| --- | --- | --- |
| 4096 (STM32U375) | any | none: plain program; every region is pinned |
| 2048 (STM32L432) | `__calibration_start__` < `__calibration_end__` | `-e [0 <page below calibration>]` |
| 2048 | empty | `-e all` |

An image without `__tag_flash_page_size__`, or a file that is not an ELF, is
refused. The erase and the program are one `STM32_Programmer_CLI` call, which
avoids latching `FLASH_SR.PEMPTY`; qtprogram does not write flash registers,
and asks for a power cycle if a mass-erased tag does not answer. Check the
rule without hardware:

```sh
qtprogram --flash-plan <elf>...
```

## Program sequence

`programStateMachine()` runs once a second:

1. Stop the tag if it is in any state other than IDLE, ABORTED, FINISHED,
   TEST or sRESET; wait out TEST and an erase in progress.
2. From ABORTED or FINISHED, have the tag's own firmware erase its log (it
   knows its layout), and wait for IDLE.
3. Program with the planned erase.
4. Attach to the new image, set its clock, and run the self-tests.
5. Start and stop it once and erase it again (steps 1-2), so the new firmware
   lays out its own log and persistent region.

Before step 1, a confirmation names the tag, UUID and image and lists what is
lost and kept. An image for the wrong processor family is refused.

## Settings

Stored with `QSettings` (organization `tag-designs`, application `qtprogram`):
`programmer`, the STM32_Programmer_CLI chosen with the tool button, and
`lastImage`, the last image programmed. Without a saved programmer,
`findProgrammer()` tries `STM32CUBEPROGRAMMER`, `STM32_PRG_PATH`, the `PATH`
and the usual install directories, as `flash_release.py` does.

qtprogram opens with no base connected; Connect looks for one again.
