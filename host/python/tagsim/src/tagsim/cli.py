"""Command line: ``tagsim run``, ``filter``, ``score``, ``view`` and ``batch``."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

from . import __version__
from .ardupilot import GlitchConfig, load_flight
from .consistency import strapdown
from .errors import ErrorConfig
from .sensors import SensorConfig
from .trajectory import FitConfig
from .simulate import (download_header, run_import, simulate, synthetic_uuid, write_download,
                       write_truth)
from .wmm import earth_field


def error_config(args: argparse.Namespace) -> ErrorConfig:
    """The sensor error parameters from ``--ideal``, ``--calibration``,
    ``--sea-level-walk`` and any ``--set-error NAME=VALUE`` overrides."""
    base = ErrorConfig.ideal() if args.ideal else ErrorConfig()
    values = base.as_dict()
    values["calibration"] = args.calibration
    values["sea_level_walk_hpa_rth"] = args.sea_level_walk
    for item in args.set_error or []:
        name, _, text = item.partition("=")
        if name not in values or not text:
            raise ValueError(f"--set-error {item!r}: expected NAME=VALUE, NAME one of "
                             f"{', '.join(sorted(values))}")
        try:
            value = json.loads(text)
        except json.JSONDecodeError:
            value = text
        values[name] = tuple(value) if isinstance(value, list) else value
    return ErrorConfig(**values)


def saturation_lines(saturation: dict) -> list[str]:
    """The per-axis saturation summary, one line per sensor."""
    return [f"saturation {name}: " + "  ".join(f"{axis} {100 * f:.3f} %"
                                                for axis, f in zip("xyz", fracs))
            for name, fracs in saturation.items()]


def truth_path(db: Path) -> Path:
    """``name.db3`` -> ``name.truth.db3``."""
    return db.with_name(db.stem + ".truth" + db.suffix)


def cmd_run(args: argparse.Namespace) -> int:
    glitch = GlitchConfig(max_accel_g=args.max_accel_g, max_rate_dps=args.max_rate_dps,
                          vel_tol_mps=args.vel_tol, att_tol_deg=args.att_tol,
                          bridge_s=args.bridge_s)
    errors = error_config(args)
    flight = load_flight(args.log, glitch, pad_s=args.pad)
    fit = FitConfig(pos_sigma_m=args.pos_sigma, vel_sigma_mps=args.vel_sigma,
                    jerk_sigma=args.jerk_sigma)
    traj, residuals = flight.trajectory(fit=fit)
    start_unix = traj.t_start + flight.gps_offset_s
    field = earth_field(*flight.origin, start_unix)
    mag_ned = tuple(args.mag_ned) if args.mag_ned else field.ned_ut
    cfg = SensorConfig(odr_hz=args.odr, accel_range_g=args.accel_range,
                       gyro_range_dps=args.gyro_range, mounting=args.mounting,
                       mag_ned_ut=mag_ned, sea_level_hpa=args.sea_level_hpa)
    sim = simulate(traj, cfg, start_unix=start_unix, duration_s=args.duration,
                   origin_alt_m=flight.origin[2], errors=errors, seed=args.seed)

    out: Path = args.output
    jsonl = out.with_suffix(".jsonl")
    uuid = synthetic_uuid(args.seed, flight.sha256)
    write_download(jsonl, download_header(sim, uuid, args.ppm), sim.pages)
    report = run_import(jsonl, out, {"simulator": f"tagsim {__version__}",
                                     "tagsim_source": flight.path.name})
    if not args.keep_jsonl:
        jsonl.unlink()

    sig = sim.signals
    consistency = strapdown(sig.t, sig.q_enu_tag, sig.pos_enu, sig.vel_enu,
                            sig.accel_mg, sig.gyro_dps)
    config = {"sensor": cfg.as_dict(), "glitch": vars(glitch), "pad_s": args.pad,
              "duration_s": args.duration, "ppm_clock_error": args.ppm, "uuid": uuid,
              "fit": fit.as_dict(), "fit_residuals": residuals,
              "trajectory": {"t0": traj.t0, "dt": traj.dt, "t_first": float(sig.t[0])},
              "origin": flight.origin, "gps_offset_s": flight.gps_offset_s,
              "wmm": field.as_dict(), "mag_ned_source": "argument" if args.mag_ned else "wmm",
              "flying_elapsed_us": [(f - float(sig.t[0])) * 1e6 for f in flight.flying]}
    write_truth(truth_path(out), sim, args.seed, config,
                {"name": flight.path.name, "sha256": flight.sha256},
                flight.repairs_as_dicts(), consistency)

    print(report)
    print(f"{len(sim.imu)} samples at {cfg.odr_hz} Hz ({len(sim.imu) / cfg.odr_hz:.1f} s), "
          f"{len(sim.pages)} pages; truth in {truth_path(out)}")
    for r in flight.repairs:
        print(f"  repair: {r.kind} {r.t_start:.3f}-{r.t_end:.3f} s ({r.samples} samples): {r.reason}")
    print(f"field: {field.model} at {field.decimal_year:.3f}: {field.total_ut:.2f} uT, "
          f"dip {field.inclination_deg:.2f} deg, declination {field.declination_deg:.2f} deg"
          + (" (overridden by --mag-ned)" if args.mag_ned else ""))
    print(f"errors: {'none (--ideal)' if args.ideal else 'datasheet'}, seed {args.seed}, "
          f"calibration table {errors.calibration}")
    for line in saturation_lines(sim.saturation):
        print(line)
    print("fit: " + json.dumps(residuals))
    print("consistency: " + json.dumps(consistency))
    return 0


def score_path(db: Path) -> Path:
    """``name.db3`` -> ``name.score.db3``."""
    return db.with_name(db.stem + ".score" + db.suffix)


def view_path(db: Path) -> Path:
    """``name.db3`` -> ``name.view.bin``, the UAV Log Viewer file."""
    return db.with_name(db.stem + ".view.bin")


def cmd_score(args: argparse.Namespace) -> int:
    from .score import enu_estimate, imu_compass_estimate, load_truth, score, summary_lines, \
        write_score

    truth = load_truth(args.truth)
    if args.table:
        est = enu_estimate(args.estimate, args.table)
    else:
        est = imu_compass_estimate(args.estimate, truth)
    result = score(truth, est)
    write_score(args.output, result, args.truth, args.estimate)
    if est.decode_residual is not None:
        bad = int((est.decode_residual > 1e-5).sum())
        print(f"{est.name}: {len(est.elapsed_us)} rows; decoded the implied attitude of all but "
              f"{bad}")
    for line in summary_lines(result):
        print(line)
    print(f"series and summary in {args.output}")
    return 0


def cmd_view(args: argparse.Namespace) -> int:
    import sqlite3

    from .score import load_truth
    from .viewer import write_viewer_log

    truth = load_truth(args.truth)
    cfg = truth.config
    con = sqlite3.connect(args.truth)
    pv = np.array(con.execute("SELECT e, n, u, ve, vn, vu FROM TruthPose WHERE SegmentId = 0 "
                              "ORDER BY SampleIndex").fetchall())
    pos, vel = pv[:, :3], pv[:, 3:]
    start_unix = con.execute("SELECT StartUnix FROM SimRun").fetchone()[0]
    con.close()
    t0_us = cfg["trajectory"]["t_first"] * 1e6
    est_t = q_est = est_pos = None
    if args.score:
        rows = np.array(sqlite3.connect(args.score).execute(
            "SELECT ElapsedUs, qw, qx, qy, qz, AltitudeErrM FROM ScoreSeries "
            "ORDER BY ElapsedUs").fetchall(), dtype=np.float64)
        est_t = rows[:, 0] + t0_us
        q_est = rows[:, 1:5]
        est_pos = np.stack([np.interp(rows[:, 0], truth.elapsed_us, pos[:, k])
                            for k in range(3)], -1)
        if args.estimate_main:
            # The estimate as the main track (POS, ATT, XKF1), the one
            # FlightCoach reads: its attitude, and its altitude (truth plus
            # the scored error) over the true horizontal path, which the
            # filter does not estimate. The truth goes to AHR2.
            alt_err = rows[:, 5]
            if np.isfinite(alt_err).any():
                est_pos[:, 2] += np.nan_to_num(alt_err)
            ok = np.all(np.isfinite(q_est), axis=1)
            n = write_viewer_log(args.output, est_t[ok], q_est[ok], est_pos[ok],
                                 tuple(cfg["origin"]), start_unix, cfg["sensor"]["mounting"],
                                 truth.elapsed_us + t0_us, truth.q_enu_tag, pos,
                                 label="estimate")
            print(f"{args.output}: {n} estimate records (POS, ATT, XKF1), "
                  f"{len(pos)} truth records (AHR2)")
            return 0
    if args.estimate_main and not args.score:
        raise ValueError("--estimate-main needs --score")
    if args.strapdown:
        # The integrated path is the main track (POS, ATT, XKF1), the one
        # FlightCoach reads; the truth goes to AHR2.
        from .consistency import strapdown_path

        kin = np.array(sqlite3.connect(args.truth).execute(
            "SELECT ax, ay, az, gx, gy, gz FROM TruthKinematics WHERE SegmentId = 0 "
            "ORDER BY SampleIndex").fetchall())
        q_true = truth.q_enu_tag
        q_sd, v_sd, p_sd = strapdown_path(truth.elapsed_us * 1e-6, q_true[0], pos[0], vel[0],
                                          kin[:, :3], kin[:, 3:], q_true)
        t_us = truth.elapsed_us + t0_us
        n = write_viewer_log(args.output, t_us, q_sd, p_sd, tuple(cfg["origin"]), start_unix,
                             cfg["sensor"]["mounting"], t_us, q_true, pos, label="strapdown",
                             vel_enu=v_sd)
        print(f"{args.output}: {n} strapdown records (POS, ATT, XKF1), "
              f"{len(q_true)} truth records (AHR2)")
        return 0
    n = write_viewer_log(args.output, truth.elapsed_us + t0_us, truth.q_enu_tag, pos,
                         tuple(cfg["origin"]), start_unix, cfg["sensor"]["mounting"],
                         est_t, q_est, est_pos, vel_enu=vel)
    print(f"{args.output}: {n} truth records (POS, ATT, XKF1)"
          + (f", {len(q_est)} estimate records (AHR2)" if q_est is not None else ""))
    return 0


def cmd_compare(args: argparse.Namespace) -> int:
    from .compare import compare_data, write_compare_page

    data = compare_data(args.truth, args.score, args.rate)
    write_compare_page(args.output, data)
    print(f"{args.output}: {len(data['t'])} samples at {data['rateHz']:.1f} Hz, "
          f"{data['t'][-1]:.1f} s ({data['estimate']})")
    return 0


def filter_config(args: argparse.Namespace, epoch_unix: float):
    """The navigation filter's configuration from ``tagsim filter``'s options.

    The Earth field comes from ``--field-ned``, else the WMM at ``--site``
    on the log's date, else the field a tagsim truth file (``--truth``) was
    generated with."""
    from .navfilter import FilterConfig, field_enu_from_ned
    from .score import load_truth

    if args.field_ned:
        ned = args.field_ned
    elif args.site:
        ned = earth_field(*args.site, epoch_unix).ned_ut
    elif args.truth:
        ned = load_truth(args.truth).field_ned_ut
    else:
        raise ValueError("give the Earth field: --field-ned, --site or --truth")
    values = FilterConfig(field_enu_ut=field_enu_from_ned(ned),
                          use_pressure=not args.no_pressure,
                          smooth=not args.no_smooth).as_dict()
    for item in args.set or []:
        name, _, text = item.partition("=")
        if name not in values or not text:
            raise ValueError(f"--set {item!r}: expected NAME=VALUE, NAME one of "
                             f"{', '.join(sorted(values))}")
        value = json.loads(text)
        values[name] = tuple(value) if isinstance(value, list) else value
    return FilterConfig(**values)


def cmd_filter(args: argparse.Namespace) -> int:
    import time

    from .navfilter import read_log, run_filter, write_estimates

    logs = read_log(args.log)
    if not logs:
        raise ValueError(f"{args.log}: no IMU samples")
    cfg = filter_config(args, logs[0].epoch_unix)
    results = []
    for log in logs:
        start = time.perf_counter()
        r = run_filter(log, cfg)
        s = r.stats
        print(f"segment {log.segment_id}: {len(log.t_imu)} IMU samples at {log.odr_hz:g} Hz, "
              f"{s['mag_updates']} mag updates ({s['mag_gated']} gated), "
              f"{s['pressure_updates']} pressure; still at start: {s['start_still']}; "
              f"gyro bias {np.round(s['gyro_bias_final_dps'], 3).tolist()} dps; "
              f"{time.perf_counter() - start:.1f} s")
        results.append(r)
    write_estimates(args.output, results, cfg, args.log)
    print(f"FilterForward and FilterSmoothed in {args.output}")
    return 0


def dataprocessing_binary() -> str:
    """``dataprocessing``: ``$DATAPROCESSING_BIN``, else the one on PATH."""
    found = os.environ.get("DATAPROCESSING_BIN") or shutil.which("dataprocessing")
    if not found:
        raise FileNotFoundError("dataprocessing not found; set DATAPROCESSING_BIN")
    return found


def _score_row(name: str, source: str, estimate: str, truth, result) -> dict:
    row = {"flight": name, "source": source, "estimate": estimate, "status": "ok",
           "duration_s": round(len(truth.elapsed_us) / truth.config["sensor"]["odr_hz"], 1)}
    for phase in ("ground", "flight"):
        for metric in ("total_deg", "tilt_deg", "heading_deg", "altitude_m", "vz_mps"):
            s = result.summary[phase].get(metric)
            if s is None:
                continue
            for stat in ("median", "p95", "max"):
                row[f"{phase}_{metric}_{stat}"] = None if s[stat] is None else round(s[stat], 3)
    return row


#: Estimates ``tagsim batch --filter`` scores: (label, file suffix, table).
FILTER_ESTIMATES = (("forward", "-filt", "FilterForward"),
                    ("smoothed", "-filt", "FilterSmoothed"),
                    ("forward-nopressure", "-filt-nop", "FilterForward"),
                    ("smoothed-nopressure", "-filt-nop", "FilterSmoothed"))


def cmd_batch(args: argparse.Namespace) -> int:
    """Every log in a directory: run, dataprocessing, score, view, and a table;
    with ``--filter`` also the navigation filter, with and without pressure."""
    from .score import enu_estimate, imu_compass_estimate, load_truth, score, write_score

    dp = dataprocessing_binary()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    logs = sorted(p for p in args.log_dir.iterdir() if p.suffix.lower() == ".bin")
    if args.only:
        logs = [p for p in logs if p.stem in args.only
                or (p.stem.isdigit() and f"f{int(p.stem)}" in args.only)]
    run_extra = ["--calibration", args.calibration]
    for item in args.set_error or []:
        run_extra += ["--set-error", item]
    filter_extra = []
    for item in args.filter_set or []:
        filter_extra += ["--set", item]
    table = []
    for log in logs:
        name = "f" + (str(int(log.stem)) if log.stem.isdigit() else log.stem)
        db = args.out_dir / f"{name}.db3"
        proc = args.out_dir / f"{name}-proc.db3"
        print(f"== {log.name} -> {db.name}", flush=True)
        if args.reuse and not (db.exists() and truth_path(db).exists() and proc.exists()):
            table.append({"flight": name, "source": log.name, "status": "not usable"})
            continue
        if not args.reuse:
            if main(["run", "--log", str(log), "-o", str(db), "--seed", str(args.seed)]
                    + run_extra) != 0:
                table.append({"flight": name, "source": log.name, "status": "not usable"})
                continue
            done = subprocess.run([dp, "-i", str(db), "-o", str(proc), "--if-exists", "replace",
                                   "-p", "imu-compass-orientation"], capture_output=True,
                                  text=True)
            if done.returncode != 0:
                raise RuntimeError(f"dataprocessing failed on {db}: {done.stderr.strip()}")
        truth = load_truth(truth_path(db))
        result = score(truth, imu_compass_estimate(proc, truth))
        write_score(score_path(db), result, truth_path(db), proc)
        main(["view", "--truth", str(truth_path(db)), "--score", str(score_path(db)),
              "-o", str(view_path(db))])
        table.append(_score_row(name, log.name, "compass", truth, result))
        if not args.filter:
            continue
        for suffix, extra in (("-filt", []), ("-filt-nop", ["--no-pressure"])):
            out = args.out_dir / f"{name}{suffix}.db3"
            if main(["filter", "--log", str(db), "-o", str(out), "--truth",
                     str(truth_path(db))] + extra + filter_extra) != 0:
                raise RuntimeError(f"filter failed on {db}")
        for label, suffix, tbl in FILTER_ESTIMATES:
            est_db = args.out_dir / f"{name}{suffix}.db3"
            result = score(truth, enu_estimate(est_db, tbl))
            write_score(args.out_dir / f"{name}-{label}.score.db3", result, truth_path(db), est_db)
            table.append(_score_row(name, log.name, label, truth, result))
        main(["view", "--truth", str(truth_path(db)), "--score",
              str(args.out_dir / f"{name}-smoothed.score.db3"),
              "-o", str(args.out_dir / f"{name}-smoothed.view.bin")])
    keys = sorted({k for r in table for k in r},
                  key=lambda k: (k not in ("flight", "source", "estimate", "status"), k))
    summary = args.out_dir / "scores.csv"
    with open(summary, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(table)
    print(f"summary in {summary}")
    for r in table:
        if r["status"] != "ok":
            print(f"{r['flight']:6} {r['status']}")
            continue
        print(f"{r['flight']:6} {r['estimate']:20} ground tilt {r['ground_tilt_deg_median']} / "
              f"{r['ground_tilt_deg_p95']}, heading {r['ground_heading_deg_median']} / "
              f"{r['ground_heading_deg_p95']};  flight tilt {r['flight_tilt_deg_median']} / "
              f"{r['flight_tilt_deg_p95']}, heading {r['flight_heading_deg_median']} / "
              f"{r['flight_heading_deg_p95']}"
              + (f"; altitude {r['flight_altitude_m_median']} m, vz {r['flight_vz_mps_median']} m/s"
                 if r.get("flight_altitude_m_median") is not None else ""))
    return 0


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="tagsim", description=__doc__)
    p.add_argument("--version", action="version", version=f"tagsim {__version__}")
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run", help="replay a recorded ArduPilot flight into an IMUTag log")
    r.add_argument("--log", type=Path, required=True, help="ArduPilot DataFlash .bin log")
    r.add_argument("-o", "--output", type=Path, required=True,
                   help="SQLite log to write; the truth goes to NAME.truth.db3")
    r.add_argument("--odr", type=int, default=100, help="IMU rate, Hz (default 100)")
    r.add_argument("--accel-range", type=int, default=4, help="accel full scale, g (default 4)")
    r.add_argument("--gyro-range", type=int, default=1000,
                   help="gyro full scale, dps (default 1000)")
    r.add_argument("--mounting", default="RFU",
                   help="body direction of tag x, y, z (default RFU: flat, battery forward)")
    r.add_argument("--mag-ned", type=float, nargs=3, metavar=("N", "E", "D"),
                   help="Earth field, uT, in place of the WMM value at the log's origin and date")
    r.add_argument("--sea-level-hpa", type=float, default=1013.25)
    r.add_argument("--sea-level-walk", type=float, default=0.0, metavar="HPA_PER_SQRT_H",
                   help="sea-level pressure random walk (default 0: constant)")
    r.add_argument("--ideal", action="store_true",
                   help="no sensor errors: no noise, bias, hard or soft iron, offsets")
    r.add_argument("--calibration", choices=("true", "perturbed"), default="true",
                   help="Calibration table: the true hard and soft iron, or perturbed ones")
    r.add_argument("--set-error", action="append", metavar="NAME=VALUE",
                   help="override one ErrorConfig parameter (JSON value), e.g. "
                        "accel_bias_mg=0 or mag_noise_ut=[0.1,0.1,0.2]; repeatable")
    r.add_argument("--ppm", type=float, default=0.0,
                   help="TagInfo.ppm_clock_error to report (no clock error is simulated)")
    r.add_argument("--pad", type=float, default=10.0,
                   help="seconds kept either side of the flying window (default 10)")
    r.add_argument("--duration", type=float, help="stop after this many seconds")
    r.add_argument("--seed", type=int, default=0)
    r.add_argument("--keep-jsonl", action="store_true", help="keep the intermediate download")
    r.add_argument("--max-accel-g", type=float, default=15.0)
    r.add_argument("--max-rate-dps", type=float, default=1000.0)
    r.add_argument("--vel-tol", type=float, default=5.0, help="core disagreement, m/s")
    r.add_argument("--att-tol", type=float, default=10.0, help="core disagreement, degrees")
    r.add_argument("--bridge-s", type=float, default=0.5)
    r.add_argument("--pos-sigma", type=float, default=5.0, help="position fit weight, m")
    r.add_argument("--vel-sigma", type=float, default=0.1, help="velocity fit weight, m/s")
    r.add_argument("--jerk-sigma", type=float, default=100.0, help="jerk penalty, m/s^3")
    r.set_defaults(func=cmd_run)

    fl = sub.add_parser("filter", help="estimate attitude, altitude and vertical velocity "
                                       "from an IMUTag log (forward EKF and RTS smoother)")
    fl.add_argument("--log", type=Path, required=True, help="decoded IMUTag SQLite log")
    fl.add_argument("-o", "--output", type=Path, required=True,
                    help="file to write FilterForward, FilterSmoothed and FilterRun to")
    fl.add_argument("--field-ned", type=float, nargs=3, metavar=("N", "E", "D"),
                    help="Earth field, uT")
    fl.add_argument("--site", type=float, nargs=3, metavar=("LAT", "LON", "ALT"),
                    help="WMM field at this site (degrees, m) on the log's date")
    fl.add_argument("--truth", type=Path,
                    help="use the field a tagsim truth file was generated with")
    fl.add_argument("--no-pressure", action="store_true",
                    help="attitude and gyro bias only: no altitude states or pressure updates")
    fl.add_argument("--no-smooth", action="store_true", help="forward pass only")
    fl.add_argument("--set", action="append", metavar="NAME=VALUE",
                    help="override one FilterConfig parameter (JSON value); repeatable")
    fl.set_defaults(func=cmd_filter)

    sc = sub.add_parser("score", help="score an orientation estimate against a truth file")
    sc.add_argument("--truth", type=Path, required=True, help="NAME.truth.db3")
    sc.add_argument("--estimate", type=Path, required=True,
                    help="SQLite file holding the estimate (default: dataprocessing's "
                         "ImuCompassOrientation)")
    sc.add_argument("--table", help="score this table instead, in tagsim's convention: "
                                    "ElapsedUs, qw qx qy qz (tag frame to ENU), optional ve vn vu, "
                                    "alt (m) and vz (m/s), e.g. FilterSmoothed")
    sc.add_argument("-o", "--output", type=Path, required=True, help="score file to write")
    sc.set_defaults(func=cmd_score)

    v = sub.add_parser("view", help="write a DataFlash .bin of truth (and an estimate) "
                                    "for UAV Log Viewer")
    v.add_argument("--truth", type=Path, required=True, help="NAME.truth.db3")
    est = v.add_mutually_exclusive_group()
    est.add_argument("--score", type=Path, help="score file whose estimate to add as AHR2")
    est.add_argument("--strapdown", action="store_true",
                     help="write the consistency check's path as the main track (POS, ATT, "
                          "XKF1) and the truth as AHR2: the ideal signals integrated over the "
                          "whole run from the true start (attitude from the gyro, position "
                          "with the true attitude)")
    v.add_argument("--estimate-main", action="store_true",
                   help="with --score: write the estimate as the main track (POS, ATT, XKF1; "
                        "its attitude and altitude over the true horizontal path) and the "
                        "truth as AHR2, for viewers that show only the main track")
    v.add_argument("-o", "--output", type=Path, required=True, help=".bin to write")
    v.set_defaults(func=cmd_view)

    c = sub.add_parser("compare", help="write an HTML page of true and estimated attitude "
                                       "and altitude side by side")
    c.add_argument("--truth", type=Path, required=True, help="NAME.truth.db3")
    c.add_argument("--score", type=Path, required=True,
                   help="score file whose estimate to show (e.g. NAME-smoothed.score.db3)")
    c.add_argument("--rate", type=float,
                   help="display rate, Hz (default: the source flight log's rate, ~25 Hz)")
    c.add_argument("-o", "--output", type=Path, required=True, help=".html to write")
    c.set_defaults(func=cmd_compare)

    b = sub.add_parser("batch", help="run, process, score and export every log in a directory")
    b.add_argument("--log-dir", type=Path, required=True)
    b.add_argument("--out-dir", type=Path, required=True)
    b.add_argument("--seed", type=int, default=0)
    b.add_argument("--filter", action="store_true",
                   help="also run and score the navigation filter, with and without pressure")
    b.add_argument("--reuse", action="store_true",
                   help="reuse NAME.db3, its truth and NAME-proc.db3 instead of regenerating")
    b.add_argument("--only", nargs="+", metavar="NAME",
                   help="only these logs, by stem or flight name (00000173 or f173)")
    b.add_argument("--calibration", choices=("true", "perturbed"), default="true",
                   help="passed to tagsim run")
    b.add_argument("--set-error", action="append", metavar="NAME=VALUE",
                   help="passed to tagsim run; repeatable")
    b.add_argument("--filter-set", action="append", metavar="NAME=VALUE",
                   help="passed to tagsim filter as --set; repeatable")
    b.set_defaults(func=cmd_batch)
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.func(args)
    except (ValueError, FileNotFoundError, RuntimeError) as e:
        print(f"tagsim: {e}", file=sys.stderr)
        return 1
