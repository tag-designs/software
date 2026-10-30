#!/usr/bin/env python3
"""Decode the retained debug scratchpad read out of SRAM2 page 3.

Read it from a tag through its base, which connects under reset. The reset
does not matter: the region is outside ram0, so crt0 never clears it, and
PWR_CR1_RRSB3 retains it through Standby. `--read` reads it with tag-flash
(host/commandline-rs) into the file first, then decodes it:

    decode_scratchpad.py --read scratch.bin [--base <serial>]

which is the same as

    tag-flash read 0x2003E000 8192 -o scratch.bin
    decode_scratchpad.py scratch.bin

The scratchpad exists only on the STM32U375; on an STM32L432 the address is
not RAM.

Records are self-describing, so this decoder does not need to know what the
firmware chose to log. See embedded/tags/common/core/inc/scratchpad.h.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys

from tag_flash import find_tag_flash

MAGIC = 0x33524353     #: "SCR3", written once the region is formatted.
HDR = 32               #: Header size in bytes.
TEXT, WORD = 1, 2      #: Record kinds; see enum TagScratchKind.


#: Labels whose value is not a bare number, and how to read it.
#:
#: STAT is written by recordState() for every state transition and packs
#: state<<16 | reason, so a capture shows how a run ended without decoding the
#: flash marker log.
TAG_STATES = {
    0: "STATE_UNSPECIFIED", 1: "TEST", 2: "IDLE", 3: "CONFIGURED",
    4: "RUNNING", 5: "HIBERNATING", 6: "ABORTED", 7: "FINISHED",
    8: "sRESET", 9: "EXCEPTION", 10: "CALIBRATE",
}
STATE_EVENTS = {
    0: "UNSPECIFIED", 1: "OK", 2: "STARTCMD", 3: "ENDTIM", 4: "STARTTIM",
    5: "STARTHIB", 6: "ENDHIB", 7: "STOPCMD", 8: "RESETCMD", 9: "LOWBATTERY",
    10: "INTERNALFULL", 11: "EXTERNALFULL", 12: "BROWNOUT", 13: "POWERFAIL",
    14: "UNKNOWN", 15: "SLEEP", 16: "STANDBY", 17: "SHUTDOWN",
    18: "EXCEPTION", 19: "STORAGEERROR", 20: "CONFIGERROR",
}


def annotate(label: str, value: int) -> str:
    """Render a value that means more than its number.

    @param label Four-character record label.
    @param value Recorded word.
    @return Trailing annotation, empty when the number speaks for itself.
    """
    tag = label.rstrip("\x00 ")
    if tag == "STAT":
        st, rs = value >> 16, value & 0xFFFF
        return (f"  state={TAG_STATES.get(st, st)} "
                f"reason={STATE_EVENTS.get(rs, rs)}")
    if tag in ("DSTA", "RSTA"):
        return f"  ({TAG_STATES.get(value, '?')})"
    if tag == "DVAL":
        return "  (BACKUP_STATE_VALID_MAGIC)" if value == 0x54414742 \
            else "  (backup state NOT valid)"
    if tag == "ESLF":
        return "  <- flash marker log full; transitions stopped being recorded"
    if tag == "ESKP":
        return "  <- external page skipped after a write error"
    if tag == "EGUP":
        return "  <- gave up: too many consecutive page write failures"
    if tag == "EECC":
        return "  <- uncorrectable ECC on this NAND page"
    if tag == "ECFG":
        return (f"  <- stored config did not read back; first bad word {value}"
                " -- the start was aborted")
    return ""


#: Bytes of payload after the 32-byte header.
CAP = 0x2000 - 32

#: Where the region is, and its size: TAG_SCRATCH_BASE in scratchpad.h, the
#: last 8 KB of the STM32U375's SRAM.
REGION_ADDRESS = 0x2003E000
REGION_BYTES = 0x2000


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
            out.append(f"  [{n0 + n:4}] {label} = 0x{value:08X}  "
                       f"({value}){annotate(label, value)}")
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


def read_region(path: str, base: str | None, explicit: str | None) -> int:
    """Read the region from a tag into @p path with tag-flash.

    @details Attaches under reset, which ends whatever the tag was doing; the
             region survives it. See docs/bench/debugging-a-tag.md.

    @param path     File to write.
    @param base     USB serial of the base, or None for the only one.
    @param explicit tag-flash path from the command line, or None.
    @return tag-flash's exit status, or 1 when it was not found.
    """
    tag_flash = find_tag_flash(explicit)
    if tag_flash is None:
        print("tag-flash was not found: pass --tag-flash, set TAG_FLASH, or "
              "build it with cargo build --release in host/commandline-rs",
              file=sys.stderr)
        return 1
    command = [tag_flash]
    if base:
        command += ["--base", base]
    command += ["read", hex(REGION_ADDRESS), str(REGION_BYTES), "-o", path]
    return subprocess.call(command)


def main() -> int:
    """Command line entry point.

    @return Process exit status.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("file", help="binary dump of the 8 KB region")
    p.add_argument("--raw", action="store_true",
                   help="also show bytes past the used length")
    p.add_argument("--read", action="store_true",
                   help="first read the region from the tag into FILE with "
                   "tag-flash (resets the tag)")
    p.add_argument("--base", help="USB serial of the base, for --read; needed "
                   "only when more than one base is connected")
    p.add_argument("--tag-flash", help="tag-flash path, for --read")
    args = p.parse_args()
    if args.read:
        status = read_region(args.file, args.base, args.tag_flash)
        if status != 0:
            return status
    return decode(args.file, args.raw)


if __name__ == "__main__":
    sys.exit(main())
