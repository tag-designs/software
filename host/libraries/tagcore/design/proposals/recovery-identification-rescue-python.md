---
type: proposal
status: proposed
summary: The unbuilt parts of the SWD recovery library - identifying older images by hash and strings, the rescue-erase procedure, and the Python binding - with the check that ends each.
---

# Recovery Library: Identification, Rescue and Python

None of this is built. The capture and loader parts of the library are built and
described in [SWD Capture and Recovery Library](../swd-recovery.md). What remains
is three pieces, each ended by a check on a bench tag, in whatever order they are
needed:

1. a Python binding;
2. identification of images that have no identity record;
3. the rescue procedure.

How the built parts were checked is in
[SWD Capture Library Bring-up](../investigations/2026-09-swd-capture-library-bring-up.md).

## Python binding

pybind11 over `SwdSession`, `ExternalFlash` and the capture procedures,
following [Python Interface Design](python-interface.md). That means:
- values returned rather than output parameters;
- exceptions rather than Booleans;
- `bytes` for data;
- sessions as context managers;
- long operations that release the GIL and take a progress callback.

```python
import tagcore

# The whole thing, as the CLI does it:
result = tagcore.recovery.capture("captures/", board=None, progress=print)
print(result.identity, result.manifest_path)

# Or the layers directly:
with tagcore.swd.open() as s:                   # attaches halted, before boot
    regs = s.read(0x40002850, 128)              # backup registers
    image = s.read_region("internal_flash")
    with s.external_flash(loader="AT25XE_PresTagv3") as xf:
        print(xf.identity)                      # JEDEC ID, SR1 as found
        data = xf.read(0, xf.size, progress=print)
# leaving the block detaches with hardware_reset by default
```

Port the loaders README bench sequence (pattern, overwrite, refuse, restore) to
a script beside `tag_lifecycle_check.py`.

*Check:* the script passes on a PresTag and restores the tag to blank.

## Identifying images without an identity record

Images built since the identity record name their loader and decoder in it;
see [decision 0021](../../../../../docs/decisions/0021-offline-rebuild-tag-identity-record.md).
Older images have no such record, such as the fw-v0.0.3 tags already deployed.
For them, three sources remain, in order of confidence:

1. **Image hash.** The capture holds the image. Each build manifest records the
   `.bin` SHA-256 and length. Hashing that many bytes of internal flash and
   looking the result up in a catalog of manifests identifies a released image
   exactly. This needs no firmware change and covers every image built since
   build manifests were introduced.
2. **Static strings.** Some images are not in the catalog, such as development
   builds and builds from dirty trees. For those, scan the image for the strings
   `monitor.c` carries in `InfoStrings`: board name, source path
   (`/embedded/tags/PresTag`), git hash and repository. Every image built with
   the common monitor has them. They sit at no fixed place, so this is a
   heuristic, and the manifest records it as one.
3. **An explicit argument.** `board=` or `loader=` from the caller. It always
   overrides the other two, and any disagreement is recorded. Today
   `tag-capture --loader` and `tag-xflash --loader` are this method.

Whatever the source, the loader confirms the JEDEC ID of the part it drives.

**The catalog.** A JSON file generated at build time and installed beside the
firmware and loaders. For each distributed tag target it records:
- the target name;
- the image SHA-256 and length;
- the board;
- the loader.

The board comes from the tag's `project.mk`, which the build already reads to
derive the distributed board set. For the mapping from board to loader,
`add_embedded_loader` gains a `BOARD` argument. Today the board is implicit in
`LOADER_BOARD_INC`.

*Check:*
- a tag running a released image is identified by hash;
- a development build is identified by strings, and marked heuristic;
- a mismatched explicit argument is reported.

## Rescue

A `Rescue` procedure and `tag-xflash rescue-erase`. The order is:
1. external erase, with a blank-check;
2. internal erase and reflash;
3. `tag-reset` in a monitor session.

The steps run as separate sessions, in sequence. Why the order matters is in
[Loader Runtime Design, Rescue erase](../../../../../embedded/loaders/design/loader-runtime.md#rescue-erase).

`rescue-erase` refuses to run unless it is given either a capture directory
from the same tag, matched by chip UID, or `--no-capture`.

A full rescue erase can run for over a minute. The host polls `ack` and
`progress` rather than blocking, and its timeout must allow for the loader's
worst-case budgets.

*Check:* on a bench tag with data in both flashes, the tag ends consistent: its
first new run downloads correctly. An interruption between the steps leaves the
harmless state described in Loader Runtime Design.

## Open questions

- **Chip UID as the tag's identity.** The capture records the MCU's 96-bit UID,
  and `rescue-erase` would match it. Where a per-deployment record of UID to
  image should live is still open; see
  [Field Data Extraction](../../../../../embedded/tags/design/proposals/field-data-extraction.md#open-questions).
