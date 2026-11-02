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

         Defaults come from the target's profile ([PROFILES]): IMUTag at
         400 Hz from 3.7 V, BitTag and PresTag at their qualification
         settings from 2.5 V. Every one can be overridden on the command line,
         and a target with no profile needs --config.

@warning Detach the Joulescope desktop app and Tag Monitor first. Both invalidate
         the result -- the app holds the instrument, and Tag Monitor holds the
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

sys.path.insert(0, TOOLS)
from power_experiment import DEFAULT_BIN, resolve_measure_python  # noqa: E402

#: Per-target defaults, chosen by --target: an exact name, else the longest
#: key the target starts with. Each value is the default of the option of the
#: same name; idle_max_ua bounds both the idle trials and the life-cycle's
#: resting states, and idle_window_s is the length of each idle trial.
#:
#: - IMUTag: a stalled Standby reads about 1035 uA and a sleeping tag about
#:   5 uA, so 100 uA separates them; run 665 uA at 400 Hz from 3.7 V, bound
#:   760 uA (docs/bench/power-testing.md, section 5).
#: - BitTag: idle 0.118 uA and run 0.508 uA at BITPERSEC from 2.5 V, agreed by
#:   two sessions; bound 1.15x (embedded/tags/BitTag/design/power-test-plan.md).
#: - PresTag: idle at most 0.22 uA, run 0.37-0.41 uA across boards at the
#:   shipped 60 s period; bound 1.15x the highest. A 1800 s run is the 30
#:   samples of the fw-v0.6 qualification
#:   (embedded/tags/families/PresTag/design/power-results.md).
#:
#: The sub-uA tags also need 120 s windows: less charge than that does not
#: integrate against their floor.
PROFILES: dict[str, dict] = {
    "IMUTag": dict(config="imutag-400.json", idle_max_ua=100.0, idle_window_s=10.0,
                   run_min_ua=None, run_max_ua=760.0, run_duration=60.0,
                   rest_duration=None, settle=None, storm_sets=3),
    "BitTag": dict(config="bittag-bitpersec.json", idle_max_ua=0.3, idle_window_s=120.0,
                   run_min_ua=0.3, run_max_ua=0.584, run_duration=1200.0,
                   rest_duration=120.0, settle=75.0, storm_sets=0),
    "PresTag": dict(config="prestag-60.json", idle_max_ua=0.22, idle_window_s=120.0,
                    run_min_ua=0.22, run_max_ua=0.47, run_duration=1800.0,
                    rest_duration=120.0, settle=75.0, storm_sets=0),
}

#: Targets that share a family prefix but not its profile. BitTagNG and
#: BitTag-legacy are other firmware, and PresTagRaw logs to different tables;
#: none has been qualified, so none inherits a qualified tag's bounds.
NO_PROFILE = ("BitTagNG", "BitTag-legacy", "PresTagRaw")


def profile_for(target: str) -> dict | None:
    """The defaults for @p target, or None when it has no profile.

    @param target Firmware target name, as passed to --target.
    @return A copy of the matching [PROFILES] entry, or None.
    """
    if target in NO_PROFILE:
        return None
    keys = [k for k in PROFILES if target == k or target.startswith(k)]
    return dict(PROFILES[max(keys, key=len)]) if keys else None


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


def measure_idle(python: str, bin_dir: str, window_s: float,
                 log_path: str) -> float | None:
    """Reset the tag, let it settle, and measure supply current.

    @param python   Interpreter that can import pyjoulescope_driver.
    @param bin_dir  Directory holding the host tools (tag-reset).
    @param window_s Measurement window in seconds.
    @param log_path File to receive the measurement output; the reset's
                    output goes beside it, with ".reset" added.
    @return Current in microamps, or None when no figure was produced,
            including when the reset failed.
    """
    rc, _ = run([os.path.join(bin_dir, "tag-reset"), "--set-rtc"],
                log_path + ".reset", 200)
    if rc != 0:
        return None
    time.sleep(16)
    rc, out = run([python, os.path.join(TOOLS, "joulescope_measure.py"),
                   "--use-server", "--duration", f"{window_s:g}", "--window", "0.5"],
                  log_path, window_s + 120)
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
    p.add_argument("--target", default="IMUTagNandBmp581",
                   help="firmware target; chooses the defaults below from its "
                        f"profile ({', '.join(PROFILES)})")
    p.add_argument("--build-dir", default="build-embedded")
    p.add_argument("--bin-dir", default=DEFAULT_BIN,
                   help="directory holding the host tools (tag-reset and the "
                        "rest), e.g. a host build tree's Release/bin; "
                        "default $TAG_BIN_DIR or build-host/bin")
    p.add_argument("--config", default=None,
                   help="run configuration; default the profile's, from "
                        "power-configs/. Required for a target with no profile")
    p.add_argument("--out-dir", default="release-checks")
    p.add_argument("--idle-trials", type=int, default=4,
                   help="idle measurements; the fault is layout-driven, so "
                        "repeat rather than trusting one reading")
    p.add_argument("--idle-window", type=float, default=None,
                   help="seconds per idle trial; default the profile's (10 s "
                        "for IMUTag, 120 s for the sub-uA tags, which need the "
                        "charge to integrate against their floor)")
    p.add_argument("--run-max-ua", type=float, default=None,
                   help="fail if the life-cycle run draws more than this, in "
                        "uA; default the profile's. Run current has twice "
                        "moved ~200 uA between IMUTag builds differing only in "
                        "code layout, and four release checks reported such a "
                        "regression and passed because only idle was bounded. "
                        "IMUTag's 760 uA is for 400 Hz from 3.7 V; its board "
                        "regulates with an SMPS, so at 3.3 V the equivalent "
                        "is 850 uA")
    p.add_argument("--idle-max-ua", type=float, default=None,
                   help="current at or below which a resting state counts as "
                        "asleep, in uA, for the idle trials and the life-cycle "
                        "check; default the profile's (100 IMUTag, 0.3 BitTag, "
                        "0.22 PresTag)")
    p.add_argument("--run-min-ua", type=float, default=None,
                   help="the life-cycle run must draw more than this, in uA, "
                        "or it collected nothing; default the profile's, else "
                        "--idle-max-ua")
    p.add_argument("--run-duration", type=float, default=None,
                   help="life-cycle run window in seconds; default the "
                        "profile's. Must span enough sample periods to "
                        "integrate: 60 s at a 30 s period is two samples, "
                        "which is alignment noise")
    p.add_argument("--rest-duration", type=float, default=None,
                   help="life-cycle resting window in seconds; default the "
                        "profile's, else the life-cycle check's 30 s")
    p.add_argument("--settle", type=float, default=None,
                   help="seconds to wait after a session closes before "
                        "measuring; default the profile's, else the "
                        "life-cycle check's 12 s. The sub-uA tags start on the "
                        "next minute alarm, so they use 75")
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
    p.add_argument("--measure-python", default=None,
                   help="interpreter with pyjoulescope_driver; default as "
                        "power_experiment.py finds one")
    p.add_argument("--skip-build", action="store_true",
                   help="use the image already flashed on the tag")
    args = p.parse_args()

    # Fill every option left unset from the target's profile. A target with
    # no profile gets no qualified bounds: it must name its config, and the
    # rest fall back to the life-cycle check's own defaults with no run bound.
    profile = profile_for(args.target)
    if profile is None:
        if args.config is None:
            p.error(f"no profile for {args.target}; pass --config and its "
                    "bounds (profiles: " + ", ".join(PROFILES) + ")")
        profile = dict(config=None, idle_max_ua=100.0, idle_window_s=10.0,
                       run_min_ua=None, run_max_ua=None, run_duration=60.0,
                       rest_duration=None, settle=None, storm_sets=0)
        print(f"  NOTE: {args.target} has no profile; using generic bounds")
    if args.config is None:
        args.config = os.path.join(TOOLS, "power-configs", profile["config"])
    if args.idle_window is None:
        args.idle_window = profile["idle_window_s"]
    for name in ("idle_max_ua", "run_min_ua", "run_max_ua", "run_duration",
                 "rest_duration", "settle", "storm_sets"):
        if getattr(args, name) is None:
            setattr(args, name, profile[name])
    if args.storm_sets == 0 and not args.target.startswith("IMUTag"):
        print(f"  (no attach storms: {args.target} is not an IMUTag "
              "target, so a storm would test nothing -- pass "
              "--storm-sets N to override)")
    python = resolve_measure_python(args.measure_python)

    stamp = time.strftime("%Y%m%d-%H%M%S")
    out = os.path.join(args.out_dir, f"release-{args.target}-{stamp}")
    os.makedirs(out, exist_ok=True)
    results: dict = {"target": args.target, "started_utc":
                     time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                     "config": os.path.relpath(args.config, REPO),
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
    print(f"[idle] {args.idle_trials} measurements of {args.idle_window:g} s, "
          f"bound {args.idle_max_ua:g} uA")
    idles = []
    for i in range(args.idle_trials):
        ua = measure_idle(python, args.bin_dir, args.idle_window,
                          os.path.join(out, f"idle{i + 1}.log"))
        idles.append(ua)
        print(f"  trial {i + 1}: {ua if ua is not None else 'NO READING'} uA")
    bad = [u for u in idles if u is None or u > args.idle_max_ua]
    results["checks"]["idle"] = {
        "readings_ua": idles, "limit_ua": args.idle_max_ua,
        "window_s": args.idle_window, "verdict": "fail" if bad else "pass"}
    if bad:
        ok = False
        print(f"  FAILED: {len(bad)} of {len(idles)} without a reading or "
              f"above {args.idle_max_ua:g} uA -- the reset failed, or the tag "
              "reports IDLE and is not sleeping")

    # 3. Every resting state, not just idle.
    print("[life-cycle] idle, running, stopped, idle again")
    lifecycle_cmd = [os.path.join(TOOLS, "tag_lifecycle_check.py"),
                     "--config", args.config,
                     "--run-duration", str(args.run_duration),
                     "--idle-max-ua", str(args.idle_max_ua),
                     "--bin-dir", args.bin_dir,
                     "--measure-python", python,
                     "--use-server"]
    # Only forward the optional values that are set, so the rest keep the
    # life-cycle check's own defaults rather than this tool's idea of them.
    for flag, value in (("--run-max-ua", args.run_max_ua),
                        ("--run-min-ua", args.run_min_ua),
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
    results["checks"]["run_duration_s"] = args.run_duration
    results["checks"]["idle_max_ua"] = args.idle_max_ua
    if args.run_max_ua is not None:
        results["checks"]["run_max_ua"] = args.run_max_ua
    if args.run_min_ua is not None:
        results["checks"]["run_min_ua"] = args.run_min_ua
    if args.rest_duration is not None:
        results["checks"]["rest_duration_s"] = args.rest_duration
    print(f"  {'passed' if rc == 0 else 'FAILED'}")
    ok &= rc == 0

    # 4. Attach storms, which is where the host/firmware races show up.
    print(f"[storm] {args.storm_sets} sets")
    storm_ok = True
    for s in range(args.storm_sets):
        rc, _ = run([os.path.join(TOOLS, "tag_attach_storm.py"),
                     "--config", args.config, "--bin-dir", args.bin_dir,
                     "--rtc-cycles", "10",
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
