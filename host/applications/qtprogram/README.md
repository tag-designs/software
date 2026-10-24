---
type: readme
status: current
summary: qtprogram (Tag Programmer) source layout, how it chooses the internal-flash erase from the image's linker symbols, its program sequence, its command-line modes, and its remembered settings.
---

# qtprogram

`qtprogram` (Tag Programmer) is a station for programming a batch of tags. It
finds STM32CubeProgrammer, the base and the tag by itself. It offers the
published firmware releases, verified against their manifests. It programs and
tests each tag through STM32_Programmer_CLI, leaves it erased and IDLE, and
records every attempt in a CSV. How it works and why:
[design/batch-station.md](design/batch-station.md). The user guide is
`host/docs/src/apps/qtprogram.md`.

| File | What it is |
| --- | --- |
| `station.*` | The start-up checks, the base and tag, the firmware choice, the record, settings. |
| `programsession.*` | The program sequence for one tag. |
| `programmercli.*` | Finding and running STM32_Programmer_CLI, and parsing its output. |
| `firmwarecatalog.*` | GitHub releases: listing, download, extraction, cache. |
| `firmwareimage.*` | One ELF: identity record, manifest check, SHA-256, erase plan. |
| `flashplan.*` | Reads the image's ELF symbols and chooses the erase. |
| `recordlog.*` | The record CSV. |
| `mainwindow.*` | The window and its dialogs. |
| `logwindow.*` | The log pane, which also shows the programmer's output. |
| `test/` | `qtprogram_checks`, the offline checks. |

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

`ProgramSession` runs once a second:

1. Stop the tag if it is in any state other than IDLE, ABORTED, FINISHED,
   TEST or sRESET; wait out TEST and an erase in progress.
2. From ABORTED or FINISHED, have the tag's own firmware erase its log (it
   knows its layout), and wait for IDLE.
3. Program with the planned erase, in one CLI call naming the base's ST-LINK
   by serial.
4. Attach to the new image, compare its git hash with the image's, set its
   clock, and run the self-tests.
5. Start and stop it once and erase it again (steps 1-2), so the new firmware
   lays out its own log and persistent region.

A board whose firmware does not answer (blank, or in its bootloader) starts at
step 3. If the new image does not answer, the window asks for a power cycle;
qtprogram writes no flash registers.

## Command-line modes

```sh
qtprogram --flash-plan <elf>...           # the erase each image would get
qtprogram --verify <elf|release dir>...   # identity, hash against manifest, erase
qtprogram --capture-screenshots [<dir>]   # the user guide's screenshots
qtprogram --bench-program <label>         # checks, then program the tag; erases it
```

`--verify` exits 1 if any image is refused. `--bench-program` exits 0 when
the result is OK, 1 when it is not, and 2 when the station could not program
at all.

## Screenshots

The user guide's screenshots come from the application:

```sh
qtprogram --capture-screenshots [<dir>]
```

It waits for the checks to settle and writes `qtprogram-station.png`. Then it
opens the Program dialog, writes `qtprogram-program.png` and cancels it.
`<dir>` defaults to `host/docs/src/images`. It uses the saved settings. If
attaching automatically is allowed, it attaches to the tag, which resets it;
nothing is erased or programmed.

## Settings

Stored with `QSettings` (organization `tag-designs`, application `qtprogram`):
`programmer`, `firmwareRelease` or `firmwareFile`, `recentFiles`, `target`,
`recordPath`, `baseSerial`, `resetsAcknowledged` and `lastLabel`. Releases are
cached under the application data directory, in `firmware/`. Without a saved
programmer, `findProgrammer()` tries `STM32CUBEPROGRAMMER`, `STM32_PRG_PATH`,
the `PATH` and the usual install directories, as `flash_release.py` does.
