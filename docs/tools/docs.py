#!/usr/bin/env python3
"""Developer documentation catalogue: discovery, checking, index and nav.

Every developer document in the tree carries a small YAML front-matter block::

    ---
    type: design
    status: current
    summary: One line saying what the document is for.
    ---

This script is the single place that knows which files are developer
documentation and what kind each one is. It replaces the hand-kept lists that
used to drift apart (the CMake staging list, the MkDocs nav, and several index
pages). See docs/documentation-guide.md for the types and the rules.

Subcommands:

  list     print every developer document, one repository-relative path a line
  check    validate front matter and relative links, and that docs/index.md is
           current; exit 1 on any problem
  index    regenerate docs/index.md
  nav      write the generated nav into a staged mkdocs.yml (used by CMake)
  links    in the staged copy, point links to unpublished repository files
           (sources, scripts, JSON) at GitHub (used by CMake)

Standard library only: it runs from CMake before MkDocs, and on a bare checkout.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INDEX = "docs/index.md"
NAV_MARKER = "# @GENERATED_NAV@"

TYPES = {
    "readme": "What is here, how to build and use it",
    "design": "How it works now, and why",
    "decision": "A choice made, its alternatives and evidence (ADR)",
    "proposal": "Something planned but not built",
    "investigation": "A dated record of a fault chased or a question measured",
    "procedure": "How to do a repeatable task",
    "results": "Append-only measurements",
    "worklist": "Open work items",
    "index": "A generated or hand-kept list of other documents",
}

STATUSES = {
    "current",      # describes the tree as it is
    "proposed",     # proposal not yet accepted or built
    "accepted",     # decision record in force
    "superseded",   # replaced; front matter names the replacement
    "open",         # investigation still in progress
    "closed",       # investigation finished
    "historical",   # kept for the record; do not quote as current
}

# Paths (prefix match on the repository-relative path) that are not developer
# documentation. The user manual has its own MkDocs site; portal scaffolding
# is staged separately; vendored trees are not ours.
EXCLUDE_PREFIXES = (
    "ChibiOS/",
    "nanopb/",
    "embedded/thirdparty/",
    "host/docs/src/",
    "docs/developer/",
    # The per-side license indexes ship inside the packages; they are user
    # material, not developer documentation (LICENSES/README.md is).
    "LICENSES/host/",
    "LICENSES/embedded/",
)
EXCLUDE_NAMES = {"AGENTS.md", "CLAUDE.md"}

# Files that are not developer docs but whose links are still checked.
LINK_ONLY_NAMES = {"AGENTS.md"}

FRONT_RE = re.compile(r"\A---\n(.*?)\n---\n", re.S)
LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)|!\[[^\]]*\]\(([^)\s]+)\)")
H1_RE = re.compile(r"^#\s+(.+?)\s*#*\s*$", re.M)
FENCE_RE = re.compile(r"^(```|~~~).*?^\1", re.S | re.M)

# Navigation groups, in sidebar order. Each is (title, prefix); a document goes
# in the first group whose prefix matches. Sub-groups are built from the
# remaining directory components.
GROUPS = (
    ("Project", "docs/"),
    ("Project", "README.md"),
    ("Shared Contracts", "proto/"),
    ("Embedded", "embedded/"),
    ("Host", "host/"),
)


@dataclass
class Doc:
    path: str
    title: str
    meta: dict = field(default_factory=dict)

    @property
    def type(self) -> str:
        return self.meta.get("type", "")

    @property
    def status(self) -> str:
        return self.meta.get("status", "")

    @property
    def summary(self) -> str:
        return self.meta.get("summary", "")


def tracked_markdown() -> list[str]:
    """Repository-relative paths of tracked and untracked-but-not-ignored .md files."""
    out = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"],
        cwd=ROOT, check=True, capture_output=True, text=True).stdout
    return sorted({p for p in out.splitlines() if (ROOT / p).is_file()})


def is_archived(path: str) -> bool:
    return "archive" in Path(path).parts


def in_scope(path: str) -> bool:
    if is_archived(path) or Path(path).name in EXCLUDE_NAMES:
        return False
    return not path.startswith(EXCLUDE_PREFIXES)


def parse_front_matter(text: str) -> dict | None:
    m = FRONT_RE.match(text)
    if not m:
        return None
    meta = {}
    for line in m.group(1).splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        key, sep, value = line.partition(":")
        if not sep:
            continue
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        meta[key.strip()] = value
    return meta


def load(path: str) -> Doc:
    text = (ROOT / path).read_text(encoding="utf-8")
    meta = parse_front_matter(text) or {}
    body = FRONT_RE.sub("", text, count=1)
    m = H1_RE.search(FENCE_RE.sub("", body))
    title = meta.get("title") or (m.group(1).strip() if m else Path(path).stem)
    return Doc(path, title, meta)


def documents() -> list[Doc]:
    return [load(p) for p in tracked_markdown() if in_scope(p)]


# ---------------------------------------------------------------- check


def iter_links(text: str):
    body = FENCE_RE.sub("", text)
    body = re.sub(r"`[^`\n]*`", "", body)
    for m in LINK_RE.finditer(body):
        yield m.group(1) or m.group(2)


def check_links(path: str) -> list[str]:
    problems = []
    src = ROOT / path
    for target in iter_links(src.read_text(encoding="utf-8")):
        if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith("#"):
            continue
        rel = target.split("#", 1)[0].split("?", 1)[0]
        if not rel:
            continue
        resolved = (src.parent / rel).resolve()
        try:
            resolved.relative_to(ROOT)
        except ValueError:
            continue  # outside the repository, e.g. the sibling hardware tree
        if not resolved.exists():
            problems.append(f"{path}: broken link -> {target}")
    return problems


def cmd_check(_args) -> int:
    problems = []
    docs = documents()
    for d in docs:
        if not d.meta:
            problems.append(f"{d.path}: no front matter")
        elif d.type not in TYPES:
            problems.append(f"{d.path}: type '{d.type}' is not one of {sorted(TYPES)}")
        if d.meta and d.status not in STATUSES:
            problems.append(f"{d.path}: status '{d.status}' is not one of {sorted(STATUSES)}")
        if d.meta and not d.summary:
            problems.append(f"{d.path}: no summary")
        if d.status == "superseded" and not d.meta.get("superseded-by"):
            problems.append(f"{d.path}: superseded but no superseded-by")
        name = Path(d.path).name
        if name not in ("README.md", "BUILD_SOURCES.md", "TODO.md") and name != name.lower():
            problems.append(f"{d.path}: file name is not lowercase-hyphenated")
        if "_" in Path(d.path).stem and name not in ("BUILD_SOURCES.md",):
            problems.append(f"{d.path}: file name uses an underscore; use hyphens")
        problems.extend(check_links(d.path))
    for p in tracked_markdown():
        if Path(p).name in LINK_ONLY_NAMES and not is_archived(p):
            problems.extend(check_links(p))
    current = (ROOT / INDEX).read_text(encoding="utf-8") if (ROOT / INDEX).exists() else ""
    if current != render_index(docs):
        problems.append(f"{INDEX}: out of date; run docs/tools/docs.py index")
    for line in problems:
        print(line)
    print(f"{len(docs)} documents, {len(problems)} problems", file=sys.stderr)
    return 1 if problems else 0


# ---------------------------------------------------------------- index


def group_of(path: str) -> tuple[str, tuple[str, ...]]:
    """The nav group title and the sub-group path components for a document."""
    for title, prefix in GROUPS:
        if path == prefix or (prefix.endswith("/") and path.startswith(prefix)):
            parts = Path(path).parts
            if title == "Project":
                sub = parts[1:-1] if parts[0] == "docs" else ()
            elif title == "Shared Contracts":
                sub = ()
            else:
                sub = parts[1:-1]
            return title, tuple(s for s in sub if s != "design")
    return "Other", Path(path).parts[:-1]


SECTION_TITLES = {
    "architecture": "Architecture",
    "shared": "Shared Contracts",
    "build": "Build",
    "release": "Release",
    "bench": "Bench and Hardware Verification",
    "decisions": "Decision Records",
    "investigations": "Investigations",
    "proposals": "Proposals",
    "tags": "Tags",
    "common": "Common",
    "families": "Families",
    "boards": "Boards",
    "bases": "Bases",
    "loaders": "Loaders",
    "libraries": "Libraries",
    "applications": "Applications",
    "commandline": "Command Line",
    "docs": "User Guide Tooling",
    "tools": "Tools",
}


def section_title(component: str, parent: str = "") -> str:
    if parent == "tags" and component == "common":
        return "Common Firmware"
    return SECTION_TITLES.get(component, component)


def section_path(sub: tuple) -> list[str]:
    return [section_title(c, sub[i - 1] if i else "") for i, c in enumerate(sub)]


def sort_key(d: Doc):
    name = Path(d.path).name
    # README first in its directory, then by title; decisions by number.
    return (0 if name == "README.md" else 1, d.path if d.type == "decision" else d.title.lower())


def tree(docs: list[Doc]):
    """{group: {subpath tuple: [docs]}} preserving GROUPS order."""
    order = []
    for title, _ in GROUPS:
        if title not in order:
            order.append(title)
    result: dict[str, dict[tuple, list[Doc]]] = {t: {} for t in order}
    for d in docs:
        if d.path == INDEX:
            continue
        g, sub = group_of(d.path)
        result.setdefault(g, {}).setdefault(sub, []).append(d)
    for g in result:
        for sub in result[g]:
            result[g][sub].sort(key=sort_key)
    return result


def badge(d: Doc) -> str:
    bits = []
    if d.type not in ("readme", "design", "index"):
        bits.append(d.type)
    if d.status not in ("current", "accepted"):
        bits.append(d.status)
    return f" *({', '.join(bits)})*" if bits else ""


def render_index(docs: list[Doc]) -> str:
    lines = [
        "---",
        "type: index",
        "status: current",
        "summary: Every developer document in the tree, grouped by owner. Generated; do not edit.",
        "---",
        "",
        "# Developer Documentation Index",
        "",
        "<!-- Generated by docs/tools/docs.py index from each document's front matter. Do not edit. -->",
        "",
        "Every developer document in the repository, grouped by the code that owns it.",
        "The types and the rules for writing each are in the",
        "[documentation guide](documentation-guide.md). The end-user manual is separate,",
        "under [host/docs](../host/docs/README.md).",
        "",
        "A type or status in italics follows any entry that is not a current README or",
        "design document.",
        "",
    ]
    for group, subs in tree(docs).items():
        if not subs:
            continue
        lines.append(f"## {group}")
        lines.append("")
        for sub in sorted(subs, key=lambda s: (len(s) > 0, [t.lower() for t in section_path(s)])):
            if sub:
                lines.append(f"### {' / '.join(section_path(sub))}")
                lines.append("")
            for d in subs[sub]:
                rel = Path("..") / d.path if not d.path.startswith("docs/") else Path(d.path).relative_to("docs")
                lines.append(f"- [{d.title}]({rel.as_posix()}){badge(d)} — {d.summary}")
            lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def cmd_index(_args) -> int:
    (ROOT / INDEX).write_text(render_index(documents()), encoding="utf-8")
    print(f"wrote {INDEX}")
    return 0


# ---------------------------------------------------------------- list / nav


def linked_markdown(paths: list[str]) -> list[str]:
    """Markdown files outside the catalogue that a listed document links to.

    A developer document may link to a user-manual page or to AGENTS.md. Those
    are staged too, so the link works in the portal, but they stay out of the
    index and the sidebar. Followed transitively; archives are never staged.
    """
    staged = set(paths)
    extra: list[str] = []
    queue = list(paths)
    while queue:
        src = queue.pop()
        for target in iter_links((ROOT / src).read_text(encoding="utf-8")):
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith("#"):
                continue
            rel = target.split("#", 1)[0]
            if not rel.endswith(".md"):
                continue
            resolved = ((ROOT / src).parent / rel).resolve()
            try:
                path = resolved.relative_to(ROOT).as_posix()
            except ValueError:
                continue
            if path in staged or is_archived(path) or not resolved.is_file():
                continue
            staged.add(path)
            extra.append(path)
            queue.append(path)
    return sorted(extra)


def cmd_list(args) -> int:
    paths = [d.path for d in documents()]
    paths += linked_markdown(paths)
    text = "\n".join(paths) + "\n"
    if args.output:
        Path(args.output).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


def yaml_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_nav(docs: list[Doc], indent: str) -> list[str]:
    out = [f"{indent}- Index: reference/{INDEX}"]
    for group, subs in tree(docs).items():
        if not subs:
            continue
        out.append(f"{indent}- {yaml_str(group)}:")
        # Build a nested dict from sub-path tuples so the sidebar mirrors the tree.
        nested: dict = {}
        for sub, items in subs.items():
            node = nested
            for c in sub:
                node = node.setdefault(c, {})
            node.setdefault("", []).extend(items)

        def emit(node: dict, depth: int, parent: str):
            pad = indent + "    " * depth
            for d in node.get("", []):
                out.append(f"{pad}- {yaml_str(d.title + badge(d).replace('*', ''))}: reference/{d.path}")
            for key in sorted(k for k in node if k):
                out.append(f"{pad}- {yaml_str(section_title(key, parent))}:")
                emit(node[key], depth + 1, key)

        emit(nested, 1, "")
    return out


def cmd_nav(args) -> int:
    path = Path(args.mkdocs)
    lines = path.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        if line.strip() == NAV_MARKER:
            indent = line[: len(line) - len(line.lstrip())]
            lines[i:i + 1] = render_nav(documents(), indent)
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return 0
    print(f"{path}: no '{NAV_MARKER}' line", file=sys.stderr)
    return 1


GITHUB_BLOB = "https://github.com/tag-designs/software/blob/main/"
GITHUB_TREE = "https://github.com/tag-designs/software/tree/main/"
STAGED_ASSETS = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".csv")


def cmd_links(args) -> int:
    """Point staged links at files the portal does not publish to GitHub.

    The portal stages Markdown (and images) only, so a link from a design
    document to a source file, script or JSON config resolves in the
    repository but not in the browser. In the staged copy only, such links
    are rewritten to the file on the main branch; line anchors (#L59) carry
    over. The source documents are not changed.
    """
    staged_root = Path(args.staged)  # <staging>/src/reference
    staged = set(Path(args.list).read_text(encoding="utf-8").split())
    changed = 0
    for rel in staged:
        out = staged_root / rel
        if not out.is_file():
            continue
        text = out.read_text(encoding="utf-8")
        src_dir = (ROOT / rel).parent

        def fix(m):
            prefix, target = m.group(1), m.group(2)
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith("#"):
                return m.group(0)
            path, sep, frag = target.partition("#")
            resolved = (src_dir / path).resolve()
            try:
                repo_path = resolved.relative_to(ROOT).as_posix()
            except ValueError:
                return m.group(0)
            if not resolved.exists() or repo_path in staged or repo_path.lower().endswith(STAGED_ASSETS):
                return m.group(0)
            base = GITHUB_TREE if resolved.is_dir() else GITHUB_BLOB
            return prefix + base + repo_path + (sep + frag if sep else "")

        new = re.sub(r"(\]\()([^)\s]+)", fix, text)
        if new != text:
            out.write_text(new, encoding="utf-8")
            changed += 1
    print(f"rewrote source links in {changed} staged documents")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("--output")
    p.set_defaults(fn=cmd_list)
    sub.add_parser("check").set_defaults(fn=cmd_check)
    sub.add_parser("index").set_defaults(fn=cmd_index)
    p = sub.add_parser("links")
    p.add_argument("staged", help="<staging>/src/reference directory")
    p.add_argument("--list", required=True, help="output of `docs.py list`")
    p.set_defaults(fn=cmd_links)
    p = sub.add_parser("nav")
    p.add_argument("mkdocs", help="staged mkdocs.yml to rewrite in place")
    p.set_defaults(fn=cmd_nav)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
