"""The navigation filter on synthetic motion: the event stream, a tag at
rest, constant-rate rotation, a constant-rate climb, a manoeuvre forward and
smoothed, more than one ODR, and dropped auxiliary samples."""

from __future__ import annotations

import numpy as np
import pytest
from scipy.spatial.transform import Rotation

from tagsim.errors import ErrorConfig
from tagsim.navfilter import (MAG, PRESSURE, FilterConfig, build_events, field_enu_from_ned,
                              poll_offset_s, run_filter, simulated_log)
from tagsim.rotations import qexp, qmul
from tagsim.score import Truth, altitude_error, attitude_errors
from tagsim.sensors import SensorConfig
from tagsim.simulate import simulate
from tagsim.trajectory import Trajectory, fit_position, stationary

FIELD = (20.5, -1.6, 47.4)  # NED, uT
DT = 0.04


def _filter_cfg(**kw) -> FilterConfig:
    return FilterConfig(field_enu_ut=field_enu_from_ned(FIELD), **kw)


def _trajectory(duration_s: float, rate_rps, vel_ned, q0=(1.0, 0.0, 0.0, 0.0)) -> Trajectory:
    """Attitude integrated from a body rate profile, position from a velocity
    profile; both functions of time (s), evaluated on the 25 Hz grid."""
    t = np.arange(-DT, duration_s + 4 * DT, DT)
    w = np.array([rate_rps(x) for x in t])
    q = [np.asarray(q0, dtype=np.float64)]
    for k in range(len(t) - 1):
        q.append(qmul(q[-1], qexp(0.5 * (w[k] + w[k + 1]) * DT)))
    vel = np.array([vel_ned(x) for x in t])
    pos = np.concatenate([[np.zeros(3)], np.cumsum(0.5 * (vel[1:] + vel[:-1]) * DT, axis=0)])
    spline, _ = fit_position(t, pos, vel, DT)
    return Trajectory(t[0], DT, np.array(q), spline)


def _ramp(t, start, length):
    """0 before ``start``, 1 after ``start + length``, a cosine between."""
    u = np.clip((t - start) / length, 0.0, 1.0)
    return 0.5 - 0.5 * np.cos(np.pi * u)


def _run(traj, odr=100, duration_s=None, errors=None, seed=1, drop=None, **cfg):
    sim = simulate(traj, SensorConfig(mag_ned_ut=FIELD, odr_hz=odr), 0.0, duration_s=duration_s,
                   errors=ErrorConfig() if errors is None else errors, seed=seed)
    drop_m = drop_p = None
    if drop is not None:
        drop_m, drop_p = drop(len(sim.aux))
    log = simulated_log(sim, drop_mag=drop_m, drop_pressure=drop_p)
    return sim, log, run_filter(log, _filter_cfg(**cfg))


def _errors(sim, q):
    return attitude_errors(q, sim.signals.q_enu_tag)


def _truth(sim):
    n = len(sim.signals.t)
    return Truth(np.arange(n) * 1e6 / sim.cfg.odr_hz, sim.signals.q_enu_tag, sim.signals.vel_enu,
                 sim.signals.pos_enu[:, 2], None, FIELD, {})


def test_event_stream():
    """Auxiliary events apply at the firmware's poll time, nine IMU periods
    after the slot's stamp, ordered by time, magnetometer first on a tie; the
    log's own time is kept beside it, and the age hook moves only the
    magnetometer."""
    for odr in (100, 400, 1600):
        _, log, _ = _run(stationary(3.0), odr=odr, duration_s=2.0, smooth=False)
        ev = build_events(log, _filter_cfg())
        assert np.all(np.diff(ev.t) >= 0)
        assert (ev.kind == MAG).sum() == len(log.t_mag) == len(log.t_pressure)
        p = ev.kind == PRESSURE
        np.testing.assert_allclose(ev.t[p] - ev.t_log[p], poll_offset_s(odr), atol=1e-9)
        np.testing.assert_allclose(ev.t[~p] - ev.t_log[~p], poll_offset_s(odr) - 4.0 / odr,
                                   atol=1e-9)
        assert set(np.round(ev.t * 1e6)) <= set(np.round(log.t_imu * 1e6))
        aged = build_events(log, _filter_cfg(mag_age_s=0.013))
        m = aged.kind == MAG
        np.testing.assert_allclose(aged.t[m] - aged.t_log[m], poll_offset_s(odr) - 0.013,
                                   atol=1e-9)
        np.testing.assert_allclose(aged.t[aged.kind == PRESSURE] - aged.t_log[aged.kind == PRESSURE],
                                   poll_offset_s(odr), atol=1e-9)


def test_stationary():
    """At rest: exact without sensor errors; with them, the compass's own
    floor (the accelerometer bias tilt), the gyro bias recovered, and the
    altitude and vertical velocity at zero."""
    q0 = np.roll(Rotation.random(1, random_state=7).as_quat(), 1, axis=1)[0]
    sim, _, r = _run(stationary(31.0, q0), duration_s=30.0, errors=ErrorConfig.ideal())
    for q in (r.q_forward, r.q_smoothed):
        assert np.median(_errors(sim, q)["total_deg"]) < 0.02
    sim, _, r = _run(stationary(31.0, q0), duration_s=30.0)
    d = sim.draw
    # The accelerometer's error at rest: its bias plus its sensitivity and
    # misalignment acting on gravity.
    f_true = sim.signals.accel_mg[0]
    f_meas = d.accel_sensitivity @ f_true + d.accel_bias_mg
    bias_tilt = np.degrees(np.arctan2(np.linalg.norm(np.cross(f_meas, f_true)),
                                      f_meas @ f_true))
    e = _errors(sim, r.q_smoothed)
    assert abs(np.median(e["tilt_deg"]) - bias_tilt) < 0.1
    assert np.median(e["total_deg"]) < 3.0
    np.testing.assert_allclose(np.degrees(r.bias_smoothed[-1]), d.gyro_bias_dps, atol=0.02)
    assert np.abs(r.alt_smoothed).max() < 0.1
    assert np.abs(r.vz_smoothed).max() < 0.05


@pytest.mark.parametrize("axis", [(0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.3, 0.8, 0.5)])
def test_constant_rate_rotation(axis):
    """A tag turning at 60 dps about a fixed body axis, after a still start:
    the attitude holds to about a degree and the gyro bias is kept."""
    w = np.radians(60.0) * np.asarray(axis) / np.linalg.norm(axis)
    traj = _trajectory(40.0, lambda t: w * _ramp(t, 5.0, 1.0), lambda t: np.zeros(3))
    sim, _, r = _run(traj, duration_s=40.0)
    moving = sim.signals.t > 6.5
    for q in (r.q_forward, r.q_smoothed):
        e = _errors(sim, q)
        assert np.median(e["total_deg"][moving]) < 1.5
        assert np.percentile(e["total_deg"][moving], 95) < 3.0
    # The sensitivity and misalignment error, (T - I) w, about 0.4 dps here,
    # is corrected as attitude by the magnetometer rather than absorbed into
    # the slowly walking bias, which stays near the turn-on bias (the
    # Gauss-Markov wander and the partial absorption are the margin).
    np.testing.assert_allclose(np.degrees(r.bias_smoothed[-1]), sim.draw.gyro_bias_dps,
                               atol=0.1)


def test_constant_rate_climb():
    """A level tag climbing at 3 m/s after a still start: the altitude change
    and the vertical velocity follow, and turning the pressure channel off
    leaves no altitude states."""
    traj = _trajectory(40.0, lambda t: np.zeros(3),
                       lambda t: np.array([0.0, 0.0, -3.0]) * _ramp(t, 5.0, 2.0))
    sim, _, r = _run(traj, duration_s=40.0)
    truth = _truth(sim)
    t_us = truth.elapsed_us
    climbing = sim.signals.t > 8.0
    for alt, vz in ((r.alt_forward, r.vz_forward), (r.alt_smoothed, r.vz_smoothed)):
        assert np.abs(altitude_error(truth, t_us, alt)[climbing]).max() < 0.3
        assert np.median(np.abs(vz - sim.signals.vel_enu[:, 2])[climbing]) < 0.05
    assert abs(r.alt_smoothed[-1] - r.alt_smoothed[0] - 3.0 * (sim.signals.t[-1] - 6.0)) < 0.3
    _, _, off = _run(traj, duration_s=40.0, use_pressure=False)
    assert off.alt_smoothed is None and off.vz_forward is None


def _manoeuvre() -> Trajectory:
    """5 s still, then 35 s of rolling, pitching and yawing while climbing and
    turning, as a pattern aircraft would."""
    def rate(t):
        g = _ramp(t, 5.0, 1.0)
        return g * np.array([np.radians(150) * np.sin(2 * np.pi * t / 6),
                             np.radians(25) * np.sin(2 * np.pi * t / 9),
                             np.radians(40) * np.cos(2 * np.pi * t / 11)])

    def vel(t):
        g = _ramp(t, 5.0, 3.0)
        return g * np.array([20 * np.cos(t / 4), 20 * np.sin(t / 4), -3 * np.sin(t / 3)])

    return _trajectory(40.0, rate, vel)


def test_smoother_not_worse_than_forward():
    sim, _, r = _run(_manoeuvre(), duration_s=40.0)
    moving = sim.signals.t > 6.0
    ef = _errors(sim, r.q_forward)["total_deg"][moving]
    es = _errors(sim, r.q_smoothed)["total_deg"][moving]
    print(f"forward {np.median(ef):.3f} / {np.percentile(ef, 95):.3f}, "
          f"smoothed {np.median(es):.3f} / {np.percentile(es, 95):.3f}")
    assert np.median(es) <= np.median(ef) * 1.05
    assert np.percentile(es, 95) <= np.percentile(ef, 95) * 1.05
    assert np.median(es) < 2.0


@pytest.mark.parametrize("odr", [100, 400])
def test_multi_odr(odr):
    """The same manoeuvre at two IMU rates, with their matching auxiliary
    rates, and no change to the filter."""
    sim, log, r = _run(_manoeuvre(), odr=odr, duration_s=40.0)
    assert r.stats["mag_updates"] == len(log.t_mag) == len(log.t_imu) // 10
    assert r.stats["pressure_updates"] == len(log.t_pressure)
    moving = sim.signals.t > 6.0
    e = _errors(sim, r.q_smoothed)
    assert np.median(e["total_deg"][moving]) < 2.0
    truth = _truth(sim)
    assert np.median(np.abs(altitude_error(truth, truth.elapsed_us, r.alt_smoothed))) < 0.2


def test_dropped_aux_samples():
    """A third of the magnetometer and pressure rows missing at random, both
    missing for 3 s, and magnetometer events moved off the IMU grid by the
    age hook (so they are applied between IMU samples): still good."""
    def drop(n):
        g = np.random.default_rng(5)
        m = g.random(n) < 0.33
        p = g.random(n) < 0.33
        gap = slice(n // 2, n // 2 + 30)
        m[gap] = p[gap] = True
        return m, p

    sim, log, r = _run(_manoeuvre(), odr=400, duration_s=40.0, drop=drop, mag_age_s=0.0013)
    n_sf = len(sim.aux)
    assert len(log.t_mag) < 0.75 * n_sf and len(log.t_pressure) < 0.75 * n_sf
    ev = build_events(log, _filter_cfg(mag_age_s=0.0013))
    on_grid = np.isin(np.round(ev.t * 1e6), np.round(log.t_imu * 1e6))
    assert not on_grid[ev.kind == MAG].any() and on_grid[ev.kind == PRESSURE].all()
    assert r.stats["mag_updates"] == len(log.t_mag)
    moving = sim.signals.t > 6.0
    e = _errors(sim, r.q_smoothed)
    assert np.median(e["total_deg"][moving]) < 2.0
    truth = _truth(sim)
    assert np.median(np.abs(altitude_error(truth, truth.elapsed_us, r.alt_smoothed))) < 0.2


def test_accel_bias_states():
    """With the accelerometer bias states, the manoeuvre's many attitudes
    make the bias observable: recovered to under 1 mg per axis (with the
    accelerometer's sensitivity and misalignment off, which would otherwise
    make the apparent bias attitude-dependent), with the attitude as good."""
    from tagsim.sensors import G0
    errors = ErrorConfig(accel_scale_error=0.0, accel_misalignment_deg=0.0, accel_bias_gm_mg=0.0)
    sim, _, r = _run(_manoeuvre(), duration_s=40.0, errors=errors, accel_bias_states=True)
    np.testing.assert_allclose(r.accel_bias_smoothed[0] * 1000.0 / G0, sim.draw.accel_bias_mg,
                               atol=1.0)
    moving = sim.signals.t > 6.0
    assert np.median(_errors(sim, r.q_smoothed)["total_deg"][moving]) < 2.0
    _, _, off = _run(_manoeuvre(), duration_s=40.0, errors=errors, accel_bias_states=False)
    assert off.accel_bias_smoothed is None


_RSS_SCRIPT = r'''
import json, resource, sys, numpy as np
from tagsim.navfilter import FilterConfig, ImuTagLog, run_filter
from tagsim.sensors import G0
n, odr, threshold, directory = int(sys.argv[1]), 1600.0, float(sys.argv[2]), sys.argv[3]
g = np.random.default_rng(0)
t = np.arange(n) / odr
field = np.array([-1.66, 20.52, -47.45])
m = n // 10
log = ImuTagLog(0, odr, 4.0, t, np.arange(n),
                np.array([0.0, 0.0, G0]) + 0.004 * g.standard_normal((n, 3)),
                np.radians(0.3) + np.radians(0.06) * g.standard_normal((n, 3)),
                np.zeros(n, dtype=bool), t[::10][:m], field + 0.2 * g.standard_normal((m, 3)),
                t[::10][:m], 1000.0 + 0.004 * g.standard_normal(m), 0.0)
scale = 1024 if sys.platform.startswith("linux") else 1
before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * scale
r = run_filter(log, FilterConfig(field_enu_ut=tuple(field), memmap_threshold_mb=threshold,
                                 memmap_dir=directory, memmap_window_nodes=4096))
after = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * scale
print(json.dumps({"growth_mb": (after - before) / 2 ** 20, "storage": r.stats["storage"],
                  "q_end": r.q_smoothed[-1].tolist()}))
'''


def test_file_backed_storage_bounds_memory(tmp_path):
    """A synthetic run long enough that its smoother storage dominates: kept
    in files, the peak resident memory grows by about the outputs alone, not
    by the storage, and the result matches the in-memory run's."""
    import json
    import subprocess
    import sys

    n = 200_000  # 125 s at 1600 Hz, 220 000 nodes
    runs = {}
    for label, threshold in (("memory", 1e6), ("file", 0.0)):
        done = subprocess.run([sys.executable, "-c", _RSS_SCRIPT, str(n), str(threshold),
                               str(tmp_path)], capture_output=True, text=True, check=True)
        runs[label] = json.loads(done.stdout)
    storage_mb = runs["file"]["storage"]["max_mb"]
    outputs_mb = 2 * n * 12 * 8 / 2 ** 20  # forward and smoothed states per sample
    print({k: round(v["growth_mb"], 1) for k, v in runs.items()},
          f"storage {storage_mb} MB, outputs {outputs_mb:.0f} MB")
    assert runs["file"]["storage"]["file_backed"] and not runs["memory"]["storage"]["file_backed"]
    assert runs["file"]["q_end"] == runs["memory"]["q_end"]
    assert list(tmp_path.iterdir()) == []
    # Peak RSS reuses memory freed while building the log, so compare the
    # two runs with each other: the file-backed one saves most of the storage.
    assert runs["memory"]["growth_mb"] - runs["file"]["growth_mb"] > 0.5 * storage_mb
    assert runs["file"]["growth_mb"] < outputs_mb + 0.25 * storage_mb
