"""Strapdown check of the ideal signals against the trajectory they came from.

Integrates the ideal (unquantized) specific force and body rate at the IMU
rate and compares the result with the spline's own pose. Whatever drift
remains is the simulator's own inconsistency: sampling, the integrator and the
spline, which must stay well below the filter errors being studied.

- Attitude is integrated over the whole run from the true initial attitude,
  with a second-order rotation vector including the coning term.
- Velocity and position are integrated over the whole run too, from the true
  initial velocity and position and with the true attitude, so they isolate
  the translational inconsistency. The largest error and the error at the
  end are reported.
"""

from __future__ import annotations

import numpy as np

from .rotations import angle_between, qexp, qmul, qnormalize, qrotate

G0 = 9.80665


def strapdown_path(t: np.ndarray, q0: np.ndarray, pos0: np.ndarray, vel0: np.ndarray,
                   accel_mg: np.ndarray, gyro_dps: np.ndarray,
                   q_et: np.ndarray | None = None) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Strapdown-integrate tag-frame signals over the whole run.

    Starts from the attitude ``q0`` (tag to ENU), position ``pos0`` and
    velocity ``vel0`` (ENU, m and m/s). Attitude uses a second-order rotation
    vector with the coning term. Velocity and position rotate the specific
    force by ``q_et`` when given (the true attitude, isolating translation),
    else by the integrated attitude.

    Returns the attitude, velocity and position at every sample of ``t``.
    """
    dt = np.diff(t)
    w = np.radians(gyro_dps)
    f = accel_mg * (G0 / 1000.0)
    g = np.array([0.0, 0.0, -G0])  # ENU

    phi = 0.5 * (w[:-1] + w[1:]) * dt[:, None] + np.cross(w[:-1], w[1:]) * (dt[:, None] ** 2) / 12.0
    steps = qexp(phi)
    q = np.empty((len(t), 4))
    q[0] = q0
    for k in range(len(steps)):
        q[k + 1] = qmul(q[k], steps[k])
        if k % 1000 == 999:
            q[k + 1] = qnormalize(q[k + 1])
    q = qnormalize(q)

    f_e = qrotate(q if q_et is None else q_et, f) + g
    dv = 0.5 * (f_e[:-1] + f_e[1:]) * dt[:, None]
    v = vel0 + np.concatenate([[np.zeros(3)], np.cumsum(dv, axis=0)])
    p = pos0 + np.concatenate([[np.zeros(3)],
                               np.cumsum(0.5 * (v[:-1] + v[1:]) * dt[:, None], axis=0)])
    return q, v, p


def strapdown(t: np.ndarray, q_et: np.ndarray, pos: np.ndarray, vel: np.ndarray,
              accel_mg: np.ndarray, gyro_dps: np.ndarray) -> dict:
    """Consistency figures for ideal signals in the tag frame and ENU truth.

    Returns a dict with ``attitude_deg_max``, ``attitude_deg_final``,
    ``velocity_mps_max``, ``velocity_mps_final``, ``position_m_max`` and
    ``position_m_final``, each over the whole run, plus ``duration_s`` and
    ``samples``.
    """
    q, v, p = strapdown_path(t, q_et[0], pos[0], vel[0], accel_mg, gyro_dps, q_et)
    att = np.degrees(angle_between(q, q_et))
    v_err = np.linalg.norm(v - vel, axis=1)
    p_err = np.linalg.norm(p - pos, axis=1)
    return {
        "samples": int(len(t)),
        "duration_s": float(t[-1] - t[0]),
        "attitude_deg_max": float(att.max()),
        "attitude_deg_final": float(att[-1]),
        "velocity_mps_max": float(v_err.max()),
        "velocity_mps_final": float(v_err[-1]),
        "position_m_max": float(p_err.max()),
        "position_m_final": float(p_err[-1]),
    }
