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
import re
import subprocess
import sys
from pathlib import Path
from typing import Optional

TOOLS = Path(__file__).resolve().parent
SELECT_SCRIPT = TOOLS / "stm32_programmer_select.py"

#: Where the image is loaded. Matches the `-g 0x08000000` the CMake download
#: targets use; every tag in this tree boots from the start of internal flash.
LOAD_ADDRESS = "0x08000000"


#: STM32L4 FLASH_SR, holding PEMPTY in bit 17 (RM0394 2.6). The address is
#: family-specific, which is why this is only used for the device IDs below.
L4_FLASH_SR = 0x40022010
L4_FLASH_SR_PEMPTY = 1 << 17

#: Device IDs this tree's STM32L4 tags report. Every L4 tag here is an L432,
#: and the PEMPTY handling below writes a hardware register, so the check is an
#: allowlist rather than a family guess: on a part where 0x40022010 means
#: something else, a write there would be its own bug. Extend deliberately.
L4_DEVICE_IDS = {0x435}

#: Escape sequences STM32_Programmer_CLI colours its output with, which would
#: otherwise land in the middle of a value being parsed.
ANSI = re.compile(r"\x1b\[[0-9;]*m")


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


def calibration_keep_erase(image: Path) -> list[str]:
    """Build the erase arguments that clear every flash page below calibration.

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
    @return STM32_Programmer_CLI arguments, e.g. `["-e", "[0", "126]"]`.
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
                         "region; use --erase")
    if page_size != 2048:
        raise SystemExit(f"error: {image.name} is not an STM32L432 image; "
                         "a plain program already keeps its calibration")
    if symbols.get("__calibration_end__", start) == start:
        raise SystemExit(f"error: {image.name} stores no calibration; use --erase")
    first_kept = (start - int(LOAD_ADDRESS, 16)) // page_size
    return ["-e", "[0", f"{first_kept - 1}]"]


def _programmer_call(programmer: str, selector: str,
                     args: list[str]) -> tuple[int, str]:
    """Run STM32_Programmer_CLI through the probe selector and capture output.

    @param programmer Path to STM32_Programmer_CLI.
    @param selector   ST-LINK selector passed through to the wrapper.
    @param args       Arguments following `--`, excluding the connection.
    @return Exit status and the output with colour escapes removed.
    """
    command = [sys.executable, str(SELECT_SCRIPT), "--programmer", programmer,
               "--selector", selector, "--", "-c", "port=SWD", "mode=UR"] + args
    done = subprocess.run(command, capture_output=True, text=True)
    return done.returncode, ANSI.sub("", done.stdout + done.stderr)


def _read32(text: str, address: int) -> Optional[int]:
    """Extract one 32-bit value from a `-r32` dump.

    @param text    Programmer output, colour escapes already removed.
    @param address Address whose value to return.
    @return The value, or None when the dump does not contain it.
    """
    found = re.search(rf"0x0*{address:X}\s*:\s*([0-9A-Fa-f]{{1,8}})", text,
                      re.IGNORECASE)
    return int(found.group(1), 16) if found else None


def clear_pempty(programmer: str, selector: str) -> str:
    """Clear FLASH_SR.PEMPTY after programming, when the part has it set.

    @details A mass erase sets PEMPTY, and with `nSWBOOT0 = 1` and BOOT0 low a
             set PEMPTY sends every reset to the ROM bootloader: the firmware
             just written does not run, and no monitor answers. Hardware
             re-evaluates the flag only at power-on or an option-byte load, so
             without this the tag needs a physical power cycle before it will
             run -- which is the kind of step that gets forgotten, and whose
             symptom (`tag-info` reporting the bootloader) looks like a failed
             flash rather than a missing power cycle.

             Writing 1 to the bit **toggles** it, so this reads first and
             writes only when the bit is set. An unconditional write would set
             PEMPTY on a healthy part and create the fault it exists to fix.

    @param programmer Path to STM32_Programmer_CLI.
    @param selector   ST-LINK selector.
    @return One line describing what happened, for the operator and the record.
    """
    status, text = _programmer_call(programmer, selector,
                                    ["-r32", hex(L4_FLASH_SR), "1"])
    if status != 0:
        return "not checked: could not read FLASH_SR"

    device = re.search(r"Device ID\s*:\s*(0x[0-9A-Fa-f]+)", text)
    if not device:
        return "not checked: no device ID reported"
    if int(device.group(1), 16) not in L4_DEVICE_IDS:
        return f"skipped: {device.group(1)} is not an STM32L4 in the allowlist"

    before = _read32(text, L4_FLASH_SR)
    if before is None:
        return "not checked: FLASH_SR not found in the read"
    if not before & L4_FLASH_SR_PEMPTY:
        return f"already clear (FLASH_SR=0x{before:x})"

    status, _ = _programmer_call(
        programmer, selector,
        ["-w32", hex(L4_FLASH_SR), hex(L4_FLASH_SR_PEMPTY)])
    if status != 0:
        return f"FAILED to write FLASH_SR (was 0x{before:x})"

    status, text = _programmer_call(programmer, selector,
                                    ["-r32", hex(L4_FLASH_SR), "1"])
    after = _read32(text, L4_FLASH_SR) if status == 0 else None
    if after is None:
        return f"written, but could not confirm (was 0x{before:x})"
    if after & L4_FLASH_SR_PEMPTY:
        return f"STILL SET after the write (0x{after:x}) -- power-cycle the tag"
    return f"cleared (0x{before:x} -> 0x{after:x}); no power cycle needed"


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
    pempty: Optional[str] = None,
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
    parser.add_argument("--programmer", help="STM32_Programmer_CLI path")
    parser.add_argument(
        "--selector",
        default="pid:0483:3748",
        help="ST-LINK selector passed through: pid:0483:3748, serial:<sn>, index:<n>, prompt, auto.",
    )
    parser.add_argument(
        "--erase",
        action="store_true",
        help="Mass erase before programming, in the same programmer "
        "invocation. Use for a major upgrade that moves or reformats the "
        "NOLOAD regions -- calibration, stored configuration, the NAND map -- "
        "which a normal program leaves in place and the new image may then "
        "read at the wrong address or in the wrong format. Destroys those "
        "regions: the tag must be reprovisioned afterwards.",
    )
    parser.add_argument(
        "--keep-calibration",
        action="store_true",
        help="Erase every flash page below the calibration table, in the same "
        "programmer invocation, and keep the table. The normal upgrade for a "
        "calibrated STM32L432 tag (CompassTag): its persistent region moves "
        "with code size and must be erased, while calibration is pinned to the "
        "top page. Stored configuration and logs are destroyed; calibration "
        "survives only if the new image uses the same calibration format.",
    )
    parser.add_argument(
        "--no-pempty-fix",
        action="store_true",
        help="Do not clear FLASH_SR.PEMPTY after programming. The check reads "
        "before it writes and is a no-op on a healthy part, so the only "
        "reason to pass this is to reproduce the bootloader condition.",
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

    programmer = find_programmer(args.programmer)
    # Erase and program in one invocation when asked. Two invocations leave a
    # reset between them with the flash empty, which is what latches
    # FLASH_SR.PEMPTY and sends the part to the ROM bootloader; one invocation
    # never exposes that window. The PEMPTY check below stays as a net.
    erase = ["-e", "all"] if args.erase else []
    if args.keep_calibration:
        erase = calibration_keep_erase(image)
    command = [
        sys.executable,
        str(SELECT_SCRIPT),
        "--programmer",
        programmer,
        "--selector",
        args.selector,
        "--",
        *erase,
        "-d",
        str(image),
        "-g",
        LOAD_ADDRESS,
    ]
    if args.erase:
        print("\nMass erase requested: NOLOAD regions are destroyed with the\n"
              "image, so a provisioned tag needs reprovisioning afterwards --\n"
              "calibration, and anything else held outside the loaded image.")
    if args.keep_calibration:
        print(f"\nErasing flash pages {erase[1][1:]}-{erase[2][:-1]} and keeping "
              "the calibration page:\nstored configuration and logs are "
              "destroyed, so reconfigure the tag afterwards.")
    print(f"\nProgramming with {programmer}")
    status = subprocess.call(command)

    # A mass erase leaves PEMPTY set, and the tag then boots the ROM bootloader
    # instead of the image just written. Doing this here rather than leaving it
    # to the operator is the point: it is conditional on a read, and a step that
    # must be remembered is a step that will be missed.
    pempty = None
    if status == 0 and not args.no_pempty_fix:
        pempty = clear_pempty(programmer, args.selector)
        print(f"  FLASH_SR.PEMPTY  {pempty}")

    # Recorded either way, with `programmed` saying which. A failed flash that
    # left no row would be indistinguishable from one that never happened, and
    # the tag in hand is not running what the operator thinks it is.
    if args.json:
        record(args.json, args.label, image, manifest_data, status == 0,
               status, pempty)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
