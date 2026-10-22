"""Command line: ``tagsim run --log X.BIN -o out.db3``."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from .ardupilot import GlitchConfig, load_flight
from .consistency import strapdown
from .sensors import SensorConfig
from .trajectory import FitConfig
from .simulate import (download_header, run_import, simulate, synthetic_uuid, write_download,
                       write_truth)


def truth_path(db: Path) -> Path:
    """``name.db3`` -> ``name.truth.db3``."""
    return db.with_name(db.stem + ".truth" + db.suffix)


def cmd_run(args: argparse.Namespace) -> int:
    glitch = GlitchConfig(max_accel_g=args.max_accel_g, max_rate_dps=args.max_rate_dps,
                          vel_tol_mps=args.vel_tol, att_tol_deg=args.att_tol,
                          bridge_s=args.bridge_s)
    cfg = SensorConfig(odr_hz=args.odr, accel_range_g=args.accel_range,
                       gyro_range_dps=args.gyro_range, mounting=args.mounting,
                       mag_ned_ut=tuple(args.mag_ned), sea_level_hpa=args.sea_level_hpa)
    flight = load_flight(args.log, glitch, pad_s=args.pad)
    fit = FitConfig(pos_sigma_m=args.pos_sigma, vel_sigma_mps=args.vel_sigma,
                    jerk_sigma=args.jerk_sigma)
    traj, residuals = flight.trajectory(fit=fit)
    sim = simulate(traj, cfg, start_unix=traj.t_start + flight.gps_offset_s,
                   duration_s=args.duration, origin_alt_m=flight.origin[2])

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
              "origin": flight.origin, "gps_offset_s": flight.gps_offset_s}
    write_truth(truth_path(out), sim, args.seed, config,
                {"name": flight.path.name, "sha256": flight.sha256},
                flight.repairs_as_dicts(), consistency)

    print(report)
    print(f"{len(sim.imu)} samples at {cfg.odr_hz} Hz ({len(sim.imu) / cfg.odr_hz:.1f} s), "
          f"{len(sim.pages)} pages; truth in {truth_path(out)}")
    for r in flight.repairs:
        print(f"  repair: {r.kind} {r.t_start:.3f}-{r.t_end:.3f} s ({r.samples} samples): {r.reason}")
    print("fit: " + json.dumps(residuals))
    print("consistency: " + json.dumps(consistency))
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
    r.add_argument("--mag-ned", type=float, nargs=3, default=(20.0, -1.5, 47.0),
                   metavar=("N", "E", "D"), help="constant Earth field, uT (placeholder for WMM)")
    r.add_argument("--sea-level-hpa", type=float, default=1013.25)
    r.add_argument("--ppm", type=float, default=0.0,
                   help="TagInfo.ppm_clock_error to report (no clock error is simulated)")
    r.add_argument("--pad", type=float, default=10.0,
                   help="seconds kept either side of the flying window (default 10)")
    r.add_argument("--duration", type=float, help="stop after this many seconds")
    r.add_argument("--seed", type=int, default=0)
    r.add_argument("--keep-jsonl", action="store_true", help="keep the intermediate download")
    r.add_argument("--max-accel-g", type=float, default=15.0)
    r.add_argument("--max-rate-dps", type=float, default=2000.0)
    r.add_argument("--vel-tol", type=float, default=5.0, help="core disagreement, m/s")
    r.add_argument("--att-tol", type=float, default=10.0, help="core disagreement, degrees")
    r.add_argument("--bridge-s", type=float, default=0.5)
    r.add_argument("--pos-sigma", type=float, default=5.0, help="position fit weight, m")
    r.add_argument("--vel-sigma", type=float, default=0.1, help="velocity fit weight, m/s")
    r.add_argument("--jerk-sigma", type=float, default=100.0, help="jerk penalty, m/s^3")
    r.set_defaults(func=cmd_run)
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return args.func(args)
    except (ValueError, FileNotFoundError, RuntimeError) as e:
        print(f"tagsim: {e}", file=sys.stderr)
        return 1
