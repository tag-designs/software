"""A tag at rest: one g of specific force, no rotation, a constant dip."""

from __future__ import annotations

import numpy as np
import pytest
from scipy.spatial.transform import Rotation

from tagsim.sensors import SensorConfig, decode_imu, ideal_signals, stored_aux, stored_imu
from tagsim.trajectory import stationary

ATTITUDES = [np.array([1.0, 0.0, 0.0, 0.0])] + [
    np.roll(Rotation.random(random_state=k).as_quat(), 1) for k in range(8)]


@pytest.mark.parametrize("q", ATTITUDES)
@pytest.mark.parametrize("accel_range,gyro_range", [(4, 1000), (16, 125)])
def test_at_rest(q, accel_range, gyro_range):
    cfg = SensorConfig(accel_range_g=accel_range, gyro_range_dps=gyro_range)
    sig = ideal_signals(stationary(3.0, q), 0.0, 20, cfg)
    np.testing.assert_allclose(np.linalg.norm(sig.accel_mg, axis=1), 1000.0, atol=1e-9)
    np.testing.assert_allclose(sig.gyro_dps, 0.0, atol=1e-9)
    accel, gyro = decode_imu(stored_imu(sig, cfg), cfg)
    # Within half an LSB per axis of the ideal value.
    assert np.all(np.abs(accel - sig.accel_mg) <= cfg.accel_lsb_mg / 2 + 1e-9)
    assert np.all(gyro == 0.0)
    assert abs(np.linalg.norm(accel[0]) - 1000.0) <= np.sqrt(3) * cfg.accel_lsb_mg / 2


def test_flat_tag_reads_plus_z():
    cfg = SensorConfig()
    sig = ideal_signals(stationary(1.0), 0.0, 1, cfg)
    accel, _ = decode_imu(stored_imu(sig, cfg), cfg)
    np.testing.assert_allclose(accel[0], [0.0, 0.0, 8197 * 0.122], atol=0.01)


def test_dip_is_constant_and_positive():
    """The host convention (docs/shared/sensor-axes.md): the stored
    magnetometer's +y, -x, -z along the accelerometer's x, y, z, as
    check_orientation_frames.py scores it."""
    cfg = SensorConfig()
    n, e, d = cfg.mag_ned_ut
    expected = np.degrees(np.arctan2(d, np.hypot(n, e)))
    dips = []
    for q in ATTITUDES:
        sig = ideal_signals(stationary(1.0, q), 0.0, 1, cfg)
        m = stored_aux(sig)[0, 1:].astype(np.float64)
        accel, _ = decode_imu(stored_imu(sig, cfg), cfg)
        mm = np.array([m[1], -m[0], -m[2]])
        a = accel[0] / np.linalg.norm(accel[0])
        dips.append(np.degrees(np.arcsin(mm @ a / np.linalg.norm(mm))))
    np.testing.assert_allclose(dips, expected, atol=0.05)
