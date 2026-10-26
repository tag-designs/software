#!/usr/bin/env python3
"""Generate the notices for the Rust crates compiled into Tag Programmer.

Tag Programmer (``host/applications/tagprogram-rs``) is one executable with
every crate it depends on compiled in, and most of those licenses (MIT, BSD,
ISC, Zlib, Apache-2.0's NOTICE clause) require their notice to accompany a
binary distribution. This script asks cargo for the application's dependency
graph, follows the normal (shipped) dependencies -- not build scripts' or
tests' -- and writes one plain-text notices file: for each crate its version,
its declared license and repository, and the license files it carries, then
those files' texts, each distinct text once.

The output is committed (``LICENSES/host/tagprogram-THIRD-PARTY-NOTICES.txt``)
and installed into the packages. Regenerate it whenever the application's
``Cargo.lock`` changes::

    LICENSES/tools/generate_rust_notices.py host/applications/tagprogram-rs \\
        tagprogram > LICENSES/host/tagprogram-THIRD-PARTY-NOTICES.txt

Like the Qt notices it errs towards listing too much: dependencies for every
target platform are included, though a given package compiles only its own.
The project's own crates (path dependencies) are left out. libusb, which the
``libusb1-sys`` crate builds from source and links in, is LGPL-2.1 and has its
own notice in ``third-party/libusb.txt``.

Needs cargo and the crates downloaded (any ``cargo build`` or ``cargo fetch``
in the workspace does that). Exits with status 1 if cargo fails or the
package is not in the workspace.
"""

import hashlib
import json
import subprocess
import sys
from pathlib import Path

#: File-name prefixes of license and notice files in a crate's root.
LICENSE_PREFIXES = ("license", "licence", "copying", "notice", "unlicense", "copyright")


def metadata(workspace):
    """``cargo metadata`` for the workspace, resolved exactly as Cargo.lock says."""
    result = subprocess.run(
        ["cargo", "metadata", "--format-version", "1", "--locked"],
        cwd=workspace, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"cargo metadata failed:\n{result.stderr}")
    return json.loads(result.stdout)


def shipped_crates(meta, package):
    """Every registry crate reachable from ``package`` by normal dependencies."""
    packages = {p["id"]: p for p in meta["packages"]}
    nodes = {n["id"]: n for n in meta["resolve"]["nodes"]}
    roots = [p["id"] for p in meta["packages"] if p["name"] == package]
    if not roots:
        sys.exit(f"no package {package} in the workspace")
    seen, stack = set(), list(roots)
    while stack:
        pid = stack.pop()
        if pid in seen:
            continue
        seen.add(pid)
        for dep in nodes[pid]["deps"]:
            if any(kind["kind"] is None for kind in dep["dep_kinds"]):
                stack.append(dep["pkg"])
    crates = [packages[pid] for pid in seen if packages[pid]["source"]]
    return sorted(crates, key=lambda p: (p["name"], p["version"]))


def license_files(crate):
    """The crate's license and notice files, as (name, text) pairs."""
    root = Path(crate["manifest_path"]).parent
    files = []
    for path in sorted(root.iterdir()):
        if path.is_file() and path.name.lower().startswith(LICENSE_PREFIXES):
            files.append((path.name, path.read_text(errors="replace").rstrip()))
    return files


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: generate_rust_notices.py WORKSPACE PACKAGE")
    workspace, package = Path(sys.argv[1]), sys.argv[2]
    crates = shipped_crates(metadata(workspace), package)

    # Each distinct license text once, numbered; texts that differ only in
    # whitespace (the many copies of Apache-2.0) count as one.
    texts, index = [], {}
    entries = []
    for crate in crates:
        refs = []
        for name, text in license_files(crate):
            key = hashlib.sha256(" ".join(text.split()).encode()).hexdigest()
            if key not in index:
                texts.append(text)
                index[key] = len(texts)
            refs.append(f"{name} [{index[key]}]")
        entries.append((crate, refs))

    out = sys.stdout
    title = f"Third-party software compiled into {package}"
    out.write(f"{title}\n{'=' * len(title)}\n\n")
    out.write(
        f"{package} contains the following {len(crates)} Rust crates, each used under\n"
        "its own license. Generated from the application's Cargo.lock by\n"
        "LICENSES/tools/generate_rust_notices.py. Dependencies for every platform\n"
        "are listed, though each package compiles only its own. Where a crate offers\n"
        "a choice of licenses it is used under any of them that the recipient\n"
        "prefers. Each crate names its license files; their texts follow the list,\n"
        "numbered, each distinct text once.\n\n")
    for crate, refs in entries:
        repo = crate.get("repository") or "https://crates.io/crates/" + crate["name"]
        out.write(f"{crate['name']} {crate['version']}\n")
        out.write(f"  License:    {crate.get('license') or crate.get('license_file') or 'not declared'}\n")
        out.write(f"  Source:     {repo}\n")
        out.write(f"  Files:      {', '.join(refs) if refs else 'none; the declared license applies (texts/)'}\n")
    for number, text in enumerate(texts, 1):
        out.write("\n" + "-" * 78 + f"\n[{number}]\n\n")
        for line in text.splitlines():
            out.write(f"    {line}".rstrip() + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
