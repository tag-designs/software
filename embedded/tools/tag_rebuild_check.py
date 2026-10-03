#!/usr/bin/env python3
"""Check that an SWD capture rebuilds to the same SQLite file as a download.

@details The offline rebuild (`tag-rebuild`, host/libraries/tagcore/recovery/
         capturesource.cc) re-implements each family's monitor handlers on
         the host. That copy is only as good as its agreement with the
         firmware, so this tool measures the agreement instead of assuming it.

         `run` drives an attached tag (hardware):
           1. reset, set the clock, start with --config;
           2. capture mid-run with tag-capture, then attach with tag-info --
              the attach must succeed (a capture that leaves the debug logic
              dirty shows up here, not in the data);
           3. stop and download with tag-dwnld --stop -f sqlite;
           4. capture again;
           5. rebuild both captures: the final rebuild must equal the
              download table by table, and the mid-run rebuild must be an
              exact prefix of it.

         `compare` rebuilds one stored capture and diffs it against its
         download (offline). Keep reference pairs from `run` and re-run
         `compare` on them whenever a host decoder changes.

         Tables are compared row by row in rowid order. The only rows allowed
         to differ are the rebuild's provenance rows in `info` (source,
         capture_dir, captured_at). A difference anywhere else is a decoder
         bug, a firmware change the decoder has not followed, or a capture
         problem; the tool does not guess which.

         Every file is kept under the output directory.

@warning Detach qtmonitor first: it holds the monitor.
"""
from __future__ import annotations

import argparse
import json
import os
import sqlite3
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

#: Info rows that only the rebuild writes.
PROVENANCE = {"source", "capture_dir", "captured_at"}


def tool(bin_dir: str, name: str) -> str:
    """Path of a host tool: a plain binary, or the macOS bundle's executable."""
    plain = os.path.join(bin_dir, name)
    if os.path.isfile(plain):
        return plain
    return os.path.join(bin_dir, f"{name}.app", "Contents", "MacOS", name)


def run(argv: list[str], log_path: str, timeout: float = 600) -> tuple[int, str]:
    """Run a command, keep its output in @p log_path, return (rc, output)."""
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        text, rc = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        text, rc = f"TIMEOUT after {timeout} s\n{e.stdout or ''}{e.stderr or ''}", 124
    with open(log_path, "w") as f:
        f.write(" ".join(argv) + "\n" + text)
    return rc, text


#: TagInfo fields a capture cannot always know. Live, infoAck() reads the
#: RV3028 offset over I2C when the stored session facts carry none; the
#: rebuild then leaves ppm_clock_error unset, and only that is tolerated.
CAPTURE_UNKNOWABLE = ("ppm_clock_error",)


def rows(db: str, table: str) -> list[tuple]:
    """All rows of @p table in rowid order; info without provenance rows."""
    con = sqlite3.connect(db)
    try:
        r = con.execute(f'SELECT * FROM "{table}" ORDER BY rowid').fetchall()
    finally:
        con.close()
    if table == "info":
        r = [x for x in r if x[0] not in PROVENANCE]
    return r


def reconcile_info(reference: list[tuple], rebuilt: list[tuple],
                   notes: list[str]) -> list[tuple]:
    """The reference info rows, less any CAPTURE_UNKNOWABLE field the rebuild
    left out of the TagInfo JSON; each one dropped is reported in @p notes."""
    new_info = dict(rebuilt)
    out = []
    for name, value in reference:
        if name == "info" and "info" in new_info:
            try:
                ref, reb = json.loads(value), json.loads(new_info["info"])
            except ValueError:
                out.append((name, value))
                continue
            for key in CAPTURE_UNKNOWABLE:
                if key in ref and key not in reb:
                    notes.append(f"info.{key}: live {ref[key]}, unknown to the capture")
                    del ref[key]
            if ref == reb:
                value = new_info["info"]
        out.append((name, value))
    return out


def tables(db: str) -> list[str]:
    con = sqlite3.connect(db)
    try:
        return [t for (t,) in con.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
    finally:
        con.close()


def compare(reference: str, rebuilt: str, prefix: bool = False) -> list[str]:
    """Differences between two downloads; empty when they agree.

    With @p prefix, @p rebuilt may stop early (a mid-run capture): each of
    its data tables must equal the first rows of the reference's, and the
    states and info tables are not compared, since the run had not ended.
    """
    problems = []
    notes: list[str] = []
    ref_t, new_t = set(tables(reference)), set(tables(rebuilt))
    if not prefix and new_t - ref_t:
        problems.append(f"tables only in the rebuild: {sorted(new_t - ref_t)}")
    for t in sorted(ref_t):
        if t not in new_t:
            if not prefix:
                problems.append(f"{t}: missing from the rebuild")
            continue
        if prefix and t in ("states", "info"):
            continue
        a, b = rows(reference, t), rows(rebuilt, t)
        if t == "info":
            a = reconcile_info(a, b, notes)
        if prefix:
            a = a[:len(b)]
        if a != b:
            first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y),
                         min(len(a), len(b)))
            problems.append(f"{t}: {len(a)} reference rows, {len(b)} rebuilt; "
                            f"first difference at row {first}")
    for n in notes:
        print(f"  note: {n}")
    return problems


def rebuild(bin_dir: str, capture: str, db: str, log: str) -> bool:
    rc, _ = run([tool(bin_dir, "tag-rebuild"), capture, "-o", db], log)
    return rc == 0


def newest_capture(parent: str, before: set[str]) -> str | None:
    """The capture directory tag-capture just created under @p parent."""
    new = sorted(d for d in os.listdir(parent)
                 if d.startswith("capture-") and d not in before
                 and os.path.isfile(os.path.join(parent, d, "manifest.json")))
    return os.path.join(parent, new[-1]) if new else None


def cmd_compare(args) -> int:
    os.makedirs(args.out_dir, exist_ok=True)
    db = os.path.join(args.out_dir, "rebuilt.db3")
    if not rebuild(args.bin_dir, args.capture, db,
                   os.path.join(args.out_dir, "rebuild.log")):
        print(f"FAILED: tag-rebuild refused the capture; see {args.out_dir}/rebuild.log")
        return 1
    problems = compare(args.download, db, prefix=args.prefix)
    for p in problems:
        print(f"  {p}")
    print("PASS" if not problems else "FAIL")
    return 0 if not problems else 1


def cmd_run(args) -> int:
    out = os.path.join(args.out_dir, time.strftime("rebuild-check-%Y%m%d-%H%M%S"))
    os.makedirs(out, exist_ok=True)
    b = args.bin_dir
    results: dict = {"started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                     "config": args.config, "run_duration_s": args.run_duration,
                     "checks": {}}
    ok = True

    def step(name: str, good: bool, note: str = "") -> bool:
        nonlocal ok
        results["checks"][name] = "pass" if good else "fail"
        print(f"[{name}] {'ok' if good else 'FAILED'}{' -- ' + note if note else ''}")
        ok &= good
        return good

    rc, _ = run([tool(b, "tag-reset"), "--set-rtc"], os.path.join(out, "reset.log"))
    if not step("reset", rc == 0):
        return finish(out, results, False)
    rc, _ = run([tool(b, "tag-start"), "-c", args.config, "--set-rtc", "--start-now"],
                os.path.join(out, "start.log"))
    if not step("start", rc == 0):
        return finish(out, results, False)

    time.sleep(args.run_duration / 2)
    before = set(os.listdir(out))
    rc, _ = run([tool(b, "tag-capture"), "-o", out, "-r", "rebuild check: mid-run"],
                os.path.join(out, "tag-capture-mid.log"))
    mid = newest_capture(out, before)
    step("capture_mid", rc == 0 and mid is not None)
    rc, text = run([tool(b, "tag-info")], os.path.join(out, "attach-after-capture.log"))
    step("attach_after_capture", rc == 0 and "MASKINTS" not in text,
         "the monitor found debug state the capture left behind"
         if "MASKINTS" in text else "")

    time.sleep(args.run_duration / 2)
    download = os.path.join(out, "download.db3")
    rc, _ = run([tool(b, "tag-dwnld"), "--stop", "-f", "sqlite", "-o", download],
                os.path.join(out, "download.log"))
    if not step("download", rc == 0 and os.path.exists(download)):
        return finish(out, results, False)
    before = set(os.listdir(out))
    rc, _ = run([tool(b, "tag-capture"), "-o", out, "-r", "rebuild check: after stop"],
                os.path.join(out, "tag-capture-final.log"))
    final = newest_capture(out, before)
    if not step("capture_final", rc == 0 and final is not None):
        return finish(out, results, False)

    db = os.path.join(out, "rebuilt-final.db3")
    if step("rebuild_final", rebuild(b, final, db, os.path.join(out, "rebuild-final.log"))):
        problems = compare(download, db)
        results["final_differences"] = problems
        step("final_equals_download", not problems, "; ".join(problems))
    if mid:
        db = os.path.join(out, "rebuilt-mid.db3")
        if step("rebuild_mid", rebuild(b, mid, db, os.path.join(out, "rebuild-mid.log"))):
            problems = compare(download, db, prefix=True)
            results["mid_differences"] = problems
            step("mid_is_prefix", not problems, "; ".join(problems))
    return finish(out, results, ok)


def finish(out: str, results: dict, ok: bool) -> int:
    results["verdict"] = "pass" if ok else "fail"
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(results, f, indent=2)
    print(f"\n{'REBUILD CHECK PASSED' if ok else 'REBUILD CHECK FAILED'}\nartifacts in {out}")
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--bin-dir", default=os.path.join(REPO, "build-host", "bin"),
                   help="directory of the host tools")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="drive an attached tag (hardware)")
    r.add_argument("--config", required=True, help="protobuf-JSON configuration to start")
    r.add_argument("--run-duration", type=float, default=180,
                   help="seconds of collection; the mid-run capture is at half")
    r.add_argument("--out-dir", default="rebuild-checks")
    c = sub.add_parser("compare", help="rebuild one stored capture and diff it (offline)")
    c.add_argument("capture", help="tag-capture directory")
    c.add_argument("download", help="tag-dwnld -f sqlite file of the same run")
    c.add_argument("--prefix", action="store_true",
                   help="the capture was taken mid-run: require a prefix")
    c.add_argument("--out-dir", default="rebuild-checks/compare")
    args = p.parse_args()
    return cmd_run(args) if args.cmd == "run" else cmd_compare(args)


if __name__ == "__main__":
    sys.exit(main())
