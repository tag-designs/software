#!/usr/bin/env python3
"""Generate the notices for third-party code bundled inside the shipped Qt.

The host packages deploy Qt's own libraries, and those libraries contain
third-party code (zlib, libpng, FreeType, HarfBuzz, PCRE2, ...) whose licenses
require their notices to accompany a binary distribution. Qt records each such
component in a ``qt_attribution.json`` file beside its source. This script
collects those records for the Qt modules the packages ship and writes one
plain-text notices file, with each component's copyright line and full license
text.

The output is committed (``LICENSES/host/Qt-THIRD-PARTY-NOTICES.txt``) and
installed into the packages. Regenerate it when the Qt version changes or a
newly deployed Qt module is added to ``SHIPPED_MODULES``::

    LICENSES/tools/generate_qt_notices.py ~/qt/6.10.3/Src \\
        > LICENSES/host/Qt-THIRD-PARTY-NOTICES.txt

It deliberately errs towards listing too much: every attribution in a shipped
module is included, even for code a given platform does not compile (for
example the Windows-only or Android-only components of qtbase). Attributions
under ``tests/``, ``examples/``, ``doc/``, ``cmake/`` and ``coin/`` are
skipped; they are never part of a deployed library.

Exits with status 1 if the source tree is missing or a shipped module has no
directory in it.
"""

import json
import sys
from pathlib import Path

#: Qt modules whose libraries or plugins are deployed into a host package by
#: any of the release builds (CI or local). Keep in step with what
#: macdeployqt/windeployqt actually copies; listing a module that is not
#: shipped only over-reports.
SHIPPED_MODULES = [
    "qtbase",
    "qtdeclarative",
    "qtsvg",
    "qtimageformats",
    "qt3d",
    "qtquick3d",
    "qtshadertools",
    "qtquicktimeline",
    "qtmultimedia",
    "qtvirtualkeyboard",
    "qtquick3dphysics",
]

#: Path components that mark an attribution as outside any shipped library:
#: tests, examples, documentation, and CMake build helpers.
SKIPPED_DIRS = {"tests", "examples", "doc", "cmake", "coin"}


def attributions(json_path):
    """Yield the attribution records in one qt_attribution.json file.

    @param json_path Path of the file; it holds one record or a list of them.
    @return An iterator of dicts.
    """
    # strict=False: some Qt attribution files carry raw tabs in strings.
    data = json.loads(json_path.read_text(encoding="utf-8"), strict=False)
    if isinstance(data, dict):
        data = [data]
    for record in data:
        yield record


def license_texts(json_path, record):
    """Read the license file or files a record names.

    @param json_path The qt_attribution.json the record came from; license
                     paths are relative to its directory.
    @param record    The attribution record.
    @return A list of (relative path, text) pairs; a missing file is reported
            in its text rather than dropped silently.
    """
    names = record.get("LicenseFiles") or [record.get("LicenseFile")]
    texts = []
    for name in names:
        if not name:
            continue
        path = json_path.parent / name
        if path.is_file():
            texts.append((name, path.read_text(encoding="utf-8", errors="replace").strip()))
        else:
            texts.append((name, f"[license file {name} not found in the Qt source]"))
    return texts


def main(argv):
    """Write the notices for every shipped module to standard output.

    @param argv Command line; argv[1] is the Qt source directory
                (the directory holding qtbase, qtdeclarative, ...).
    @return Process exit status.
    """
    if len(argv) != 2:
        print(f"usage: {argv[0]} QT_SOURCE_DIR", file=sys.stderr)
        return 2
    src = Path(argv[1]).expanduser()
    missing = [m for m in SHIPPED_MODULES if not (src / m).is_dir()]
    if missing:
        print(f"{src}: no source for {', '.join(missing)}", file=sys.stderr)
        return 1

    out = sys.stdout
    out.write("Third-party software included in the Qt libraries\n")
    out.write("==================================================\n\n")
    out.write(
        "The Qt libraries deployed with these tools contain the following\n"
        "third-party components. Generated from the qt_attribution.json files\n"
        f"of the Qt modules: {', '.join(SHIPPED_MODULES)}.\n"
        "This list may include components a given platform does not compile.\n\n")

    seen = set()
    for module in SHIPPED_MODULES:
        for json_path in sorted((src / module).rglob("qt_attribution.json")):
            rel = json_path.relative_to(src)
            if SKIPPED_DIRS.intersection(rel.parts):
                continue
            for record in attributions(json_path):
                key = (record.get("Name"), record.get("Version"))
                if key in seen:
                    continue
                seen.add(key)
                out.write("-" * 78 + "\n")
                out.write(f"{record.get('Name', record.get('Id', '?'))}")
                if record.get("Version"):
                    out.write(f" {record['Version']}")
                out.write(f"\n  Qt module: {module}\n")
                out.write(f"  License:   {record.get('LicenseId', record.get('License', '?'))}\n")
                copyright_text = record.get("Copyright", "")
                if isinstance(copyright_text, list):
                    copyright_text = "\n".join(copyright_text)
                if copyright_text:
                    out.write("  Copyright:\n")
                    for line in copyright_text.strip().splitlines():
                        out.write(f"    {line}\n")
                for name, text in license_texts(json_path, record):
                    out.write(f"\n  License text ({name}):\n\n")
                    for line in text.splitlines():
                        out.write(f"    {line}".rstrip() + "\n")
                out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
