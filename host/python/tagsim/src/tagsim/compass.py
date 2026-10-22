"""dataprocessing's single-sample IMUTag compass, and the attitude it implies.

``dataprocessing -p imu-compass-orientation`` stores, for each magnetometer
row, a quaternion from ``compasscore::solveOrientation``
(``host/libraries/sensoranalysis/core/compass_core.cpp``), computed from the
calibrated field and the mean accelerometer of the row's superframe. That
quaternion is **not** a fixed relabelling of the tag's attitude, so it cannot
be compared with truth by a constant change of frame:

- where the stored accelerometer z is negative (the tag's z axis below the
  horizon), it is exactly ``A R_et^T B`` for constant rotations A and B;
- where z is positive, no constant A and B fit at all, because the
  algorithm's ``q2``/``q1`` swap of the tilt quaternion and its magnetometer
  flips relabel the two vectors differently in that branch.

tagsim therefore scores the attitude the estimate **implies**: the tag
attitude ``q`` for which this same algorithm, given the ideal inputs at ``q``
(gravity only, and the true field), returns the stored quaternion. Finding it
is a small per-sample Gauss-Newton solve, started from the true attitude, of
``f(q) = q_est``. Errors between that attitude and truth are then physical
attitude errors in the tag frame, whatever the algorithm's convention, and
they include its declination handling, since ``f`` uses the full field.

:func:`solve_orientation` is a vectorised port of ``solveOrientation``. It
agrees with the dataprocessing binary to about 1e-7 per component (float
versus double).
"""

from __future__ import annotations

import numpy as np

from .rotations import qconj, qexp, qlog, qmul, qrotate
from .sensors import R_ENU_NED, stored_mag

_S2 = np.sqrt(0.5)


def solve_orientation(mag: np.ndarray, accel: np.ndarray) -> np.ndarray:
    """``compasscore::solveOrientation``'s quaternion (w, x, y, z), per row.

    ``mag`` is the calibrated stored field and ``accel`` the accelerometer,
    any units, shape (n, 3). Rows the algorithm cannot solve come back NaN.
    """
    a = np.asarray(accel, dtype=np.float64)
    m = np.asarray(mag, dtype=np.float64)
    a = a / np.linalg.norm(a, axis=-1, keepdims=True)
    m = m / np.linalg.norm(m, axis=-1, keepdims=True)
    ax, ay, az = a[:, 0], -a[:, 1], a[:, 2]
    m = np.stack([m[:, 0], m[:, 1], -m[:, 2]], -1)
    up = az >= 0
    with np.errstate(divide="ignore", invalid="ignore"):
        q0u = np.sqrt((az + 1) * 0.5)
        xd = np.sqrt((1 - az) * 0.5)
        q0 = np.where(up, q0u, -ay / (2 * xd))
        q1 = np.where(up, -ay / (2 * q0u), xd)
        q2 = np.where(up, ax / (2 * q0u), 0.0)
        q3 = np.where(up, 0.0, ax / (2 * xd))
        qacc = np.stack([q0, q2, q1, q3], -1)
        qacc /= np.linalg.norm(qacc, axis=-1, keepdims=True)
        rm = qrotate(qacc, m)
        g = rm[:, 0] ** 2 + rm[:, 1] ** 2
        beta = np.sqrt(g + rm[:, 0] * np.sqrt(g))
        betaneg = np.sqrt(g - rm[:, 0] * np.sqrt(g))
        pos = rm[:, 0] >= 0
        q0m = np.where(pos, beta / np.sqrt(2 * g), rm[:, 1] / (np.sqrt(2) * betaneg))
        q3m = np.where(pos, rm[:, 1] / (np.sqrt(2) * beta), betaneg / np.sqrt(2 * g))
    zero = np.zeros_like(q0m)
    qmag = np.stack([q0m, zero, zero, -q3m], -1)
    qmag = qmul(np.array([_S2, 0.0, 0.0, _S2]), qmag)
    qmag /= np.linalg.norm(qmag, axis=-1, keepdims=True)
    q = qmul(qacc, qmag)
    q /= np.linalg.norm(q, axis=-1, keepdims=True)
    return q * np.array([1.0, -1.0, -1.0, 1.0])


def ideal_compass(q_enu_tag: np.ndarray, field_ned_ut) -> np.ndarray:
    """The algorithm's output for a tag at rest at each attitude, with the
    true field and a correct calibration."""
    q = np.asarray(q_enu_tag, dtype=np.float64)
    q_te = qconj(q)
    up = np.broadcast_to([0.0, 0.0, 1.0], q.shape[:-1] + (3,))
    b_enu = R_ENU_NED @ np.asarray(field_ned_ut, dtype=np.float64)
    accel = qrotate(q_te, up)  # specific force at rest: up, tag frame
    mag = stored_mag(qrotate(q_te, np.broadcast_to(b_enu, up.shape)))
    return solve_orientation(mag.astype(np.float64), accel)


def _qdiff(p: np.ndarray, q: np.ndarray) -> np.ndarray:
    """Rotation vector of ``conj(p) q``, sign-insensitive."""
    d = qmul(qconj(p), q)
    d = np.where(d[..., :1] < 0, -d, d)
    return qlog(d)


def _gauss_newton(q_est, field_ned_ut, q_init, iterations, eps):
    q = np.array(q_init, dtype=np.float64)
    for _ in range(iterations):
        r = _qdiff(ideal_compass(q, field_ned_ut), q_est)
        jac = np.empty(r.shape + (3,))
        for k in range(3):
            d = np.zeros(3)
            d[k] = eps
            qk = qmul(q, qexp(np.broadcast_to(d, r.shape)))
            jac[..., k] = (_qdiff(ideal_compass(qk, field_ned_ut), q_est) - r) / eps
        try:
            step = np.linalg.solve(jac, -r[..., None])[..., 0]
        except np.linalg.LinAlgError:
            step = np.linalg.lstsq(jac.reshape(-1, 3), -r.reshape(-1), rcond=None)[0]
        step = np.clip(step, -0.5, 0.5)
        q = qmul(q, qexp(np.nan_to_num(step)))
    resid = np.linalg.norm(_qdiff(ideal_compass(q, field_ned_ut), q_est), axis=-1)
    return q, np.nan_to_num(resid, nan=np.pi)


def implied_attitude(q_est: np.ndarray, field_ned_ut, q_init: np.ndarray,
                     iterations: int = 15, eps: float = 1e-6, tol: float = 1e-5,
                     restarts: int = 24, seed: int = 0) -> tuple[np.ndarray, np.ndarray]:
    """The tag attitude (tag frame to ENU) whose ideal compass output is ``q_est``.

    Solved per row by Gauss-Newton on a rotation-vector perturbation of
    ``q_init`` (normally the true attitude). Rows that do not converge to
    within ``tol`` (rad), mostly near the algorithm's branch switch where
    the tag's z axis is horizontal, are retried from random starts around
    ``q_init``, up to 90 degrees away, keeping the best. Returns the attitude
    and the residual angle (rad); a residual above ``tol`` means no attitude
    reproducing the estimate was found.
    """
    q_est = np.asarray(q_est, dtype=np.float64)
    q_init = np.asarray(q_init, dtype=np.float64)
    q, resid = _gauss_newton(q_est, field_ned_ut, q_init, iterations, eps)
    rng = np.random.default_rng(seed)
    for k in range(restarts):
        bad = np.where(resid > tol)[0]
        if len(bad) == 0:
            break
        spread = np.radians(10.0 + 80.0 * k / max(restarts - 1, 1))
        start = qmul(q_init[bad], qexp(rng.normal(size=(len(bad), 3)) * spread))
        qb, rb = _gauss_newton(q_est[bad], field_ned_ut, start, iterations, eps)
        better = rb < resid[bad]
        q[bad[better]] = qb[better]
        resid[bad[better]] = rb[better]
    return q, resid


__all__ = ["solve_orientation", "ideal_compass", "implied_attitude"]
