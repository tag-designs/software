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
    p.add_argument("--run-max-ua", type=float, default=760.0,
                   help="fail if the life-cycle run draws more than this, in "
                        "uA. Sized for the default 400 Hz config at a 3.7 V "
                        "supply, where a healthy run is about 665 uA; run "
                        "current has twice moved ~200 uA between builds "
                        "differing only in code layout, and four release "
                        "checks reported such a regression and passed because "
                        "only idle was bounded. The shipping board regulates "
                        "with an SMPS, so this scales with supply voltage: "
                        "at 3.3 V the equivalent limit is 850 uA")
    p.add_argument("--idle-max-ua", type=float, default=None,
                   help="current at or below which a state counts as asleep, "
                        "in uA. This bound does two jobs in the life-cycle "
                        "check: a resting state must come UNDER it, and the "
                        "running state must come OVER it, because a run "
                        "drawing idle current collected nothing. The default "
                        "(100 uA) suits IMUTag, whose run and rest are three "
                        "orders of magnitude apart. It fails every sub-uA "
                        "L432 tag: CompassTagAT25 rests at 0.23 uA and runs "
                        "at 1.96 uA, so 100 uA declares the run asleep. Pass "
                        "1 for CompassTag -- above the floor, below the run")
    p.add_argument("--run-duration", type=float, default=60.0,
                   help="life-cycle run window in seconds. Must span enough "
                        "sample periods to integrate: 60 s at CompassTag's "
                        "30 s period is two samples, which is alignment "
                        "noise. Its plan specifies 900")
    p.add_argument("--rest-duration", type=float, default=None,
                   help="life-cycle resting window in seconds. The default "
                        "(30 s) is not enough charge to integrate a sub-uA "
                        "floor against; the sub-uA tags use 120")
    p.add_argument("--settle", type=float, default=None,
                   help="seconds to wait after a session closes before "
                        "measuring, passed to the life-cycle check")
    p.add_argument("--storm-sets", type=int, default=None,
                   help="attach-storm sets to run. Default: 3 on IMUTag "
                        "targets, 0 on every other. The storm is an IMUTag "
                        "test: IMUTag does not use standby or shutdown while "
                        "running, so an attach interrupts a live collection "
                        "and the firmware must restart it, and that recovery "
                        "is what the storm exercises. The L432 targets collect "
                        "on events and sit in standby or shutdown between "
                        "them, so there is no live collection to interrupt; "
                        "attaching mid-run just resets them. A storm that "
                        "passes on one of those targets is not evidence of "
                        "anything")
    p.add_argument("--measure-python",
                   default=os.path.expanduser("~/opt/joulescope-mcp/.venv/bin/python"))
    p.add_argument("--skip-build", action="store_true",
                   help="use the image already flashed on the tag")
    args = p.parse_args()

    # Storms are an IMUTag test; see --storm-sets. Defaulting them on for every
    # target ran 240 attach/detach cycles against tags where an attach only
    # resets the run, and made a deeply sleeping tag look like a harness
    # failure when the storm could not attach to it.
    if args.storm_sets is None:
        args.storm_sets = 3 if args.target.startswith("IMUTag") else 0
        if args.storm_sets == 0:
            print(f"  (no attach storms: {args.target} is not an IMUTag "
                  "target, so a storm would test nothing -- pass "
                  "--storm-sets N to override)")

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
    lifecycle_cmd = [os.path.join(TOOLS, "tag_lifecycle_check.py"),
                     "--config", args.config,
                     "--run-duration", str(args.run_duration),
                     "--run-max-ua", str(args.run_max_ua),
                     "--use-server"]
    # Only forward the optional bounds that were given, so an unqualified run
    # keeps the life-cycle check's own defaults rather than this tool's idea
    # of them.
    for flag, value in (("--idle-max-ua", args.idle_max_ua),
                        ("--rest-duration", args.rest_duration),
                        ("--settle", args.settle)):
        if value is not None:
            lifecycle_cmd += [flag, str(value)]
    # The life cycle walks five states: one run window, four resting windows,
    # and a settle before each. A fixed 1800 s budget silently capped the run
    # duration this tool is now allowed to pass -- --run-duration 1800 spent
    # the entire budget on the run and the step was killed mid-walk, reported
    # as a life-cycle failure with no measurement behind it. Size the timeout
    # from what was actually asked for, with room for the resets, downloads
    # and attach retries between windows.
    rest_s = args.rest_duration if args.rest_duration is not None else 30.0
    settle_s = args.settle if args.settle is not None else 12.0
    lifecycle_timeout = int(args.run_duration + 4 * rest_s + 5 * settle_s + 900)
    print(f"  (life-cycle budget {lifecycle_timeout} s)")
    rc, out_txt = run(lifecycle_cmd, os.path.join(out, "lifecycle.log"),
                      lifecycle_timeout)
    results["checks"]["lifecycle"] = "pass" if rc == 0 else "fail"
    results["checks"]["run_max_ua"] = args.run_max_ua
    results["checks"]["run_duration_s"] = args.run_duration
    if args.idle_max_ua is not None:
        results["checks"]["idle_max_ua"] = args.idle_max_ua
    if args.rest_duration is not None:
        results["checks"]["rest_duration_s"] = args.rest_duration
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
