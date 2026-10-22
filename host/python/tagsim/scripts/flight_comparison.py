#!/usr/bin/env python3
"""Build the flight comparison: simulate, filter, and publish pages and tables.

The comparison in ``design/flight-comparison/`` runs every pattern flight
under three sensor-error conditions, filters each simulated log, and shows a
few flights as side-by-side attitude pages with tables of their scores. This
script does each step, so the folder can be rebuilt from the source logs:

``run``
    For each condition, ``tagsim batch --filter`` over the log directory:
    simulate, ``dataprocessing``'s compass, the navigation filter with and
    without pressure, scores and view files, in ``OUT/<condition>``. Hours
    for all flights; ``--only`` limits it.
``pages``
    ``tagsim compare`` pages, ``<flight>-<condition>.html``, for the chosen
    flights under every condition.
``tables``
    A Markdown table per flight of the smoothed filter's scores under every
    condition, for pasting into the results document.

Three more back figures the results document quotes:

``survey``
    Every message type in a source log, with its count, rate and columns:
    what the log contains.
``spectra``
    Where the power of a flight's ideal specific force and body rate lies
    (Welch spectra from the truth file): the simulated signals' bandwidth.
``deadreckon``
    Horizontal position from the simulated log alone, integrating the
    accelerometer twice from the true start, rotated by the filter's
    smoothed attitude and by the true attitude: how fast it drifts.

The conditions:

``basic``
    tagsim's default errors with the gyro and accelerometer scale,
    misalignment and bias instability set to zero.
``realistic``
    tagsim's default errors.
``miscalibrated``
    the defaults, with a deliberately wrong magnetometer calibration in the
    log (``--calibration perturbed``).

All use seed 0 and tagsim's default sensor settings (100 Hz, +/-4 g,
+/-1000 dps, battery forward). Usage is in
``design/flight-comparison/reproducing.md``.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

from tagsim.cli import main as tagsim
from tagsim.compare import compare_data, write_compare_page

#: Condition name -> (label, extra ``tagsim batch`` arguments).
CONDITIONS = {
    "basic": ("Basic errors", [
        f"--set-error={name}=0" for name in (
            "accel_scale_error", "gyro_scale_error", "accel_misalignment_deg",
            "gyro_misalignment_deg", "accel_bias_gm_mg", "gyro_bias_gm_dps")]),
    "realistic": ("Realistic errors", []),
    "miscalibrated": ("Realistic errors, wrong magnetometer calibration",
                      ["--calibration", "perturbed"]),
}


def run_dirs(args) -> dict[str, Path]:
    """Each condition's batch output directory: ``OUT/<condition>`` unless
    ``--run-dir CONDITION=PATH`` says otherwise."""
    dirs = {c: args.out / c for c in CONDITIONS}
    for item in args.run_dir or []:
        name, _, path = item.partition("=")
        if name not in CONDITIONS or not path:
            sys.exit(f"--run-dir {item!r}: expected CONDITION=PATH, CONDITION one of "
                     f"{', '.join(CONDITIONS)}")
        dirs[name] = Path(path).expanduser()
    return dirs


def cmd_run(args) -> int:
    for name, (_, extra) in CONDITIONS.items():
        out = run_dirs(args)[name]
        print(f"=== {name} -> {out}", flush=True)
        argv = ["batch", "--log-dir", str(args.log_dir), "--out-dir", str(out), "--filter",
                "--seed", str(args.seed)] + extra
        if args.only:
            argv += ["--only", *args.only]
        if tagsim(argv) != 0:
            return 1
    return 0


def cmd_pages(args) -> int:
    args.pages.mkdir(parents=True, exist_ok=True)
    for flight in args.flights:
        for name, d in run_dirs(args).items():
            out = args.pages / f"{flight}-{name}.html"
            data = compare_data(d / f"{flight}.truth.db3", d / f"{flight}-smoothed.score.db3")
            data["title"] = f"{flight}: {CONDITIONS[name][0].lower()}"
            write_compare_page(out, data)
            print(f"{out}: {len(data['t'])} samples at {data['rateHz']:.1f} Hz")
    return 0


def _summary(score: Path) -> dict:
    con = sqlite3.connect(score)
    try:
        return {m: (med, p95, mx) for m, med, p95, mx in con.execute(
            "SELECT Metric, Median, P95, Max FROM ScoreSummary WHERE Phase = 'flight'")}
    finally:
        con.close()


def _flight_facts(truth: Path) -> str:
    con = sqlite3.connect(truth)
    try:
        cfg, repairs, sat = con.execute(
            "SELECT ConfigJson, RepairsJson, SaturationJson FROM SimRun").fetchone()
        n, t = con.execute("SELECT COUNT(*), MAX(ElapsedUs) FROM TruthPose").fetchone()
    finally:
        con.close()
    cfg, repairs, sat = json.loads(cfg), json.loads(repairs or "[]"), json.loads(sat or "{}")
    splits = sum(1 for r in repairs if r["kind"].startswith("split"))
    flying = cfg.get("flying_elapsed_us") or [0.0]
    start = ("on the ground, %.0f s before take-off" % (flying[0] * 1e-6)
             if flying[0] > 0 else "in flight")
    clip = max(sat.get("accel") or [0.0])
    return (f"{t * 1e-6 / 60:.1f} min simulated, starting {start}; "
            f"{len(repairs)} glitch repairs ({splits} splits); "
            f"accelerometer at full scale on {100 * clip:.1f} % of samples on its worst axis")


def cmd_tables(args) -> int:
    dirs = run_dirs(args)
    for flight in args.flights:
        print(f"#### {flight}\n")
        print(_flight_facts(dirs["realistic"] / f"{flight}.truth.db3") + ".\n")
        print("| Condition | Attitude, median / 95 % / max | Tilt, median | Heading, median "
              "| Altitude, median / max | Page |")
        print("| --- | --- | --- | --- | --- | --- |")
        for name, d in dirs.items():
            s = _summary(d / f"{flight}-smoothed.score.db3")
            tot, tilt, head, alt = (s["total_deg"], s["tilt_deg"], s["heading_deg"],
                                    s["altitude_m"])
            print(f"| {CONDITIONS[name][0]} | {tot[0]:.2f}° / {tot[1]:.2f}° / {tot[2]:.2f}° "
                  f"| {tilt[0]:.2f}° | {head[0]:.2f}° "
                  f"| {100 * alt[0]:.1f} cm / {100 * alt[2]:.1f} cm "
                  f"| [{flight}-{name}](pages/{flight}-{name}.html) |")
        print()
    return 0


def cmd_survey(args) -> int:
    import numpy as np

    from tagsim.dataflash import DataFlashLog

    log = DataFlashLog(args.log)
    rows = []
    for f in log.formats.values():
        if not log.has(f.name):
            continue
        if "TimeUS" not in f.columns:
            rows.append((f.name, None, None, ""))
            continue
        t = log.messages(f.name)["TimeUS"] * 1e-6
        rate = (len(t) - 1) / (t.max() - t.min()) if len(t) > 1 and t.max() > t.min() else None
        rows.append((f.name, len(t), rate, ",".join(c for c in f.columns if c != "TimeUS")))
    print("| Message | Count | Rate, Hz | Columns |\n| --- | --- | --- | --- |")
    for name, n, rate, cols in sorted(rows, key=lambda r: (-(r[2] or 0), r[0])):
        print(f"| {name} | {n if n is not None else '–'} | "
              f"{f'{rate:.1f}' if rate else '–'} | {cols} |")
    return 0


def cmd_spectra(args) -> int:
    import numpy as np
    from scipy.signal import welch

    con = sqlite3.connect(args.truth)
    try:
        k = np.array(con.execute("SELECT ax, ay, az, gx, gy, gz FROM TruthKinematics "
                                 "WHERE SegmentId = 0 ORDER BY SampleIndex").fetchall())
        t = np.array(con.execute("SELECT ElapsedUs FROM TruthKinematics WHERE SegmentId = 0 "
                                 "ORDER BY SampleIndex").fetchall())[:, 0] * 1e-6
    finally:
        con.close()
    fs = 1.0 / np.median(np.diff(t))
    for name, cols in (("specific force", slice(0, 3)), ("body rate", slice(3, 6))):
        x = k[:, cols] - k[:, cols].mean(axis=0)
        f, p = welch(x, fs=fs, nperseg=4096, axis=0)
        p = p.sum(axis=1)
        c = np.cumsum(p) / p.sum()
        ref = p[np.searchsorted(f, 1.0)]
        rel = {hz: 10 * np.log10(p[np.searchsorted(f, hz)] / ref) for hz in (5, 10, 12.5, 20)}
        print(f"{name}: 99 % of power below {f[np.searchsorted(c, 0.99)]:.2f} Hz, "
              f"99.9 % below {f[np.searchsorted(c, 0.999)]:.2f} Hz; PSD relative to 1 Hz: "
              + ", ".join(f"{hz:g} Hz {db:.1f} dB" for hz, db in rel.items()))
    return 0


def cmd_deadreckon(args) -> int:
    import numpy as np

    from tagsim.navfilter import read_log
    from tagsim.rotations import qrotate

    g0 = 9.80665
    d = args.run
    print("| Flight | Minutes | Attitude | After 10 s | After 60 s | At the end |\n"
          "| --- | --- | --- | --- | --- | --- |")
    for flight in args.flights:
        log = read_log(d / f"{flight}.db3")[0]
        con = sqlite3.connect(d / f"{flight}.truth.db3")
        tr = np.array(con.execute("SELECT e, n, u, ve, vn, vu, qw, qx, qy, qz FROM TruthPose "
                                  "WHERE SegmentId = 0 ORDER BY SampleIndex").fetchall())
        con.close()
        con = sqlite3.connect(d / f"{flight}-filt.db3")
        qf = np.array(con.execute("SELECT qw, qx, qy, qz FROM FilterSmoothed "
                                  "WHERE SegmentId = 0 ORDER BY SampleIndex").fetchall())
        con.close()
        t = log.t_imu
        dt = np.diff(t)[:, None]
        for label, q in (("filter", qf), ("true", tr[:, 6:])):
            a = qrotate(q, log.accel_mps2)
            a[:, 2] -= g0
            v = tr[0, 3:6] + np.vstack([np.zeros(3), np.cumsum(0.5 * (a[1:] + a[:-1]) * dt, 0)])
            p = tr[0, :3] + np.vstack([np.zeros(3), np.cumsum(0.5 * (v[1:] + v[:-1]) * dt, 0)])
            err = np.linalg.norm(p[:, :2] - tr[:, :2], axis=1)

            def at(s):
                return err[min(len(t) - 1, np.searchsorted(t - t[0], s))]

            print(f"| {flight} | {(t[-1] - t[0]) / 60:.1f} | {label} | {at(10):.1f} m "
                  f"| {at(60):.0f} m | {err[-1] / 1000:.2f} km |")
    return 0


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--out", type=Path, default=Path("~/Documents/tagsim-output/comparison"),
                   help="parent of the per-condition batch directories "
                        "(default ~/Documents/tagsim-output/comparison)")
    p.add_argument("--run-dir", action="append", metavar="CONDITION=PATH",
                   help="use PATH for one condition's batch directory; repeatable")
    sub = p.add_subparsers(dest="command", required=True)

    r = sub.add_parser("run", help="simulate and filter every flight under every condition")
    r.add_argument("--log-dir", type=Path, required=True, help="the ArduPilot .bin logs")
    r.add_argument("--only", nargs="+", metavar="NAME", help="only these flights (f173 …)")
    r.add_argument("--seed", type=int, default=0)
    r.set_defaults(func=cmd_run)

    g = sub.add_parser("pages", help="write the comparison pages")
    g.add_argument("--pages", type=Path, required=True, help="directory to write them in")
    g.add_argument("flights", nargs="+", metavar="FLIGHT", help="e.g. f173 f177")
    g.set_defaults(func=cmd_pages)

    t = sub.add_parser("tables", help="print the per-flight tables as Markdown")
    t.add_argument("flights", nargs="+", metavar="FLIGHT")
    t.set_defaults(func=cmd_tables)

    s = sub.add_parser("survey", help="list a source log's message types and rates")
    s.add_argument("log", type=Path, help="an ArduPilot .bin log")
    s.set_defaults(func=cmd_survey)

    sp = sub.add_parser("spectra", help="bandwidth of a flight's ideal signals")
    sp.add_argument("truth", type=Path, help="NAME.truth.db3")
    sp.set_defaults(func=cmd_spectra)

    dr = sub.add_parser("deadreckon", help="horizontal drift of double-integrated acceleration")
    dr.add_argument("--run", type=Path, required=True,
                    help="a batch directory with NAME.db3, NAME.truth.db3 and NAME-filt.db3")
    dr.add_argument("flights", nargs="+", metavar="FLIGHT")
    dr.set_defaults(func=cmd_deadreckon)
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    args.out = args.out.expanduser()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
