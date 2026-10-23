#!/usr/bin/env python3
"""Compare two sets of configuration-probe results.

    compare.py <baseline-dir> <new-dir> [--ignore KEY ...] [--max N]

Matches results by case file and case id. For each case it compares:

  get_config_ok, restore, restore_detail   exactly
  sent                                     byte for byte (sent_hex); a
                                           difference is shown field by field
  sent_with_schedule_tab                   present in either, or different
  ui, ui_with_schedule_tab                 line by line
  layout                                   line by line
  dialogs                                  exactly

Prints every difference, then a count per key. Exits 0 when nothing differs
outside the --ignore'd keys, 1 otherwise. Every difference a change is allowed
to make belongs in expected-differences.md, with its reason. See README.md.
"""

import argparse
import difflib
import json
import sys
from pathlib import Path

KEYS = ["error", "get_config_ok", "restore", "restore_detail", "sent",
        "sent_with_schedule_tab", "ui", "ui_with_schedule_tab", "layout",
        "dialogs"]


def load(directory):
    """{file name: {case id: result}} for every result file in directory."""
    out = {}
    for path in sorted(Path(directory).glob("*.json")):
        doc = json.loads(path.read_text())
        if doc.get("schema") != "qtmon-config-probe-results/1":
            sys.exit(f"{path} is not a qtmon-config-probe-results/1 file")
        out[path.name] = {r["id"]: r for r in doc["results"]}
    return out


def leaves(value, prefix=""):
    """{dotted path: leaf value} for a JSON value."""
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            out.update(leaves(v, f"{prefix}.{k}" if prefix else k))
        return out
    if isinstance(value, list):
        out = {}
        for i, v in enumerate(value):
            out.update(leaves(v, f"{prefix}[{i}]"))
        if not value:
            out[prefix] = []
        return out
    return {prefix: value}


def describe_json(old, new):
    """Lines naming each leaf that differs between two JSON values."""
    a, b = leaves(old or {}), leaves(new or {})
    lines = []
    for path in sorted(set(a) | set(b)):
        if a.get(path, "<absent>") != b.get(path, "<absent>"):
            lines.append(f"      {path}: {a.get(path, '<absent>')!r} -> "
                         f"{b.get(path, '<absent>')!r}")
    return lines


def describe_lines(old, new):
    """Unified diff of two line lists, without the file header."""
    diff = difflib.unified_diff(old or [], new or [], lineterm="", n=0)
    return ["      " + d for d in diff if not d.startswith(("---", "+++", "@@"))]


def compare_case(old, new, ignore):
    """{key: [detail lines]} for every key that differs."""
    found = {}
    for key in KEYS:
        if key in ignore:
            continue
        a, b = old.get(key), new.get(key)
        if key == "sent":
            if old.get("sent_hex") != new.get("sent_hex"):
                found[key] = describe_json(a, b) or ["      (bytes differ)"]
        elif a != b:
            if key in ("ui", "ui_with_schedule_tab", "layout"):
                found[key] = describe_lines(a, b)
            elif isinstance(a, (dict, type(None))) and isinstance(b, (dict, type(None))):
                found[key] = describe_json(a, b) or [f"      {a!r} -> {b!r}"]
            else:
                found[key] = [f"      {a!r} -> {b!r}"]
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("baseline")
    parser.add_argument("new")
    parser.add_argument("--ignore", action="append", default=[], choices=KEYS,
                        help="a result key to leave out of the comparison")
    parser.add_argument("--max", type=int, default=0,
                        help="show details for at most this many cases (0: all)")
    args = parser.parse_args()

    base, new = load(args.baseline), load(args.new)
    counts = {}
    shown = 0
    problems = 0

    for name in sorted(set(base) | set(new)):
        if name not in new or name not in base:
            print(f"{name}: only in {'baseline' if name in base else 'new'}")
            problems += 1
            continue
        b, n = base[name], new[name]
        for case_id in sorted(set(b) | set(n)):
            if case_id not in n or case_id not in b:
                print(f"{name} {case_id}: only in {'baseline' if case_id in b else 'new'}")
                problems += 1
                continue
            found = compare_case(b[case_id], n[case_id], set(args.ignore))
            if not found:
                continue
            problems += 1
            for key in found:
                counts[key] = counts.get(key, 0) + 1
            if args.max and shown >= args.max:
                continue
            shown += 1
            print(f"{name} {case_id}")
            for key, lines in found.items():
                print(f"   {key}:")
                for line in lines:
                    print(line)

    print()
    if problems == 0:
        print("No differences.")
        return 0
    print(f"{problems} case(s) differ.")
    for key in KEYS:
        if key in counts:
            print(f"  {key}: {counts[key]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
