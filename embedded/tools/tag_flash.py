"""Locate tag-flash, the Rust tool that programs and reads a tag through a base.

@details tag-flash (host/commandline-rs) replaces STM32_Programmer_CLI for the
         bench scripts. A bench machine may have it from the host tools
         package, on PATH, or from a cargo build of this repository; this finds
         it in either place without a configured build tree.
"""
from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Optional

#: The repository's cargo target directory for host/commandline-rs.
_REPO_TARGET = Path(__file__).resolve().parents[2] / "host" / "commandline-rs" / "target"


def find_tag_flash(explicit: Optional[str] = None) -> Optional[str]:
    """Return the tag-flash executable to run, or None when there is none.

    @details Looks, in order, at @p explicit, the `TAG_FLASH` environment
             variable, PATH, and this repository's
             `host/commandline-rs/target/release`.

    @param explicit A path given on the command line, or None.
    @return The path, or None when tag-flash was not found.
    @throws SystemExit if @p explicit or `TAG_FLASH` names a missing file,
            since a path the operator gave must not be silently replaced.
    """
    for given, origin in ((explicit, "--tag-flash"),
                          (os.environ.get("TAG_FLASH"), "TAG_FLASH")):
        if given:
            if not Path(given).is_file():
                raise SystemExit(f"error: {origin} names {given}, which does not exist")
            return given

    found = shutil.which("tag-flash")
    if found:
        return found

    for name in ("tag-flash", "tag-flash.exe"):
        candidate = _REPO_TARGET / "release" / name
        if candidate.is_file():
            return str(candidate)
    return None
