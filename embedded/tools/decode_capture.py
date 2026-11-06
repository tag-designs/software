#!/usr/bin/env python3
"""Decode the retained state in a tag capture directory.

@details Reads a directory written by `tag-capture` (host/commandline-rs), or
         one written by the retired tag_capture_state.py, and prints:

         - what was captured: when, why, from which MCU and UID, and the
           image's identity;
         - the build stored with it (`tag-capture --with-build`) and whether
           that ELF matched the captured flash;
         - pState, the BackupState mirror in the backup registers, decoded
           with the layout the image's identity record gives (word count,
           valid magic, and the words holding valid, state, pages,
           external_blocks and resetCause). BackupState differs by family,
           so without a record the common layout is assumed and said so;
         - the boot diagnostics (tag_backup_diag_*) that an image built with
           TAG_RETAINED_RUN_DIAGNOSTICS keeps in SRAM, located through the
           stored ELF's symbols, from whichever SRAM regions were captured
           (`tag-capture --sram`).

         The identity record is read from the captured internal flash, or,
         when the capture holds no image (tag_capture_state.py saved only
         the persistent pages), from the stored ELF.

         `--scratchpad-out FILE` writes the image's retained scratchpad page
         (U375 TAG_SCRATCHPAD) from the captured SRAM, for
         decode_scratchpad.py. The capture directory is never written.

         Exit status: 0 decoded, 1 not a capture directory.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys

# decode_tag_identity.py sits beside this script; import its record parser.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decode_tag_identity as tid  # noqa: E402

#: SRAM1 base on both MCUs; the legacy format's single sram.bin starts here.
SRAM_BASE = 0x20000000

#: The STM32L432 maps SRAM2 at 0x10000000 and again at 0x2000C000; images
#: link it at either address.
L432_SRAM2 = (0x10000000, 0x2000C000)

#: The common BackupState, used only when the image carries no identity
#: record (embedded/tags/common/core/inc/persistent.h). Families override it.
COMMON_LAYOUT = {"word_count": 8, "valid_magic": None, "word_valid": 0,
                 "word_state": 3, "word_pages": 4,
                 "word_external_blocks": None, "word_reset_cause": 2}

TAG_STATES = {
    0: "STATE_UNSPECIFIED", 1: "TEST", 2: "IDLE", 3: "CONFIGURED",
    4: "RUNNING", 5: "HIBERNATING", 6: "ABORTED", 7: "FINISHED",
    8: "sRESET", 9: "EXCEPTION", 10: "CALIBRATE",
}

#: Boot phases recorded by tagBackupStateDebug(), from main.c.
PHASES = {1: "after backup enable", 2: "after reset cause", 4: "after device init"}


def symbols(elf: str) -> dict[str, int]:
    """Map symbol name to address for every symbol in an image.

    @param elf Firmware ELF.
    @return Name to address; empty when the image cannot be read.
    """
    try:
        res = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True,
                             text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return {}
    out = {}
    for line in res.stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            try:
                out[p[2]] = int(p[0], 16)
            except ValueError:
                pass
    return out


class Capture:
    """One capture directory, in either format.

    @details `fmt` is "tag-capture" or "legacy". `regions` lists
             (address, bytes) for every region file present, so memory can
             be read by absolute address whatever the format.
    """

    def __init__(self, path: str):
        """Load the manifest and every region file.

        @param path Capture directory.
        @exception FileNotFoundError when it holds no manifest.json.
        """
        self.path = path
        with open(os.path.join(path, "manifest.json")) as f:
            self.manifest = json.load(f)
        m = self.manifest
        self.fmt = "tag-capture" if "format" in m else "legacy"
        self.regions: list[tuple[int, bytes, str]] = []
        if self.fmt == "tag-capture":
            entries = [(r["name"], int(r["addr"], 16), r.get("file"))
                       for r in m.get("regions", []) if r.get("ok", True)]
        else:
            entries = [(name, int(r["address"], 16), r.get("file"))
                       for name, r in m.get("regions", {}).items()]
        for name, addr, file in entries:
            fp = os.path.join(path, file) if file else None
            if fp and os.path.exists(fp):
                with open(fp, "rb") as f:
                    self.regions.append((addr, f.read(), name))
        if self.mcu() == "STM32L432":
            # Make SRAM2 readable at both of its addresses.
            for addr, data, name in list(self.regions):
                if addr == L432_SRAM2[0]:
                    self.regions.append((L432_SRAM2[1], data, name))

    def mcu(self) -> str | None:
        """The MCU named in the manifest, or None (legacy captures: U375)."""
        if self.fmt == "tag-capture":
            return self.manifest.get("target", {}).get("mcu")
        return "STM32U375"

    def read(self, addr: int, length: int) -> bytes | None:
        """Bytes at an absolute address, if one captured region holds them all.

        @param addr Target address.
        @param length Byte count.
        @return The bytes, or None when no region covers the range.
        """
        for base, data, _ in self.regions:
            off = addr - base
            if 0 <= off and off + length <= len(data):
                return data[off:off + length]
        return None

    def region(self, name: str) -> bytes | None:
        """A region's bytes by name, or None."""
        for _, data, n in self.regions:
            if n == name:
                return data
        return None

    def elf(self) -> str | None:
        """Path of the ELF stored with the capture, or None."""
        m = self.manifest
        if self.fmt == "tag-capture":
            rel = (m.get("build") or {}).get("elf")
            return os.path.join(self.path, rel) if rel else None
        for a in m.get("artifacts", []):
            if a["file"].endswith(".elf"):
                return os.path.join(self.path, a["file"])
        return None


def identity(cap: Capture, elf: str | None) -> tuple[dict | None, str]:
    """The image's identity record, and where it was read from.

    @param cap The capture.
    @param elf The stored or overriding ELF, if any.
    @return (record, source); record is None when none was found.
    """
    flash = cap.region("internal_flash")
    mcus = [cap.mcu()] if cap.mcu() in tid.OFFSETS else sorted(tid.OFFSETS)
    sources = []
    if flash:
        sources.append(("captured flash", flash))
    if elf and os.path.exists(elf):
        try:
            sources.append(("stored ELF", tid.image_from(elf)))
        except (OSError, subprocess.CalledProcessError):
            pass
    for source, image in sources:
        for mcu in mcus:
            try:
                return tid.decode(image[tid.OFFSETS[mcu]:]), source
            except ValueError:
                continue
    return None, "none"


def print_header(cap: Capture) -> None:
    """Print when, why and from what the capture was taken."""
    m = cap.manifest
    print(f"capture {os.path.basename(os.path.normpath(cap.path))}  ({cap.fmt} format)")
    if cap.fmt == "tag-capture":
        t = m.get("target", {})
        print(f"  taken    {m.get('captured_at')}")
        print(f"  reason   {m.get('reason') or '(none given)'}")
        print(f"  target   {t.get('mcu')}  uid {t.get('uid')}  {t.get('voltage')} V")
        ident = m.get("identity", {})
        if ident.get("found"):
            print(f"  image    {ident.get('target')} on {ident.get('board')}, "
                  f"git {str(ident.get('git_sha'))[:8]}")
        else:
            print("  image    no identity record")
        print(f"  complete {m.get('complete')}")
    else:
        print(f"  taken    {m.get('captured_utc')}")
        print(f"  reason   {m.get('reason') or '(none given)'}")
        print(f"  tree     {m.get('tree_git_hash')}")


def print_build(cap: Capture, elf: str | None, override: bool) -> None:
    """Print the stored build and whether it matches the captured flash."""
    if override:
        print(f"\n  ELF      {elf} (--elf)")
        return
    if not elf or not os.path.exists(elf):
        print("\n  no ELF stored with this capture (tag-capture --with-build); "
              "SRAM symbols cannot be located")
        return
    print(f"\n  ELF      {os.path.relpath(elf, cap.path)}")
    if cap.fmt == "tag-capture":
        check = (cap.manifest.get("build") or {}).get("image_check") or {}
        result = check.get("result")
        if result == "matches":
            print(f"  image    matches the captured flash "
                  f"({check.get('bytes_compared')} bytes compared)")
        elif result == "differs":
            print(f"  image    DIFFERS from the captured flash, first at "
                  f"{check.get('first_difference')}: this ELF is not what was running")
        else:
            print(f"  image    not checked against the captured flash {json.dumps(check)}")


def print_backup_state(cap: Capture, rec: dict | None, source: str) -> None:
    """Decode pState from backup_regs.bin with the record's layout."""
    data = cap.region("backup_regs")
    if data is None:
        print("\n  no backup registers in this capture")
        return
    words = [struct.unpack_from("<I", data, o)[0] for o in range(0, len(data) - 3, 4)]
    layout = (rec or {}).get("backup_state")
    if layout:
        print(f"\n  pState (backup registers, layout from the identity record in the {source})")
        magic = int(layout["valid_magic"], 16)
    else:
        layout = COMMON_LAYOUT
        magic = None
        print("\n  pState (backup registers; NO identity record, common layout ASSUMED -- "
              "families override it, so check against the image's persistent.h)")
    named = {layout["word_valid"]: "valid", layout["word_state"]: "state",
             layout["word_pages"]: "pages", layout["word_reset_cause"]: "resetCause"}
    if layout.get("word_external_blocks") is not None:
        named[layout["word_external_blocks"]] = "external_blocks"
    for i in range(min(layout["word_count"], len(words))):
        v = words[i]
        name = named.get(i, f"word {i}")
        note = ""
        if name == "valid":
            if magic is None:
                note = "  (magic unknown without a record)"
            elif v == magic:
                note = "  <- valid"
            else:
                note = f"  <- NOT VALID (magic 0x{magic:X}): backup domain lost or never set"
        elif name == "state":
            note = f"  ({TAG_STATES.get(v, '?')})"
        print(f"    {name:<16} 0x{v:08X}  {v}{note}")


def print_diagnostics(cap: Capture, syms: dict[str, int]) -> None:
    """Print tag_backup_diag_* from captured SRAM, when there is any."""
    if not any(n.startswith("sram") for _, _, n in cap.regions):
        print("\n  no SRAM in this capture (tag-capture --sram)")
        return
    diag = sorted(n for n in syms if n.startswith("tag_backup_diag_"))
    if not diag:
        print("\n  no tag_backup_diag_* symbols: the image was built "
              "without TAG_RETAINED_RUN_DIAGNOSTICS")
        return
    print("\n  boot diagnostics (tag_backup_diag_*)")
    for n in diag:
        b = cap.read(syms[n], 4)
        if b is None:
            continue
        (v,) = struct.unpack("<I", b)
        short = n[len("tag_backup_diag_"):]
        note = ""
        if short == "state":
            note = f"  ({TAG_STATES.get(v, '?')})"
        if short == "latest_phase":
            note = f"  ({PHASES.get(v, '?')})"
        if short == "phase_mask":
            hit = [d for bit, d in PHASES.items() if v & bit]
            note = "  (" + ", ".join(hit) + ")" if hit else ""
        print(f"    {short:<18} 0x{v:08X}  {v}{note}")


def image_differs(cap: Capture) -> bool:
    """True when tag-capture found the stored ELF unlike the captured flash."""
    check = (cap.manifest.get("build") or {}).get("image_check") or {}
    return check.get("result") == "differs"


def scratchpad(cap: Capture, rec: dict | None) -> tuple[int, bytes] | None:
    """The retained scratchpad page, if the record names it and SRAM holds it."""
    region = (rec or {}).get("scratchpad")
    if not region:
        return None
    start = int(region["start"], 16)
    end = region["end"]
    if not isinstance(end, str) or not end.startswith("0x"):
        return None
    data = cap.read(start, int(end, 16) - start)
    return (start, data) if data is not None else None


def main() -> int:
    """Print the decoded contents of a capture directory.

    @return 0 on success, 1 when the capture is unusable.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("capture_dir",
                   help="directory written by tag-capture (or tag_capture_state.py)")
    p.add_argument("--elf", help="use this ELF instead of the one stored with the capture")
    p.add_argument("--scratchpad-out", metavar="FILE",
                   help="write the retained scratchpad page to FILE, for decode_scratchpad.py")
    args = p.parse_args()

    try:
        cap = Capture(args.capture_dir)
    except FileNotFoundError:
        print(f"{args.capture_dir}: not a capture directory (no manifest.json)",
              file=sys.stderr)
        return 1

    elf = args.elf or cap.elf()
    print_header(cap)
    print_build(cap, elf, bool(args.elf))
    rec, source = identity(cap, elf)
    print_backup_state(cap, rec, source)
    syms = symbols(elf) if elf and os.path.exists(elf) else {}
    if syms and not args.elf and image_differs(cap):
        print("\n  SRAM symbols not used: the stored ELF is not the image that ran "
              "(pass --elf with the right one)")
    elif syms:
        print_diagnostics(cap, syms)

    page = scratchpad(cap, rec)
    if page:
        start, data = page
        print(f"\n  scratchpad page at 0x{start:08X}, {len(data)} bytes captured")
        if args.scratchpad_out:
            with open(args.scratchpad_out, "wb") as f:
                f.write(data)
            print(f"    written to {args.scratchpad_out}; "
                  f"decode with decode_scratchpad.py {args.scratchpad_out}")
    elif args.scratchpad_out:
        print("\n  no scratchpad page: the image names none, or SRAM was not captured "
              "(tag-capture --sram)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
