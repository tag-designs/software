"""A recorded ArduPilot flight as a trajectory source, with glitch repair.

Truth comes from EKF3:

- core 0: ``XKQ1`` (attitude quaternion) and ``XKF1`` (``PN PE PD``,
  ``VN VE VD``), joined on ``TimeUS``;
- core 1: ``XKQ2`` and ``XKF6``, interpolated to core 0's times, used only to
  repair core 0;
- ``ORGN`` for the origin, ``STAT.isFlying`` for the flight window, and
  ``GPS`` (``GWk``, ``GMS``) to put ``TimeUS`` on UTC.

The ``XKQ`` quaternion rotates body (FRD) vectors into NED. It is the EKF's
own frame: ``XKF1``'s Euler angles differ from it by the autopilot's trim
(``AHRS_TRIM``), a fraction of a degree in the first logs.

Glitch handling, before fitting (design/proposals/imutag-simulator.md):

1. Each core is checked on its own: a sample is *implausible* when, against
   the last plausible sample, it implies more than ``max_accel_g`` of
   velocity change or ``max_rate_dps`` of rotation.
2. Where core 0 is implausible and core 1 is plausible, core 1 is
   substituted, and the merged series is checked again.
3. Where both cores are plausible but disagree by more than ``vel_tol_mps``
   or ``att_tol_deg``, neither is trusted.
4. Untrusted runs no longer than ``bridge_s`` are bridged: the samples are
   dropped and the uniform resampling interpolates across them, unless the
   jump persists after the run (a lasting step, such as an EKF reset), which
   bridging would only spread out. Lasting steps and longer runs split the
   flight; the longest piece inside the flight window is used.

Every repair is returned as a record for the truth file's provenance.
"""

from __future__ import annotations

import hashlib
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np

from .dataflash import DataFlashLog
from .rotations import angle_between, make_continuous, qnormalize, slerp
from .trajectory import FitConfig, Trajectory, fit_position

G0 = 9.80665
#: GPS time minus UTC, constant since 2017-01-01.
GPS_LEAP_SECONDS = 18
#: Unix time of the GPS epoch, 1980-01-06.
GPS_EPOCH_UNIX = 315964800


@dataclass
class GlitchConfig:
    """Thresholds for flagging EKF samples that are not motion."""

    max_accel_g: float = 15.0
    max_rate_dps: float = 1000.0
    vel_tol_mps: float = 5.0
    att_tol_deg: float = 10.0
    bridge_s: float = 0.5


@dataclass
class Repair:
    """One change made to the source track before fitting."""

    kind: str  #: "substitute", "bridge", "split", "split: lasting step" or "drop"
    t_start: float  #: log time (s, TimeUS / 1e6) of the first affected sample
    t_end: float  #: log time of the last affected sample
    samples: int
    reason: str


@dataclass
class SourceFlight:
    """A recorded flight, ready to fit."""

    path: Path
    sha256: str
    origin: tuple[float, float, float]  #: ORGN latitude, longitude (deg), altitude (m AMSL)
    gps_offset_s: float  #: UTC unix time minus log time
    flying: tuple[float, float]  #: first and last log time with STAT.isFlying
    t: np.ndarray  #: log time (s) of the repaired samples used
    q_nb: np.ndarray
    pos_ned: np.ndarray
    vel_ned: np.ndarray
    repairs: list[Repair] = field(default_factory=list)

    def trajectory(self, dt: float | None = None,
                   fit: FitConfig | None = None) -> tuple[Trajectory, dict]:
        """Fit the splines; returns the trajectory and the fit residuals.

        Attitude is resampled (slerp) onto a uniform grid ``dt`` apart,
        default the median sample spacing, which also bridges dropped
        samples. Position is fitted at the samples' own times.
        """
        if dt is None:
            dt = float(np.median(np.diff(self.t)))
        n = int(np.floor((self.t[-1] - self.t[0]) / dt)) + 1
        grid = self.t[0] + dt * np.arange(n)
        q = slerp(self.t, self.q_nb, grid)
        position, resid = fit_position(self.t, self.pos_ned, self.vel_ned, dt, fit)
        return Trajectory(float(grid[0]), dt, q, position), resid

    def repairs_as_dicts(self) -> list[dict]:
        return [asdict(r) for r in self.repairs]


def _cols(m: dict[str, np.ndarray], names: list[str]) -> np.ndarray:
    return np.stack([np.asarray(m[c], dtype=np.float64) for c in names], -1)


def _core(log: DataFlashLog, xkq: str, xkf: str) -> tuple[np.ndarray, ...]:
    q = log.messages(xkq)
    k = log.messages(xkf)
    tq, tk = q["TimeUS"].astype(np.int64), k["TimeUS"].astype(np.int64)
    common, iq, ik = np.intersect1d(tq, tk, assume_unique=False, return_indices=True)
    quat = make_continuous(qnormalize(_cols(q, ["Q1", "Q2", "Q3", "Q4"])[iq]))
    pos = _cols(k, ["PN", "PE", "PD"])[ik]
    vel = _cols(k, ["VN", "VE", "VD"])[ik]
    return common * 1e-6, quat, pos, vel


def _implausible(t, q, v, cfg: GlitchConfig) -> np.ndarray:
    """Forward pass: flag samples implausible against the last good one.

    A run that stays implausible for longer than ``bridge_s`` is ended by
    taking the current sample as the new reference, so a persistent step
    (an EKF reset) flags one run instead of the rest of the flight.
    """
    max_acc = cfg.max_accel_g * G0
    max_rate = np.radians(cfg.max_rate_dps)
    bad = np.zeros(len(t), dtype=bool)
    g = 0
    for i in range(1, len(t)):
        dt = t[i] - t[g]
        acc = np.linalg.norm(v[i] - v[g]) / dt
        rate = angle_between(q[g], q[i]) / dt
        if acc > max_acc or rate > max_rate:
            bad[i] = True
            if dt > cfg.bridge_s:
                g = i
        else:
            g = i
    return bad


def _lasting_step(q: np.ndarray, v: np.ndarray, a: int, b: int) -> bool:
    """True when the untrusted run ``a..b`` is a step that persists, not a spike.

    A spike returns: the sample after the run is near the sample before it.
    A step stays: the change from before to after is at least half of the
    largest jump into the run, in attitude or in velocity. Bridging a step
    would only spread it over the gap, so the flight is split there instead.
    """
    before, after, run = a - 1, b + 1, slice(a, b + 1)
    jump_q = angle_between(q[before], q[run]).max()
    jump_v = np.linalg.norm(v[run] - v[before], axis=-1).max()
    stays_q = angle_between(q[before], q[after]) >= 0.5 * jump_q
    stays_v = np.linalg.norm(v[after] - v[before]) >= 0.5 * jump_v
    # Judge by whichever quantity made the larger jump, relative to the
    # sample-to-sample noise of a clean EKF track (about 1 degree, 0.1 m/s).
    if np.degrees(jump_q) / 1.0 >= jump_v / 0.1:
        return bool(stays_q)
    return bool(stays_v)


def _runs(mask: np.ndarray) -> list[tuple[int, int]]:
    """Inclusive (start, end) index pairs of the True runs in ``mask``."""
    edges = np.diff(np.concatenate([[0], mask.astype(np.int8), [0]]))
    return list(zip(np.where(edges == 1)[0], np.where(edges == -1)[0] - 1))


def gps_offset(log: DataFlashLog) -> float:
    """UTC unix time minus log time, from GPS messages with a 3D fix."""
    g = log.messages("GPS")
    ok = (g["Status"] >= 3) & (g["GWk"] > 0)
    if not np.any(ok):
        raise ValueError("log has no GPS fix to set the epoch")
    unix = (GPS_EPOCH_UNIX - GPS_LEAP_SECONDS + g["GWk"][ok].astype(np.float64) * 604800.0
            + g["GMS"][ok].astype(np.float64) * 1e-3)
    return float(np.median(unix - g["TimeUS"][ok].astype(np.float64) * 1e-6))


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_flight(path: str | Path, cfg: GlitchConfig | None = None,
                pad_s: float = 10.0) -> SourceFlight:
    """Read, repair and trim a flight to its flying window plus ``pad_s``."""
    cfg = cfg or GlitchConfig()
    path = Path(path)
    log = DataFlashLog(path)
    t, q0, p0, v0 = _core(log, "XKQ1", "XKF1")
    repairs: list[Repair] = []

    # Core 1 at core 0's times.
    t1, q1, p1, v1 = _core(log, "XKQ2", "XKF6")
    inside = (t >= t1[0]) & (t <= t1[-1])
    q1i = np.tile(q0[:1], (len(t), 1))
    q1i[inside] = slerp(t1, q1, t[inside])
    q1i = make_continuous(q1i)
    # Same hemisphere as core 0, so substitution keeps the series continuous.
    q1i *= np.where(np.sum(q1i * q0, -1) < 0, -1.0, 1.0)[:, None]
    p1i = np.stack([np.interp(t, t1, p1[:, k]) for k in range(3)], -1)
    v1i = np.stack([np.interp(t, t1, v1[:, k]) for k in range(3)], -1)

    bad0 = _implausible(t, q0, v0, cfg)
    bad1 = _implausible(t, q1i, v1i, cfg) | ~inside
    sub = bad0 & ~bad1
    q, p, v = q0.copy(), p0.copy(), v0.copy()
    q[sub], p[sub], v[sub] = q1i[sub], p1i[sub], v1i[sub]
    for a, b in _runs(sub):
        repairs.append(Repair("substitute", float(t[a]), float(t[b]), int(b - a + 1),
                              "core 0 implausible, core 1 plausible"))
    still_bad = _implausible(t, q, v, cfg)
    disagree = (~bad0 & ~bad1
                & ((np.linalg.norm(v0 - v1i, axis=-1) > cfg.vel_tol_mps)
                   | (np.degrees(angle_between(q0, q1i)) > cfg.att_tol_deg)))
    untrusted = still_bad | disagree

    # Bridge short runs, split at long ones.
    keep = ~untrusted
    cuts: list[tuple[int, int]] = []
    for a, b in _runs(untrusted):
        before = t[a - 1] if a > 0 else t[a]
        after = t[b + 1] if b + 1 < len(t) else t[b]
        reason = "cores disagree" if disagree[a:b + 1].all() else "implausible in both cores"
        bridgeable = after - before <= cfg.bridge_s and a > 0 and b + 1 < len(t)
        if bridgeable and _lasting_step(q, v, a, b):
            repairs.append(Repair("split: lasting step", float(t[a]), float(t[b]),
                                  int(b - a + 1), reason + "; the jump persists after the run"))
            cuts.append((a, b))
        elif bridgeable:
            repairs.append(Repair("bridge", float(t[a]), float(t[b]), int(b - a + 1), reason))
        else:
            repairs.append(Repair("split", float(t[a]), float(t[b]), int(b - a + 1), reason))
            cuts.append((a, b))

    # Flight window.
    stat = log.messages("STAT")
    flying_t = stat["TimeUS"][stat["isFlying"] == 1] * 1e-6
    if len(flying_t) == 0:
        raise ValueError(f"{path.name}: STAT.isFlying is never set; no flight to replay")
    flying = (float(flying_t[0]), float(flying_t[-1]))
    lo, hi = flying[0] - pad_s, flying[1] + pad_s

    # Pieces between splits; keep the longest overlap with the window.
    bounds = [0]
    for a, b in cuts:
        bounds += [a, b + 1]
    bounds.append(len(t))
    pieces = [(bounds[k], bounds[k + 1]) for k in range(0, len(bounds), 2)]
    def overlap(piece):
        a, b = piece
        if b <= a:
            return 0.0
        return max(0.0, min(t[b - 1], hi) - max(t[a], lo))
    best = max(pieces, key=overlap)
    if overlap(best) <= 0:
        raise ValueError(f"{path.name}: no clean piece overlaps the flight window")
    a, b = best
    lo_t, hi_t = t[a], t[b - 1]
    # Substitutions and bridges outside the piece used change nothing.
    repairs = [r for r in repairs
               if r.kind.startswith("split") or (r.t_end >= lo_t and r.t_start <= hi_t)]
    for piece in pieces:
        a, b = piece
        if piece != best and b > a:
            repairs.append(Repair("drop", float(t[a]), float(t[b - 1]), int(b - a),
                                  "not the longest clean piece in the flight window"))
    a, b = best
    sel = np.zeros(len(t), dtype=bool)
    sel[a:b] = True
    sel &= keep & (t >= lo) & (t <= hi)

    org = log.messages("ORGN")
    origin = (float(org["Lat"][0]), float(org["Lng"][0]), float(org["Alt"][0]))
    return SourceFlight(path=path, sha256=sha256_of(path), origin=origin,
                        gps_offset_s=gps_offset(log), flying=flying,
                        t=t[sel], q_nb=make_continuous(q[sel]), pos_ned=p[sel],
                        vel_ned=v[sel], repairs=repairs)
