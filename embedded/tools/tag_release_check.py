#!/usr/bin/env python3
"""Qualify one firmware image for release, on hardware.

@details Builds the target, flashes it, and runs every check that has caught a
         real regression on this tree: idle current repeated, the full
         life-cycle power walk, and attach storms. Everything it runs is stored
         under the output directory, including the image itself, so a release
         can be re-examined later without rebuilding.

         The reason this exists rather than a code review: Standby entry on the
         STM32U375 depends on where code lands in the image, and the same
         source can sleep or stall depending on changes elsewhere in the tree.
         A build that passes every functional test can still draw 240x the idle
         current. The only way to know is to measure the image being shipped.

@warning Detach the Joulescope desktop app and qtmonitor first. Both invalidate
         the result -- the app holds the instrument, and qtmonitor holds the
         monitor so the tag never sleeps at all -- and neither failure is
         obvious in the output.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.dirname(os.path.abspath(__file__))

#: Idle must be far below this. A stalled Standby reads about 1035 uA and a
#: sleeping tag about 5 uA, so anything in between is a failure, not a margin.
IDLE_LIMIT_UA = 100.0


def run(argv: list[str], log_path: str, timeout: float) -> tuple[int, str]:
    """Run a command, storing all of its output.

    @param argv     Command and arguments.
    @param log_path File to receive stdout and stderr, kept whatever happens.
    @param timeout  Seconds before the command is killed.
    @return Exit status and the combined output.
    """
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        out, rc = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        out, rc = f"TIMEOUT after {timeout} s\n{e.stdout or ''}{e.stderr or ''}", -1
    except FileNotFoundError as e:
        out, rc = str(e), -1
    with open(log_path, "w") as fh:
        fh.write(" ".join(argv) + "\n\n" + out)
    return rc, out


def measure_idle(python: str, log_path: str) -> float | None:
    """Reset the tag, let it settle, and measure supply current.

    @param python   Interpreter that can import pyjoulescope_driver.
    @param log_path File to receive the measurement output.
    @return Current in microamps, or None when no figure was produced.
    """
    subprocess.run([os.path.join(REPO, "build-host", "bin", "tag-reset"),
                    "--set-rtc"], capture_output=True, text=True, timeout=200)
    time.sleep(16)
    rc, out = run([python, os.path.join(TOOLS, "joulescope_measure.py"),
                   "--use-server", "--duration", "10", "--window", "0.5"],
                  log_path, 120)
    for line in out.splitlines():
        if "charge/time" in line:
            try:
                return float(line.split(":")[-1].split()[0])
            except (ValueError, IndexError):
                return None
    return None


def main() -> int:
    """Run the release battery and report a single verdict.

    @return 0 when every check passed, 1 otherwise.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--target", default="IMUTagNandBmp581", help="firmware target")
    p.add_argument("--build-dir", default="build-embedded")
    p.add_argument("--config", default=os.path.join(
        TOOLS, "power-configs", "imutag-400.json"))
    p.add_argument("--out-dir", default="release-checks")
    p.add_argument("--idle-trials", type=int, default=4,
                   help="idle measurements; the fault is layout-driven, so "
                        "repeat rather than trusting one reading")
    p.add_argument("--run-max-ua", type=float, default=850.0,
                   help="fail if the life-cycle run draws more than this, in "
                        "uA. Sized for the default 400 Hz config, where a "
                        "healthy run is about 750 uA; run current has twice "
                        "moved ~200 uA between builds differing only in code "
                        "layout, and four release checks reported such a "
                        "regression and passed because only idle was bounded")
    p.add_argument("--storm-sets", type=int, default=3,
                   help="attach-storm sets to run")
    p.add_argument("--measure-python",
                   default=os.path.expanduser("~/opt/joulescope-mcp/.venv/bin/python"))
    p.add_argument("--skip-build", action="store_true",
                   help="use the image already flashed on the tag")
    args = p.parse_args()

    stamp = time.strftime("%Y%m%d-%H%M%S")
    out = os.path.join(args.out_dir, f"release-{args.target}-{stamp}")
    os.makedirs(out, exist_ok=True)
    results: dict = {"target": args.target, "started_utc":
                     time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                     "checks": {}}
    try:
        results["git_hash"] = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], capture_output=True,
            text=True).stdout.strip()
        results["git_dirty"] = bool(subprocess.run(
            ["git", "status", "--porcelain"], capture_output=True,
            text=True).stdout.strip())
    except OSError:
        pass
    if results.get("git_dirty"):
        print("  NOTE: the tree is dirty; this is not a reproducible release")

    ok = True

    # 1. Build and flash, so what is measured is what would ship.
    if not args.skip_build:
        print(f"[build] {args.target}")
        tdir = os.path.join(args.build_dir, "embedded", "tags", args.target)
        for sub in ("build", "dep"):
            shutil.rmtree(os.path.join(tdir, sub), ignore_errors=True)
        rc, _ = run(["cmake", "--build", args.build_dir, "--target",
                     f"{args.target}-download"],
                    os.path.join(out, "build.log"), 1800)
        good = rc == 0 and "download complete" in \
            open(os.path.join(out, "build.log")).read().lower()
        results["checks"]["build_flash"] = "pass" if good else "fail"
        print(f"  {'ok' if good else 'FAILED'}")
        if not good:
            json.dump(results, open(os.path.join(out, "results.json"), "w"),
                      indent=2)
            print(f"\ncheck artifacts in {out}")
            return 1
        elf = os.path.join(tdir, "build", f"{args.target}.elf")
        if os.path.exists(elf):
            shutil.copy2(elf, os.path.join(out, os.path.basename(elf)))

    # 2. Idle, repeated. This is the check that catches the Standby stall.
    print(f"[idle] {args.idle_trials} measurements")
    idles = []
    for i in range(args.idle_trials):
        ua = measure_idle(args.measure_python,
                          os.path.join(out, f"idle{i + 1}.log"))
        idles.append(ua)
        print(f"  trial {i + 1}: {ua if ua is not None else 'NO READING'} uA")
    bad = [u for u in idles if u is None or u > IDLE_LIMIT_UA]
    results["checks"]["idle"] = {
        "readings_ua": idles, "limit_ua": IDLE_LIMIT_UA,
        "verdict": "fail" if bad else "pass"}
    if bad:
        ok = False
        print(f"  FAILED: {len(bad)} of {len(idles)} above {IDLE_LIMIT_UA} uA "
              "-- the tag reports IDLE and is not sleeping")

    # 3. Every resting state, not just idle.
    print("[life-cycle] idle, running, stopped, idle again")
    rc, out_txt = run([os.path.join(TOOLS, "tag_lifecycle_check.py"),
                       "--config", args.config, "--run-duration", "60",
                       "--run-max-ua", str(args.run_max_ua),
                       "--use-server"],
                      os.path.join(out, "lifecycle.log"), 1800)
    results["checks"]["lifecycle"] = "pass" if rc == 0 else "fail"
    results["checks"]["run_max_ua"] = args.run_max_ua
    print(f"  {'passed' if rc == 0 else 'FAILED'}")
    ok &= rc == 0

    # 4. Attach storms, which is where the host/firmware races show up.
    print(f"[storm] {args.storm_sets} sets")
    storm_ok = True
    for s in range(args.storm_sets):
        rc, _ = run([os.path.join(TOOLS, "tag_attach_storm.py"),
                     "--config", args.config, "--rtc-cycles", "10",
                     "--storm-rounds", "2", "--cycles", "40",
                     "--keep-download", os.path.join(out, f"storm{s + 1}")],
                    os.path.join(out, f"storm{s + 1}.log"), 3000)
        print(f"  set {s + 1}: {'passed' if rc == 0 else 'FAILED'}")
        storm_ok &= rc == 0
    results["checks"]["storm"] = "pass" if storm_ok else "fail"
    ok &= storm_ok

    results["verdict"] = "pass" if ok else "fail"
    json.dump(results, open(os.path.join(out, "results.json"), "w"), indent=2)
    print(f"\n{'RELEASE CHECK PASSED' if ok else 'RELEASE CHECK FAILED'}")
    print(f"artifacts in {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
