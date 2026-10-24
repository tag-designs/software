---
type: proposal
status: proposed
summary: Rearchitecting qtprogram into a programming station that finds its own tools, base and firmware, steps through a batch of tags, and records every attempt in a CSV.
---

# qtprogram as a batch programming station

qtprogram becomes a station for programming many tags in a row. On opening it
finds STM32_Programmer_CLI, the base and the tag by itself, offers the
published firmware releases from GitHub, and remembers everything from the
last session. For each tag the operator presses **Program**, types the board's
label, and gets a result. Every attempt, failures included, adds one row to a
CSV. **Next tag** releases the base so the operator can swap the board. The
erase rule (`flashplan.*`) and the stop-erase-program-test-erase sequence stay
as they are now ([README](../../README.md#program-sequence)). What changes is
everything around them.

This applies the lessons from
[`flash_release.py`](../../../../../embedded/tools/flash_release.py) and the
[release procedure](../../../../../docs/release/release-procedure.md) (see
[Lessons carried over](#lessons-carried-over)). Questions still open are
marked **[Q]** and collected at the end.

## What the operator does

Today the operator has to find the CLI, choose an `.elf`, press Connect, read
the state, press Program, and keep their own record. After this change:

1. Open qtprogram. Each check on the station panel turns green, or says what
   is wrong and how to fix it.
2. Check the firmware and tag type, which are remembered from last time.
3. Press **Program**, type the label, and confirm.
4. Read the result. The CSV row has already been written.
5. Press **Next tag**, swap the board, and press **Attach**. Go back to step 3.

## Start-up checks

The checks run in order. Each one shows a status light on the station panel
(green, amber or red) and one line of text. A check that fails stops the checks
after it until it is fixed, and its line says how to fix it.

| # | Check | Passes when | When it fails |
| --- | --- | --- | --- |
| 1 | **Programmer** | The saved path, or `findProgrammer()`'s search, gives a CLI that runs `--version` | Red. The operator chooses the STM32CubeProgrammer `bin` directory, which is checked and saved. The CLI's version is shown and recorded |
| 2 | **Base on USB** | Exactly one tag base is enumerated, and its ST-LINK serial is read (see [Base serial number](#base-serial-number)) | None found: amber, "Plug in the base", and the station keeps polling USB once a second (it does not attach). Several found: the operator picks one, and its serial is saved |
| 3 | **SWD connect (CLI)** | `-c port=SWD sn=<serial> mode=UR` reads a device ID and the target voltage | Red, with the CLI's message. "No target" means there is no tag or no power. An unknown device ID means this is not one of our MCUs |
| 4 | **Supply voltage** | The voltage from step 3 is in range for the selected target | Amber below the range. **Red, and Program disabled,** above 3.6 V with an STM32L432 target, since that is the L432's absolute maximum ([AGENTS.md](../../../../../AGENTS.md#firmware-and-bench-work), step 0) |
| 5 | **Tag (monitor)** | tagcore attaches and `GetTagInfo` answers | Amber, "No firmware answers". This is normal for a blank board, a board in the ROM bootloader, or a board running foreign firmware. The tag type must then be chosen by hand, limited to the MCU family the device ID from step 3 reports |
| 6 | **Tag state** | IDLE | See [Tag state on attach](#tag-state-on-attach) |
| 7 | **Firmware** | The chosen image is cached, matches its manifest's SHA-256, and fits the tag's MCU family and type | Red, with the reason. The image is never programmed |
| 8 | **Record file** | The saved CSV exists and can be written | Missing: the operator is asked whether to create one at the default path or a path they choose. Not writable (on Windows, Excel locks open files): red, and Program is disabled until it is |

Steps 3 and 5 both reset the tag. The current qtprogram does not attach when it
opens, because attaching ends a running deployment. The station attaches on its
own, both when it opens and when a base is plugged in, but only after the
operator has accepted, once, that "this station resets any tag it finds". That
acceptance is saved in settings. Until it is given, the checks stop after check
2 and the operator presses **Attach**.

### Base serial number

The serial number in the base's USB descriptor (`iSerialNumber`) is the string
the CLI takes as `sn=`. This was checked on the bench base on 2026-10-24: an
ST-LINK/V2 (PID `0x3748`, firmware `V2J37S7`) reported
`0037001A5556431520303537` to macOS, and `STM32_Programmer_CLI -l st-link`
listed the same value as `ST-LINK SN`. So the base that tagcore enumerates can
be tied to a CLI probe without asking the CLI.

tagcore does not keep the serial yet. `UsbDev` holds only the VID, PID, bus and
address, and `LinkAdapt::Available()` reads no string descriptors. The change
is to add a `std::string serial` to `UsbDev` and fill it in `Available()` with
`libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, …)`, which
`linkadapt.cc` already uses elsewhere. Opening the device to read the
descriptor does not touch SWD, so it does not reset the tag. Listing probes with
the CLI (`-l st-link`) does not connect to the target either, and it stays as a
fallback when a descriptor cannot be read.

### Tag state on attach

| State | What the station does |
| --- | --- |
| IDLE | Nothing. Ready |
| Running, hibernating, or any other active state | Amber: "This tag is deployed. Programming stops it and erases its log." Offers **Stop**. Program stops the tag anyway, so this only makes it visible |
| FINISHED, ABORTED | Amber: "This tag holds a log. Programming erases it. Download it with Tag Monitor first." Offers **Erase now**, which is `tag-reset`: the erase is done by the firmware that wrote the log ([flashing protocol](../../../../../AGENTS.md#firmware-and-bench-work), step 2) |
| TEST, erasing | Waits, with progress |

## Firmware selection

There is one drop-down. It lists published `fw-v*` releases, newest first,
then **Other ELF file…**, then the ELF files used recently. Choosing a release
fills the image from the tag type.

**From GitHub.** The repository is public, so no token is needed.
`GET /repos/tag-designs/software/releases` returns the published releases;
drafts are not visible without a token, which is what we want. Releases whose
tag is not `fw-v*` are dropped. A prerelease is listed with a "prerelease"
marker. The asset `tag-firmware-fw-vX.Y.tar.gz` (about 1.5 MB) is downloaded
once into `QStandardPaths::AppDataLocation/firmware/fw-vX.Y/`. Only
`firmware/<Target>/<Target>.elf` and `<Target>-build-manifest.json` are
extracted, by a small ustar reader that rejects any path outside that pattern.
Each image is checked against its manifest's SHA-256 and size when it is
extracted, and again before it is programmed. When the station is offline,
the cached releases are still listed and are marked "cached".

**Other ELF file.** This is planned with `planFlash()` as it is now. If a
`<stem>-build-manifest.json` sits beside the image, it is verified in the same
way, and a dirty tree raises a warning. Without one, the row's firmware
version is `custom`, and the SHA-256 is computed and recorded anyway.

**Qualification.** A matching hash shows that the image is the archived one,
not that it was qualified. The panel says so and links the release notes,
which list what was qualified. The station does not parse the notes.

## Tag type selection

The second drop-down lists the **build targets** (`BitTag`, `PresTag`,
`CompassTagAT25`, `IMUTagNandBmp581`, `UIUCTag`), not the protobuf `TagType`.
The reason is that one `TagType` covers several targets: `COMPASSTAG` covers
CompassTag, CompassTagAT25 and the Breakout, and `IMUTAG` covers IMUTagNand and
IMUTagNandBmp581. Each target is what a release directory is named after.

- **The tag answers (check 5).** The station maps `TagInfo.tag_type` plus
  `board_desc` to a target and selects it. If the mapping is ambiguous, or
  the tag's type differs from the selected target, the drop-down turns amber
  and Program asks before going ahead. This check is new: `flash_release.py`
  cannot make it, because the programmer reports the MCU, not the board.
- **The tag does not answer.** The operator chooses. The list is limited to
  the MCU family from the device ID in check 3, which catches an
  IMUTag-versus-L432 mix-up but nothing finer, so the label matters.

The build manifest gains `tag_type` and `board` fields, so that each release
describes which tags its images are for. This is a separate change to the
manifest generator. Releases made before it, `fw-v0.7` and earlier, are mapped
by a table in qtprogram.

## Programming one tag

**Program** opens one dialog. It replaces both the label prompt and the
current confirmation:

- **Label**, typed by the operator. The station does not suggest one.
- A warning if the label is already in the CSV with a different UUID, or the
  UUID is there with a different label. It is a warning, not a refusal,
  because reprogramming a tag is normal.
- The tag (type, UUID, state, voltage), the image (release, target, commit,
  SHA-256), the erase from `FlashPlan`, and what is lost and what is kept. This
  is the same content as today's confirmation.

The sequence is today's `programStateMachine()`, moved into its own class and
made non-blocking. `flashTag()` currently blocks the GUI thread inside
`waitForFinished()`. There is one new step at the end:

1. Stop the tag, and have its own firmware erase its log.
2. Erase and program in a single CLI invocation, with `sn=<serial>`.
3. Attach to the new image. If it does not answer after a mass erase, the
   station shows "Unplug and replug the tag, then press Retry" (the PEMPTY
   case), and the station does not write flash registers itself.
4. Set the clock and run the self-tests.
5. Start and stop the tag once, and erase it again.
6. **New:** read `TagInfo` again and compare its `githash` with the
   manifest's commit. If they differ, the tag is not running the image the
   station meant to program ("verify what you built"). The row is marked
   `MISMATCH`.
7. Write the CSV row. A row is written for every attempt that reached step 2,
   including attempts that failed or were cancelled.

## Next tag

**Next tag** detaches tagcore and frees the SWD link. It clears the tag panel
(state, UUID, test result) and keeps the firmware, the tag type and the CSV.
The station panel then shows "Swap the tag, then press Attach". The station
does not poll for the new board, because every poll would reset whatever is in
the cradle.

## The CSV

The first seven columns are the ones in the specification, in its order. The
others are what `flash_release.py`'s JSON record shows is needed later:

| Column | Source |
| --- | --- |
| `label` | The operator |
| `tag_type` | The build target, e.g. `CompassTagAT25` |
| `uuid` | `TagInfo` (read before programming, and again after) |
| `firmware_version` | `fw-v0.7`, or `custom` |
| `git_hash` | Read back from the tag after programming. If the tag never answered, the manifest's commit |
| `date` | UTC, ISO 8601 |
| `test_status` | `TestResult` name, e.g. `ALL_PASSED`, or empty if the tests did not run |
| `result` | `OK`, `TEST_FAILED`, `PROGRAM_FAILED: <reason>`, `NO_ANSWER`, `MISMATCH`, `CANCELLED` |
| `image_sha256` | Computed at programming time. A tag cannot report the hash of its own image, so this is the only point at which it can be recorded |
| `image` | The ELF file name |
| `erase` | `none`, `below-calibration`, or `all` |
| `voltage_v` | From check 3 |
| `probe_serial` | The base's ST-LINK serial |
| `host_version` | The qtprogram version |

The file is UTF-8 with a header row, quoted as RFC 4180 requires, and
appended to one row at a time; the file is never rewritten. There is one file
overall, and its path is saved. **Show CSV** opens the folder that holds it in
Finder or Explorer. The operator copies or opens the file from there.

## Window layout

```
+--------------------------------------------------------------------+
| Firmware  [ fw-v0.7 (2026-10-15)                   v ] [notes]     |
| Tag type  [ CompassTagAT25                         v ]  detected   |
+------------------------------+-------------------------------------+
| Station                      | Tag                                 |
|  * Programmer  v2.22.0       |  Label     CompassTag-012 (last)    |
|  * Base        sn 0035...    |  UUID      2F0041...                |
|  * SWD         0x435  2.49 V |  State     IDLE                     |
|  * Tag         answers       |  Firmware  fw-v0.6  a1b2c3d          |
|  * Firmware    verified      |  Tests     ALL_PASSED               |
|  * Record      12 rows       |  Result    OK                       |
+------------------------------+-------------------------------------+
|        [ Program ]   [ Next tag ]   [ Attach ]   [ Show CSV ]      |
+--------------------------------------------------------------------+
| Batch: 12 programmed this session, 1 failed       [log v]          |
+--------------------------------------------------------------------+
```

The log pane (with the CLI's output) stays, and is collapsed by default. The
"Batch" line counts this session's rows. There is no table of rows: the CSV
holds the record.

## Saved state

The station saves the following with `QSettings`, under the existing
`tag-designs/qtprogram`: the programmer path, the last firmware choice (a
release and target, or an ELF path), recent ELF files, the CSV path, the base
serial, the last label, and the window geometry. `programmer` and `lastImage`
are carried over from the current keys.

## Structure

`mainwindow.cpp` (736 lines) does everything at present. The proposal splits
it so that the window only displays state, and each piece can be checked
without a window.

| Class | Owns |
| --- | --- |
| `StationSettings` | The `QSettings` keys above |
| `ProgrammerCli` | Finding and validating the CLI, listing probes, the connect check (device ID and voltage), and the program call. Everything goes through asynchronous `QProcess` with `sn=` |
| `FirmwareCatalog` | Listing releases (`QNetworkAccessManager`), downloading, the cache, extraction, manifest verification, and ELFs given by hand. A `FirmwareImage` carries the path, manifest, SHA-256 and `FlashPlan` |
| `StationChecks` | Start-up checks 1-8 as a state machine, emitting one status per check |
| `ProgramSession` | The program sequence for one tag. Emits progress, and ends with one `ProgramRecord` |
| `ProgramRecordLog` | Appending to and reading back the CSV, and the label and UUID lookups |
| `flashplan.*` | No change |

New dependencies: Qt Network, plus zlib for gzip, which vcpkg already
provides. `qtprogram --flash-plan` stays, and gains a `--verify <release dir>`
that checks the manifest without hardware, as `flash_release.py
--verify-only` does.

## Lessons carried over

From `flash_release.py` and the release procedure:

- **Program the archived bytes.** Each image is verified against its
  manifest's SHA-256 and size, and a mismatch is refused without an override.
- **Record the hash at programming time.** The tag cannot report it later.
- **Record every attempt.** A failed program that leaves no row cannot be told
  apart from one that never happened.
- **Labels are the board's identity.** Nothing in the image or the programmer
  identifies the board. qtprogram can do slightly better than the script,
  because it can check a running tag's type (see
  [Tag type selection](#tag-type-selection)).
- **Erase and program in one invocation** (unchanged), so the empty-flash
  reset that latches PEMPTY never happens.
- **Name the probe** with `sn=`. The current `flashTag()` passes only
  `port=SWD`, which is ambiguous with two bases connected.
- **Flag a dirty tree.** A `custom` ELF whose manifest records `dirty` gets a
  warning.
- **Do not claim qualification.** See [Firmware selection](#firmware-selection).
- **PEMPTY.** `flash_release.py` reads FLASH_SR and toggles PEMPTY back.
  qtprogram keeps its current rule: it writes no flash registers and asks for
  a power cycle. This matches the release procedure's "do not clear the
  flag".

## Decided

- **Attaching on open:** yes, after a one-time acknowledgement that is saved in
  settings.
- **PEMPTY:** prompt for a power cycle. The station writes no flash registers.
- **Board swap:** the operator presses **Attach**. The station does not poll.
- **"csv download":** a **Show CSV** button that opens the file's folder.
- **Labels:** typed by the operator, with no suggestion. A duplicate label or
  UUID gets a warning.
- **CSV columns:** the specification's seven, then the extra columns above.
- **A CSV that cannot be written:** Program is disabled until it can be.
- **Target mapping:** `tag_type` and `board` go into the build manifest, as a
  separate change, with a table in qtprogram for older releases.
- **Batch view:** a count of programmed and failed tags only, not a table.
- **Self-test failure:** the row records `TEST_FAILED`, and the station offers
  **Retest**.
