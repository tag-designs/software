#!/usr/bin/env python3
"""Run one tag power experiment end to end and report the measured current.

An experiment is seven steps, matching how power is measured by hand:

1. bring the tag to a known idle state with its clock set;
2. program a configuration and start collection;
3. drop the monitor session, so the tag runs detached;
4. measure supply current for the requested window;
5. reattach and stop;
6. download the data and sanity-check it;
7. erase the tag.

The tag stays physically wired to both the monitor interface and the Joulescope
throughout. "Disconnect" in step 3 therefore means closing the monitor session,
not unplugging anything: each host tool attaches, acts, and detaches on exit, so
no session is held open across the measurement window.

Measurement duration is a parameter rather than a constant, because tags differ
by orders of magnitude in how long they must run to produce a meaningful average.
Configuration is a parameter for the same reason: this script knows nothing about
sample rates, and a per-tag sweep supplies the configurations it cares about.

Examples:
    # idle current, no collection
    power_experiment.py --idle --duration 30

    # 400 Hz for two minutes, using a per-rate configuration
    power_experiment.py --config cfg/imutag-400.json --duration 120

    # long run, keep the downloaded database
    power_experiment.py --config cfg/imutag-100.json --duration 3600 \\
        --output results.csv --keep-download runs/
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field, asdict

#: Tools are located relative to this file so the script works from any cwd.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

#: Default host tool directory produced by the build-host preset.
DEFAULT_BIN = os.path.join(REPO_ROOT, "build-host", "bin")

#: The measurement tool lives beside this script.
MEASURE_TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "joulescope_measure.py")

#: Terminal tag states, from which tag-reset is able to erase.
TERMINAL_STATES = ("FINISHED", "ABORTED")

#: Interpreters tried, in order, when locating one that can import
#: pyjoulescope_driver. The driver is frequently installed in a virtualenv rather
#: than system-wide, so the interpreter running this script often cannot see it.
MEASURE_PYTHON_CANDIDATES = (
    os.environ.get("JOULESCOPE_PYTHON", ""),
    sys.executable,
    os.path.expanduser("~/opt/joulescope-mcp/.venv/bin/python"),
    os.path.expanduser("~/.venvs/joulescope/bin/python"),
    os.path.join(REPO_ROOT, ".venv", "bin", "python"),
)


def resolve_measure_python(explicit: str | None) -> str:
    """Find an interpreter that can import pyjoulescope_driver.

    @details Checked by running the import rather than by inspecting paths, so a
             venv, a pipx install, and a system install are all handled the same
             way.

    @param explicit Interpreter given on the command line, or None to search.
    @return Path to a usable interpreter.
    @raise ExperimentError when no candidate can import the driver, listing what
           was tried so the fix is obvious.
    """
    candidates = [explicit] if explicit else list(MEASURE_PYTHON_CANDIDATES)
    tried = []
    for c in candidates:
        if not c or not os.path.exists(c):
            if c:
                tried.append(f"{c} (not found)")
            continue
        probe = subprocess.run(
            [c, "-c", "import pyjoulescope_driver"],
            capture_output=True, text=True)
        if probe.returncode == 0:
            return c
        tried.append(f"{c} (cannot import pyjoulescope_driver)")
    raise ExperimentError(
        "no interpreter found that can import pyjoulescope_driver. Pass "
        "--measure-python, or set JOULESCOPE_PYTHON. Tried: "
        + "; ".join(tried or ["nothing"]))


class ExperimentError(RuntimeError):
    """A step failed in a way that makes the measurement meaningless."""


@dataclass
class StepResult:
    """Outcome of one shelled-out command."""

    name: str
    argv: list[str]
    returncode: int
    stdout: str
    stderr: str

    @property
    def ok(self) -> bool:
        """True when the command exited zero."""
        return self.returncode == 0


@dataclass
class Measurement:
    """Parsed output of joulescope_measure.py."""

    current_ua: float | None = None
    current_mean_ua: float | None = None
    voltage_v: float | None = None
    blocks: int | None = None
    span_s: float | None = None
    warnings: list[str] = field(default_factory=list)


@dataclass
class Experiment:
    """Everything one run produced, suitable for a CSV row."""

    label: str
    config: str
    duration_s: float
    current_ua: float | None = None
    voltage_v: float | None = None
    external_pages: int | None = None
    internal_pages: int | None = None
    sanity: str = "not checked"
    notes: str = ""


def run(name: str, argv: list[str], timeout: float,
        verbose: bool) -> StepResult:
    """Run a command, capturing output.

    @param name    Human-readable step name for messages.
    @param argv    Command and arguments.
    @param timeout Seconds before the command is killed.
    @param verbose True to echo the command and its output.
    @return Captured result; the caller decides whether a non-zero exit is fatal.
    """
    if verbose:
        print(f"  $ {' '.join(argv)}", flush=True)
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        res = StepResult(name, argv, p.returncode, p.stdout, p.stderr)
    except subprocess.TimeoutExpired:
        res = StepResult(name, argv, -1, "", f"timed out after {timeout} s")
    except FileNotFoundError as e:
        res = StepResult(name, argv, -1, "", str(e))
    if verbose and res.stdout:
        print("".join(f"    {l}\n" for l in res.stdout.splitlines()), end="")
    if res.stderr.strip():
        print(f"    [{name} stderr] {res.stderr.strip()}", file=sys.stderr)
    return res


def tag_state(text: str) -> str | None:
    """Extract the last reported tag state from a tool's output.

    @param text Combined stdout of a tag tool.
    @return State name, or None when no state line was printed.
    """
    state = None
    for line in text.splitlines():
        # tag-start reports a refusal as "Start skipped: tag is FINISHED", which
        # carries the state without a "State:" prefix.
        if "tag is " in line:
            state = line.split("tag is ", 1)[1].strip().rstrip(".")
            continue
        for prefix in ("Final state:", "State:", "state:"):
            if prefix in line:
                state = line.split(prefix, 1)[1].strip()
    return state


def parse_measurement(text: str) -> Measurement:
    """Parse joulescope_measure.py output.

    @param text Combined stdout of the measurement tool.
    @return Parsed values; fields stay None when a line was not found, which the
            caller must treat as a failed measurement rather than a zero.
    """
    m = Measurement()
    for line in text.splitlines():
        s = line.strip()
        try:
            if "(charge/time)" in s:
                m.current_ua = float(s.split(":")[1].split()[0])
            elif "(window mean)" in s or "(block mean)" in s:
                m.current_mean_ua = float(s.split(":")[1].split()[0])
            elif s.startswith("voltage"):
                m.voltage_v = float(s.split(":")[1].split()[0])
            elif s.startswith("blocks=") or s.startswith("windows="):
                for tok in s.split():
                    if tok.startswith(("blocks=", "windows=")):
                        m.blocks = int(tok.split("=")[1])
                if "integrated over" in s:
                    m.span_s = float(s.split("integrated over")[1].split()[0])
            elif "WARNING" in s:
                m.warnings.append(s)
        except (ValueError, IndexError):
            continue
    return m


def to_idle(bin_dir: str, base: str | None, timeout: float,
            verbose: bool) -> str:
    """Step 1: stop, erase, and set the clock, leaving the tag idle.

    @param bin_dir Directory holding the host tools.
    @param base    Optional bus:device selector.
    @param timeout Per-command timeout in seconds.
    @param verbose True to echo commands.
    @return The tag state reported after the reset.
    @raise ExperimentError when the tag does not reach IDLE.
    """
    argv = [os.path.join(bin_dir, "tag-reset"), "--set-rtc"]
    if base:
        argv += ["-b", base]
    res = run("reset", argv, timeout, verbose)
    if not res.ok:
        raise ExperimentError(f"tag-reset failed: {res.stderr.strip()}")
    state = tag_state(res.stdout)
    if state != "IDLE":
        raise ExperimentError(
            f"tag is {state or 'in an unknown state'} after reset, not IDLE. "
            "A tag in FINISHED or ABORTED needs a second reset to erase; one "
            "in EXCEPTION needs attention before it can be measured.")
    return state


def start(bin_dir: str, config: str | None, base: str | None, merge: bool,
          timeout: float, verbose: bool) -> None:
    """Step 2: program a configuration and begin collection immediately.

    @param bin_dir Directory holding the host tools.
    @param config  Protobuf-JSON configuration path, or None to use the tag's.
    @param base    Optional bus:device selector.
    @param merge   True to overlay a partial configuration onto the tag's.
    @param timeout Per-command timeout in seconds.
    @param verbose True to echo commands.
    @raise ExperimentError when the tag does not reach RUNNING.
    """
    argv = [os.path.join(bin_dir, "tag-start"), "--set-rtc", "--start-now"]
    if config:
        argv += ["-c", config]
    if merge:
        argv += ["--merge"]
    if base:
        argv += ["-b", base]
    res = run("start", argv, timeout, verbose)
    if not res.ok:
        raise ExperimentError(f"tag-start failed: {res.stderr.strip()}")
    state = tag_state(res.stdout)
    if state not in ("RUNNING", "CONFIGURED"):
        raise ExperimentError(
            f"tag is {state or 'in an unknown state'} after start, expected "
            "RUNNING or CONFIGURED")


def measure(python: str, duration: float, window: float,
            verbose: bool) -> Measurement:
    """Step 4: measure supply current with no monitor session open.

    @param python   Interpreter that can import pyjoulescope_driver.
    @param duration Measurement window in seconds.
    @param window   Statistics block length in seconds.
    @param verbose  True to echo commands.
    @return Parsed measurement.
    @raise ExperimentError when the tool produced no usable current figure.
    """
    argv = [python, MEASURE_TOOL,
            "--duration", str(duration), "--window", str(window)]
    res = run("measure", argv, duration + 60.0, verbose)
    m = parse_measurement(res.stdout)
    if not res.ok and m.current_ua is None:
        raise ExperimentError(f"measurement failed: {res.stderr.strip()}")
    if m.current_ua is None:
        raise ExperimentError(
            "measurement produced no current figure; is the Joulescope "
            "attached and its current range not off?")
    return m


def stop(bin_dir: str, base: str | None, timeout: float,
         verbose: bool) -> str | None:
    """Step 5: reattach and stop collection.

    @param bin_dir Directory holding the host tools.
    @param base    Optional bus:device selector.
    @param timeout Per-command timeout in seconds.
    @param verbose True to echo commands.
    @return State reported after stopping.
    """
    argv = [os.path.join(bin_dir, "tag-stop")]
    if base:
        argv += ["-b", base]
    res = run("stop", argv, timeout, verbose)
    if not res.ok:
        raise ExperimentError(f"tag-stop failed: {res.stderr.strip()}")
    return tag_state(res.stdout)


def download(bin_dir: str, out_path: str, base: str | None, timeout: float,
             verbose: bool) -> StepResult:
    """Step 6a: download the collected data as SQLite.

    @param bin_dir  Directory holding the host tools.
    @param out_path Destination database path.
    @param base     Optional bus:device selector.
    @param timeout  Command timeout in seconds; downloads can be slow.
    @param verbose  True to echo commands.
    @return The command result, so the caller can report a failed download.
    """
    argv = [os.path.join(bin_dir, "tag-dwnld"), "-f", "sqlite", "-o", out_path]
    if base:
        argv += ["-b", base]
    return run("download", argv, timeout, verbose)


def check_download(db_path: str, duration_s: float, expected_hz: float | None,
                   tolerance: float) -> tuple[str, str]:
    """Step 6b: sanity-check a downloaded database.

    @details Checks that the database exists and is non-trivial, that IMU sample
             timestamps advance monotonically, and, when an expected rate is
             supplied, that the sample count is consistent with the measurement
             window. The rate check is deliberately loose: collection starts
             before the measurement window and stops after it, and warmup
             discards samples, so an exact count is not expected.

    @param db_path      Downloaded SQLite file.
    @param duration_s   Measurement window in seconds.
    @param expected_hz  Nominal sample rate, or None to skip the count check.
    @param tolerance    Fractional tolerance on the expected sample count.
    @return A verdict word and a human-readable detail string.
    """
    if not os.path.exists(db_path):
        return "fail", "no database produced"
    size = os.path.getsize(db_path)
    if size == 0:
        return "fail", "database is empty"

    try:
        import sqlite3
    except ImportError:
        return "unknown", f"sqlite3 unavailable; database is {size} bytes"

    try:
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error as e:
        return "fail", f"cannot open database: {e}"

    try:
        cur = con.cursor()
        cur.execute("SELECT name FROM sqlite_master WHERE type='table'")
        tables = sorted(r[0] for r in cur.fetchall())
        if not tables:
            return "fail", "database has no tables"

        # Pick the largest table with a time-like column as the sample stream.
        best, best_rows, best_timecol = None, -1, None
        for t in tables:
            try:
                cur.execute(f'SELECT COUNT(*) FROM "{t}"')
                rows = cur.fetchone()[0]
            except sqlite3.Error:
                continue
            cur.execute(f'PRAGMA table_info("{t}")')
            cols = [c[1] for c in cur.fetchall()]
            timecol = next((c for c in cols
                            if c.lower() in ("time", "timestamp", "epoch",
                                             "millis", "t", "time_us",
                                             "time_ms")), None)
            if timecol and rows > best_rows:
                best, best_rows, best_timecol = t, rows, timecol

        if best is None:
            return "unknown", f"no time-series table found among {tables}"
        if best_rows == 0:
            return "fail", f"table {best} is empty"

        cur.execute(f'SELECT COUNT(*) FROM (SELECT "{best_timecol}" AS t, '
                    f'LAG("{best_timecol}") OVER (ORDER BY rowid) AS prev '
                    f'FROM "{best}") WHERE prev IS NOT NULL AND t < prev')
        regressions = cur.fetchone()[0]

        detail = f"{best}: {best_rows} rows"
        if regressions:
            return "fail", f"{detail}, {regressions} non-monotonic timestamps"

        if expected_hz:
            expected = expected_hz * duration_s
            ratio = best_rows / expected if expected else 0.0
            detail += f", {ratio:.2f}x expected ({expected:.0f} @ {expected_hz} Hz)"
            if ratio < (1.0 - tolerance):
                return "fail", detail + " -- too few samples"
        return "pass", detail
    except sqlite3.Error as e:
        return "unknown", f"query failed: {e}"
    finally:
        con.close()


def main() -> int:
    """Sequence one experiment and report its result."""
    p = argparse.ArgumentParser(
        description="Run one tag power experiment: idle, start, measure, stop, "
                    "download, erase.")
    p.add_argument("--config", default=None,
                   help="Protobuf-JSON configuration to program. Omit with "
                        "--idle to measure the idle state instead.")
    p.add_argument("--idle", action="store_true",
                   help="Measure the idle state: skip start, stop and download.")
    p.add_argument("--duration", type=float, required=True,
                   help="Measurement window in seconds. Long runs are expected "
                        "for tags whose average needs minutes or hours.")
    p.add_argument("--label", default=None,
                   help="Name for this run in the results row; defaults to the "
                        "configuration basename or 'idle'.")
    p.add_argument("--expected-hz", type=float, default=None,
                   help="Nominal sample rate, used only to sanity-check the "
                        "downloaded sample count.")
    p.add_argument("--tolerance", type=float, default=0.5,
                   help="Fractional shortfall tolerated on the expected sample "
                        "count (default 0.5).")
    p.add_argument("--merge", action="store_true",
                   help="Overlay a partial --config onto the tag's stored "
                        "configuration rather than replacing it.")
    p.add_argument("--window", type=float, default=0.5,
                   help="Measurement statistics block length in seconds.")
    p.add_argument("--settle", type=float, default=2.0,
                   help="Seconds to wait after starting before measuring, so "
                        "warmup and the monitor detach are excluded.")
    p.add_argument("--bin-dir", default=DEFAULT_BIN,
                   help="Directory holding the tag-* host tools.")
    p.add_argument("--measure-python", default=None,
                   help="Interpreter that can import pyjoulescope_driver. "
                        "Defaults to a search of $JOULESCOPE_PYTHON, this "
                        "interpreter, and the usual virtualenv locations.")
    p.add_argument("--base", default=None, help="Monitor selector, bus:device.")
    p.add_argument("--output", default=None,
                   help="Append a CSV row to this file, writing a header when "
                        "the file is new.")
    p.add_argument("--keep-download", default=None, metavar="DIR",
                   help="Keep the downloaded database in DIR instead of a "
                        "temporary directory.")
    p.add_argument("--no-erase", action="store_true",
                   help="Skip the final erase, leaving data on the tag.")
    p.add_argument("--timeout", type=float, default=120.0,
                   help="Per-command timeout in seconds for the tag tools.")
    p.add_argument("--download-timeout", type=float, default=1800.0,
                   help="Timeout for the download step, which can be slow.")
    p.add_argument("-v", "--verbose", action="store_true",
                   help="Echo each command and its output.")
    args = p.parse_args()

    if not args.idle and not args.config:
        p.error("give --config, or --idle to measure the idle state")
    if args.idle and args.config:
        p.error("--idle measures with no collection; do not also pass --config")
    if not os.path.exists(MEASURE_TOOL):
        print(f"measurement tool not found: {MEASURE_TOOL}", file=sys.stderr)
        return 2

    label = args.label or ("idle" if args.idle
                           else os.path.splitext(os.path.basename(args.config))[0])
    exp = Experiment(label=label, config=args.config or "", duration_s=args.duration)

    workdir = args.keep_download or tempfile.mkdtemp(prefix="power-exp-")
    os.makedirs(workdir, exist_ok=True)
    db_path = os.path.join(workdir, f"{label}.db3")

    try:
        # Resolve this first: failing here must not leave the tag reset and
        # erased for a measurement that was never going to run.
        measure_python = resolve_measure_python(args.measure_python)
        if args.verbose:
            print(f"  measurement interpreter: {measure_python}")

        print(f"[1/7] reset to idle, set clock")
        to_idle(args.bin_dir, args.base, args.timeout, args.verbose)

        if args.idle:
            print(f"[2/7] skipped (idle measurement)")
            print(f"[3/7] monitor session closed")
        else:
            print(f"[2/7] start with {args.config}")
            start(args.bin_dir, args.config, args.base, args.merge,
                  args.timeout, args.verbose)
            print(f"[3/7] monitor session closed; settling {args.settle} s")
            time.sleep(args.settle)

        print(f"[4/7] measure {args.duration} s")
        m = measure(measure_python, args.duration, args.window, args.verbose)
        exp.current_ua = m.current_ua
        exp.voltage_v = m.voltage_v
        if m.warnings:
            exp.notes = "; ".join(m.warnings)
        print(f"      {m.current_ua:.4f} uA at {m.voltage_v:.4f} V "
              f"({m.blocks} blocks)")

        if args.idle:
            print(f"[5/7] skipped (idle measurement)")
            print(f"[6/7] skipped (idle measurement)")
            exp.sanity = "n/a (idle)"
        else:
            print(f"[5/7] stop")
            state = stop(args.bin_dir, args.base, args.timeout, args.verbose)
            if state not in TERMINAL_STATES:
                exp.notes = (exp.notes + "; " if exp.notes else "") + \
                    f"unexpected state after stop: {state}"

            print(f"[6/7] download and check")
            dres = download(args.bin_dir, db_path, args.base,
                            args.download_timeout, args.verbose)
            if not dres.ok:
                exp.sanity = "fail"
                exp.notes = (exp.notes + "; " if exp.notes else "") + \
                    "download failed"
            else:
                verdict, detail = check_download(
                    db_path, args.duration, args.expected_hz, args.tolerance)
                exp.sanity = verdict
                print(f"      {verdict}: {detail}")
                if verdict != "pass":
                    exp.notes = (exp.notes + "; " if exp.notes else "") + detail

        if args.no_erase:
            print(f"[7/7] erase skipped")
        else:
            print(f"[7/7] erase")
            to_idle(args.bin_dir, args.base, args.timeout, args.verbose)

    except ExperimentError as e:
        print(f"EXPERIMENT FAILED: {e}", file=sys.stderr)
        exp.notes = (exp.notes + "; " if exp.notes else "") + str(e)
        exp.sanity = "fail"
        _emit(exp, args.output)
        return 1
    finally:
        if not args.keep_download and os.path.isdir(workdir):
            shutil.rmtree(workdir, ignore_errors=True)

    _emit(exp, args.output)
    return 0 if exp.sanity in ("pass", "n/a (idle)") else 1


def _emit(exp: Experiment, output: str | None) -> None:
    """Print the result and optionally append it to a CSV.

    @param exp    Completed experiment record.
    @param output CSV path, or None to print only.
    """
    row = asdict(exp)
    print("\nresult: " + json.dumps(row))
    if not output:
        return
    new = not os.path.exists(output) or os.path.getsize(output) == 0
    with open(output, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(row.keys()))
        if new:
            w.writeheader()
        w.writerow(row)
    print(f"appended to {output}")


if __name__ == "__main__":
    sys.exit(main())
