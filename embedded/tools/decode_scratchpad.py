#!/usr/bin/env python3
"""Decode the retained debug scratchpad read out of SRAM2 page 3.

Read it from a tag with the debug probe, connecting under reset because
hotplug does not work on this rig. The reset does not matter: the region is
outside ram0, so crt0 never clears it, and PWR_CR1_RRSB3 retains it through
Standby.

    STM32_Programmer_CLI -c port=SWD mode=UR -u 0x2003E000 8192 scratch.bin
    decode_scratchpad.py scratch.bin

Records are self-describing, so this decoder does not need to know what the
firmware chose to log. See embedded/tags/common/core/inc/scratchpad.h.
"""

from __future__ import annotations

import argparse
import struct
import sys

MAGIC = 0x33524353     #: "SCR3", written once the region is formatted.
HDR = 32               #: Header size in bytes.
TEXT, WORD = 1, 2      #: Record kinds; see enum TagScratchKind.


def decode(path: str, show_raw: bool) -> int:
    """Print the scratchpad contents.

    @param path     Binary dump of the region.
    @param show_raw True to also print undecodable trailing bytes.
    @return 0 when a valid scratchpad was decoded, 1 otherwise.
    """
    d = open(path, "rb").read()
    if len(d) < HDR:
        print(f"{path}: too short ({len(d)} bytes)", file=sys.stderr)
        return 1
    magic, seq, used, overflow = struct.unpack("<4I", d[:16])
    if magic != MAGIC:
        print(f"{path}: no scratchpad here (magic 0x{magic:08X}).\n"
              "  Either it was never formatted, or it was not retained -- check\n"
              "  that PWR_CR1_RRSB3 is set before the Standby arming sequence.",
              file=sys.stderr)
        return 1
    print(f"scratchpad: seq={seq} used={used} bytes overflow={overflow}")
    if overflow:
        print(f"  WARNING: {overflow} bytes discarded, the log is truncated")

    body = d[HDR:HDR + used]
    i, n = 0, 0
    while i < len(body):
        kind = body[i]
        i += 1
        if kind == TEXT:
            end = body.find(b"\x00", i)
            if end < 0:
                print("  (truncated text record)")
                break
            print(f"  [{n:3}] {body[i:end].decode('ascii', 'replace')}")
            i = end + 1
        elif kind == WORD:
            if i + 8 > len(body):
                print("  (truncated word record)")
                break
            label = body[i:i + 4].decode("ascii", "replace")
            (value,) = struct.unpack("<I", body[i + 4:i + 8])
            print(f"  [{n:3}] {label} = 0x{value:08X}  ({value})")
            i += 8
        else:
            print(f"  (unknown record kind {kind} at offset {i - 1}; stopping)")
            break
        n += 1
    print(f"  {n} record(s)")
    if show_raw and used < len(d) - HDR:
        tail = d[HDR + used:HDR + used + 64]
        print(f"  next 64 unused bytes: {tail.hex()}")
    return 0


def main() -> int:
    """Command line entry point.

    @return Process exit status.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("file", help="binary dump of the 8 KB region")
    p.add_argument("--raw", action="store_true",
                   help="also show bytes past the used length")
    args = p.parse_args()
    return decode(args.file, args.raw)


if __name__ == "__main__":
    sys.exit(main())
