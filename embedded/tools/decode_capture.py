#!/usr/bin/env python3
"""Decode a capture taken by tag_capture_state.py.

@details Reads named globals out of the SRAM image and the retained state out
         of the backup registers, using the ELF stored alongside the capture to
         locate them. Symbol addresses come from the image that produced the
         capture, so a firmware change moves the decoder with it instead of
         silently reporting the wrong words.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys

SRAM_BASE = 0x20000000
BACKUP_STATE_VALID_MAGIC = 0x54414742

#: pState field order; see BackupState in
#: embedded/tags/common/core/inc/persistent.h.
BACKUP_FIELDS = [
    ("valid", "I"), ("safe", "I"), ("resetCause", "I"), ("state", "I"),
    ("pages", "I"), ("lastactstart", "i"), ("temp10", "i"), ("vdd100", "I"),
]

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


def read_u32(sram: bytes, addr: int) -> int | None:
    """Read one word from an SRAM image by absolute address.

    @param sram SRAM contents starting at SRAM_BASE.
    @param addr Absolute target address.
    @return The word, or None when the address is outside the image.
    """
    off = addr - SRAM_BASE
    if off < 0 or off + 4 > len(sram):
        return None
    return struct.unpack_from("<I", sram, off)[0]


def main() -> int:
    """Print the decoded contents of a capture directory.

    @return 0 on success, 1 when the capture is unusable.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("capture_dir", help="directory written by tag_capture_state.py")
    p.add_argument("--elf", help="override the ELF stored with the capture")
    args = p.parse_args()

    mpath = os.path.join(args.capture_dir, "manifest.json")
    if not os.path.exists(mpath):
        print(f"{mpath}: not a capture directory", file=sys.stderr)
        return 1
    manifest = json.load(open(mpath))
    print(f"capture {os.path.basename(args.capture_dir)}")
    print(f"  taken   {manifest.get('captured_utc')}")
    print(f"  reason  {manifest.get('reason') or '(none given)'}")
    print(f"  tree    {manifest.get('tree_git_hash')}")

    elf = args.elf
    if not elf:
        for a in manifest.get("artifacts", []):
            if a["file"].endswith(".elf"):
                elf = os.path.join(args.capture_dir, a["file"])
    if not elf or not os.path.exists(elf):
        print("  no ELF stored with this capture; cannot locate symbols",
              file=sys.stderr)

    # Retained state, which is what a reset failure is usually about.
    bpath = os.path.join(args.capture_dir, "backup_regs.bin")
    if os.path.exists(bpath):
        d = open(bpath, "rb").read()
        print("\n  pState (TAMP backup registers)")
        off = 0
        for name, fmt in BACKUP_FIELDS:
            (v,) = struct.unpack_from("<" + fmt, d, off)
            off += 4
            note = ""
            if name == "valid":
                note = ("  <- BACKUP_STATE_VALID_MAGIC"
                        if v == BACKUP_STATE_VALID_MAGIC
                        else "  <- NOT VALID: backup domain lost or never set")
            if name == "state":
                note = f"  ({TAG_STATES.get(v, '?')})"
            print(f"    {name:<14} 0x{v & 0xFFFFFFFF:08X}  {v}{note}")

    # Boot diagnostics, present only when TAG_RETAINED_RUN_DIAGNOSTICS is set.
    spath = os.path.join(args.capture_dir, "sram.bin")
    if elf and os.path.exists(elf) and os.path.exists(spath):
        syms = symbols(elf)
        sram = open(spath, "rb").read()
        diag = sorted(n for n in syms if n.startswith("tag_backup_diag_"))
        if not diag:
            print("\n  no tag_backup_diag_* symbols: the image was built "
                  "without TAG_RETAINED_RUN_DIAGNOSTICS")
        else:
            print("\n  boot diagnostics (tag_backup_diag_*)")
            for n in diag:
                v = read_u32(sram, syms[n])
                if v is None:
                    continue
                short = n[len("tag_backup_diag_"):]
                note = ""
                if short == "state":
                    note = f"  ({TAG_STATES.get(v, '?')})"
                if short == "latest_phase":
                    note = f"  ({PHASES.get(v, '?')})"
                if short == "phase_mask":
                    hit = [d for b, d in PHASES.items() if v & b]
                    note = "  (" + ", ".join(hit) + ")" if hit else ""
                print(f"    {short:<18} 0x{v:08X}  {v}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
