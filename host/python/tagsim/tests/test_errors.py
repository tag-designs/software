"""Phase-2 sensor errors on a tag at rest: the statistics match the
configured datasheet values, a correct calibration recovers the true field,
and every run is reproducible from its seed."""

from __future__ import annotations

import numpy as np
import pytest
from scipy.spatial.transform import Rotation

from tagsim.errors import ErrorConfig, mag_rate_hz, white_sigma
from tagsim.sensors import SensorConfig, decode_imu, stored_mag
from tagsim.simulate import simulate
from tagsim.trajectory import stationary

DURATION_S = 300.0
Q = np.roll(Rotation.from_euler("xyz", [20, -35, 110], degrees=True).as_quat(), 1)


@pytest.fixture(scope="module", params=[(100, 4, 1000), (400, 8, 2000)],
                ids=["100Hz", "400Hz"])
def still(request):
    odr, ar, gr = request.param
    cfg = SensorConfig(odr_hz=odr, accel_range_g=ar, gyro_range_dps=gr)
    # Bias instability off here: its slow wander would bias the white-noise
    # statistics over 300 s (test_bias_instability checks it on its own).
    errors = ErrorConfig(accel_bias_gm_mg=0.0, gyro_bias_gm_dps=0.0)
    sim = simulate(stationary(DURATION_S + 1, Q), cfg, start_unix=0.0,
                   duration_s=DURATION_S, errors=errors, seed=11)
    return sim


def _rms_with_quantization(sigma: float, lsb: float) -> float:
    return float(np.sqrt(sigma ** 2 + lsb ** 2 / 12.0))


def test_imu_noise_and_bias(still):
    sim = still
    cfg, err, draw = sim.cfg, sim.errors, sim.draw
    accel, gyro = decode_imu(sim.imu, cfg)
    # The error left after the drawn sensitivity and misalignment, T true.
    da = accel - sim.signals.accel_mg @ draw.accel_sensitivity.T
    dg = gyro - sim.signals.gyro_dps @ draw.gyro_sensitivity.T
    sa = white_sigma(err.accel_noise_ug_rthz / 1000.0, cfg.odr_hz)
    sg = white_sigma(err.gyro_noise_mdps_rthz / 1000.0, cfg.odr_hz)
    print(f"accel sigma {da.std(axis=0)} vs {sa:.4f} mg; gyro {dg.std(axis=0)} vs {sg:.4f} dps")
    np.testing.assert_allclose(da.std(axis=0), _rms_with_quantization(sa, cfg.accel_lsb_mg),
                               rtol=0.05)
    np.testing.assert_allclose(dg.std(axis=0), _rms_with_quantization(sg, cfg.gyro_lsb_dps),
                               rtol=0.05)
    n = len(da)
    np.testing.assert_allclose(da.mean(axis=0), draw.accel_bias_mg, atol=5 * sa / np.sqrt(n) + 0.01)
    np.testing.assert_allclose(dg.mean(axis=0), draw.gyro_bias_dps, atol=5 * sg / np.sqrt(n) + 0.002)
    assert np.all(np.abs(draw.accel_bias_mg) <= err.accel_bias_mg)
    assert np.all(np.abs(draw.gyro_bias_dps) <= err.gyro_bias_dps)
    assert sim.saturation == {"accel": [0.0] * 3, "gyro": [0.0] * 3}


def test_mag_calibration_recovers_field(still):
    sim = still
    m = sim.aux[:, 1:].astype(np.float64)
    true = stored_mag(sim.signals.mag_ut).astype(np.float64)
    a, v = sim.draw.cal_soft_iron, sim.draw.cal_hard_iron_ut
    corrected = (m - v) @ a.T
    # The raw field is distorted well beyond the noise ...
    assert np.abs(m - true).max() > 1.0
    # ... and the host correction M' = A(M - V) leaves only A times the noise.
    resid = corrected - true
    expected = np.sqrt(np.diag(a @ np.diag(np.square(sim.errors.mag_noise_ut)) @ a.T))
    print(f"mag residual sigma {resid.std(axis=0)} vs {expected}")
    np.testing.assert_allclose(resid.std(axis=0), expected, rtol=0.1)
    np.testing.assert_allclose(resid.mean(axis=0), 0.0, atol=0.05)
    b = np.linalg.norm(sim.signals.mag_ut, axis=1)
    np.testing.assert_allclose(np.linalg.norm(corrected, axis=1).mean(), b.mean(), atol=0.05)


def test_pressure_noise_and_offset(still):
    sim = still
    d_pa = (sim.aux[:, 0].astype(np.float64) - sim.signals.pressure_hpa) * 100.0
    print(f"pressure offset {d_pa.mean():.3f} vs {sim.draw.pressure_offset_pa:.3f} Pa, "
          f"sigma {d_pa.std():.4f} Pa")
    np.testing.assert_allclose(d_pa.std(), sim.errors.pressure_noise_pa, rtol=0.1)
    assert abs(d_pa.mean() - sim.draw.pressure_offset_pa) < 0.1
    # 1/64 Pa steps, within float32 resolution of the stored hPa.
    pa = sim.aux[:, 0].astype(np.float64) * 100.0
    assert np.abs(pa * 64 - np.rint(pa * 64)).max() < 0.01 * 64


def test_mag_sample_age(still):
    sim = still
    sig = sim.signals
    assert not np.isnan(sig.t_mag).any()  # 1.25 conversions per poll: never stale
    age = sig.t_aux - sig.t_mag
    period = 1.0 / sim.draw.mag_rate_hz
    assert age.min() >= -1e-9 and age.max() < period + 1e-9
    # The age sweeps the whole conversion period over the run.
    assert age.max() - age.min() > 0.9 * period
    nominal = mag_rate_hz(sim.cfg.odr_hz)
    assert abs(sim.draw.mag_rate_hz / nominal - 1.0) <= sim.errors.mag_rate_error
    # Conversions are on the sensor's own grid.
    k = (sig.t_mag - sig.t_mag[0]) * sim.draw.mag_rate_hz
    np.testing.assert_allclose(k, np.rint(k), atol=1e-6)


def test_missed_conversion_is_nan():
    """A magnetometer slower than the poll leaves missing-value slots."""
    cfg = SensorConfig()
    errors = ErrorConfig.ideal()
    errors.mag_noise_ut = (0.0, 0.0, 0.0)
    from tagsim.sensors import MagClock, ideal_signals
    sig = ideal_signals(stationary(3.0), 0.0, 20, cfg, mag_clock=MagClock(7.0, -0.01))
    missed = np.isnan(sig.t_mag)
    assert 0 < missed.sum() < 20
    assert np.isnan(sig.mag_ut[missed]).all()


def test_seeded_and_deterministic():
    cfg = SensorConfig()
    traj = stationary(4.0, Q)
    one = simulate(traj, cfg, 0.0, errors=ErrorConfig(), seed=5)
    two = simulate(traj, cfg, 0.0, errors=ErrorConfig(), seed=5)
    other = simulate(traj, cfg, 0.0, errors=ErrorConfig(), seed=6)
    assert [p.data for p in one.pages] == [p.data for p in two.pages]
    assert one.draw.as_dict() == two.draw.as_dict()
    assert [p.data for p in one.pages] != [p.data for p in other.pages]


def test_perturbed_calibration():
    cfg = SensorConfig()
    traj = stationary(2.0)
    true = simulate(traj, cfg, 0.0, errors=ErrorConfig(), seed=5).draw
    pert = simulate(traj, cfg, 0.0, errors=ErrorConfig(calibration="perturbed"), seed=5).draw
    # Same distortion, different table.
    np.testing.assert_array_equal(true.hard_iron_ut, pert.hard_iron_ut)
    np.testing.assert_array_equal(true.cal_hard_iron_ut, true.hard_iron_ut)
    assert np.abs(pert.cal_hard_iron_ut - pert.hard_iron_ut).max() > 0
    np.testing.assert_allclose(pert.cal_soft_iron, pert.cal_soft_iron.T)


def test_ideal_is_noise_free():
    cfg = SensorConfig()
    sim = simulate(stationary(2.0, Q), cfg, 0.0, errors=ErrorConfig.ideal(), seed=5)
    accel, _ = decode_imu(sim.imu, cfg)
    assert np.all(np.abs(accel - sim.signals.accel_mg) <= cfg.accel_lsb_mg / 2 + 1e-9)
    np.testing.assert_allclose(sim.aux[:, 1:], stored_mag(sim.signals.mag_ut), atol=1e-4)


def test_sea_level_walk():
    from tagsim.errors import sea_level_walk
    t = np.arange(0, 3600 * 50, 60.0)
    walk = sea_level_walk(t, 1013.25, ErrorConfig(sea_level_walk_hpa_rth=1.0), 3)
    assert walk[0] == 1013.25
    step = np.diff(walk)
    np.testing.assert_allclose(step.std(), np.sqrt(60 / 3600), rtol=0.05)
    assert np.all(sea_level_walk(t, 1000.0, ErrorConfig(), 3) == 1000.0)


def test_sensitivity_and_misalignment():
    """Without noise or bias, the measurement is T times the truth, with T's
    scale and off-diagonal terms inside their configured ranges."""
    from tagsim.errors import draw_errors, measure_imu
    errors = ErrorConfig.ideal()
    errors.accel_scale_error, errors.gyro_scale_error = 0.01, 0.003
    errors.accel_misalignment_deg = errors.gyro_misalignment_deg = 0.5
    d = draw_errors(errors, 100, seed=4)
    for t, scale in ((d.accel_sensitivity, 0.01), (d.gyro_sensitivity, 0.003)):
        diag = np.diag(t) - 1.0
        off = t - np.diag(np.diag(t))
        assert 0 < np.abs(diag).max() <= scale
        assert 0 < np.abs(off).max() <= np.radians(0.5)
    g = np.random.default_rng(0)
    a_true, w_true = g.normal(0, 3000, (500, 3)), g.normal(0, 400, (500, 3))
    a, w = measure_imu(a_true, w_true, 100, errors, d, seed=4)
    np.testing.assert_allclose(a, a_true @ d.accel_sensitivity.T, atol=1e-9)
    np.testing.assert_allclose(w, w_true @ d.gyro_sensitivity.T, atol=1e-9)
    # A 400 dps rate about one axis leaks about 3.5 dps into the others.
    assert np.abs(d.gyro_sensitivity[:, 0] * 400.0)[1:].max() > 0.5
    # The ideal configuration has none.
    np.testing.assert_array_equal(draw_errors(ErrorConfig.ideal(), 100, 4).gyro_sensitivity,
                                  np.eye(3))


def test_bias_instability():
    """The Gauss-Markov wander has the configured RMS and correlation time."""
    from tagsim.errors import gauss_markov
    g = np.random.default_rng(1)
    odr, tau, sigma = 10.0, 5.0, 0.003
    x = gauss_markov(400_000, sigma, tau, odr, g)
    np.testing.assert_allclose(x.std(axis=0), sigma, rtol=0.05)
    lag = int(tau * odr)
    r = np.mean(x[lag:] * x[:-lag], axis=0) / x.var(axis=0)
    np.testing.assert_allclose(r, np.exp(-1.0), atol=0.05)
    assert not gauss_markov(10, 0.0, tau, odr, g).any()


def test_vibration_band():
    """Band-limited vibration has the configured RMS and no power outside
    its band; it is off by default."""
    from tagsim.errors import band_noise
    odr = 400.0
    x = band_noise(40_000, 50.0, (60.0, 90.0), odr, np.random.default_rng(2))
    np.testing.assert_allclose(x.std(axis=0), 50.0, rtol=1e-6)
    p = np.abs(np.fft.rfft(x, axis=0)) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / odr)
    inside = (f >= 60.0) & (f <= 90.0)
    assert p[~inside].sum() < 1e-9 * p[inside].sum()
    assert ErrorConfig().vibration_accel_mg == ErrorConfig().vibration_gyro_dps == 0.0
    cfg = SensorConfig()
    errors = ErrorConfig.ideal()
    errors.vibration_gyro_dps = 2.0
    sim = simulate(stationary(20.0), cfg, 0.0, errors=errors, seed=3)
    _, gyro = decode_imu(sim.imu, cfg)
    np.testing.assert_allclose(gyro.std(axis=0), 2.0, rtol=0.05)
