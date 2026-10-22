"""Scoring an orientation estimate against a tagsim truth file.

An estimate is a series of attitudes, and optionally velocities, on the
log's ``ElapsedUs``. Truth is interpolated to those times from
``TruthPose`` (slerp for attitude, linear for velocity). For every estimate
row the error rotation ``E = q_est conj(q_true)``, both tag frame to ENU, is
split about the vertical:

- **total**: the angle of E;
- **tilt**: the angle between the estimated and true vertical in the tag
  frame, the roll and pitch part of the error;
- **heading**: E's twist about the vertical, signed clockwise from above, as
  a compass heading error, in (-180, 180].

Where the estimate has them, it also scores **velocity** (``|v_est -
v_true|``), **altitude** relative to the start and **vertical velocity**.
Altitude is compared as a change: the estimate's and the truth's altitude
both relative to their own value at the start, taken as the mean offset
between them over the estimate's first second (so neither a datum nor one
noisy first row matters).

Rows are split into **flight** (inside the source log's ``STAT.isFlying``
window) and **ground** (the padding either side), and each metric is
summarised by median, 95th percentile and maximum of its absolute value.

Estimates:

- ``imu-compass``: ``dataprocessing``'s ``ImuCompassOrientation`` table. Its
  quaternion is in the algorithm's own convention, so the attitude it
  implies is recovered with :func:`tagsim.compass.implied_attitude`.
- ``enu``: any table with ``ElapsedUs``, ``qw qx qy qz`` (tag frame to
  ENU, tagsim's convention) and optionally ``ve vn vu`` (m/s), ``alt`` (m,
  any datum) and ``vz`` (m/s, up). :mod:`tagsim.navfilter` writes this form.
"""

from __future__ import annotations

import json
import sqlite3
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .compass import implied_attitude
from .rotations import make_continuous, qconj, qmul, qrotate, slerp

METRICS = ("total_deg", "tilt_deg", "heading_deg", "velocity_mps", "altitude_m", "vz_mps")

#: Span (s) at the start of an estimate over which its altitude datum is
#: matched to truth's.
ALTITUDE_REFERENCE_S = 1.0


@dataclass
class Truth:
    """What scoring needs from a truth file."""

    elapsed_us: np.ndarray
    q_enu_tag: np.ndarray
    vel_enu: np.ndarray
    up_m: np.ndarray  #: ENU up about the EKF origin
    flying_us: tuple[float, float] | None  #: flying window, truth ElapsedUs
    field_ned_ut: tuple[float, float, float]
    config: dict


def load_truth(path: Path) -> Truth:
    con = sqlite3.connect(path)
    try:
        rows = np.array(con.execute(
            "SELECT ElapsedUs, qw, qx, qy, qz, ve, vn, vu, u FROM TruthPose "
            "WHERE SegmentId = 0 ORDER BY SampleIndex").fetchall(), dtype=np.float64)
        config = json.loads(con.execute("SELECT ConfigJson FROM SimRun").fetchone()[0])
    finally:
        con.close()
    flying = config.get("flying_elapsed_us")
    return Truth(rows[:, 0], make_continuous(rows[:, 1:5]), rows[:, 5:8], rows[:, 8],
                 tuple(flying) if flying else None,
                 tuple(config["sensor"]["mag_ned_ut"]), config)


@dataclass
class Estimate:
    """An attitude series, tag frame to ENU, on the log's ElapsedUs."""

    name: str
    segment_id: np.ndarray
    sample_index: np.ndarray
    elapsed_us: np.ndarray
    q_enu_tag: np.ndarray
    vel_enu: np.ndarray | None = None
    #: How well the estimate's own convention was decoded (rad), if it had to be.
    decode_residual: np.ndarray | None = None
    alt_m: np.ndarray | None = None  #: altitude, any datum
    vz_mps: np.ndarray | None = None  #: vertical velocity, up


def truth_at(truth: Truth, elapsed_us: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Truth attitude and velocity at ``elapsed_us``."""
    t = np.asarray(elapsed_us, dtype=np.float64)
    q = slerp(truth.elapsed_us, truth.q_enu_tag, t)
    v = np.stack([np.interp(t, truth.elapsed_us, truth.vel_enu[:, k]) for k in range(3)], -1)
    return q, v


def altitude_error(truth: Truth, elapsed_us: np.ndarray, alt_m: np.ndarray) -> np.ndarray:
    """Error (m) of an altitude change: estimate minus truth, after matching
    their datums by the mean offset over the first ``ALTITUDE_REFERENCE_S``."""
    t = np.asarray(elapsed_us, dtype=np.float64)
    d = np.asarray(alt_m, dtype=np.float64) - np.interp(t, truth.elapsed_us, truth.up_m)
    first = (t <= t[0] + ALTITUDE_REFERENCE_S * 1e6) & np.isfinite(d)
    return d - (d[first].mean() if first.any() else np.nan)


def imu_compass_estimate(processed: Path, truth: Truth) -> Estimate:
    """``ImuCompassOrientation`` from a ``dataprocessing`` output, decoded
    into the attitude it implies."""
    con = sqlite3.connect(processed)
    try:
        rows = np.array(con.execute(
            "SELECT SegmentId, SampleIndex, ElapsedUs, qw, qx, qy, qz "
            "FROM ImuCompassOrientation ORDER BY SegmentId, SampleIndex").fetchall(),
            dtype=np.float64)
    finally:
        con.close()
    q_true, _ = truth_at(truth, rows[:, 2])
    q, resid = implied_attitude(rows[:, 3:7], truth.field_ned_ut, q_true)
    return Estimate("imu-compass", rows[:, 0].astype(np.int64), rows[:, 1].astype(np.int64),
                    rows[:, 2], q, None, resid)


def enu_estimate(db: Path, table: str) -> Estimate:
    """A table in tagsim's convention (tag frame to ENU), velocity optional."""
    con = sqlite3.connect(db)
    try:
        cols = {r[1] for r in con.execute(f"PRAGMA table_info({table})")}
        extra = [c for c in ("ve", "vn", "vu", "alt", "vz") if c in cols]
        seg = "SegmentId" if "SegmentId" in cols else "0"
        idx = "SampleIndex" if "SampleIndex" in cols else "0"
        rows = np.array(con.execute(
            f"SELECT {seg}, {idx}, ElapsedUs, qw, qx, qy, qz"
            + "".join(f", {c}" for c in extra) + f" FROM {table} ORDER BY ElapsedUs").fetchall(),
            dtype=np.float64)
    finally:
        con.close()

    def col(name):
        if name not in extra:
            return None
        v = rows[:, 7 + extra.index(name)]
        return None if np.isnan(v).all() else v

    has_v = all(c in extra for c in ("ve", "vn", "vu"))
    vel = np.stack([col(c) for c in ("ve", "vn", "vu")], -1) if has_v else None
    return Estimate(table, rows[:, 0].astype(np.int64), rows[:, 1].astype(np.int64), rows[:, 2],
                    rows[:, 3:7], vel, alt_m=col("alt"), vz_mps=col("vz"))


def attitude_errors(q_est: np.ndarray, q_true: np.ndarray) -> dict[str, np.ndarray]:
    """Total, tilt and signed heading error (degrees), tag frame to ENU."""
    e = qmul(q_est, qconj(q_true))
    e = np.where(e[:, :1] < 0, -e, e)
    total = 2.0 * np.degrees(np.arctan2(np.linalg.norm(e[:, 1:], axis=1), e[:, 0]))
    up = np.array([0.0, 0.0, 1.0])
    u_est = qrotate(qconj(q_est), up)
    u_true = qrotate(qconj(q_true), up)
    tilt = np.degrees(np.arccos(np.clip(np.sum(u_est * u_true, axis=1), -1.0, 1.0)))
    # Twist about ENU up is counter-clockwise; a compass heading is clockwise.
    twist = 2.0 * np.degrees(np.arctan2(e[:, 3], e[:, 0]))
    heading = -((twist + 180.0) % 360.0 - 180.0)
    return {"total_deg": total, "tilt_deg": tilt, "heading_deg": heading}


def summarise(values: np.ndarray) -> dict:
    a = np.abs(values[np.isfinite(values)])
    if len(a) == 0:
        return {"n": 0, "median": None, "p95": None, "max": None}
    return {"n": int(len(a)), "median": float(np.median(a)),
            "p95": float(np.percentile(a, 95)), "max": float(a.max())}


@dataclass
class Score:
    estimate: Estimate
    flying: np.ndarray  #: bool per row
    errors: dict[str, np.ndarray]
    summary: dict  #: phase -> metric -> {n, median, p95, max}


def score(truth: Truth, est: Estimate) -> Score:
    q_true, v_true = truth_at(truth, est.elapsed_us)
    errors = attitude_errors(est.q_enu_tag, q_true)
    if est.vel_enu is not None:
        errors["velocity_mps"] = np.linalg.norm(est.vel_enu - v_true, axis=1)
    if est.alt_m is not None:
        errors["altitude_m"] = altitude_error(truth, est.elapsed_us, est.alt_m)
    if est.vz_mps is not None:
        errors["vz_mps"] = est.vz_mps - v_true[:, 2]
    if truth.flying_us is None:
        flying = np.zeros(len(est.elapsed_us), dtype=bool)
    else:
        flying = (est.elapsed_us >= truth.flying_us[0]) & (est.elapsed_us <= truth.flying_us[1])
    summary = {}
    for phase, mask in (("flight", flying), ("ground", ~flying), ("all", np.ones_like(flying))):
        summary[phase] = {k: summarise(v[mask]) for k, v in errors.items()}
    return Score(est, flying, errors, summary)


def write_score(path: Path, result: Score, truth_path: Path, estimate_path: Path,
                undecoded_tol_rad: float = 1e-5) -> None:
    """``ScoreSeries`` (one row per estimate row), ``ScoreSummary`` and ``ScoreRun``."""
    if path.exists():
        path.unlink()
    est = result.estimate
    n = len(est.elapsed_us)
    cols = [result.errors.get(m, np.full(n, np.nan)) for m in METRICS]
    resid = est.decode_residual if est.decode_residual is not None else np.zeros(n)
    con = sqlite3.connect(path)
    try:
        con.executescript("""
            CREATE TABLE ScoreSeries (Estimate TEXT, SegmentId INTEGER, SampleIndex INTEGER,
                ElapsedUs INTEGER, Flying INTEGER, TotalDeg REAL, TiltDeg REAL, HeadingDeg REAL,
                VelocityErrMps REAL, AltitudeErrM REAL, VzErrMps REAL, qw REAL, qx REAL, qy REAL, qz REAL, DecodeResidualDeg REAL);
            CREATE TABLE ScoreSummary (Estimate TEXT, Phase TEXT, Metric TEXT, N INTEGER,
                Median REAL, P95 REAL, Max REAL);
            CREATE TABLE ScoreRun (Estimate TEXT, TruthFile TEXT, EstimateFile TEXT,
                Undecoded INTEGER, SummaryJson TEXT);
        """)

        def f(x):
            return None if not np.isfinite(x) else float(x)

        con.executemany(
            "INSERT INTO ScoreSeries VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            ((est.name, int(s), int(i), int(e), int(fl), *(f(c[k]) for c in cols),
              *(float(x) for x in est.q_enu_tag[k]), float(np.degrees(resid[k])))
             for k, (s, i, e, fl) in enumerate(zip(est.segment_id, est.sample_index,
                                                    est.elapsed_us, result.flying))))
        con.executemany("INSERT INTO ScoreSummary VALUES (?,?,?,?,?,?,?)",
                        ((est.name, phase, metric, s["n"], s["median"], s["p95"], s["max"])
                         for phase, metrics in result.summary.items()
                         for metric, s in metrics.items()))
        con.execute("INSERT INTO ScoreRun VALUES (?,?,?,?,?)",
                    (est.name, str(truth_path.name), str(estimate_path.name),
                     int((resid > undecoded_tol_rad).sum()), json.dumps(result.summary)))
        con.commit()
    finally:
        con.close()


def summary_lines(result: Score) -> list[str]:
    lines = []
    for phase in ("flight", "ground"):
        for metric, s in result.summary[phase].items():
            if s["n"]:
                lines.append(f"{phase:6} {metric:12} n {s['n']:6}  median {s['median']:8.3f}  "
                             f"p95 {s['p95']:8.3f}  max {s['max']:8.3f}")
    return lines


__all__ = ["Truth", "Estimate", "Score", "load_truth", "imu_compass_estimate", "enu_estimate",
           "truth_at", "altitude_error", "attitude_errors", "score", "write_score", "summary_lines"]
