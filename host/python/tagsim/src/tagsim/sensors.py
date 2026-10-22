"""Ideal IMUTag sensor signals, in the axes and units the firmware stores.

This module holds the ideal model and the firmware's storage format; the
datasheet errors that turn ideal values into measurements are in
:mod:`tagsim.errors`. What it does:

- **Accelerometer and gyroscope** (LSM6DSV16X): specific force
  ``R_tn (a_n - g_n)`` in mg and body rate in dps, at every IMU sample time,
  quantized to ``int16`` at tagcore's LSB for the configured range
  (``host/libraries/tagcore/sqlitelog/imutag.cc``) and saturated at full
  scale.
- **Magnetometer** (BMM350): a constant Earth field, given in NED (the WMM
  value for the site and date, see :mod:`tagsim.wmm`), rotated into the tag
  frame at each conversion's true time. The sensor free-runs on its own
  clock (:class:`MagClock`); the firmware polls it after each superframe's
  last IMU sample and takes the newest completed conversion, or writes the
  missing-value NaN if none completed since the last poll.
- **Pressure** (BMP581): the International Standard Atmosphere (troposphere)
  at the true altitude, under a given sea-level pressure, at the same poll
  time. The sensor is taken to be shielded from the airflow. The page
  temperature is the ISA temperature at that altitude.

Axes (docs/shared/sensor-axes.md). The tag frame is x right, y toward the
battery, z up. The model goes tag frame -> each part's own axes -> the
firmware's mapping in ``embedded/tags/families/IMUTag/src/sensors.c``, so the
stored values are what the firmware writes:

- IMU: ``orient_imu_xyz`` gives ``x' = -y, y' = x``, so the part reads
  ``(y_t, -x_t, z_t)`` and the stored values are in the tag frame.
- Magnetometer, IMUTagNandBmp581: the board's ``IMUTAG_MAG_TAG_*`` macros give
  ``x' = y, y' = x, z' = -z``. The contract says the stored magnetometer's
  ``+y, -x, -z`` lie along the tag's ``x, y, z``. That relabelling is only
  determined up to an overall sign by the inclination-spread check that
  established it, and real IMUTagNandBmp581 data fixes the sign the other way
  from a literal reading: the 2026-10-11 qtcalibrate captures score an
  inclination of +63.8 degrees (field below the horizon) under the host
  convention, and a flat bench download of 2026-10-02 (before the macros, so
  stored = part) reads ``mz = +46.8 uT``. So the relabelled stored field is
  ``-B``: the part reads ``(-B_x, B_y, -B_z)``, a 180 degree turn about y,
  and the stored field is ``(B_y, -B_x, B_z)``. With that sign
  ``dataprocessing imu-compass-orientation`` reports the site's dip as
  positive, as it does for real tags.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from .rotations import dcmtoq, qconj, qmul, qrotate
from .trajectory import Trajectory

G0 = 9.80665
ACCEL_MG_PER_LSB = {2: 0.061, 4: 0.122, 8: 0.244, 16: 0.488}
GYRO_DPS_PER_LSB = {125: 0.004375, 250: 0.00875, 500: 0.0175, 1000: 0.035,
                    2000: 0.07, 4000: 0.14}
ODRS_HZ = (100, 200, 400, 800, 1600)

#: ENU from NED.
R_ENU_NED = np.array([[0.0, 1.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, -1.0]])

_BODY_AXES = {"F": (1, 0, 0), "B": (-1, 0, 0), "R": (0, 1, 0), "L": (0, -1, 0),
              "D": (0, 0, 1), "U": (0, 0, -1)}

# ISA troposphere.
_ISA_T0_K = 288.15
_ISA_LAPSE_K_PER_M = 0.0065
_ISA_EXPONENT = G0 * 0.0289644 / (8.3144598 * _ISA_LAPSE_K_PER_M)


def mounting_matrix(spec: str) -> np.ndarray:
    """``R_bt``, tag axes in body (FRD) axes, from a three-letter spec.

    Each letter is the body direction of tag x, y and z in turn: F, B
    (forward, back), R, L (right, left), U, D (up, down). The default ``RFU``
    is the tag lying flat with its battery end forward.
    """
    spec = spec.upper()
    if len(spec) != 3 or any(c not in _BODY_AXES for c in spec):
        raise ValueError(f"mounting {spec!r}: expected three of F B R L U D")
    r = np.array([_BODY_AXES[c] for c in spec], dtype=np.float64).T
    if not np.isclose(np.linalg.det(r), 1.0):
        raise ValueError(f"mounting {spec!r} is not a right-handed rotation")
    return r


@dataclass
class SensorConfig:
    """IMUTag acquisition settings and the phase-1 environment model."""

    odr_hz: int = 100
    accel_range_g: int = 4
    gyro_range_dps: int = 1000
    mounting: str = "RFU"
    #: Earth field, NED, microtesla. ``tagsim run`` sets it from the WMM.
    mag_ned_ut: tuple[float, float, float] = (20.0, -1.5, 47.0)
    sea_level_hpa: float = 1013.25

    def __post_init__(self) -> None:
        if self.odr_hz not in ODRS_HZ:
            raise ValueError(f"ODR {self.odr_hz} Hz: IMUTag accepts {ODRS_HZ}")
        if self.accel_range_g not in ACCEL_MG_PER_LSB:
            raise ValueError(f"accel range {self.accel_range_g} g: one of {sorted(ACCEL_MG_PER_LSB)}")
        if self.gyro_range_dps not in GYRO_DPS_PER_LSB:
            raise ValueError(f"gyro range {self.gyro_range_dps} dps: one of {sorted(GYRO_DPS_PER_LSB)}")
        mounting_matrix(self.mounting)

    @property
    def accel_lsb_mg(self) -> float:
        return ACCEL_MG_PER_LSB[self.accel_range_g]

    @property
    def gyro_lsb_dps(self) -> float:
        return GYRO_DPS_PER_LSB[self.gyro_range_dps]

    def as_dict(self) -> dict:
        return asdict(self)


@dataclass
class MagClock:
    """The BMM350's own conversion clock: conversion ``k`` completes at
    ``t0 + k / rate_hz`` (trajectory time, s)."""

    rate_hz: float
    t0: float

    def newest(self, t: np.ndarray) -> np.ndarray:
        """Index of the newest conversion completed at or before each ``t``."""
        return np.floor((np.asarray(t) - self.t0) * self.rate_hz + 1e-9).astype(np.int64)

    def time(self, k: np.ndarray) -> np.ndarray:
        return self.t0 + np.asarray(k, dtype=np.float64) / self.rate_hz


@dataclass
class IdealSignals:
    """Ideal values at each IMU sample and superframe, before quantization."""

    t: np.ndarray  #: trajectory time (s) of each IMU sample
    q_enu_tag: np.ndarray  #: attitude, tag frame to local ENU
    pos_enu: np.ndarray  #: m
    vel_enu: np.ndarray  #: m/s
    accel_mg: np.ndarray  #: specific force, tag frame
    gyro_dps: np.ndarray  #: body rate, tag frame
    t_aux: np.ndarray  #: auxiliary poll time: each superframe's last IMU sample
    t_mag: np.ndarray  #: true time of the conversion read at each poll; NaN if missed
    mag_ut: np.ndarray  #: field, tag frame, at ``t_mag``; NaN rows if missed
    altitude_m: np.ndarray  #: AMSL, at t_aux
    sea_level_hpa: np.ndarray  #: at t_aux
    pressure_hpa: np.ndarray  #: at t_aux
    temperature_c: np.ndarray  #: at t_aux


def isa_pressure_hpa(altitude_m: np.ndarray, sea_level_hpa: float) -> np.ndarray:
    """ISA troposphere static pressure at geopotential altitude."""
    return sea_level_hpa * (1.0 - _ISA_LAPSE_K_PER_M * altitude_m / _ISA_T0_K) ** _ISA_EXPONENT


def isa_temperature_c(altitude_m: np.ndarray) -> np.ndarray:
    return _ISA_T0_K - _ISA_LAPSE_K_PER_M * np.asarray(altitude_m) - 273.15


def ideal_signals(traj: Trajectory, t_first: float, n_superframes: int,
                  cfg: SensorConfig, origin_alt_m: float = 0.0,
                  mag_clock: MagClock | None = None,
                  sea_level_hpa: np.ndarray | None = None) -> IdealSignals:
    """Ideal sensor values for ``n_superframes`` starting at ``t_first``.

    ``mag_clock`` defaults to conversions completing exactly at each poll (no
    sample age); ``sea_level_hpa``, one value per superframe, defaults to
    ``cfg.sea_level_hpa`` throughout.
    """
    n = n_superframes * 10
    t = t_first + np.arange(n) / cfg.odr_hz
    r_bt = mounting_matrix(cfg.mounting)
    q_bt = dcmtoq(r_bt)
    q_nb, w_b = traj.attitude(t)
    pos, vel, acc = traj.kinematics(t)
    q_nt = qmul(q_nb, q_bt)
    # Specific force in NED: a - g, with g = +G0 down.
    f_n = acc - np.array([0.0, 0.0, G0])
    f_t = qrotate(qconj(q_nt), f_n)
    w_t = w_b @ r_bt  # R_tb w_b, R_tb = R_bt^T
    q_enu_ned = dcmtoq(R_ENU_NED)
    q_et = qmul(q_enu_ned, q_nt)
    i_aux = np.arange(n_superframes) * 10 + 9
    t_aux = t[i_aux]

    # Magnetometer: the newest conversion at each poll, NaN if it was
    # already read at the previous poll.
    if mag_clock is None:
        t_mag = t_aux.copy()
    else:
        k = mag_clock.newest(t_aux)
        t_mag = mag_clock.time(k)
        stale = np.zeros(n_superframes, dtype=bool)
        stale[1:] = k[1:] == k[:-1]
        t_mag[stale] = np.nan
    mag_t = np.full((n_superframes, 3), np.nan)
    ok = ~np.isnan(t_mag)
    if ok.any():
        q_mag, _ = traj.attitude(np.clip(t_mag[ok], traj.t_start, traj.t_end))
        q_mag_t = qmul(q_mag, np.broadcast_to(q_bt, q_mag.shape))
        b_n = np.asarray(cfg.mag_ned_ut, dtype=np.float64)
        mag_t[ok] = qrotate(qconj(q_mag_t), np.broadcast_to(b_n, (int(ok.sum()), 3)))

    alt = origin_alt_m - pos[i_aux, 2]
    slp = (np.full(n_superframes, cfg.sea_level_hpa) if sea_level_hpa is None
           else np.asarray(sea_level_hpa, dtype=np.float64))
    return IdealSignals(
        t=t, q_enu_tag=q_et, pos_enu=pos @ R_ENU_NED.T, vel_enu=vel @ R_ENU_NED.T,
        accel_mg=f_t / G0 * 1000.0, gyro_dps=np.degrees(w_t),
        t_aux=t_aux, t_mag=t_mag, mag_ut=mag_t, altitude_m=alt, sea_level_hpa=slp,
        pressure_hpa=isa_pressure_hpa(alt, slp), temperature_c=isa_temperature_c(alt))


def _quantize(values: np.ndarray, lsb: float, full_scale: float) -> np.ndarray:
    limit = min(round(full_scale / lsb), 32767)
    return np.clip(np.rint(values / lsb), -limit, limit).astype(np.int16)


def _orient_imu_xyz(v: np.ndarray) -> np.ndarray:
    """``orient_imu_xyz`` in sensors.c: ``x' = -y, y' = x``, int16 arithmetic."""
    out = v.copy()
    out[:, 0] = -v[:, 1]  # int16 negation wraps like the C cast
    out[:, 1] = v[:, 0]
    return out


def _imu_part_from_tag(v: np.ndarray) -> np.ndarray:
    return np.stack([v[:, 1], -v[:, 0], v[:, 2]], -1)


def stored_imu_from(accel_mg: np.ndarray, gyro_dps: np.ndarray,
                    cfg: SensorConfig) -> np.ndarray:
    """The ``t_ImuTagImuSample`` values, (n, 6) int16 ``ax ay az gx gy gz``,
    for tag-frame accel (mg) and gyro (dps)."""
    accel = _quantize(_imu_part_from_tag(accel_mg), cfg.accel_lsb_mg, cfg.accel_range_g * 1000.0)
    gyro = _quantize(_imu_part_from_tag(gyro_dps), cfg.gyro_lsb_dps, float(cfg.gyro_range_dps))
    return np.concatenate([_orient_imu_xyz(accel), _orient_imu_xyz(gyro)], axis=1)


def stored_imu(sig: IdealSignals, cfg: SensorConfig) -> np.ndarray:
    """The stored IMU values for the ideal signals (no sensor errors)."""
    return stored_imu_from(sig.accel_mg, sig.gyro_dps, cfg)


def stored_mag(mag_tag_ut: np.ndarray) -> np.ndarray:
    """IMUTagNandBmp581 stored magnetometer values from the tag-frame field."""
    b = np.asarray(mag_tag_ut, dtype=np.float64)
    part = np.stack([-b[:, 0], b[:, 1], -b[:, 2]], -1)  # BMM350 axes (module docstring)
    # IMUTAG_MAG_TAG_X/Y/Z: (my, mx, -mz).
    return np.stack([part[:, 1], part[:, 0], -part[:, 2]], -1).astype(np.float32)


def aux_slots(pressure_hpa: np.ndarray, mag_stored_ut: np.ndarray) -> np.ndarray:
    """The ``t_ImuTagAuxSample`` values: (n_superframes, 4) float32
    ``pressure (hPa), mag_x, mag_y, mag_z (uT)``. NaN marks a missing value;
    the page packer writes it as the firmware's quiet NaN."""
    return np.concatenate([np.asarray(pressure_hpa)[:, None].astype(np.float32),
                           np.asarray(mag_stored_ut).astype(np.float32)], axis=1)


def stored_aux(sig: IdealSignals) -> np.ndarray:
    """The auxiliary slots for the ideal signals (no sensor errors)."""
    return aux_slots(sig.pressure_hpa, stored_mag(sig.mag_ut))


def decode_imu(stored: np.ndarray, cfg: SensorConfig) -> tuple[np.ndarray, np.ndarray]:
    """What tagcore writes for stored samples: accel (mg) and gyro (dps)."""
    return stored[:, :3] * cfg.accel_lsb_mg, stored[:, 3:] * cfg.gyro_lsb_dps


__all__ = ["SensorConfig", "IdealSignals", "MagClock", "ideal_signals", "stored_imu",
           "stored_imu_from", "stored_aux", "aux_slots",
           "stored_mag", "decode_imu", "mounting_matrix", "isa_pressure_hpa",
           "isa_temperature_c"]
