#!/usr/bin/env python3
"""Capture a tag's volatile and persistent state after a failure.

@details Connects under reset and stores the three places a tag keeps state
         that a download cannot reach: SRAM, the writable part of internal
         flash, and the RTC backup registers. Nothing runs on the tag to
         produce the output, so the capture does not disturb what it is
         recording -- see embedded/tags/design/debugging.md for why that
         matters on this target.

         Intended to run immediately after a test fails, before anything
         resets or erases the tag. `tag_attach_storm.py --stop-on-failure`
         exists to make that possible.

@warning Connecting under reset restarts the tag, so anything that does not
         survive a reset is already gone by the time this runs. SRAM survives a
         reset; a successful Standby does not retain it unless
         PWR_CR1_RRSB3 was armed.

@warning **SRAM may capture as noise, and it is not obvious.** Catch the tag
         mid-Standby and SRAM is powered down, while mode=UR holds the core in
         reset so it never boots to repopulate it -- the dump is uninitialised
         memory that decodes as plausible-looking garbage rather than failing.
         Observed on one capture out of several taken seconds apart. Sanity
         check before believing a decode: a live image has large zeroed .bss
         and readable strings, noise has neither. The backup registers are in
         the always-powered backup domain and do not have this problem, which
         is why they are the more trustworthy of the two.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

#: Whole SRAM, SRAM1 and SRAM2 together. Covers .data/.bss, the heap, both
#: stacks, the monitor handoff word at the base, and the retained scratchpad in
#: SRAM2 page 3 at 0x2003E000.
SRAM_BASE = 0x20000000
SRAM_SIZE = 256 * 1024

#: Backup registers: TAMP->BKP0R, 32 registers. This block *is* pState; see
#: TAG_BACKUP_STATE_REG0 in embedded/tags/common/core/src/main.c.
BKP_BASE = 0x40007D00
BKP_SIZE = 32 * 4

#: RCC->APB1ENR1, whose bit 30 (RTCAPBEN) clocks the RTC/TAMP APB interface.
#:
#: Holding the core in reset also resets RCC, so this bit is clear and every
#: backup register reads as zero -- indistinguishable from a tag whose backup
#: domain was genuinely lost, which is exactly the thing this capture exists to
#: tell apart. Setting it before the read returns real values. Writing it is
#: safe: the core is held in reset and is reset again afterwards, so nothing
#: observes the change.
RCC_APB1ENR1 = 0x40030C9C
RCC_APB1ENR1_RTCAPBEN = 1 << 30

#: Internal flash page size, and the fixed floor of the persistent region
#: (__tag_code_limit__). Everything below this is code and read-only data,
#: which the built image already tells us; only what a program can write is
#: worth capturing.
FLASH_PAGE = 0x1000
DEFAULT_PERSIST_BASE = 0x08020000

#: Config and NAND-map pages sit at the top of flash, above the internal log
#: headers (__tag_config_start__ and __tag_nand_map_start__). They are captured
#: separately because the header sweep stops at the first erased page and would
#: otherwise never reach them.
DEFAULT_FLASH_END = 0x08100000


def run(argv: list[str], timeout: float = 120.0) -> subprocess.CompletedProcess:
    """Run a command, capturing everything it prints.

    @param argv    Command and arguments.
    @param timeout Seconds before the command is killed.
    @return The completed process, whose output the caller stores verbatim.
    """
    return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)


def upload(addr: int, size: int, out_path: str, verbose: bool,
           pre_write: tuple[int, int] | None = None) -> tuple[bool, str]:
    """Read a memory region over SWD into a file.

    @param addr      Start address.
    @param size      Bytes to read.
    @param out_path  Destination file.
    @param verbose   True to echo the command.
    @param pre_write Optional (address, value) written in the same session
                     before the read, for regions that need a clock enabled to
                     be readable at all.
    @return Success flag and the tool's combined output, which is kept even on
            success so a partial or odd read can be reviewed later.
    """
    argv = ["STM32_Programmer_CLI", "-c", "port=SWD", "mode=UR"]
    if pre_write is not None:
        argv += ["-w32", hex(pre_write[0]), hex(pre_write[1])]
    argv += ["-u", hex(addr), str(size), out_path]
    if verbose:
        print("  $ " + " ".join(argv))
    try:
        res = run(argv)
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        return False, str(e)
    text = res.stdout + res.stderr
    ok = res.returncode == 0 and os.path.exists(out_path) \
        and os.path.getsize(out_path) == size
    return ok, text


def elf_symbols(elf_path: str) -> dict[str, int]:
    """Read the linker-defined region bounds out of a firmware image.

    @details The bounds are properties of the build, not constants: flash size
             and the persistent-region floor vary per target. Reading them from
             the image that produced the capture keeps the two in step, and
             means a capture from an unfamiliar target needs no arguments.

    @param elf_path Firmware ELF.
    @return Symbol name to address, empty when the image cannot be read.
    """
    wanted = ("__tag_code_limit__", "__tag_config_start__",
              "__tag_nand_map_start__", "__flash0_end__")
    out: dict[str, int] = {}
    try:
        res = run(["arm-none-eabi-nm", elf_path], timeout=60.0)
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return out
    if res.returncode != 0:
        return out
    for line in res.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in wanted:
            out[parts[2]] = int(parts[0], 16)
    return out


def sha256(path: str) -> str:
    """Hash a file, so a capture names the exact image that produced it.

    @param path File to hash.
    @return Hex digest, or an empty string when the file cannot be read.
    """
    try:
        h = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(65536), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return ""


def first_erased_page(data: bytes) -> int:
    """Offset of the first wholly erased flash page.

    @details A single 0xFFFFFFFF word can occur inside a valid record, so the
             sweep stops on a whole erased page rather than one erased word.
             That still discards the bulk of an empty region while never
             truncating live data.

    @param data Contents of the persistent region.
    @return Byte offset of the first erased page, or len(data) if none.
    """
    blank = b"\xff" * FLASH_PAGE
    for off in range(0, len(data) - FLASH_PAGE + 1, FLASH_PAGE):
        if data[off:off + FLASH_PAGE] == blank:
            return off
    return len(data)


def main() -> int:
    """Capture SRAM, persistent flash and the backup registers.

    @return 0 when every region was captured, 1 otherwise.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--out-dir", default="captures",
                   help="parent directory for the timestamped capture")
    p.add_argument("--reason", default="",
                   help="what failed, recorded in the manifest")
    p.add_argument("--persist-base", type=lambda v: int(v, 0),
                   default=DEFAULT_PERSIST_BASE,
                   help="__tag_code_limit__, the floor of writable flash")
    p.add_argument("--flash-end", type=lambda v: int(v, 0),
                   default=DEFAULT_FLASH_END,
                   help="__flash0_end__, one past the last flash byte")
    p.add_argument("--elf",
                   help="firmware ELF that is running on the tag. Stored with "
                        "the capture and used for the region bounds. Without "
                        "it a capture cannot be decoded with any confidence: "
                        "test images carry UDEFS that leave no trace in the "
                        "git hash, so the tree alone does not identify them")
    p.add_argument("--extra", action="append", default=[],
                   help="additional file to store with the capture, such as "
                        "the target's project.mk or a .map. Repeatable")
    p.add_argument("--keep-blank", action="store_true",
                   help="store the whole persistent region instead of stopping "
                        "at the first erased page")
    p.add_argument("--verbose", action="store_true", help="echo commands")
    args = p.parse_args()

    stamp = time.strftime("%Y%m%d-%H%M%S")
    outdir = os.path.join(args.out_dir, f"capture-{stamp}")
    os.makedirs(outdir, exist_ok=True)

    persist_base = args.persist_base
    flash_end = args.flash_end
    config_base = flash_end - 2 * FLASH_PAGE
    nandmap_base = flash_end - FLASH_PAGE

    artifacts: list[dict] = []
    if args.elf:
        syms = elf_symbols(args.elf)
        persist_base = syms.get("__tag_code_limit__", persist_base)
        flash_end = syms.get("__flash0_end__", flash_end)
        config_base = syms.get("__tag_config_start__", flash_end - 2 * FLASH_PAGE)
        nandmap_base = syms.get("__tag_nand_map_start__", flash_end - FLASH_PAGE)
        dest = os.path.join(outdir, os.path.basename(args.elf))
        try:
            shutil.copy2(args.elf, dest)
            artifacts.append({"file": os.path.basename(dest),
                              "sha256": sha256(dest),
                              "source": os.path.abspath(args.elf),
                              "symbols_used": {k: hex(v) for k, v in syms.items()}})
        except OSError as e:
            print(f"  warning: could not store the ELF: {e}", file=sys.stderr)

    for extra in args.extra:
        dest = os.path.join(outdir, os.path.basename(extra))
        try:
            shutil.copy2(extra, dest)
            artifacts.append({"file": os.path.basename(dest),
                              "sha256": sha256(dest),
                              "source": os.path.abspath(extra)})
        except OSError as e:
            print(f"  warning: could not store {extra}: {e}", file=sys.stderr)

    manifest: dict = {
        "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "reason": args.reason,
        "regions": {},
        "artifacts": [],
        "notes": [],
    }
    try:
        manifest["tree_git_hash"] = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], capture_output=True,
            text=True).stdout.strip()
    except OSError:
        pass

    manifest["artifacts"] = artifacts
    if not artifacts:
        manifest["notes"].append(
            "no firmware image stored: decoding this capture relies on "
            "guessing which build produced it")

    ok_all = True
    log_lines = []

    # 1. SRAM. Captured first: it is the most volatile and the most likely to
    #    be disturbed by anything else the tool does.
    sram_path = os.path.join(outdir, "sram.bin")
    ok, text = upload(SRAM_BASE, SRAM_SIZE, sram_path, args.verbose)
    log_lines.append(f"--- sram ---\n{text}")
    manifest["regions"]["sram"] = {
        "address": hex(SRAM_BASE), "size": SRAM_SIZE, "file": "sram.bin",
        "ok": ok,
        "contains": "SRAM1+SRAM2: .data/.bss, heap, stacks, monitor handoff, "
                    "scratchpad at 0x2003E000",
    }
    ok_all &= ok
    print(f"  sram            {'ok' if ok else 'FAILED'}")

    # 2. Writable internal flash, truncated at the first erased page.
    persist_size = config_base - persist_base
    raw_path = os.path.join(outdir, "persist.bin")
    ok, text = upload(persist_base, persist_size, raw_path, args.verbose)
    log_lines.append(f"--- persist ---\n{text}")
    kept = persist_size
    if ok and not args.keep_blank:
        data = open(raw_path, "rb").read()
        kept = first_erased_page(data)
        if kept < len(data):
            open(raw_path, "wb").write(data[:kept])
            manifest["notes"].append(
                f"persist truncated at the first erased page: {kept} of "
                f"{persist_size} bytes kept")
    manifest["regions"]["persist"] = {
        "address": hex(persist_base), "size_read": persist_size,
        "size_kept": kept, "file": "persist.bin", "ok": ok,
        "contains": "internal log headers written by the program, from "
                    "__tag_code_limit__ upward",
    }
    ok_all &= ok
    print(f"  persist         {'ok' if ok else 'FAILED'}  "
          f"({kept} of {persist_size} bytes)")

    # 3. Config and NAND map. Above the header sweep, so captured by address.
    for name, base, what in (
            ("config", config_base,
             "provisioned configuration (__tag_config_start__)"),
            ("nandmap", nandmap_base,
             "NAND logical-to-physical map and factory bad-block exclusions "
             "(__tag_nand_map_start__)")):
        path = os.path.join(outdir, f"{name}.bin")
        ok, text = upload(base, FLASH_PAGE, path, args.verbose)
        log_lines.append(f"--- {name} ---\n{text}")
        manifest["regions"][name] = {
            "address": hex(base), "size": FLASH_PAGE, "file": f"{name}.bin",
            "ok": ok, "contains": what,
        }
        ok_all &= ok
        print(f"  {name:<15} {'ok' if ok else 'FAILED'}")

    # 4. Backup registers, which are pState. The RTC/TAMP APB clock has to be
    #    turned on first or every register reads zero; see RCC_APB1ENR1 above.
    bkp_path = os.path.join(outdir, "backup_regs.bin")
    ok, text = upload(BKP_BASE, BKP_SIZE, bkp_path, args.verbose,
                      pre_write=(RCC_APB1ENR1, RCC_APB1ENR1_RTCAPBEN))
    log_lines.append(f"--- backup_regs ---\n{text}")
    manifest["regions"]["backup_regs"] = {
        "address": hex(BKP_BASE), "size": BKP_SIZE, "file": "backup_regs.bin",
        "ok": ok, "contains": "TAMP->BKP0R..BKP31R, the backing store for pState",
    }
    ok_all &= ok
    print(f"  backup_regs     {'ok' if ok else 'FAILED'}")

    with open(os.path.join(outdir, "programmer.log"), "w") as fh:
        fh.write("\n".join(log_lines))
    with open(os.path.join(outdir, "manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    print(f"\ncapture in {outdir}")
    if not ok_all:
        print("  one or more regions failed; see programmer.log",
              file=sys.stderr)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
