#!/usr/bin/env python3
"""Bench check of a NOR external-flash loader's write path, through tagcore.

@details The loaders README's bench sequence ("Testing a loader on the
         bench"), run through the tagcore Python binding
         (host/python/tagcore-py) and the loader's Serve() entry point, with
         no STM32CubeProgrammer:

         1. read the whole part twice with the read-only (RO) loader and
            compare, and keep the backup registers;
         2. write a random pattern, at an unaligned offset and length, with
            the read-write (RW) loader, and read it back with RO, checking the
            bytes either side of it are still blank;
         3. write a second pattern over the first, which needs real erases;
         4. ask RO to erase and to program: both must be refused, and the data
            must be unchanged;
         5. erase the test window with RW;
         6. check the whole part reads exactly as in step 1, and the backup
            registers are unchanged: the loaders write neither.

         The test window is at the end of the part, and must be blank to
         start, so a tag's log at the start of the part is left alone; step 6
         proves it. One SWD session, opened halted at the reset vector, holds
         every loader session in turn; the tag is reset at the end.

         Needs the tagcore package (maturin develop in host/python/tagcore-py)
         and the loader images, by path or by name under --loader-dir or
         $TAG_LOADER_DIR.

         Exit status: 0 every step passed, 1 a step failed (the tag may then
         hold test data in the window: re-run, or erase it with the RW loader).
"""
from __future__ import annotations

import argparse
import random
import sys
import time

try:
    import tagcore
    from tagcore import swd
except ImportError:
    sys.exit("tagcore is not installed: run `maturin develop` in host/python/tagcore-py")


def progress(label: str):
    """A read progress callback that prints a percentage in place."""
    def show(done: int, total: int) -> None:
        print(f"\r    {label}: {100 * done // total:3d}%", end="", flush=True)
        if done == total:
            print()
    return show


class Check:
    """Prints and counts step results."""

    def __init__(self) -> None:
        self.failures = 0

    def step(self, ok: bool, text: str) -> bool:
        """Records one result."""
        print(f"  {'PASS' if ok else 'FAIL'}  {text}")
        if not ok:
            self.failures += 1
        return ok


def erase_window(xf, start: int, end: int) -> int:
    """Erases every sector overlapping [start, end); returns how many."""
    sec = xf.sector_size
    first = start - start % sec
    n = 0
    for off in range(first, end, sec):
        xf.erase_sector(off)
        n += 1
    return n


def main() -> int:
    """Runs the sequence; returns the exit status."""
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ro", required=True, help="read-only loader: path or name, e.g. AT25XE_PresTagv3")
    p.add_argument("--rw", required=True, help="read-write loader: path or name, e.g. AT25XE_PresTagv3-RW")
    p.add_argument("--loader-dir", action="append", help="directory searched for <name>.stldr (repeatable)")
    p.add_argument("--base", help="base serial or bus:address (default: the only base)")
    p.add_argument("--window-kb", type=int, default=64, help="test window at the end of the part (default 64)")
    p.add_argument("--seed", type=int, help="random seed (default: from the clock)")
    args = p.parse_args()

    seed = args.seed if args.seed is not None else int(time.time())
    rng = random.Random(seed)
    c = Check()
    t0 = time.time()
    with swd.open(base=args.base) as s:
        print(f"{s.mcu_name} {s.uid}, halted at 0x{s.attach['pc']:08X} (firmware did not run); seed {seed}")
        bkp_before = s.read_region("backup_regs")

        print("1. read the part twice with RO")
        with s.external_flash(args.ro, loader_dirs=args.loader_dir) as xf:
            c.step(not xf.writable, f"{xf.loader} is read-only")
            size, sec = xf.size, xf.sector_size
            print(f"    JEDEC 0x{xf.jedec:06X}, {size} bytes, {sec}-byte sectors, SR1 0x{xf.sr1:02X}")
            if xf.page_bytes:
                print("  a paged (NAND) part: this check is for NOR loaders")
                return 1
            before = xf.read(0, size, progress=progress("read 1"))
            again = xf.read(0, size, progress=progress("read 2"))
            c.step(before == again, "two whole-part reads agree")
            all_ff = before.count(0xFF) == size
            c.step(not all(b == 0 for b in before[:4096]),
                   "the part does not read as zeros (the loader is in use)")
            print(f"    {'blank' if all_ff else str(size - before.count(0xFF)) + ' bytes in use'}")

        wstart = size - args.window_kb * 1024
        if before[wstart:] != b"\xff" * (size - wstart):
            print(f"  the test window 0x{wstart:06X}-0x{size:06X} is not blank: refusing to write")
            return 1
        off = wstart + 2 * sec + 123                 # unaligned, inside the window
        length = min(3 * sec + 17, size - off - 64)  # unaligned, several sectors
        a = bytes(rng.getrandbits(8) for _ in range(length))
        b = bytes(rng.getrandbits(8) for _ in range(length))

        def readback(expect: bytes, label: str) -> None:
            with s.external_flash(args.ro, loader_dirs=args.loader_dir) as ro:
                got = ro.read(wstart, size - wstart)
            lo, hi = off - wstart, off - wstart + length
            c.step(got[lo:hi] == expect, f"{label}: RO reads back the {length} bytes at 0x{off:06X}")
            c.step(got[:lo] == b"\xff" * lo and got[hi:] == b"\xff" * (len(got) - hi),
                   f"{label}: the bytes either side are still blank")

        print("2. write pattern A with RW, read it back with RO")
        with s.external_flash(args.rw, loader_dirs=args.loader_dir) as rw:
            c.step(rw.writable, f"{rw.loader} is writable")
            erase_window(rw, off, off + length)
            rw.program(off, a)
        readback(a, "pattern A")

        print("3. write pattern B over A (real erases), read it back with RO")
        with s.external_flash(args.rw, loader_dirs=args.loader_dir) as rw:
            n = erase_window(rw, off, off + length)
            rw.program(off, b)
        print(f"    {n} sectors erased")
        readback(b, "pattern B")

        print("4. RO must refuse to erase and to program")
        with s.external_flash(args.ro, loader_dirs=args.loader_dir) as ro:
            for what, op in (("erase", lambda: ro.erase_sector(off)),
                             ("program", lambda: ro.program(off, b"\x00" * 16))):
                try:
                    op()
                    c.step(False, f"RO {what} was accepted")
                except tagcore.RecoveryError as e:
                    c.step("READ_ONLY" in str(e).upper().replace("-", "_").replace(" ", "_"),
                           f"RO {what} refused: {e}")
        readback(b, "after the refusals")

        print("5. erase the test window with RW")
        with s.external_flash(args.rw, loader_dirs=args.loader_dir) as rw:
            erase_window(rw, off, off + length)

        print("6. the part and the backup registers are as found")
        with s.external_flash(args.ro, loader_dirs=args.loader_dir) as ro:
            after = ro.read(0, size, progress=progress("read 3"))
        c.step(after == before, "the whole part reads as in step 1")
        c.step(s.read_region("backup_regs") == bkp_before, "the backup registers are unchanged")

    print(f"{'PASSED' if c.failures == 0 else f'FAILED ({c.failures})'} in {time.time() - t0:.0f} s")
    return 0 if c.failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
