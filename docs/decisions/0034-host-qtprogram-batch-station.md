---
type: decision
status: accepted
summary: qtprogram became a batch programming station -- automatic discovery of the CLI, base (by ST-LINK serial) and tag after a one-time consent to resets, GitHub releases verified against their manifests, targets matched through the image identity record rather than a table, a typed label per tag, a power-cycle prompt rather than PEMPTY writes, and an append-only CSV with a row for every attempt.
---

# 0034. Host: qtprogram as a batch programming station

Date: 2026-10-24

## Context

To program a tag with qtprogram, the operator found STM32_Programmer_CLI,
chose an `.elf`, pressed Connect, read the state, pressed Program, and kept
their own record of what went onto which board. Nothing checked the image
against its release manifest, as `embedded/tools/flash_release.py` does.
Nothing recorded the image's SHA-256, which a tag cannot report about itself.
The programmer was called without a probe serial, which is ambiguous with two
bases attached. The window blocked while the programmer ran. Programming a
batch of tags meant repeating all of this for each one.

## Decision

The specification and its answered questions are in
[the design](../../host/applications/qtprogram/design/batch-station.md).
The choices were:

- **Find everything automatically, remember it, and attach automatically
  only after consent.** The operator answers once whether the station may
  attach automatically, which resets any tag it finds. Without that answer
  qtprogram keeps its old rule of not attaching until asked.
- **Name the base by its ST-LINK serial.** tagcore's `UsbDev` gained the
  serial from the USB descriptor, and every CLI call passes `sn=`.
- **Offer published `fw-v*` GitHub releases**, download each once, extract
  only validated member names with the system `tar`, and refuse any image
  whose bytes do not match its manifest. A file chosen by hand is still
  allowed.
- **Match images to tags through the identity record,** whose build target
  equals `TagInfo.source_path`'s last component. There is no mapping table.
  The manifest's new `tag_type` and `board` fields come from the same record.
- **The operator types the label;** none is suggested. A duplicate label or
  UUID gets a warning, not a refusal.
- **Prompt for a power cycle** when the new image does not answer. Writing
  FLASH_SR.PEMPTY, as `flash_release.py` does, was rejected.
- **Wait for Attach after Next Tag;** do not poll for the new board.
- **Append one CSV row per attempt** that reached the programmer, failures
  included, and one per retest. The specification's seven columns come first;
  the result, image SHA-256, image, erase, voltage, probe serial and host
  version follow. An unwritable record disables Program. **Show CSV** opens
  the folder. The window counts the session's rows rather than listing them.
- **After a failed self-test, offer Retest.** Leave the tag as programmed and
  record `TEST_FAILED`.
- **Split the window from the logic:** `Station`, `ProgramSession`,
  `ProgrammerCli`, `FirmwareCatalog`, `FirmwareImage`, `RecordLog`.

## Alternatives considered

- **A table from TagType to build target, or the manifest's fields alone.**
  The table was the proposal's plan for releases up to `fw-v0.7`. The identity
  record is in every image, older releases and custom builds included, so a
  table would only have duplicated it and could drift. The bench BitTag
  reports TagType `BITTAG_LE`, not `BITTAG`, which a hand-made table could
  easily have got wrong.
- **Extract with zlib and a built-in tar reader.** zlib is not in the vcpkg
  graph, and adding it would change every CI build. The system `tar` exists
  on all three platforms. Naming each member to extract, after validating its
  name, keeps extraction confined.
- **Clear PEMPTY after a mass erase.** The release procedure says not to
  ("writing 1 toggles it"), and the user declined it.
- **Poll for the next board.** Each poll would reset whatever is in the base.
- **Suggest the next label by incrementing the last one.** Declined by the
  user.
- **A table of the session's rows in the window.** Declined; the CSV is the
  record.

## Evidence

- On the bench base, the ST-LINK's USB descriptor serial, tagcore's reading
  of it, and `STM32_Programmer_CLI -l st-link` all agree
  (`0037001A5556431520303537`).
- The first tagcore attach after the CLI exits fails on macOS, and one
  100 ms later succeeds. Hence the attach retry.
- `fw-v0.7`'s five images all carry identity records whose targets match their
  directories, and `qtprogram --verify` passes them all. A copy changed by one
  byte is refused.
- `qtprogram --bench-program` programmed `fw-v0.7` onto the bench BitTag in
  13 s: ALL_PASSED, row recorded, and `tag-info` then reported commit
  `169cda2c` and IDLE.
