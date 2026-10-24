---
type: design
status: current
summary: qtprogram is a batch programming station -- it finds STM32_Programmer_CLI, the base and the tag itself, offers verified GitHub firmware releases, identifies tags and images by their identity records, programs and tests each tag, and records every attempt in a CSV.
---

# qtprogram as a batch programming station

qtprogram finds its programmer, base, tag and firmware by itself, and remembers
them between sessions. For each tag the operator presses **Program**, types the
board's label and confirms. The station stops and erases the tag, programs and
tests it, and leaves it erased and IDLE. It appends one row per attempt,
failures included, to a CSV. **Next Tag** releases the tag for the next board.
The choices behind this design are in
[decision 0034](../../../../docs/decisions/0034-host-qtprogram-batch-station.md).

| Piece | File | Owns |
| --- | --- | --- |
| `Station` | `station.*` | The checks, the base and tag, the firmware choice, the record, settings |
| `ProgramSession` | `programsession.*` | The program sequence for one tag |
| `ProgrammerCli` | `programmercli.*` | Finding, running and parsing STM32_Programmer_CLI |
| `FirmwareCatalog` | `firmwarecatalog.*` | GitHub releases, their download, extraction and cache |
| `FirmwareImage` | `firmwareimage.*` | One ELF: identity record, manifest check, SHA-256, erase plan |
| `RecordLog` | `recordlog.*` | The CSV |
| `MainWindow` | `mainwindow.*` | Display and dialogs only |
| `planFlash()` | `flashplan.*` | The erase rule ([README](../README.md#choosing-the-erase)) |

## Start-up checks

The station panel shows one light per check. The hardware checks run in
order, and a failure stops the checks after it. The programmer, firmware and
record checks stand alone.

| Check | Passes when | Otherwise |
| --- | --- | --- |
| **Programmer** | The saved path, or `findProgrammer()`'s search, runs `--version` | Red. **Choose…** takes the STM32CubeProgrammer folder: its `bin`, the install folder or the macOS bundle |
| **Base** | One ST-LINK is on USB, and its serial is read | No base: amber, and USB is polled every second, without attaching. Several: the operator picks one, and the serial is saved |
| **SWD** | `-c port=SWD sn=<serial> mode=UR` reports a device ID | Red, with the CLI's first `Error:` line |
| **Supply** | The ST-LINK's target voltage is in range | Red above 3.6 V on an STM32L432 (its absolute maximum), and Program is disabled. Amber below 1.8 V |
| **Tag** | tagcore attaches and `GetTagInfo` answers | Amber: a blank board, one in its bootloader, or foreign firmware. The tag type must then be chosen by hand |
| **Tag state** | IDLE | FINISHED or ABORTED: amber, with **Erase**, which the tag's own firmware does. Deployed: amber, with **Stop**. Program does both in any case |
| **Firmware** | The image is present, matches its manifest, and is for the tag's MCU | Red, and Program is disabled. Amber if the image is for a different target from the one the tag runs, or was built from a dirty tree |
| **Record** | The CSV is open and can be appended to | Red, and Program is disabled until it is fixed |

Attaching resets the tag. The station attaches by itself only after the
operator has answered yes, once, to "Attach automatically?". The answer is
saved, and **Station ▸ Attach Automatically** changes it. Until then the checks
stop after **Base** and wait for **Attach**.

The SWD check and the tagcore attach both use the ST-LINK. On macOS the
first open after the CLI exits fails, so the attach retries every 200 ms, up
to ten times. Nothing reaches the tag until an attempt succeeds.

### The base's serial

The base is named to the CLI by its ST-LINK serial (`sn=`). tagcore reads that
serial from the USB descriptor in `LinkAdapt::Available()`, which opens the
device without claiming it, so the tag is not reset. On the bench base, an
ST-LINK/V2 with firmware V2J37S7, macOS, tagcore and `STM32_Programmer_CLI -l
st-link` all report `0037001A5556431520303537`. Old ST-LINK/V2 firmware reports
a binary serial. tagcore leaves that empty, and the station asks the CLI's
probe list instead, which works only when one probe is attached.

## Identifying images and tags

Every image this tree builds carries a tag identity record after its vectors
(`tag_identity.h`). The record holds the build target, the board, the TagType
and the git hash. A running tag reports the same fields through `TagInfo`, so:

- **The Tag type drop-down lists build targets** (`BitTag`,
  `CompassTagAT25`, …), the names of a release's directories. It does not list
  TagTypes, because one TagType covers several targets.
- **A tag that answers selects its target**, from the last part of
  `TagInfo.source_path` (`/embedded/tags/BitTag` → `BitTag`), and the drop-down
  says "detected from the tag". The bench BitTag reports TagType `BITTAG_LE`
  and board `BitTag V6`, which is why the target, not the TagType, is matched.
- **A file chosen by hand names its own target**, from its record.
- **The MCU family is checked** against the SWD device ID: 0x435 is the
  STM32L432 and 0x454 the STM32U375. The image's family comes from its flash
  page size.

Build manifests also record `tag_type` and `board`, from the same record, from
the release after `fw-v0.7` onwards. The station reads the record itself, so it
handles older releases and custom images the same way.

## Firmware

The Firmware drop-down lists the published `fw-v*` releases, newest first,
then recent ELF files, then **Other ELF file…**. The repository is public,
so `GET /repos/tag-designs/software/releases` needs no token, and drafts are
not listed. A release without a `tag-firmware-<tag>.tar.gz` asset is not
offered.

Choosing a release downloads its archive once, into
`QStandardPaths::AppDataLocation/firmware/<tag>/`. The station lists the
archive with the system `tar` and keeps only member names of the form
`firmware/<T>/<T>.elf` and `firmware/<T>/<T>-build-manifest.json`. It then
extracts exactly those names, verifies every image against its manifest, and
only then marks the release cached. The last listing is kept in
`releases.json`, so cached releases are still offered offline.

An image is loaded again, and hashed again, whenever it is chosen. An image
whose bytes do not match its manifest is refused; a release image must have a
manifest. An image with no `__tag_flash_page_size__` symbol (the L432 images
of every release before `fw-v0.7`) gets a mass erase: its calibration and
persistent regions cannot be trusted to be where a current tree puts them, and
the release notes for `fw-v0.7` already require a mass erase when a calibrated
tag leaves `fw-v0.6`. The flash size ChibiOS linked it for
(`__flash0_base__`..`__flash0_end__`) still names the MCU, so the check that
the image fits the tag stands. A file without a manifest is usable: its SHA-256 is still recorded,
and its version is `custom`. **Release Notes** opens the release page, which
says what was qualified.

The Firmware light turns amber, and the Program dialog repeats the warning,
when the chosen release is not qualified for the chosen target. A release
counts as qualified for a target when it carries that target's
`qualification-<Target>-<date>.tar.gz` asset, the evidence the release
procedure uploads as each target is cleared; the listing already returns the
asset names, so this needs no extra request and no parsing of the release
notes. Offline, before any listing has been read, qualification is unknown and
says so. A local ELF file is never qualified. It is a warning, not a refusal:
bench and development programming use unqualified images. A matching hash shows that the image is the archived
one, not that it is fit to fly.

## Programming one tag

**Program** opens one dialog. It holds the label, typed by the operator with
nothing suggested. It shows the tag, the image (release, commit and SHA-256),
the erase, and what is lost and what is kept. It warns, without refusing, when
the label is already recorded for another UUID, or the tag under another
label, or when the image is for a target other than the one the tag runs.

`ProgramSession` then runs the sequence with a one-second tick, and the
programmer runs asynchronously:

1. **Stop** a running tag, and have its own firmware erase a FINISHED or
   ABORTED log. A tag that does not answer skips this step.
2. **Program:** the erase and the write in one CLI call with `sn=`, after
   detaching tagcore.
3. **Connect** to the new image, retrying every second, six times. If it does
   not answer, the window asks for the base to be unplugged and plugged in
   again, and offers **Retry** and **Give Up**. The station writes no flash
   registers (FLASH_SR.PEMPTY).
4. **Test:** read `TagInfo`, compare its git hash with the image's, set the
   clock, and run the self-tests.
5. **Finish:** start the tag, stop it, and have it erase again, so the new
   firmware lays out its own storage. The tag is left attached and IDLE.

Cancel is accepted before step 2 and at step 3, never while the programmer is
writing. **Retest** runs step 4 alone on the attached tag, and records it as a
row of its own.

## The record

The CSV holds one row per attempt that reached the programmer, and one per
retest. Rows are appended and flushed, and the file is never rewritten. A
file whose header does not start with the first seven columns below is
refused.

| Column | Value |
| --- | --- |
| `label` | Typed by the operator |
| `tag_type` | Build target |
| `uuid` | From the tag after programming, else from before |
| `firmware_version` | `fw-vX.Y`, or `custom` |
| `git_hash` | Reported by the tag after programming, else the image's |
| `date` | UTC, ISO 8601 |
| `test_status` | `TestResult` name |
| `result` | `OK`, `TEST_FAILED`, `MISMATCH` (the tag's commit is not the image's), `NO_ANSWER`, `PROGRAM_FAILED: …`, `FAILED: …` |
| `image_sha256` | Computed when programming, since a tag cannot report the hash of its own image |
| `image` | ELF file name |
| `erase` | `none`, `below-calibration`, `all` |
| `voltage_v` | From the SWD check |
| `probe_serial` | The base's ST-LINK serial |
| `host_version` | The host package version |

**Show CSV** opens the file's folder. The first time, the station offers to
create `Documents/Tag Programmer/programmed-tags.csv`, to open an existing
file, or to create one elsewhere. The window's batch line counts this
session's OK and other rows.

## Settings

`QSettings`, organization `tag-designs`, application `qtprogram`:
`programmer`, `firmwareRelease` or `firmwareFile`, `recentFiles`, `target`,
`recordPath`, `baseSerial`, `resetsAcknowledged` and `lastLabel`. qtprogram
1.x's `lastImage` is read as `firmwareFile` when nothing else is set.

## Checking it

- `qtprogram_checks` (`-DBUILD_QTPROGRAM_CHECKS=ON`) needs no hardware or
  network. It checks the CLI parsers against captured output, the CSV, the
  archive member filter and the release-list parser. Given a release archive,
  it also checks the whole download, extract and verify path through a
  `file://` URL. See [test/README.md](../test/README.md).
- `qtprogram --verify <elf|dir>…` checks images as Program would, without
  hardware.
- `qtprogram --bench-program <label>` runs the checks and then programs the
  tag on the base without the dialog, and exits with the result. On
  2026-10-24 it programmed `fw-v0.7` onto the bench BitTag (UUID
  `2035374D303150190059002F`, 3.23 V) in 13 s: mass erase, ALL_PASSED, and
  `tag-info` then reported commit `169cda2c` and IDLE. No power cycle was
  needed.
