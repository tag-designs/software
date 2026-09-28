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
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

TOOLS = Path(__file__).resolve().parent
SELECT_SCRIPT = TOOLS / "stm32_programmer_select.py"

#: Where the image is loaded. Matches the `-g 0x08000000` the CMake download
#: targets use; every tag in this tree boots from the start of internal flash.
LOAD_ADDRESS = "0x08000000"


def find_programmer(explicit: Optional[str]) -> str:
    """Locate STM32_Programmer_CLI without a configured build tree."""
    if explicit:
        if not Path(explicit).exists():
            raise SystemExit(f"error: no STM32_Programmer_CLI at {explicit}")
        return explicit

    env = os.environ.get("STM32_PRG_PATH")
    if env:
        candidate = Path(env) / "STM32_Programmer_CLI"
        if candidate.exists():
            return str(candidate)

    found = shutil.which("STM32_Programmer_CLI")
    if found:
        return found

    system = platform.system()
    if system == "Darwin":
        roots = [
            Path.home() / "Software/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin",
            Path("/Applications/STMicroelectronics/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin"),
        ]
    elif system == "Windows":
        roots = [Path("C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin")]
    else:
        roots = [
            Path.home() / "Software/STM32CubeProgrammer/bin",
            Path("/opt/st/stm32cubeprog/bin"),
        ]

    for root in roots:
        for name in ("STM32_Programmer_CLI", "STM32_Programmer_CLI.exe"):
            candidate = root / name
            if candidate.exists():
                return str(candidate)

    raise SystemExit(
        "error: STM32_Programmer_CLI was not found. Pass --programmer, or set "
        "STM32_PRG_PATH to the directory containing it."
    )


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
) -> None:
    """Append one JSON object describing what was programmed.

    @details A path rather than stdout, because STM32_Programmer_CLI writes to
             stdout and a redirect would capture its progress bar along with the
             record.

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
    parser.add_argument("--programmer", help="STM32_Programmer_CLI path")
    parser.add_argument(
        "--selector",
        default="pid:0483:3748",
        help="ST-LINK selector passed through: pid:0483:3748, serial:<sn>, index:<n>, prompt, auto.",
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
        "`tag-info --json`. A file rather than stdout, because the programmer "
        "writes there too.",
    )
    args = parser.parse_args(argv)

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

    programmer = find_programmer(args.programmer)
    command = [
        sys.executable,
        str(SELECT_SCRIPT),
        "--programmer",
        programmer,
        "--selector",
        args.selector,
        "--",
        "-d",
        str(image),
        "-g",
        LOAD_ADDRESS,
    ]
    print(f"\nProgramming with {programmer}")
    status = subprocess.call(command)

    # Recorded either way, with `programmed` saying which. A failed flash that
    # left no row would be indistinguishable from one that never happened, and
    # the tag in hand is not running what the operator thinks it is.
    if args.json:
        record(args.json, args.label, image, manifest_data, status == 0, status)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
