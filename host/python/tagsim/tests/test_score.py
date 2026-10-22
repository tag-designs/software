"""Scoring: the compass decode, the error split, and the viewer export."""

from __future__ import annotations

import numpy as np
from scipy.spatial.transform import Rotation

from tagsim.compass import ideal_compass, implied_attitude, solve_orientation
from tagsim.dataflash import DataFlashLog
from tagsim.errors import ErrorConfig
from tagsim.rotations import angle_between, qexp, qmul
from tagsim.score import Estimate, Truth, attitude_errors, score
from tagsim.sensors import SensorConfig, decode_imu
from tagsim.simulate import simulate
from tagsim.trajectory import stationary
from tagsim.compare import three_quaternions
from tagsim.viewer import body_euler_deg, write_viewer_log

FIELD = (20.5, -1.6, 47.4)


def _random_q(n, seed):
    return np.roll(Rotation.random(n, random_state=seed).as_quat(), 1, axis=1)


def test_implied_attitude_inverts_the_compass():
    """Every attitude, upright or inverted, is recovered from the algorithm's
    output for ideal inputs, including near its branch switch."""
    q = _random_q(500, 3)
    qi, resid = implied_attitude(ideal_compass(q, FIELD), FIELD,
                                 qmul(q, qexp(np.random.default_rng(0).normal(size=(500, 3))
                                               * np.radians(5))))
    assert resid.max() < 1e-5
    assert np.degrees(angle_between(qi, q)).max() < 1e-3


def test_error_split():
    q = _random_q(200, 4)
    # A pure heading change: 5 degrees counter-clockwise about ENU up, which
    # is a compass heading 5 degrees smaller.
    yaw = np.array([np.cos(np.radians(2.5)), 0.0, 0.0, np.sin(np.radians(2.5))])
    e = attitude_errors(qmul(yaw, q), q)
    np.testing.assert_allclose(e["heading_deg"], -5.0, atol=1e-9)
    np.testing.assert_allclose(e["tilt_deg"], 0.0, atol=1e-5)
    np.testing.assert_allclose(e["total_deg"], 5.0, atol=1e-9)
    # A pure tilt: 3 degrees about ENU east.
    tilt = np.array([np.cos(np.radians(1.5)), np.sin(np.radians(1.5)), 0.0, 0.0])
    e = attitude_errors(qmul(tilt, q), q)
    np.testing.assert_allclose(e["tilt_deg"], 3.0, atol=1e-6)
    np.testing.assert_allclose(e["heading_deg"], 0.0, atol=1e-5)


def _still_truth(sim, flying_us):
    n = len(sim.signals.t)
    return Truth(np.arange(n) * 1e6 / sim.cfg.odr_hz, sim.signals.q_enu_tag,
                 sim.signals.vel_enu, sim.signals.pos_enu[:, 2], flying_us, sim.cfg.mag_ned_ut, {})


def test_compass_at_rest_scores_its_errors():
    """The compass on a tag at rest, decoded and scored: zero error without
    sensor errors; with them, the tilt the accelerometer error implies."""
    q0 = _random_q(1, 7)[0]
    for errors, tilt_max in ((ErrorConfig.ideal(), 0.01), (ErrorConfig(), 1.5)):
        cfg = SensorConfig(mag_ned_ut=FIELD)
        sim = simulate(stationary(31.0, q0), cfg, 0.0, duration_s=30.0, errors=errors, seed=2)
        accel, _ = decode_imu(sim.imu, cfg)
        mean_accel = accel.reshape(-1, 10, 3).mean(axis=1)
        d = sim.draw
        mag = (sim.aux[:, 1:].astype(np.float64) - d.cal_hard_iron_ut) @ d.cal_soft_iron.T
        truth = _still_truth(sim, (10e6, 20e6))
        idx = np.arange(len(mag)) * 10
        q_est, _ = implied_attitude(solve_orientation(mag, mean_accel), FIELD,
                                    sim.signals.q_enu_tag[idx])
        est = Estimate("compass", np.zeros(len(idx), dtype=np.int64), idx, truth.elapsed_us[idx],
                       q_est)
        result = score(truth, est)
        assert result.flying.sum() == 101
        s = result.summary["all"]
        assert s["tilt_deg"]["max"] < tilt_max
        # The accelerometer's error at rest: bias plus sensitivity and
        # misalignment acting on gravity.
        f_true = sim.signals.accel_mg[0]
        f_meas = d.accel_sensitivity @ f_true + d.accel_bias_mg
        bias_tilt = np.degrees(np.arctan2(np.linalg.norm(np.cross(f_meas, f_true)),
                                          f_meas @ f_true))
        assert abs(s["tilt_deg"]["median"] - bias_tilt) < 0.05


def test_viewer_log_round_trip(tmp_path):
    n = 2000
    t_us = 5e6 + np.arange(n) * 10_000.0
    q = _random_q(n, 9)
    pos = np.column_stack([np.linspace(0, 100, n), np.linspace(0, -50, n), np.full(n, 30.0)])
    path = tmp_path / "v.bin"
    write_viewer_log(path, t_us, q, pos, (40.0, -86.0, 200.0), 1.689e9, "RFU",
                     t_us[::10], q[::10], pos[::10])
    log = DataFlashLog(path)
    att, ahr = log.messages("ATT"), log.messages("AHR2")
    eul = body_euler_deg(q, "RFU")
    k = np.searchsorted(t_us, att["TimeUS"])
    # ArduPilot's own formats: attitude in centidegrees, yaw unsigned.
    np.testing.assert_allclose(att["Roll"], eul[k, 0], atol=0.006)
    yaw_err = (att["Yaw"] - eul[k, 2] + 180.0) % 360.0 - 180.0
    assert np.abs(yaw_err).max() < 0.006
    np.testing.assert_array_equal(att["DesRoll"], att["Roll"])
    assert len(ahr["TimeUS"]) == n // 10
    pos_msgs = log.messages("POS")
    np.testing.assert_allclose(pos_msgs["Alt"][-1], 230.0, atol=1e-3)
    assert abs(pos_msgs["Lat"][0] - 40.0) < 1e-6
    # XKF1 carries the truth in NED; the velocity is differenced here.
    xkf = log.messages("XKF1")
    np.testing.assert_allclose(xkf["PN"], pos[k, 1], atol=1e-3)
    np.testing.assert_allclose(xkf["PD"], -30.0, atol=1e-3)
    np.testing.assert_allclose(xkf["VE"], 100.0 / ((n - 1) * 0.01), rtol=1e-3)
    # What UAV Log Viewer and FlightCoach require besides.
    for name in ("GPS", "MSG", "MODE", "PARM", "ORGN"):
        assert log.has(name), name


def test_compare_model_frames():
    """The page's model points its nose along the airframe's heading and
    pitch, and banks its right wing down for positive roll."""
    from scipy.spatial.transform import Rotation

    q = _random_q(200, 4)
    r = Rotation.from_quat(three_quaternions(q, "RFU")).as_matrix()
    roll, pitch, yaw = np.radians(body_euler_deg(q, "RFU")).T
    fwd = np.stack([np.cos(pitch) * np.sin(yaw), np.sin(pitch),
                    -np.cos(pitch) * np.cos(yaw)], -1)  # three.js: x east, y up, z south
    np.testing.assert_allclose(r[:, :, 0], fwd, atol=1e-9)
    np.testing.assert_allclose(r[:, 1, 2], -np.sin(roll) * np.cos(pitch), atol=1e-9)
