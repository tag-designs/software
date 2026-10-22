"""Strapdown check of the ideal signals against the trajectory they came from.

Integrates the ideal (unquantized) specific force and body rate at the IMU
rate and compares the result with the spline's own pose. Whatever drift
remains is the simulator's own inconsistency: sampling, the integrator and the
spline, which must stay well below the filter errors being studied.

- Attitude is integrated over the whole run from the true initial attitude,
  with a second-order rotation vector including the coning term.
- Velocity and position drift without bound under any integrator, so they are
  restarted from truth every ``window_s`` and the error at each window's end
  is reported.
"""

from __future__ import annotations

import numpy as np

from .rotations import angle_between, qexp, qmul, qnormalize, qrotate

G0 = 9.80665


def strapdown(t: np.ndarray, q_et: np.ndarray, pos: np.ndarray, vel: np.ndarray,
              accel_mg: np.ndarray, gyro_dps: np.ndarray, window_s: float = 10.0) -> dict:
    """Consistency figures for ideal signals in the tag frame and ENU truth.

    Returns a dict with ``attitude_deg_max``, ``attitude_deg_final``,
    ``velocity_mps_max``, ``velocity_mps_median``, ``position_m_max`` and
    ``position_m_median`` (the last four over ``window_s`` windows), plus
    ``window_s`` and ``samples``.
    """
    dt = np.diff(t)
    w = np.radians(gyro_dps)
    f = accel_mg * (G0 / 1000.0)
    g = np.array([0.0, 0.0, -G0])  # ENU

    # Attitude, whole run.
    phi = 0.5 * (w[:-1] + w[1:]) * dt[:, None] + np.cross(w[:-1], w[1:]) * (dt[:, None] ** 2) / 12.0
    steps = qexp(phi)
    q = np.empty_like(q_et)
    q[0] = q_et[0]
    for k in range(len(steps)):
        q[k + 1] = qmul(q[k], steps[k])
        if k % 1000 == 999:
            q[k + 1] = qnormalize(q[k + 1])
    att = np.degrees(angle_between(qnormalize(q), q_et))

    # Velocity and position, restarted from truth each window, using the true
    # attitude so the figures isolate the translational inconsistency.
    f_e = qrotate(q_et, f) + g
    per = max(1, int(round(window_s / np.median(dt))))
    v_err, p_err = [], []
    for a in range(0, len(t) - per, per):
        b = a + per
        dv = 0.5 * (f_e[a:b] + f_e[a + 1:b + 1]) * dt[a:b, None]
        v = vel[a] + np.concatenate([[np.zeros(3)], np.cumsum(dv, axis=0)])
        p = pos[a] + np.concatenate([[np.zeros(3)],
                                     np.cumsum(0.5 * (v[:-1] + v[1:]) * dt[a:b, None], axis=0)])
        v_err.append(np.linalg.norm(v[-1] - vel[b]))
        p_err.append(np.linalg.norm(p[-1] - pos[b]))
    v_err = np.array(v_err) if v_err else np.zeros(1)
    p_err = np.array(p_err) if p_err else np.zeros(1)
    return {
        "samples": int(len(t)),
        "window_s": float(window_s),
        "attitude_deg_max": float(att.max()),
        "attitude_deg_final": float(att[-1]),
        "velocity_mps_max": float(v_err.max()),
        "velocity_mps_median": float(np.median(v_err)),
        "position_m_max": float(p_err.max()),
        "position_m_median": float(np.median(p_err)),
    }
