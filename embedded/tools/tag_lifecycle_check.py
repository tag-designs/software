#!/usr/bin/env python3
"""Walk a tag through its whole life cycle, measuring power at every rest state.

A tag spends almost all of its life in one of three states that should draw
microamps -- idle with its clock set, waiting in a terminal state after a run,
and idle again after the data is off it -- and a short time running. Measuring
only the run, which is what a power sweep does, cannot see a fault in any of
the other three. This walks the whole cycle and measures each one:

  1. reset --set-rtc  -> IDLE      measured: a prepared tag, awaiting deployment
  2. start            -> RUNNING   measured after a settling delay: collecting
  3. stop             -> FINISHED  measured: a returned tag, data not yet read
  4. download                      not measured; the monitor is attached
  5. reset --set-rtc  -> IDLE      measured: the same state as step 1

Step 1 sets the clock deliberately. Setting the clock is normally the last
thing done to a tag before it is deployed, so idle-with-the-clock-set is the
state a prepared tag actually sits in for weeks. Measuring idle after a plain
reset instead measures a state no deployed tag is ever in.

Steps 1 and 5 are the same logical state reached by different histories, and
comparing them is the point of the test. A tag that reports IDLE while drawing
run current is indistinguishable from a healthy one through every functional
check there is; it is only visible as a number, and only visible at all if
something measures that state. The regression this tool was written after --
an I2C bus clear that parked SDA and SCL as GPIO outputs against the board
pull-ups, so any tag whose clock had been set drew about 1 mA instead of 5 uA
until its next reset -- was invisible to the rate sweep, because 1036 uA at
idle is close enough to a real 400 Hz run current of about 970 uA to hide in
it. It would have failed step 1 here on the first run.

Examples:
    tag_lifecycle_check.py --config cfg/imutag-400.json --run-duration 60
    tag_lifecycle_check.py --config cfg/imutag-100.json --idle-max-ua 50
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
import time
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from power_experiment import (  # noqa: E402
    DEFAULT_BIN,
    ExperimentError,
    Measurement,
    check_download,
    config_odr_hz,
    download,
    measure,
    recorded_config,
    resolve_measure_python,
    start,
    stop,
    to_idle,
)

#: Current at or below which a state counts as genuinely asleep, in uA.
#:
#: Idle on a healthy IMUTagNandBmp581 measures about 5 uA and the failure mode
#: this tool exists to catch measured about 1036 uA, so anything in between
#: separates them. The threshold is deliberately far above the healthy figure
#: rather than close to it: this is a "did it sleep at all" check, not a
#: regression test on the idle figure itself, and a tag that has genuinely got
#: worse by 10 uA is a different investigation from one that never slept.
DEFAULT_IDLE_MAX_UA = 100.0

#: Seconds to wait after a monitor session closes before measuring.
#:
#: The tag refuses every sleep path while it believes a session is open, and
#: teardown takes up to two heartbeat periods. Measuring immediately reads run
#: current in every state, which is a failure that looks exactly like the one
#: this tool is looking for.
DEFAULT_SETTLE_S = 12.0


@dataclass
class Point:
    """One measured rest state."""

    name: str
    what: str
    state: str | None = None
    current_ua: float | None = None
    expect_asleep: bool = True
    verdict: str = "not measured"

    @property
    def ok(self) -> bool:
        """True when the measured current matches what the state should draw."""
        return self.verdict == "pass"


@dataclass
class Cycle:
    """Everything one life-cycle pass produced."""

    points: list[Point] = field(default_factory=list)
    sanity: str = "not checked"
    failures: list[str] = field(default_factory=list)


def settle_and_measure(python: str, settle: float, duration: float,
                       window: float, verbose: bool,
                       attempts: int = 3) -> Measurement:
    """Wait for the tag to reach its resting state, then measure it.

    Retries a capture that produced no figure. Opening the instrument fails
    occasionally and transiently, with an empty error, and a capture that did
    not happen is not evidence about the tag: reporting it as a failed state
    would blame the firmware for an instrument hiccup. A capture that runs and
    returns a number is taken at face value, however unwelcome the number.

    @param python   Interpreter that can import pyjoulescope_driver.
    @param settle   Seconds to wait before measuring; see DEFAULT_SETTLE_S.
    @param duration Measurement window in seconds.
    @param window   Statistics block length in seconds.
    @param verbose  True to echo commands.
    @param attempts Captures to try before giving up.
    @return Parsed measurement.
    @raise ExperimentError when no attempt produced a usable current figure.
    """
    print(f"      settling {settle:.0f} s")
    time.sleep(settle)
    last: ExperimentError | None = None
    for attempt in range(attempts):
        try:
            return measure(python, duration, window, verbose)
        except ExperimentError as e:
            last = e
            if attempt < attempts - 1:
                print(f"      capture produced nothing, retry "
                      f"{attempt + 2}/{attempts} in 5 s")
                time.sleep(5.0)
    raise ExperimentError(
        f"no capture succeeded in {attempts} attempts: {last}")


def judge(point: Point, idle_max_ua: float) -> None:
    """Decide whether a measured point matches the state it was taken in.

    Sets @p point.verdict to "pass", "FAIL" or "unmeasured". A resting state
    must be at or below @p idle_max_ua; the running state must be above it,
    because a run that draws idle current collected nothing.

    @param point       The point to judge, with current_ua already filled in.
    @param idle_max_ua Threshold separating asleep from awake, in uA.
    """
    if point.current_ua is None:
        point.verdict = "unmeasured"
        return
    asleep = point.current_ua <= idle_max_ua
    if point.expect_asleep:
        point.verdict = "pass" if asleep else "FAIL"
    else:
        point.verdict = "FAIL" if asleep else "pass"


def main() -> int:
    """Run one life-cycle pass and report each state.

    @return 0 when every measured state matched expectation, 1 otherwise.
    """
    p = argparse.ArgumentParser(
        description="Measure tag power at every life-cycle rest state.")
    p.add_argument("--config", help="protobuf-JSON configuration for the run")
    p.add_argument("--merge", action="store_true",
                   help="overlay a partial configuration onto the tag's")
    p.add_argument("--bin-dir", default=DEFAULT_BIN,
                   help="directory holding the host tools")
    p.add_argument("--base", help="bus:device selector")
    p.add_argument("--run-duration", type=float, default=60.0,
                   help="run measurement window in seconds")
    p.add_argument("--rest-duration", type=float, default=30.0,
                   help="resting-state measurement window in seconds")
    p.add_argument("--window", type=float, default=0.5,
                   help="statistics block length in seconds")
    p.add_argument("--settle", type=float, default=DEFAULT_SETTLE_S,
                   help="seconds to wait after a session closes")
    p.add_argument("--idle-max-ua", type=float, default=DEFAULT_IDLE_MAX_UA,
                   help="current at or below which a state counts as asleep")
    p.add_argument("--timeout", type=float, default=180.0,
                   help="per-command timeout in seconds")
    p.add_argument("--download-timeout", type=float, default=900.0,
                   help="download timeout in seconds")
    p.add_argument("--tolerance", type=float, default=0.25,
                   help="fractional tolerance on the expected sample count")
    p.add_argument("--keep-download", help="directory to keep the database in")
    p.add_argument("--measure-python", help="interpreter with pyjoulescope_driver")
    p.add_argument("--verbose", action="store_true", help="echo commands")
    args = p.parse_args()

    cyc = Cycle()
    try:
        python = resolve_measure_python(args.measure_python)
        if args.verbose:
            print(f"  measurement interpreter: {python}")

        # 1. Prepared tag: idle with the clock set.
        print("[1/5] reset to idle, set clock")
        pt = Point("idle_prepared", "idle, clock set")
        pt.state = to_idle(args.bin_dir, args.base, args.timeout,
                           args.verbose, set_rtc=True)
        m = settle_and_measure(python, args.settle, args.rest_duration,
                               args.window, args.verbose)
        pt.current_ua = m.current_ua
        judge(pt, args.idle_max_ua)
        cyc.points.append(pt)
        print(f"      {pt.current_ua:.2f} uA  [{pt.verdict}]")

        # 2. Running, measured after a settling delay.
        print("[2/5] start collection")
        pt = Point("running", "collecting", expect_asleep=False)
        start(args.bin_dir, args.config, args.base, args.merge,
              args.timeout, args.verbose)
        pt.state = "RUNNING"
        m = settle_and_measure(python, args.settle, args.run_duration,
                               args.window, args.verbose)
        pt.current_ua = m.current_ua
        judge(pt, args.idle_max_ua)
        cyc.points.append(pt)
        print(f"      {pt.current_ua:.2f} uA  [{pt.verdict}]")

        # 3. Returned tag: stopped, data still on it.
        print("[3/5] stop collection")
        pt = Point("stopped", "terminal, data not yet read")
        pt.state = stop(args.bin_dir, args.base, args.timeout, args.verbose)
        m = settle_and_measure(python, args.settle, args.rest_duration,
                               args.window, args.verbose)
        pt.current_ua = m.current_ua
        judge(pt, args.idle_max_ua)
        cyc.points.append(pt)
        print(f"      state {pt.state}, {pt.current_ua:.2f} uA  [{pt.verdict}]")

        # 4. Download. Not measured: the monitor is attached throughout.
        print("[4/5] download")
        tmp = None
        if args.keep_download:
            os.makedirs(args.keep_download, exist_ok=True)
            db = os.path.join(args.keep_download,
                              f"lifecycle-{int(time.time())}.db3")
        else:
            tmp = tempfile.NamedTemporaryFile(suffix=".db3", delete=False)
            tmp.close()
            db = tmp.name
        res = download(args.bin_dir, db, args.base, args.download_timeout,
                       args.verbose)
        if not res.ok:
            cyc.sanity = "download failed"
            cyc.failures.append(f"download failed: {res.stderr.strip()}")
        else:
            cfg = recorded_config(db)
            verdict, detail = check_download(db, args.run_duration,
                                             config_odr_hz(cfg),
                                             args.tolerance)
            cyc.sanity = verdict
            print(f"      {verdict}: {detail}")
            if verdict.lower().startswith("fail"):
                cyc.failures.append(f"download sanity: {detail}")
        if tmp is not None:
            os.unlink(db)

        # 5. Same state as step 1, reached by a different history.
        print("[5/5] reset to idle, set clock")
        pt = Point("idle_after_cycle", "idle, clock set, after a full cycle")
        pt.state = to_idle(args.bin_dir, args.base, args.timeout,
                           args.verbose, set_rtc=True)
        m = settle_and_measure(python, args.settle, args.rest_duration,
                               args.window, args.verbose)
        pt.current_ua = m.current_ua
        judge(pt, args.idle_max_ua)
        cyc.points.append(pt)
        print(f"      {pt.current_ua:.2f} uA  [{pt.verdict}]")

    except ExperimentError as e:
        cyc.failures.append(str(e))
        print(f"  aborted: {e}", file=sys.stderr)

    print()
    print("  state                  what                             uA  verdict")
    for pt in cyc.points:
        cur = "  n/a" if pt.current_ua is None else f"{pt.current_ua:9.2f}"
        print(f"  {pt.name:22} {pt.what:32} {cur}  {pt.verdict}")
    print(f"  download sanity: {cyc.sanity}")

    for pt in cyc.points:
        if pt.verdict == "FAIL":
            if pt.expect_asleep:
                cyc.failures.append(
                    f"{pt.name}: {pt.current_ua:.2f} uA, above the "
                    f"{args.idle_max_ua:.0f} uA sleep threshold -- the tag "
                    f"reports {pt.state} but is not sleeping")
            else:
                cyc.failures.append(
                    f"{pt.name}: {pt.current_ua:.2f} uA, at or below the "
                    f"{args.idle_max_ua:.0f} uA sleep threshold -- the tag "
                    "was not collecting")
        elif pt.verdict == "unmeasured":
            cyc.failures.append(f"{pt.name}: no current figure produced")

    # The pair that matters. Steps 1 and 5 are the same state, so a difference
    # between them is a fault the absolute numbers can hide: both could sit
    # under a generous threshold while one is ten times the other.
    a = next((x for x in cyc.points if x.name == "idle_prepared"), None)
    b = next((x for x in cyc.points if x.name == "idle_after_cycle"), None)
    if a and b and a.current_ua and b.current_ua:
        hi, lo = max(a.current_ua, b.current_ua), min(a.current_ua, b.current_ua)
        if lo > 0 and hi / lo > 3.0:
            cyc.failures.append(
                f"idle differs by {hi / lo:.1f}x between step 1 "
                f"({a.current_ua:.2f} uA) and step 5 ({b.current_ua:.2f} uA); "
                "the same state should not depend on how it was reached")

    print()
    if cyc.failures:
        print("  FAILED")
        for f in cyc.failures:
            print(f"    - {f}")
        return 1
    print("  PASSED: every rest state slept and the run collected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
