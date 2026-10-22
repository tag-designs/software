"""Strapdown consistency of the ideal signals against the spline pose."""

from __future__ import annotations

import numpy as np

from tagsim.consistency import strapdown
from tagsim.rotations import qexp, qmul
from tagsim.sensors import SensorConfig, ideal_signals
from tagsim.trajectory import Trajectory, fit_position


def manoeuvre() -> Trajectory:
    """A 20 s synthetic sequence at 25 Hz: a climbing turn with a roll."""
    dt = 0.04
    t = np.arange(0, 20, dt)
    rate = np.stack([np.radians(180) * np.sin(2 * np.pi * t / 5),        # roll
                     np.radians(20) * np.ones_like(t),                   # pitch
                     np.radians(30) * np.cos(2 * np.pi * t / 10)], -1)   # yaw
    q = [np.array([1.0, 0, 0, 0])]
    for w in rate[:-1]:
        q.append(qmul(q[-1], qexp(w * dt)))
    vel = np.stack([25 * np.cos(t / 3), 25 * np.sin(t / 3), -2 * np.sin(t / 2)], -1)
    pos = np.cumsum(vel * dt, axis=0)
    spline, _ = fit_position(t, pos, vel, dt)
    return Trajectory(0.0, dt, np.array(q), spline)


def test_manoeuvre_consistency():
    traj = manoeuvre()
    cfg = SensorConfig(odr_hz=400)
    sig = ideal_signals(traj, traj.t_start, int((traj.t_end - traj.t_start - 0.1) * 40), cfg)
    report = strapdown(sig.t, sig.q_enu_tag, sig.pos_enu, sig.vel_enu,
                       sig.accel_mg, sig.gyro_dps)
    print(report)
    assert report["attitude_deg_max"] < 0.01
    assert report["velocity_mps_max"] < 1e-3
    assert report["position_m_max"] < 1e-3
