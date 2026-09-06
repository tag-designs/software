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


#: Bytes of payload after the 32-byte header.
CAP = 0x2000 - 32


def _walk(body: bytes, base: int, n0: int, out: list) -> tuple[int, bool]:
    """Decode records from a byte run, appending formatted lines to @p out.

    @param body Bytes to decode.
    @param base Offset of @p body within the region, for messages.
    @param n0   Record number to start counting from.
    @param out  Receives one formatted line per record.
    @return Number of records decoded, and whether the run ended cleanly.
    """
    i, n = 0, 0
    while i < len(body):
        kind = body[i]
        if kind == 0:
            # Gap left when a record would not fit the tail. Everything from
            # here to the end of the run is padding.
            if body[i:].strip(b"\x00") == b"":
                return n, True
            i += 1
            continue
        i += 1
        if kind == TEXT:
            e = body.find(b"\x00", i)
            if e < 0:
                return n, False
            out.append(f"  [{n0 + n:4}] {body[i:e].decode('ascii', 'replace')}")
            i = e + 1
        elif kind == WORD:
            if i + 8 > len(body):
                return n, False
            label = body[i:i + 4].decode("ascii", "replace")
            (value,) = struct.unpack("<I", body[i + 4:i + 8])
            out.append(f"  [{n0 + n:4}] {label} = 0x{value:08X}  ({value})")
            i += 8
        else:
            return n, False
        n += 1
    return n, True


def _walk_resync(body: bytes, base: int, n0: int, out: list) -> tuple[int, int]:
    """Decode a run whose first record may be truncated by a ring wrap.

    @details A wrapped ring restarts writing at offset zero, so the bytes just
             after the write cursor are the tail of a record from the previous
             lap. Records here are not self-synchronising, so the phase is
             recovered by trying each of the first nine offsets and keeping the
             one that decodes the most records and ends cleanly.

    @return Records decoded, and the offset that was skipped to resynchronise.
    """
    best, best_skip, best_clean = [], 0, False
    for skip in range(9):
        trial = []
        n, clean = _walk(body[skip:], base + skip, n0, trial)
        if (clean, n) > (best_clean, len(best)):
            best, best_skip, best_clean = trial, skip, clean
    out.extend(best)
    return len(best), best_skip


def decode(path: str, show_raw: bool) -> int:
    """Print the scratchpad contents, oldest record first.

    @param path     Binary dump of the region.
    @param show_raw True to also print undecodable trailing bytes.
    @return 0 when a valid scratchpad was decoded, 1 otherwise.
    """
    d = open(path, "rb").read()
    if len(d) < HDR:
        print(f"{path}: too short ({len(d)} bytes)", file=sys.stderr)
        return 1
    magic, seq, used, overflow, wrapped = struct.unpack("<5I", d[:20])
    if magic != MAGIC:
        print(f"{path}: no scratchpad here (magic 0x{magic:08X}).\n"
              "  Either it was never formatted, or it was not retained -- check\n"
              "  that PWR_CR1_RRSB3 is set before the Standby arming sequence.",
              file=sys.stderr)
        return 1
    # Exactly 1, not merely non-zero: this word was reserved padding before
    # ring mode existed, so a dump from older firmware still decodes as linear.
    wrapped = (wrapped == 1)

    print(f"scratchpad: seq={seq} used={used} bytes overflow={overflow} "
          f"({'ring, wrapped' if wrapped else 'linear'})")

    lines: list[str] = []
    if wrapped:
        # Two runs with independent framing. [used, CAP) is the previous lap,
        # older, and starts mid-record. [0, used) is this lap, newer, and
        # always starts on a record boundary. Oldest is printed first.
        older = d[HDR + used:HDR + CAP]
        newer = d[HDR:HDR + used]
        n_old, skip = _walk_resync(older, used, 0, lines)
        if skip:
            lines.insert(0, f"  (resynchronised {skip} bytes into the older "
                            f"lap; a partial record was discarded)")
        n_new, clean = _walk(newer, 0, n_old, lines)
        n = n_old + n_new
        print(f"  ring wrapped: {n_old} older record(s) from offset {used}, "
              f"then {n_new} newer from offset 0")
        if not clean:
            print("  WARNING: the newest run did not decode cleanly")
    else:
        if overflow:
            print(f"  WARNING: {overflow} bytes discarded, the log is "
                  f"truncated -- consider -DTAG_SCRATCHPAD_RING=1")
        n, clean = _walk(d[HDR:HDR + used], 0, 0, lines)
        if not clean:
            print("  WARNING: the log did not decode cleanly")

    for line in lines:
        print(line)
    print(f"  {n} record(s)")
    if show_raw and not wrapped and used < len(d) - HDR:
        print(f"  next 64 unused bytes: {d[HDR + used:HDR + used + 64].hex()}")
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
