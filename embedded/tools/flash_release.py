#!/usr/bin/env python3
"""Program a tag from a released firmware artifact, after verifying it.

@details `<Tag>-download` programs whatever is in a build tree, which is right
         for development and wrong for the field: the image that flies is then
         never the image that was archived, and nothing afterwards can say which
         bytes went onto the tag. This flashes a released artifact instead, and
         refuses if its bytes do not match the SHA-256 its build manifest
         records.

         A release directory is self-describing -- `BitTag.elf` sits beside
         `BitTag-build-manifest.json` -- so this needs no build tree, no CMake
         configure and no toolchain. Unzip a release on the machine with the
         ST-LINK attached and run it there.

         It programs with tag-flash (host/commandline-rs), found on PATH, in
         `TAG_FLASH`, or built in this repository. A Rust toolchain is assumed;
         there is no STM32_Programmer_CLI fallback.

         By default tag-flash erases by the image's flash plan: on an
         STM32L432 every page below the calibration page (or the whole part
         when there is none), because the persistent region and the stored
         configuration in it start at the first page after the code and move
         whenever the image crosses a page boundary; on an STM32U375 only the
         image's pages, since every region it saves is pinned.

@warning A matching hash says the image is the one that was archived. It does
         not say the image was ever qualified: CI builds candidates, and only a
         bench power measurement produces a release. This tool checks identity,
         not fitness to fly.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Optional

from tag_flash import find_tag_flash

TOOLS = Path(__file__).resolve().parent

#: Start of internal flash on both supported MCUs; flash page 0 is here.
FLASH_BASE = 0x08000000


def elf_symbols(image: Path, wanted: tuple[str, ...]) -> dict[str, int]:
    """Read linker-defined symbols from a 32-bit little-endian ELF image.

    @details Parsed directly rather than through `arm-none-eabi-nm`, because
             this tool runs on a bench machine with no toolchain.

    @param image  Firmware ELF.
    @param wanted Symbol names to return.
    @return Name to value for each wanted symbol present; others are absent.
    @throws SystemExit if the file is not a 32-bit little-endian ELF.
    """
    import struct

    data = image.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit(f"error: {image} is not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
                for i in range(shnum)]
    out: dict[str, int] = {}
    for _, sh_type, _, _, offset, size, link, _, _, entsize in sections:
        if sh_type != 2:  # SHT_SYMTAB
            continue
        strtab = sections[link][4]
        for pos in range(offset, offset + size, entsize):
            name_off, value = struct.unpack_from("<II", data, pos)
            end = data.index(b"\0", strtab + name_off)
            name = data[strtab + name_off:end].decode("ascii", "replace")
            if name in wanted:
                out[name] = value
    return out


def calibration_keep_erase(image: Path) -> tuple[int, int]:
    """Check @p image keeps a calibration page, and name the pages erased below it.

    @details STM32L432xC.ld pins a family's calibration table to the top flash
             pages, while the persistent region below it still starts wherever
             the code ends. An upgrade must therefore erase that region but can
             keep calibration, and erasing pages 0 through the one below
             `__calibration_start__` does exactly that.

             Restricted to 2 KB-page (STM32L432) images. The U375 layout pins
             the persistent region as well, so a plain program already keeps
             calibration and stored configuration there and needs no erase.

    @param image Firmware ELF being programmed; the page is read from it, so it
                 must be the new image, whose layout governs from now on.
    @return First and last flash page erased, e.g. `(0, 126)`; the same span
            tag-flash's `--erase plan` erases for this image.
    @throws SystemExit if the image reserves no calibration pages or is not an
            L432 image.
    """
    symbols = elf_symbols(image, ("__calibration_start__",
                                  "__calibration_end__",
                                  "__tag_flash_page_size__"))
    start = symbols.get("__calibration_start__")
    page_size = symbols.get("__tag_flash_page_size__")
    if start is None or page_size is None:
        raise SystemExit(f"error: {image.name} has no pinned calibration "
                         "region; leave out --keep-calibration")
    if page_size != 2048:
        raise SystemExit(f"error: {image.name} is not an STM32L432 image; "
                         "the default erase already keeps its calibration")
    if symbols.get("__calibration_end__", start) == start:
        raise SystemExit(f"error: {image.name} stores no calibration; "
                         "leave out --keep-calibration")
    first_kept = (start - FLASH_BASE) // page_size
    return 0, first_kept - 1


def tag_flash_command(tag_flash: str, base: Optional[str], image: Path,
                      erase_all: bool, keep_calibration: bool) -> list[str]:
    """Build the tag-flash command that programs @p image.

    @details tag-flash erases, programs, verifies and starts the image in one
             session, so there is no window with the flash empty between an
             erase and the program, and it clears FLASH_SR.PEMPTY on an
             STM32L4 itself before starting the image. Without a flag the erase
             is `--erase plan`, Tag Programmer's flash plan for the image;
             `--keep-calibration` asks for the same plan, after the caller has
             checked with calibration_keep_erase() that the image keeps a
             calibration page, so a mistaken flag cannot mass-erase one that
             does not.

    @param tag_flash        Path to tag-flash.
    @param base             USB serial of the base, or None for the only one.
    @param image            Firmware ELF.
    @param erase_all        Mass erase first (`--erase`).
    @param keep_calibration Erase below calibration (`--keep-calibration`);
                            the plan, as when neither flag is given.
    @return The command line.
    """
    erase = "all" if erase_all else "plan"
    command = [tag_flash]
    if base:
        command += ["--base", base]
    return command + ["program", str(image), "--erase", erase]


def resolve_release(target: Path) -> tuple[Path, Path]:
    """Return (image, manifest) for a release directory or an image file."""
    if target.is_dir():
        manifests = sorted(target.glob("*-build-manifest.json"))
        if not manifests:
            # Pointing at the unzipped release root rather than one tag inside
            # it is the obvious mistake; name what is there instead of saying
            # only that nothing is here.
            nested = sorted(
                child.name
                for child in target.iterdir()
                if child.is_dir() and any(child.glob("*-build-manifest.json"))
            )
            if nested:
                listing = "\n".join(f"  {target / name}" for name in nested)
                raise SystemExit(
                    f"error: {target} holds several tags. Point at one of:\n{listing}"
                )
            raise SystemExit(
                f"error: no build manifest in {target}. Expected a release "
                f"directory containing <Tag>.elf and <Tag>-build-manifest.json."
            )
        if len(manifests) > 1:
            names = ", ".join(m.name for m in manifests)
            raise SystemExit(
                f"error: {target} holds several manifests ({names}); point at "
                f"one tag's directory."
            )
        manifest = manifests[0]
        name = manifest.name[: -len("-build-manifest.json")]
        image = target / f"{name}.elf"
        if not image.exists():
            raise SystemExit(f"error: {manifest.name} is present but {image.name} is not.")
        return image, manifest

    if not target.exists():
        raise SystemExit(f"error: {target} does not exist")

    # An image was named directly; its manifest must be beside it, because the
    # manifest is the only thing that makes the image identifiable.
    stem = target.name.rsplit(".", 1)[0]
    manifest = target.parent / f"{stem}-build-manifest.json"
    if not manifest.exists():
        raise SystemExit(
            f"error: no {manifest.name} beside {target.name}. Flashing an image "
            f"whose provenance cannot be checked is what this tool exists to "
            f"prevent; use the CMake <Tag>-download target for a build tree."
        )
    return target, manifest


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(image: Path, manifest_data: dict) -> None:
    artifacts = manifest_data.get("artifacts") or {}
    recorded = artifacts.get(image.name)
    if recorded is None:
        known = ", ".join(sorted(k for k, v in artifacts.items() if v)) or "none"
        raise SystemExit(
            f"error: the manifest records no hash for {image.name} "
            f"(it records: {known})."
        )

    actual = sha256(image)
    if actual != recorded["sha256"]:
        raise SystemExit(
            f"error: {image.name} does not match its manifest.\n"
            f"  recorded: {recorded['sha256']}\n"
            f"  actual:   {actual}\n"
            f"This image is not the one that was archived. Do not flash it."
        )

    size = image.stat().st_size
    if size != recorded["bytes"]:
        raise SystemExit(
            f"error: {image.name} is {size} bytes, manifest says {recorded['bytes']}."
        )


def describe(manifest_data: dict, image: Path) -> str:
    src = manifest_data.get("source") or {}
    sub = (manifest_data.get("submodules") or {}).get("ChibiOS") or {}
    tools = manifest_data.get("tools") or {}
    build = manifest_data.get("build") or {}
    gcc = tools.get("arm_gcc") or {}
    recorded = (manifest_data.get("artifacts") or {}).get(image.name) or {}

    lines = [
        f"  target       {manifest_data.get('target', '?')}",
        f"  image        {image.name}  ({recorded.get('bytes', '?')} bytes)",
        f"  sha256       {recorded.get('sha256', '?')}",
        f"  commit       {src.get('describe') or src.get('commit', '?')}",
        f"  tree         {'DIRTY' if src.get('dirty') else 'clean'}",
        f"  ChibiOS      {sub.get('describe') or sub.get('commit', '?')}"
        + (f" on {sub['tracks_branch']}" if sub.get("tracks_branch") else ""),
        f"  toolchain    {gcc.get('version') or '?'}",
        f"  built        {manifest_data.get('built_at', '?')}",
        f"  strict mode  {build.get('reproducible_mode')}",
    ]
    return "\n".join(lines)


def record(
    path: Path,
    label: Optional[str],
    image: Path,
    manifest_data: dict,
    programmed: bool,
    exit_code: Optional[int],
    pempty: Optional[str] = None,
) -> None:
    """Append one JSON object describing what was programmed.

    @details A path rather than stdout, because tag-flash writes to stdout and
             a redirect would capture its output along with the record.

             This exists for one field. `tag-info --json` reports everything a
             tag knows about itself -- its UUID, the commit it was built from,
             when that build was compiled -- but no tag can report the SHA-256
             of its own image, because an image cannot contain its own hash.
             That number is available only here, at the moment of programming,
             and is lost if it is not written down now.
    """
    src = manifest_data.get("source") or {}
    sub = (manifest_data.get("submodules") or {}).get("ChibiOS") or {}
    tools = manifest_data.get("tools") or {}
    recorded = (manifest_data.get("artifacts") or {}).get(image.name) or {}

    entry = {
        "label": label,
        "flashed_at": datetime.datetime.now(datetime.timezone.utc)
        .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "programmed": programmed,
        "target": manifest_data.get("target"),
        "image": image.name,
        "sha256": recorded.get("sha256"),
        "bytes": recorded.get("bytes"),
        "commit": src.get("commit"),
        "describe": src.get("describe"),
        "dirty": src.get("dirty"),
        "built_at": manifest_data.get("built_at"),
        "chibios": sub.get("commit"),
        "chibios_describe": sub.get("describe"),
        "arm_gcc": (tools.get("arm_gcc") or {}).get("version"),
        "nanopb_runtime": tools.get("nanopb_runtime"),
    }
    if exit_code is not None:
        entry["exit_code"] = exit_code
    if pempty is not None:
        entry["pempty"] = pempty

    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry, separators=(",", ":")) + "\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Verify a released firmware image against its build manifest, "
        "then program it."
    )
    parser.add_argument(
        "release",
        type=Path,
        help="A release directory (containing <Tag>.elf and its manifest), or the image itself.",
    )
    parser.add_argument(
        "--tag-flash",
        help="tag-flash path; otherwise TAG_FLASH, PATH, then this "
        "repository's host/target/release.",
    )
    parser.add_argument(
        "--base",
        help="USB serial of the base, as `tag-flash list` prints it; needed "
        "only when more than one base is connected.",
    )
    parser.add_argument(
        "--erase",
        action="store_true",
        help="Mass erase before programming, in the same tag-flash session. "
        "Use for an upgrade that changes the calibration format, or on an "
        "STM32U375 one that moves or reformats its pinned regions -- "
        "calibration, stored configuration, the NAND map -- which the default "
        "erase keeps. Destroys those regions: the tag must be reprovisioned "
        "afterwards.",
    )
    parser.add_argument(
        "--keep-calibration",
        action="store_true",
        help="The default erase, refused unless the image keeps a calibration "
        "page: every flash page below the calibration table is erased and the "
        "table kept. For a calibrated STM32L432 tag (CompassTag), whose "
        "persistent region moves with code size. Stored configuration is "
        "destroyed; calibration survives only if the new image uses the same "
        "calibration format.",
    )
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="Check the image against its manifest and report, without programming.",
    )
    parser.add_argument(
        "--label",
        help="The board's physical label, recorded with the flash. Boards are "
        "labelled because nothing in the image or the programmer identifies "
        "which board is attached.",
    )
    parser.add_argument(
        "--json",
        type=Path,
        metavar="FILE",
        help="Append one JSON object per flash to FILE, to join against "
        "`tag-info --json`. A file rather than stdout, because tag-flash "
        "writes there too.",
    )
    args = parser.parse_args(argv)
    if args.erase and args.keep_calibration:
        parser.error("--erase and --keep-calibration are exclusive")

    image, manifest = resolve_release(args.release)
    try:
        manifest_data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"error: could not read {manifest}: {exc}")

    verify(image, manifest_data)

    print(f"Verified {image.name} against {manifest.name}:")
    if args.label:
        print(f"  label        {args.label}")
    print(describe(manifest_data, image))

    if (manifest_data.get("source") or {}).get("dirty"):
        print(
            "\nwarning: this image was built from a tree with uncommitted "
            "changes, so its commit does not describe its sources.",
            file=sys.stderr,
        )

    if args.verify_only:
        if args.json:
            record(args.json, args.label, image, manifest_data, False, None)
        return 0

    tag_flash = find_tag_flash(args.tag_flash)
    if tag_flash is None:
        raise SystemExit(
            "error: tag-flash was not found. Pass --tag-flash, set TAG_FLASH, "
            "or build it: cargo build --release in host/commandline-rs."
        )
    if args.keep_calibration:
        # Run for its checks: it refuses an image that is not a calibrated
        # STM32L432 one, for which the plan would mean something else.
        first, last = calibration_keep_erase(image)
    command = tag_flash_command(tag_flash, args.base, image, args.erase,
                                args.keep_calibration)
    if args.erase:
        print("\nMass erase requested: NOLOAD regions are destroyed with the\n"
              "image, so a provisioned tag needs reprovisioning afterwards --\n"
              "calibration, and anything else held outside the loaded image.")
    if args.keep_calibration:
        print(f"\nErasing flash pages {first}-{last} and keeping the calibration "
              "page:\nstored configuration and logs are destroyed, so "
              "reconfigure the tag afterwards.")
    # Flushed so that, piped to a log, these lines precede tag-flash's.
    print(f"\nProgramming with {tag_flash}", flush=True)
    status = subprocess.call(command)

    # tag-flash clears FLASH_SR.PEMPTY itself, read first, on an STM32L4; a set
    # PEMPTY after a mass erase would otherwise send the tag to its ROM
    # bootloader instead of the image just written.
    pempty = "cleared by tag-flash when set" if status == 0 else None

    # Recorded either way, with `programmed` saying which. A failed flash that
    # left no row would be indistinguishable from one that never happened, and
    # the tag in hand is not running what the operator thinks it is.
    if args.json:
        record(args.json, args.label, image, manifest_data, status == 0,
               status, pempty)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
