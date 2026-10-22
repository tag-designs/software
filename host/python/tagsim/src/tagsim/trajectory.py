"""Smooth trajectories: attitude and position splines sampled at any time.

- **Attitude** is a cumulative cubic B-spline on the unit quaternions
  (Kim, Kim and Shin 1995; the body-rate recursion of Sommer et al. 2020),
  on a uniform grid. The grid samples are its control points, so it
  approximates rather than interpolates them, and its body rate comes
  analytically. It is valid from the second grid point to the second last.
- **Position** is a quintic B-spline fitted by least squares to the position
  *and* velocity samples, with a penalty on jerk (:func:`fit_position`).
  Velocity and acceleration are its analytic derivatives. An exact quintic
  Hermite through both cannot be used on EKF output: there the position and
  the velocity disagree by 2-7 cm per 40 ms step, and an interpolant that
  honours both swings to tens of g between samples. The fit weights the
  velocity, which carries the dynamics, more than the position, which absorbs
  GPS corrections.

Frames: attitude is ``q_nb``, body FRD to local NED; position, velocity and
acceleration are NED, metres and seconds.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np
import scipy.sparse as sparse
import scipy.sparse.linalg as sparse_linalg
from scipy.interpolate import BSpline

from .rotations import make_continuous, qconj, qexp, qlog, qmul, qnormalize, qrotate

_DEGREE = 5


@dataclass
class FitConfig:
    """Weights of the position fit, as the standard deviations they stand for.

    EKF position and integrated EKF velocity drift metres apart over a
    flight, so the position weight is loose: a tight one makes the fit buy
    position back with violent velocity excursions wherever the samples have
    a gap (a 0.2 s dropout in 00000167 became 150 g at ``pos_sigma_m``
    0.5). At 5 m the first logs' peak specific force is 5-6 g, matching the
    differenced EKF velocity, and the spline stays within a few metres of the
    EKF position.

    ``jerk_sigma`` (m/s^3) sets how much jerk the fit allows: a pattern
    aircraft pulling 5 g in a tenth of a second reaches about 500, and 25 Hz
    EKF output resolves nothing that fast. Smaller values smooth more.
    """

    pos_sigma_m: float = 5.0
    vel_sigma_mps: float = 0.1
    jerk_sigma: float = 100.0

    def as_dict(self) -> dict:
        return asdict(self)


def _derivative_matrix(knots: np.ndarray, k: int) -> sparse.csr_matrix:
    """Map degree-``k`` B-spline coefficients to its derivative's."""
    m = len(knots) - k - 1
    d = k / (knots[k + 1:k + m] - knots[1:m])
    return sparse.diags([-d, d], [0, 1], shape=(m - 1, m), format="csr")


def _design(knots: np.ndarray, k: int, x: np.ndarray, order: int) -> sparse.csr_matrix:
    """The ``order``-th derivative at ``x``, as a matrix on the coefficients."""
    m = sparse.identity(len(knots) - k - 1, format="csr")
    kk = knots
    for j in range(order):
        m = _derivative_matrix(kk, k - j) @ m
        kk = kk[1:-1]
    return BSpline.design_matrix(x, kk, k - order) @ m


def fit_position(t: np.ndarray, pos: np.ndarray, vel: np.ndarray, knot_dt: float,
                 cfg: FitConfig | None = None) -> tuple[BSpline, dict]:
    """Least-squares quintic B-spline through position and velocity samples.

    Minimises ``|p(t) - pos|^2 / pos_sigma^2 + |p'(t) - vel|^2 / vel_sigma^2
    + integral |p'''|^2 / jerk_sigma^2`` over uniform knots ``knot_dt``
    apart, clamped at the first and last sample. Returns the spline (vector
    valued, NED) and its residuals at the samples, for provenance.
    """
    cfg = cfg or FitConfig()
    t = np.asarray(t, dtype=np.float64)
    n_int = max(1, int(np.ceil((t[-1] - t[0]) / knot_dt)))
    inner = t[0] + knot_dt * np.arange(n_int + 1)
    k = _DEGREE
    knots = np.concatenate([[inner[0]] * k, inner, [inner[-1]] * k])
    b0, b1 = _design(knots, k, t, 0), _design(knots, k, t, 1)
    xe = inner[0] + (np.arange(4 * n_int) + 0.5) * (knot_dt / 4)
    b3 = _design(knots, k, xe, 3)
    wp, wv = 1.0 / cfg.pos_sigma_m ** 2, 1.0 / cfg.vel_sigma_mps ** 2
    wj = (knot_dt / 4) / cfg.jerk_sigma ** 2
    a = (wp * (b0.T @ b0) + wv * (b1.T @ b1) + wj * (b3.T @ b3)).tocsc()
    rhs = wp * (b0.T @ pos) + wv * (b1.T @ vel)
    coef = sparse_linalg.splu(a).solve(np.asarray(rhs))
    spline = BSpline(knots, coef, k, extrapolate=False)
    pr = np.linalg.norm(b0 @ coef - pos, axis=1)
    vr = np.linalg.norm(b1 @ coef - vel, axis=1)
    resid = {"pos_residual_m_median": float(np.median(pr)), "pos_residual_m_max": float(pr.max()),
             "vel_residual_mps_median": float(np.median(vr)),
             "vel_residual_mps_max": float(vr.max())}
    return spline, resid


@dataclass
class Trajectory:
    """Attitude on a uniform grid ``t0 + k * dt`` and a position spline.

    ``q_nb`` (N, 4) are the attitude spline's control points; ``position`` is
    a vector-valued spline in NED, normally from :func:`fit_position`, which
    must cover the attitude spline's range.
    """

    t0: float
    dt: float
    q_nb: np.ndarray
    position: BSpline

    def __post_init__(self) -> None:
        self.q_nb = make_continuous(qnormalize(self.q_nb))
        if len(self.q_nb) < 4:
            raise ValueError("a trajectory needs at least 4 attitude samples")
        self._velocity = self.position.derivative(1)
        self._acceleration = self.position.derivative(2)
        # Relative rotations between consecutive control points.
        self._omega = qlog(qmul(qconj(self.q_nb[:-1]), self.q_nb[1:]))

    @property
    def t_start(self) -> float:
        """First time the attitude spline is defined."""
        return self.t0 + self.dt

    @property
    def t_end(self) -> float:
        """End of the attitude spline's range (exclusive)."""
        return self.t0 + (len(self.q_nb) - 2) * self.dt

    def attitude(self, t: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """Attitude ``q_nb`` and body rate (rad/s, body frame) at ``t``.

        Times outside [t_start, t_end) are clamped to the nearest valid
        interval and extrapolated, which callers should avoid.
        """
        s = (np.asarray(t, dtype=np.float64) - self.t0) / self.dt
        i = np.clip(np.floor(s).astype(np.int64), 1, len(self.q_nb) - 3)
        u = s - i
        u2, u3 = u * u, u * u * u
        b = np.stack([(5 + 3 * u - 3 * u2 + u3) / 6, (1 + 3 * u + 3 * u2 - 2 * u3) / 6, u3 / 6])
        db = np.stack([(3 - 6 * u + 3 * u2) / 6, (3 + 6 * u - 6 * u2) / 6, 3 * u2 / 6]) / self.dt
        q = self.q_nb[i - 1]
        w = np.zeros((len(i), 3))
        for j in range(3):
            om = self._omega[i - 1 + j]
            a = qexp(b[j][:, None] * om)
            q = qmul(q, a)
            # Body rate recursion: w_j = A_j^T w_{j-1} + dB_j * Omega_j.
            w = qrotate(qconj(a), w) + db[j][:, None] * om
        return qnormalize(q), w

    def kinematics(self, t: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Position, velocity and acceleration (NED) at ``t``; NaN outside the fit."""
        t = np.asarray(t, dtype=np.float64)
        return self.position(t), self._velocity(t), self._acceleration(t)


def stationary(duration_s: float, q_nb=(1.0, 0.0, 0.0, 0.0), dt: float = 0.04) -> Trajectory:
    """A trajectory that holds one attitude at the origin, at rest.

    Its usable range starts at 0 and lasts at least ``duration_s``.
    """
    n = int(np.ceil(duration_s / dt)) + 4
    q = np.tile(np.asarray(q_nb, dtype=np.float64), (n, 1))
    t0 = -dt
    inner = t0 + dt * np.arange(n)
    knots = np.concatenate([[inner[0]] * _DEGREE, inner, [inner[-1]] * _DEGREE])
    position = BSpline(knots, np.zeros((len(knots) - _DEGREE - 1, 3)), _DEGREE, extrapolate=False)
    return Trajectory(t0, dt, q, position)
