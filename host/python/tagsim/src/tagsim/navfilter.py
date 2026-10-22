"""A basic multi-rate post-processing filter for IMUTag logs: attitude,
gyro and accelerometer bias, and relative altitude with vertical velocity.

It is a prototype for the later C++ port into the Qt-free ``sensoranalysis``
core; the design and its results are in ``design/navigation-filter.md``.

**Event stream.** The decoded log becomes one time-ordered stream
(:func:`build_events`). The IMU (gyro and accelerometer, one event per
sample) runs at the configured ODR; the magnetometer and the pressure sensor
each give one event per ``ImuMag`` / ``ImuPressure`` row, so their rates
follow the ODR too. Missing values (NaN slots, absent rows) and gaps are
simply absent events. Nothing assumes a fixed ratio between the sensors.

Every auxiliary event carries two times:

- ``t``: when the filter applies it, the measurement time it believes;
- ``t_log``: the row's own ``ElapsedUs``.

``t`` is ``t_log`` plus the firmware's poll offset: the auxiliary slot is
stamped with its superframe's first IMU sample, but the firmware polls the
magnetometer and the pressure sensor after copying the superframe's last one
(``sampleDataCollection`` in ``families/IMUTag/src/sensors.c``), nine IMU
periods later. A magnetometer reading is also older than its poll by an
age the log does not record: anything from 0 to one conversion period of the
BMM350's free-running clock (0.8 of a superframe at the rate the firmware
selects). That clock has a fixed phase and a rate a little off nominal, so
each reading is the newest conversion ``t0 + k T`` completed at its poll;
:func:`estimate_mag_clock` fits ``t0`` and ``T`` from a first,
attitude-only smoothing pass, and each magnetometer event is then applied
at its own conversion time (:func:`mag_conversion_times`). Without a clock,
the filter subtracts the **mean** age, half a nominal period, and treats the
rest as noise that grows with the turn rate; :attr:`FilterConfig.mag_age_s`
sets a constant age explicitly.

**Filter.** An error-state EKF over the nominal state

- ``q``: attitude, tag frame to ENU (as tagsim's ``TruthPose``);
- ``b``: gyro bias, rad/s, tag frame;
- ``K``: the gyro's scale-factor and misalignment correction, the corrected
  rate being ``(I + K)(w_m - b)`` (``gyro_cal_states``);
- ``ba``: accelerometer bias, m/s^2, tag frame (``accel_bias_states``);
- ``dm``: a magnetometer offset left by the log's calibration, uT, tag frame
  (``mag_bias_states``);
- ``h``, ``vz``: altitude relative to the first pressure sample, m, and
  vertical velocity, m/s (dropped when the pressure channel is off),

with the attitude error ``dtheta`` taken in ENU: ``q_true = exp(dtheta) q``.
Between consecutive events it integrates the gyro (bias removed) and the
vertical component of the rotated specific force plus gravity, interpolating
the IMU samples linearly to an auxiliary event that falls between them. The
updates are:

- **accelerometer**, at every IMU sample: the specific force against gravity,
  with a noise that grows with ``||f| - g|`` and with the turn rate (the
  centripetal acceleration of an aircraft turning at ``|w|`` is about
  ``|w| v``), so manoeuvres down-weight it smoothly;
- **magnetometer**, per event: the calibrated field (the log's
  ``Calibration`` table) against the WMM field, gated on its magnitude, with
  a noise that grows with the turn rate times the sample age it cannot see;
- **pressure**, per event: ISA altitude from pressure under a constant
  reference, relative to the first sample, against ``h``. No sea-level drift
  is assumed.

A forward pass stores, at every node, the filtered nominal state, the RTS
gain and the node's time (:class:`NodeStore`: plain arrays, or windowed
``numpy.memmap`` files above ``memmap_threshold_mb``); a backward
Rauch-Tung-Striebel pass then smooths the whole record, recomputing each
prediction from the stored state rather than storing it.
"""

from __future__ import annotations

import json
import math
import sqlite3
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np

from .rotations import qconj, qmul, qrotate, qtodcm
from .sensors import ACCEL_MG_PER_LSB, G0, R_ENU_NED

#: IMU samples per superframe; the firmware polls the auxiliary sensors once
#: per superframe, after its last IMU sample.
SUPERFRAME = 10

_ISA_T0_K = 288.15
_ISA_LAPSE_K_PER_M = 0.0065
_ISA_EXPONENT = G0 * 0.0289644 / (8.3144598 * _ISA_LAPSE_K_PER_M)

#: Nominal-state columns stored per node: q (4), gyro bias (3), h, vz,
#: accelerometer bias (3; zero when not estimated), and the gyro correction
#: matrix ``K`` (9, row-major; zero when not estimated): the corrected rate is
#: ``(I + K)(w_m - b)``; then the magnetometer offset ``dm`` (3, uT, tag
#: frame; zero when not estimated).
NOMINAL = 24

#: Event kinds in the stream.
IMU, MAG, PRESSURE = 0, 1, 2


def isa_altitude_m(pressure_hpa, sea_level_hpa: float = 1013.25):
    """ISA troposphere altitude (m) of a static pressure."""
    return _ISA_T0_K / _ISA_LAPSE_K_PER_M * (
        1.0 - (np.asarray(pressure_hpa, dtype=np.float64) / sea_level_hpa) ** (1.0 / _ISA_EXPONENT))


# ---------------------------------------------------------------------------
# The decoded log


@dataclass
class ImuTagLog:
    """One segment of a decoded IMUTag log, in the filter's units."""

    segment_id: int
    odr_hz: float
    accel_range_g: float
    t_imu: np.ndarray  #: s, from ElapsedUs
    sample_index: np.ndarray
    accel_mps2: np.ndarray  #: specific force, tag frame
    gyro_rps: np.ndarray  #: tag frame
    accel_saturated: np.ndarray  #: bool per sample: some axis at full scale
    t_mag: np.ndarray  #: s, the rows' own ElapsedUs
    mag_ut: np.ndarray  #: calibrated, tag frame
    t_pressure: np.ndarray  #: s, ElapsedUs
    pressure_hpa: np.ndarray
    epoch_unix: float  #: UTC of the segment's first sample


def mag_stored_to_tag(m: np.ndarray) -> np.ndarray:
    """IMUTagNandBmp581's stored magnetometer axes to the tag frame.

    The stored field is ``(B_y, -B_x, B_z)`` (see ``tagsim.sensors``)."""
    m = np.asarray(m, dtype=np.float64)
    return np.stack([-m[..., 1], m[..., 0], m[..., 2]], -1)


def read_calibration(con: sqlite3.Connection) -> tuple[np.ndarray, np.ndarray]:
    """Hard iron V and soft iron A from the log's newest ``Calibration`` row;
    the identity if there is none. The correction is ``A (M - V)``."""
    row = con.execute("SELECT Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1").fetchone()
    v, a = np.zeros(3), np.eye(3)
    if row:
        m = json.loads(row[0]).get("magnetometer", {})
        v = np.array([float(m.get(f"v{i}", 0.0)) for i in range(3)])
        a = np.array([[float(m.get(f"a{i}{j}", 1.0 if i == j else 0.0)) for j in range(3)]
                      for i in range(3)])
    return v, a


def read_log(path: Path) -> list[ImuTagLog]:
    """Every segment of a decoded IMUTag SQLite log."""
    con = sqlite3.connect(path)
    try:
        info = dict(con.execute("SELECT fieldname, value FROM info").fetchall())
        cfg = json.loads(info.get("config", "{}")).get("lsm6", {})
        accel_range = float(str(cfg.get("accel_rng", "R4G")).strip("RG") or 4)
        v, a = read_calibration(con)
        segs = con.execute("SELECT SegmentId, ConfiguredOdrHz, Epoch, Millisecond FROM ImuSegment "
                           "ORDER BY SegmentId").fetchall()
        out = []
        for seg, odr, epoch, ms in segs:
            def rows(sql):
                r = con.execute(sql, (seg,)).fetchall()
                return np.array(r, dtype=np.float64).reshape(len(r), -1)

            acc = rows("SELECT SampleIndex, ElapsedUs, ax, ay, az FROM ImuAccel "
                       "WHERE SegmentId = ? ORDER BY SampleIndex")
            gyr = rows("SELECT gx, gy, gz FROM ImuGyro WHERE SegmentId = ? ORDER BY SampleIndex")
            mag = rows("SELECT ElapsedUs, mx, my, mz FROM ImuMag WHERE SegmentId = ? "
                       "ORDER BY SampleIndex")
            prs = rows("SELECT ElapsedUs, Pressure FROM ImuPressure WHERE SegmentId = ? "
                       "ORDER BY SampleIndex")
            if len(acc) == 0:
                continue
            lsb = ACCEL_MG_PER_LSB.get(int(accel_range), 0.122)
            limit_mg = min(accel_range * 1000.0, 32767 * lsb) - 1.5 * lsb
            mag = mag[np.all(np.isfinite(mag), axis=1)]
            prs = prs[np.all(np.isfinite(prs), axis=1)]
            m_cal = (mag[:, 1:4] - v) @ a.T
            out.append(ImuTagLog(
                segment_id=int(seg), odr_hz=float(odr), accel_range_g=accel_range,
                t_imu=acc[:, 1] * 1e-6, sample_index=acc[:, 0].astype(np.int64),
                accel_mps2=acc[:, 2:5] * (G0 / 1000.0), gyro_rps=np.radians(gyr),
                accel_saturated=np.any(np.abs(acc[:, 2:5]) >= limit_mg, axis=1),
                t_mag=mag[:, 0] * 1e-6, mag_ut=mag_stored_to_tag(m_cal),
                t_pressure=prs[:, 0] * 1e-6, pressure_hpa=prs[:, 1],
                epoch_unix=float(epoch) + float(ms) / 1000.0))
        return out
    finally:
        con.close()


def simulated_log(sim, drop_mag: np.ndarray | None = None,
                  drop_pressure: np.ndarray | None = None) -> ImuTagLog:
    """The log a :class:`tagsim.simulate.Simulation` decodes to, without the
    page round trip through ``tag-log-import``: the stored values, scaled as
    tagcore scales them, keyed by ``ElapsedUs`` as ``write_truth`` keys them.

    ``drop_mag`` and ``drop_pressure`` are boolean masks over superframes
    whose rows to leave out, as a log with missing auxiliary slots would.
    """
    from .sensors import decode_imu

    cfg = sim.cfg
    accel_mg, gyro_dps = decode_imu(sim.imu, cfg)
    n = len(sim.imu)
    t = np.arange(n) * (1_000_000 // cfg.odr_hz) * 1e-6
    t_aux = t[::SUPERFRAME][:len(sim.aux)]
    aux = sim.aux.astype(np.float64)
    keep_m = np.all(np.isfinite(aux[:, 1:4]), axis=1)
    keep_p = np.isfinite(aux[:, 0])
    if drop_mag is not None:
        keep_m &= ~np.asarray(drop_mag, dtype=bool)
    if drop_pressure is not None:
        keep_p &= ~np.asarray(drop_pressure, dtype=bool)
    if sim.draw is None:
        v, a = np.zeros(3), np.eye(3)
    else:
        v, a = sim.draw.cal_hard_iron_ut, sim.draw.cal_soft_iron
    lsb = cfg.accel_lsb_mg
    limit_mg = min(cfg.accel_range_g * 1000.0, 32767 * lsb) - 1.5 * lsb
    return ImuTagLog(
        segment_id=0, odr_hz=float(cfg.odr_hz), accel_range_g=float(cfg.accel_range_g),
        t_imu=t, sample_index=np.arange(n), accel_mps2=accel_mg * (G0 / 1000.0),
        gyro_rps=np.radians(gyro_dps), accel_saturated=np.any(np.abs(accel_mg) >= limit_mg, axis=1),
        t_mag=t_aux[keep_m], mag_ut=mag_stored_to_tag((aux[keep_m, 1:4] - v) @ a.T),
        t_pressure=t_aux[keep_p], pressure_hpa=aux[keep_p, 0], epoch_unix=sim.start_unix)


# ---------------------------------------------------------------------------
# Configuration


@dataclass
class FilterConfig:
    """Tuning and switches. Noise figures are 1-sigma."""

    use_pressure: bool = True  #: altitude and vertical-velocity states, pressure updates
    #: Earth field, ENU, uT (the WMM value at the site and date).
    field_enu_ut: tuple[float, float, float] = (-1.66, 20.52, -47.45)
    # Process noise.
    gyro_noise_dps_rthz: float = 0.01  #: angle random walk, with model margin
    gyro_bias_walk_dps_rts: float = 1e-4  #: bias random walk, dps per sqrt(s)
    accel_vertical_noise_mps2_rthz: float = 0.2  #: vertical-channel process noise
    accel_saturated_mps2: float = 20.0  #: extra vertical noise over a clipped sample
    # Accelerometer update; the sigma is per sample at 100 Hz and scales with
    # sqrt(ODR / 100) so the information per second does not depend on the ODR.
    accel_sigma_mps2: float = 0.5
    accel_sigma_per_excess: float = 3.0  #: added per m/s^2 of ||f| - g|
    accel_speed_mps: float = 25.0  #: |w| * this is added (centripetal acceleration)
    accel_gate_g: float = 1.0  #: skip the update when ||f| - g| exceeds this
    # Magnetometer update.
    mag_sigma_ut: float = 1.0
    mag_gate_ut: float = 5.0  #: skip when ||m| - |B|| exceeds this
    #: Age (s) subtracted from each magnetometer event's poll time, for
    #: every reading; None: from the magnetometer clock if it is known or
    #: estimated, else the mean, half the nominal BMM350 period.
    mag_age_s: float | None = None
    #: SD (s) of the age left after a constant age; None: a uniform age's
    #: over one nominal period. Multiplied by the turn rate and the field, it
    #: is the magnetometer noise the unrecorded age adds.
    mag_age_sigma_s: float | None = None
    #: The BMM350's conversion clock, in log time: conversion k completes at
    #: ``mag_clock_t0_s + k * mag_clock_period_s``; each reading is the newest
    #: conversion completed at its poll. None: estimated (below) or unknown.
    mag_clock_t0_s: float | None = None
    mag_clock_period_s: float | None = None
    #: Estimate that clock (its phase and rate) from a first, attitude-only
    #: smoothing pass when it is not given.
    mag_clock_estimate: bool = True
    mag_clock_sigma_s: float = 0.0005  #: age SD left once the clock is known
    mag_clock_min_rate_dps: float = 30.0  #: readings used to fit it turn at least this fast
    mag_clock_rate_range: float = 0.03  #: fractional rate error searched (the sheet's +/-2 %)
    mag_clock_window_s: float = 900.0  #: span of the coarse periodogram search
    # Pressure update.
    pressure_sigma_m: float = 0.06
    sea_level_hpa: float = 1013.25  #: constant ISA reference
    # Initialisation.
    still_gyro_dps: float = 0.3  #: per-superframe gyro SD below which the tag is still
    still_accel_mg: float = 10.0  #: per-superframe accel SD below which the tag is still
    still_rate_dps: float = 3.0  #: and mean rate below this (a turn-on bias is up to ~1.7)
    still_min_s: float = 2.0  #: shortest still run used
    init_attitude_deg: float = 2.0  #: from a still period at the start
    init_attitude_moving_deg: float = 30.0  #: from one sample, when the start is not still
    init_bias_dps: float = 0.05  #: from still periods
    init_bias_unknown_dps: float = 1.0  #: when there are none
    #: When the start is not still, seed the attitude and bias from a first,
    #: attitude-only smoothing pass, to this accuracy.
    bootstrap: bool = True
    init_attitude_bootstrap_deg: float = 3.0
    init_vertical_fit_s: float = 0.5  #: pressure span fitted for a moving start's altitude and vz
    init_vz_moving_mps: float = 2.0  #: its vz sigma
    #: Gyro scale-factor and misalignment states: the nine elements of ``K``
    #: in the corrected rate ``(I + K)(w_m - b)``, so that ``(I + K)^-1`` is
    #: the gyro's sensitivity and misalignment matrix ``T`` (measured =
    #: ``T`` true + bias).
    gyro_cal_states: bool = True
    init_gyro_scale: float = 0.003  #: diagonal sigma (the LSM6DSV's G_So%, +/-0.3 %)
    init_gyro_misalignment_deg: float = 0.5  #: off-diagonal sigma (the simulator's +/-0.5 deg)
    gyro_cal_walk_rts: float = 0.0  #: random walk of each element, per sqrt(s)
    #: Accelerometer bias states (tag frame), estimated alongside the gyro bias.
    accel_bias_states: bool = True
    #: Magnetometer offset states: a residual hard iron ``dm`` (uT, tag
    #: frame) left by the log's calibration, the field read as ``m - dm``.
    mag_bias_states: bool = True
    init_mag_bias_ut: float = 2.0  #: their sigma
    init_accel_bias_mg: float = 10.0  #: their initial sigma (turn-on bias up to 12 mg)
    accel_bias_walk_mg_rts: float = 0.01  #: their random walk, mg per sqrt(s)
    smooth: bool = True
    # Smoother storage (see NodeStore).
    #: Above this many MiB of per-node storage, keep it in memory-mapped files.
    memmap_threshold_mb: float = 1024.0
    memmap_dir: str | None = None  #: directory for those files; None: the system temp dir
    memmap_window_nodes: int = 1 << 16  #: rows mapped at a time per file
    gain_float32: bool = True  #: store the RTS gains as float32 (float64 if False)

    def as_dict(self) -> dict:
        return asdict(self)


# ---------------------------------------------------------------------------
# Event stream


@dataclass
class Events:
    """The time-ordered auxiliary events of one segment.

    The IMU samples are the stream's backbone (``ImuTagLog.t_imu``); these
    are the events between and on them, sorted by ``t``.
    """

    kind: np.ndarray  #: MAG or PRESSURE
    t: np.ndarray  #: s, the time the filter applies the event at
    t_log: np.ndarray  #: s, the row's own ElapsedUs
    index: np.ndarray  #: row in the log's mag or pressure arrays
    #: s, SD of the magnetometer age left unknown (0 for pressure events)
    age_sigma: np.ndarray | None = None


def poll_offset_s(odr_hz: float) -> float:
    """Firmware poll time after the auxiliary slot's stamp: the superframe's
    last IMU sample, nine IMU periods after its first."""
    return (SUPERFRAME - 1) / odr_hz


def mag_age_s(cfg: FilterConfig, odr_hz: float) -> float:
    """The constant magnetometer age subtracted from its poll time when the
    clock is not known: ``mag_age_s``, else half a nominal period."""
    from .errors import mag_rate_hz

    return cfg.mag_age_s if cfg.mag_age_s is not None else 0.5 / mag_rate_hz(odr_hz)


def _us(t):
    return np.rint(np.asarray(t, dtype=np.float64) * 1e6)


def mag_conversion_times(log: ImuTagLog, cfg: FilterConfig) -> tuple[np.ndarray, np.ndarray]:
    """Each magnetometer reading's conversion time (s, whole microseconds)
    and the SD (s) of its error.

    With the clock known (``mag_clock_t0_s``, ``mag_clock_period_s``, and no
    ``mag_age_s``) a reading polled at ``t_p`` is conversion
    ``floor((t_p - t0) / T)``, completed at ``t0 + k T``, with an SD of
    ``mag_clock_sigma_s``; a reading within twice that of a conversion
    boundary could be either conversion, so it gets the SD of an age
    unknown over one period. Otherwise every reading is ``mag_age_s`` (or
    half a nominal period) older than its poll, with that same SD unless
    ``mag_age_sigma_s`` is given."""
    from .errors import mag_rate_hz

    t_p = _us(log.t_mag) + round(poll_offset_s(log.odr_hz) * 1e6)
    period = 1.0 / mag_rate_hz(log.odr_hz)
    uniform = period / math.sqrt(12.0)
    clock = (cfg.mag_age_s is None and cfg.mag_clock_t0_s is not None
             and cfg.mag_clock_period_s is not None)
    if not clock:
        t_c = (t_p - round(mag_age_s(cfg, log.odr_hz) * 1e6)) * 1e-6
        sd = cfg.mag_age_sigma_s if cfg.mag_age_sigma_s is not None else uniform
        return t_c, np.full(len(t_c), sd)
    t0, T = cfg.mag_clock_t0_s, cfg.mag_clock_period_s
    x = (t_p * 1e-6 - t0) / T
    k = np.floor(x)
    t_c = _us(t0 + k * T) * 1e-6
    edge = np.minimum(x - k, k + 1 - x) * T < 2.0 * cfg.mag_clock_sigma_s
    sd = np.where(edge, T / math.sqrt(12.0), cfg.mag_clock_sigma_s)
    return t_c, sd


def build_events(log: ImuTagLog, cfg: FilterConfig) -> Events:
    """Magnetometer and pressure events, ordered by the time they apply at:
    the magnetometer at its conversion time (:func:`mag_conversion_times`),
    pressure at its poll.

    Times are formed in whole microseconds, as ``ElapsedUs`` is, so an event
    that falls on an IMU sample has exactly that sample's time."""
    off_us = round(poll_offset_s(log.odr_hz) * 1e6)
    t_c, sd = mag_conversion_times(log, cfg)
    parts = [(np.full(len(log.t_mag), MAG), t_c, log.t_mag, np.arange(len(log.t_mag)), sd)]
    if cfg.use_pressure:
        parts.append((np.full(len(log.t_pressure), PRESSURE),
                      (_us(log.t_pressure) + off_us) * 1e-6, log.t_pressure,
                      np.arange(len(log.t_pressure)), np.zeros(len(log.t_pressure))))
    kind, t, t_log, idx, sds = (np.concatenate(x) for x in zip(*parts))
    order = np.lexsort((kind, t))
    return Events(kind[order].astype(np.int64), t[order], t_log[order], idx[order], sds[order])


def _qinterp(q: np.ndarray, t: np.ndarray, at: np.ndarray) -> np.ndarray:
    """Quaternions ``q`` at times ``t`` interpolated (normalised linear, sign
    aligned) to times ``at``, clamped to the ends."""
    i = np.clip(np.searchsorted(t, at) - 1, 0, len(t) - 2)
    u = np.clip((at - t[i]) / (t[i + 1] - t[i]), 0.0, 1.0)[..., None]
    a, b = q[i], q[i + 1]
    b = np.where((np.sum(a * b, axis=-1) < 0)[..., None], -b, b)
    r = a + u * (b - a)
    return r / np.linalg.norm(r, axis=-1, keepdims=True)


def estimate_mag_clock(log: ImuTagLog, ref: "FilterResult", cfg: FilterConfig) -> dict:
    """The BMM350 conversion clock (phase and period) fitted to the
    magnetometer readings against a reference attitude.

    For each reading taken while the tag turns faster than
    ``mag_clock_min_rate_dps``, the age that best matches it with the
    reference attitude is found by a search over one period (the field seen
    in the tag frame moves at the turn rate, so a reading's age shows in
    where it fits); its conversion time is its poll less that age. Those
    times are then all ``t0 + k T`` for integers k: a weighted periodogram
    over periods within ``mag_clock_rate_range`` of nominal finds ``T`` and
    the phase on the first ``mag_clock_window_s``, and weighted least squares
    on the residuals, wrapped to a period, refines both over the whole
    record. Returns the fit's figures; ``t0_s`` and ``period_s`` are None
    when there are too few turning readings or they do not agree on a
    clock."""
    from .errors import mag_rate_hz

    out = {"t0_s": None, "period_s": None}
    t_nom = 1.0 / mag_rate_hz(log.odr_hz)
    q = ref.q_smoothed if ref.q_smoothed is not None else ref.q_forward
    b = ref.bias_smoothed if ref.bias_smoothed is not None else ref.bias_forward
    kc = ref.gyro_cal_smoothed if ref.gyro_cal_smoothed is not None else ref.gyro_cal_forward
    u = log.gyro_rps - b
    w = u + (np.einsum("nij,nj->ni", kc, u) if kc is not None else 0.0)
    dm = ref.stats.get("mag_bias_final_ut", [0.0, 0.0, 0.0])
    field = np.asarray(cfg.field_enu_ut, dtype=np.float64)
    t_p = (_us(log.t_mag) + round(poll_offset_s(log.odr_hz) * 1e6)) * 1e-6
    i_p = np.clip(np.searchsorted(log.t_imu, t_p - 1e-7), 0, len(log.t_imu) - 1)
    rate = np.linalg.norm(w[i_p], axis=1)
    use = (rate > math.radians(cfg.mag_clock_min_rate_dps)) & (t_p - t_nom * 1.1 > log.t_imu[0])
    out["readings"] = int(use.sum())
    if use.sum() < 50:
        return out
    m = log.mag_ut[use] - np.asarray(dm)
    tp = t_p[use]
    ages = np.linspace(0.0, 1.1 * t_nom, 89)
    tt = tp[:, None] - ages[None, :]
    qa = _qinterp(q, log.t_imu, tt.reshape(-1)).reshape(len(tp), len(ages), 4)
    pred = qrotate(qconj(qa), np.broadcast_to(field, qa.shape[:-1] + (3,)))
    cost = np.sum((pred - m[:, None, :]) ** 2, axis=-1)
    k = np.argmin(cost, axis=1)
    inner = (k > 0) & (k < len(ages) - 1)
    kk = np.clip(k, 1, len(ages) - 2)
    r = np.arange(len(tp))
    c0, c1, c2 = cost[r, kk - 1], cost[r, kk], cost[r, kk + 1]
    den = c0 - 2 * c1 + c2
    shift = np.where(den > 0, 0.5 * (c0 - c2) / np.where(den > 0, den, 1.0), 0.0)
    age = ages[kk] + np.clip(shift, -1, 1) * (ages[1] - ages[0])
    # Weight: the cost curve's curvature, how sharply the reading fixes its age.
    wt = np.where(inner & (den > 0), den, 0.0)
    tc = tp - age
    keep = wt > 0
    tc, wt = tc[keep], wt[keep]
    out["readings_fitted"] = int(keep.sum())
    if len(tc) < 50:
        return out
    wt = wt / wt.sum()
    t_ref = float(tc[0])
    x = tc - t_ref
    win = x <= cfg.mag_clock_window_s
    span = max(float(x[win].max()), 10 * t_nom)
    f_nom = 1.0 / t_nom
    df = 0.05 / span
    freqs = np.arange(f_nom * (1 - cfg.mag_clock_rate_range),
                      f_nom * (1 + cfg.mag_clock_rate_range), df)
    power = np.empty(len(freqs))
    for s0 in range(0, len(freqs), 512):
        z = np.exp(2j * np.pi * freqs[s0:s0 + 512, None] * x[win][None, :]) @ wt[win]
        power[s0:s0 + 512] = np.abs(z)
    f = float(freqs[int(np.argmax(power))])
    T = 1.0 / f
    phase = float(np.angle(np.exp(2j * np.pi * f * x[win]) @ wt[win]))
    t0 = t_ref - phase / (2 * np.pi) * T  # conversions at t0 + k T
    resid = None
    for _ in range(4):
        n_k = np.rint((tc - t0) / T)
        resid = tc - (t0 + n_k * T)
        inl = np.abs(resid) < 0.25 * T
        a = np.column_stack([np.ones(inl.sum()), n_k[inl]])
        sw = np.sqrt(wt[inl])
        d0, dT = np.linalg.lstsq(a * sw[:, None], resid[inl] * sw, rcond=None)[0]
        t0, T = t0 + d0, T + dT
    n_k = np.rint((tc - t0) / T)
    resid = tc - (t0 + n_k * T)
    inl = np.abs(resid) < 0.25 * T
    rayleigh = float(np.abs(np.exp(2j * np.pi * tc / T) @ wt))
    out.update({"rayleigh": round(rayleigh, 4), "inlier_fraction": round(float(inl.mean()), 4),
                "residual_rms_ms": round(float(np.sqrt(np.average(resid[inl] ** 2,
                                                                  weights=wt[inl]))) * 1e3, 3),
                "rate_error_ppm": round(float(t_nom / T - 1.0) * 1e6, 1)})
    if rayleigh < 0.3:
        return out
    # Express t0 as the conversion just before the log's first sample.
    t0 = t0 + math.floor((log.t_imu[0] - t0) / T) * T
    out["t0_s"], out["period_s"] = float(t0), float(T)
    return out


# ---------------------------------------------------------------------------
# Initialisation


def still_mask(log: ImuTagLog, cfg: FilterConfig) -> np.ndarray:
    """Per IMU sample: inside a still run of at least ``still_min_s``.

    Still is judged per superframe, from the spread of the gyro and the
    accelerometer about their means, and a mean rate small enough to be gyro
    bias (a steady turn has no spread)."""
    n = len(log.t_imu) // SUPERFRAME * SUPERFRAME
    gyro = np.degrees(log.gyro_rps[:n]).reshape(-1, SUPERFRAME, 3)
    g = gyro.std(axis=1).max(axis=1)
    rate = np.linalg.norm(gyro.mean(axis=1), axis=1)
    a = (log.accel_mps2[:n] * 1000.0 / G0).reshape(-1, SUPERFRAME, 3).std(axis=1).max(axis=1)
    sf = (g < cfg.still_gyro_dps) & (a < cfg.still_accel_mg) & (rate < cfg.still_rate_dps)
    min_sf = int(math.ceil(cfg.still_min_s * log.odr_hz / SUPERFRAME))
    keep = np.zeros_like(sf)
    edges = np.flatnonzero(np.diff(np.concatenate([[0], sf.astype(np.int8), [0]])))
    for start, stop in zip(edges[::2], edges[1::2]):
        if stop - start >= min_sf:
            keep[start:stop] = True
    mask = np.zeros(len(log.t_imu), dtype=bool)
    mask[:n] = np.repeat(keep, SUPERFRAME)
    return mask


def triad(f_tag: np.ndarray, m_tag: np.ndarray, field_enu: np.ndarray) -> np.ndarray:
    """Attitude (tag to ENU) from specific force taken as up, and the field."""
    def frame(up, b):
        u = up / np.linalg.norm(up)
        e = np.cross(b, u)
        e /= np.linalg.norm(e)
        return np.column_stack([e, np.cross(u, e), u])

    r = frame(np.array([0.0, 0.0, 1.0]), field_enu) @ frame(f_tag, m_tag).T
    from .rotations import dcmtoq
    return dcmtoq(r)


# ---------------------------------------------------------------------------
# Small single-quaternion helpers (plain floats are much faster than numpy
# for one 4-vector at a time).


def _qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def _qexp(x, y, z):
    a = math.sqrt(x * x + y * y + z * z)
    if a < 1e-12:
        return (1.0, 0.5 * x, 0.5 * y, 0.5 * z)
    s = math.sin(0.5 * a) / a
    return (math.cos(0.5 * a), s * x, s * y, s * z)


def _qnorm(q):
    n = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)


def _dcm(q):
    w, x, y, z = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
        [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
        [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])


def _skew(v):
    return np.array([[0.0, -v[2], v[1]], [v[2], 0.0, -v[0]], [-v[1], v[0], 0.0]])


def _qlog_global(qa, qb):
    """Rotation vector of ``qa conj(qb)``: the ENU-frame error taking qb to qa."""
    d = _qmul(qa, (qb[0], -qb[1], -qb[2], -qb[3]))
    if d[0] < 0:
        d = (-d[0], -d[1], -d[2], -d[3])
    s = math.sqrt(d[1] * d[1] + d[2] * d[2] + d[3] * d[3])
    if s < 1e-12:
        return np.array([2.0 * d[1], 2.0 * d[2], 2.0 * d[3]])
    k = 2.0 * math.atan2(s, d[0]) / s
    return np.array([k * d[1], k * d[2], k * d[3]])


# ---------------------------------------------------------------------------
# The filter


@dataclass
class FilterResult:
    """Forward and smoothed estimates at every IMU sample of a segment."""

    segment_id: int
    sample_index: np.ndarray
    t: np.ndarray  #: s
    q_forward: np.ndarray  #: tag to ENU
    q_smoothed: np.ndarray | None
    alt_forward: np.ndarray | None  #: m, relative to the first pressure sample
    alt_smoothed: np.ndarray | None
    vz_forward: np.ndarray | None  #: m/s, up
    vz_smoothed: np.ndarray | None
    bias_forward: np.ndarray  #: rad/s
    bias_smoothed: np.ndarray | None
    stats: dict = field(default_factory=dict)
    #: m/s^2, tag frame; None without the accelerometer bias states
    accel_bias_forward: np.ndarray | None = None
    accel_bias_smoothed: np.ndarray | None = None
    #: (n, 3, 3) gyro correction ``K`` (corrected rate ``(I + K)(w_m - b)``);
    #: None without the gyro calibration states
    gyro_cal_forward: np.ndarray | None = None
    gyro_cal_smoothed: np.ndarray | None = None
    #: The files behind the arrays when the outputs are file-backed.
    outputs: "OutputStore | None" = field(default=None, repr=False, compare=False)

    def close(self) -> None:
        """Delete the output files, if any. The arrays must not be used
        afterwards (on POSIX the mapped pages stay readable until released)."""
        if self.outputs is not None:
            self.outputs.close()


def gyro_sensitivity(k: np.ndarray) -> np.ndarray:
    """The gyro sensitivity and misalignment matrix ``T = (I + K)^-1``
    (measured = ``T`` true + bias) of correction matrices ``K``, ``(..., 3, 3)``."""
    return np.linalg.inv(np.eye(3) + np.asarray(k, dtype=np.float64))


def run_filter(log: ImuTagLog, cfg: FilterConfig) -> FilterResult:
    """Forward error-state EKF over the event stream, then RTS smoothing."""
    vert = cfg.use_pressure and len(log.t_pressure) > 0
    ab = cfg.accel_bias_states
    gc = cfg.gyro_cal_states
    mb = cfg.mag_bias_states
    # Error-state layout: dtheta 0:3, gyro bias 3:6, then the gyro correction
    # K ik:ik+9 (row-major) if estimated, the accelerometer bias ia:ia+3 if
    # estimated, then h and vz at ih, ih+1 if vertical.
    ik = 6
    ia = ik + 9 * gc
    im = ia + 3 * ab
    ih = im + 3 * mb
    n = ih + (2 if vert else 0)
    odr = log.odr_hz
    t_imu, f_all, w_all = log.t_imu, log.accel_mps2, log.gyro_rps
    n_imu = len(t_imu)
    field = np.asarray(cfg.field_enu_ut, dtype=np.float64)
    field_norm = float(np.linalg.norm(field))

    # Initialisation from still periods.
    still = still_mask(log, cfg)
    if still.any():
        b0 = w_all[still].mean(axis=0)
        g = float(np.linalg.norm(f_all[still], axis=1).mean())
        sb = math.radians(cfg.init_bias_dps)
    else:
        b0 = np.zeros(3)
        g = G0
        sb = math.radians(cfg.init_bias_unknown_dps)
    sf_len = SUPERFRAME
    start_still = bool(still[:sf_len].all())
    # A first, attitude-only smoothing pass, when the start must be
    # bootstrapped or the magnetometer clock estimated.
    boot = ref = None
    want_clock = (cfg.mag_clock_estimate and cfg.mag_age_s is None
                  and cfg.mag_clock_t0_s is None and len(log.t_mag) > 0)
    if (not start_still and cfg.bootstrap) or want_clock:
        ref = run_filter(log, dataclass_replace(cfg, use_pressure=False, bootstrap=False,
                                                smooth=True, mag_clock_estimate=False))
        if not start_still and cfg.bootstrap:
            boot = ref
    clock_fit = None
    if want_clock:
        clock_fit = estimate_mag_clock(log, ref, cfg)
        if clock_fit["t0_s"] is not None:
            cfg = dataclass_replace(cfg, mag_clock_t0_s=clock_fit["t0_s"],
                                    mag_clock_period_s=clock_fit["period_s"])
    ev = build_events(log, dataclass_replace(cfg, use_pressure=vert))
    if start_still:
        stop = int(np.argmin(still)) if not still.all() else n_imu
        f0 = f_all[:stop].mean(axis=0)
        t_end = t_imu[stop - 1]
        sel = log.t_mag <= t_end
        m0 = log.mag_ut[sel].mean(axis=0) if sel.any() else log.mag_ut[0]
        s_att = math.radians(cfg.init_attitude_deg)
    else:
        f0 = f_all[:sf_len].mean(axis=0)
        m0 = log.mag_ut[0]
        s_att = math.radians(cfg.init_attitude_moving_deg)
    q = tuple(float(x) for x in triad(f0, m0, field))
    b = b0.astype(np.float64).copy()
    ba = np.zeros(3)
    K = np.zeros((3, 3))
    dm = np.zeros(3)
    if boot is not None:
        q = tuple(float(x) for x in boot.q_smoothed[0])
        b = boot.bias_smoothed[0].copy()
        if boot.accel_bias_smoothed is not None:
            ba = boot.accel_bias_smoothed[0].copy()
        s_att = math.radians(cfg.init_attitude_bootstrap_deg)
        sb = min(sb, math.radians(cfg.init_bias_dps))
    h, vz = 0.0, 0.0
    s_h, s_vz = cfg.pressure_sigma_m, 0.1
    p_ref = alt_ref = 0.0
    if vert:
        # Altitude is relative to the first pressure sample; the initial
        # altitude and vertical velocity come from a line through the first
        # second of pressure altitude (vz is 0 on a still start).
        p_ref = float(log.pressure_hpa[0])
        alt_ref = float(isa_altitude_m(p_ref, cfg.sea_level_hpa))
        t_p = log.t_pressure + poll_offset_s(odr)
        first = t_p <= t_p[0] + cfg.init_vertical_fit_s
        z = isa_altitude_m(log.pressure_hpa[first], cfg.sea_level_hpa) - alt_ref
        if first.sum() >= 3 and not start_still:
            slope, icpt = np.polyfit(t_p[first] - t_imu[0], z, 1)
            h, vz = float(icpt), float(slope)
            s_vz = cfg.init_vz_moving_mps
        else:
            h = float(z.mean()) if start_still else float(z[0])
    P = np.zeros((n, n))
    P[0:3, 0:3] = np.eye(3) * s_att ** 2
    P[3:6, 3:6] = np.eye(3) * sb ** 2
    if gc:
        diag = np.eye(3).reshape(9).astype(bool)
        P[ik:ik + 9, ik:ik + 9] = np.diag(np.where(
            diag, cfg.init_gyro_scale, math.radians(cfg.init_gyro_misalignment_deg)) ** 2)
    if ab:
        P[ia:ia + 3, ia:ia + 3] = np.eye(3) * (cfg.init_accel_bias_mg * G0 / 1000.0) ** 2
    if mb:
        P[im:im + 3, im:im + 3] = np.eye(3) * cfg.init_mag_bias_ut ** 2
    if vert:
        P[ih, ih] = s_h ** 2
        P[ih + 1, ih + 1] = s_vz ** 2 if (start_still or first.sum() >= 3) else 5.0 ** 2

    # Noise.
    rate_scale = math.sqrt(odr / 100.0)
    q_theta = math.radians(cfg.gyro_noise_dps_rthz) ** 2
    q_bias = math.radians(cfg.gyro_bias_walk_dps_rts) ** 2
    q_vz = cfg.accel_vertical_noise_mps2_rthz ** 2
    q_ba = (cfg.accel_bias_walk_mg_rts * G0 / 1000.0) ** 2
    q_k = cfg.gyro_cal_walk_rts ** 2
    r_p = cfg.pressure_sigma_m ** 2

    # Storage. The outputs are kept per IMU sample (OutputStore); for the smoother, the
    # forward pass also stores, per node (each IMU sample and each auxiliary
    # event between samples), the filtered nominal state, the RTS gain to the
    # next node, and where the node is (its time and IMU interval), from which
    # the backward pass recomputes the prediction into the next node.
    n_nodes = n_imu + len(ev.t)  # an upper bound
    outputs = OutputStore(n_imu, cfg.smooth, cfg)
    out_f = outputs.forward
    store = NodeStore(n_nodes, n, cfg) if cfg.smooth else None
    stats = {"accel_updates": 0, "accel_gated": 0, "mag_updates": 0, "mag_gated": 0,
             "pressure_updates": 0, "still_fraction": float(still.mean()),
             "start_still": start_still, "g_mps2": g,
             "gyro_bias_init_dps": np.degrees(b0).tolist(),
             "mag_clock": clock_fit,
             "mag_clock_used": (cfg.mag_age_s is None and cfg.mag_clock_t0_s is not None)}
    if store is not None:
        stats["storage"] = store.describe()
    stats["outputs"] = outputs.describe()
    flush_every = cfg.memmap_window_nodes
    I_n = np.eye(n)

    def pack(q, b, h, vz):
        return (q[0], q[1], q[2], q[3], b[0], b[1], b[2], h, vz, ba[0], ba[1], ba[2],
                *K.reshape(9), dm[0], dm[1], dm[2])

    def inject(dx):
        nonlocal q, b, h, vz, ba, K, dm
        q = _qnorm(_qmul(_qexp(dx[0], dx[1], dx[2]), q))
        b = b + dx[3:6]
        if gc:
            K = K + dx[ik:ik + 9].reshape(3, 3)
        if ab:
            ba = ba + dx[ia:ia + 3]
        if mb:
            dm = dm + dx[im:im + 3]
        if vert:
            h += dx[ih]
            vz += dx[ih + 1]

    def update(H, r, R):
        nonlocal P
        PHt = P @ H.T
        S = H @ PHt + R
        G = np.linalg.solve(S, PHt.T).T
        inject(G @ r)
        A = I_n - G @ H
        P = A @ P @ A.T + G @ R @ G.T

    def predict(f0_, w0_, f1_, w1_, dt, saturated, node, t_node, i_node):
        """Propagate over dt with the IMU interpolated linearly from (f0, w0)
        to (f1, w1) into ``node``, at ``t_node`` in IMU interval ``i_node``,
        and store the previous node's state and gain."""
        nonlocal q, P, h, vz
        if store is not None:
            store.put_state(node - 1, pack(q, b, h, vz))
        q, h, vz, R, fE, u = _propagate(q, b, h, vz, ba, K, f0_, w0_, f1_, w1_, dt, g, vert)
        F = np.eye(n)
        Qd = np.zeros((n, n))
        if gc:
            # w_true - w = dK u - (I + K) db, u the bias-corrected raw rate.
            F[0:3, 3:6] = -(R @ (np.eye(3) + K)) * dt
            F[0:3, ik:ik + 9] = (R[:, :, None] * u).reshape(3, 9) * dt
            if q_k:
                Qd[ik:ik + 9, ik:ik + 9] = np.eye(9) * (q_k * dt)
        else:
            F[0:3, 3:6] = -R * dt
        Qd[0:3, 0:3] = np.eye(3) * (q_theta * dt)
        Qd[3:6, 3:6] = np.eye(3) * (q_bias * dt)
        if ab:
            Qd[ia:ia + 3, ia:ia + 3] = np.eye(3) * (q_ba * dt)
        if vert:
            F[ih, ih + 1] = dt
            F[ih + 1, 0] = fE[1] * dt
            F[ih + 1, 1] = -fE[0] * dt
            F[ih, 0] = 0.5 * fE[1] * dt * dt
            F[ih, 1] = -0.5 * fE[0] * dt * dt
            if ab:
                F[ih + 1, ia:ia + 3] = -R[2, :] * dt
                F[ih, ia:ia + 3] = -0.5 * R[2, :] * dt * dt
            qv = q_vz * dt + (cfg.accel_saturated_mps2 * dt) ** 2 * saturated
            Qd[ih + 1, ih + 1] = qv
            Qd[ih, ih] = qv * dt * dt / 3.0
            Qd[ih, ih + 1] = Qd[ih + 1, ih] = qv * dt / 2.0
        Pp = F @ P @ F.T + Qd
        if store is not None:
            store.put_gain(node - 1, np.linalg.solve(Pp, F @ P).T)
            store.put_node(node, t_node, i_node)
        P = 0.5 * (Pp + Pp.T)

    up_skew = _skew(np.array([0.0, 0.0, g]))
    IK = np.eye(3)
    field_skew = _skew(field)
    n_ev = len(ev.t)
    j = 0  # next auxiliary event
    node = 0  # the current node: an instant at which updates are applied
    if store is not None:
        store.put_node(0, t_imu[0], 0)

    def aux(j, w):
        """One magnetometer or pressure update at the current state, with
        ``w`` the corrected rate there."""
        if ev.kind[j] == MAG:
            m = log.mag_ut[ev.index[j]] - dm
            if abs(float(np.linalg.norm(m)) - field_norm) > cfg.mag_gate_ut:
                stats["mag_gated"] += 1
                return
            R = _dcm(q)
            H = np.zeros((3, n))
            H[:, 0:3] = R.T @ field_skew
            if mb:
                H[:, im:im + 3] = np.eye(3)
            sigma = cfg.mag_sigma_ut + field_norm * float(np.linalg.norm(w)) * ev.age_sigma[j]
            update(H, m - R.T @ field, np.eye(3) * sigma ** 2)
            stats["mag_updates"] += 1
        elif vert:
            z = float(isa_altitude_m(log.pressure_hpa[ev.index[j]], cfg.sea_level_hpa)) - alt_ref
            H = np.zeros((1, n))
            H[0, ih] = 1.0
            update(H, np.array([z - h]), np.array([[r_p]]))
            stats["pressure_updates"] += 1

    try:
        for i in range(n_imu):
            if i > 0:
                # Propagate from t_{i-1} to t_i, stopping at each auxiliary event
                # strictly between them, with the IMU interpolated linearly.
                ta, tb = t_imu[i - 1], t_imu[i]
                f_a, w_a, f_b, w_b = f_all[i - 1], w_all[i - 1], f_all[i], w_all[i]
                span = tb - ta
                sat = bool(log.accel_saturated[i - 1] or log.accel_saturated[i])
                tc, fc, wc = ta, f_a, w_a
                while j < n_ev and ev.t[j] < tb:
                    te = ev.t[j]
                    if te > tc:
                        u = (te - ta) / span
                        fe, we = f_a + u * (f_b - f_a), w_a + u * (w_b - w_a)
                        node += 1
                        predict(fc, wc, fe, we, te - tc, sat, node, te, i)
                        tc, fc, wc = te, fe, we
                    aux(j, (0.5 * (wc + w_b) - b) @ IK.T)
                    j += 1
                node += 1
                predict(fc, wc, f_b, w_b, tb - tc, sat, node, tb, i)
            # Accelerometer update at the IMU sample.
            f = f_all[i] - ba
            excess = abs(float(np.linalg.norm(f)) - g)
            IK = np.eye(3) + K
            if excess < cfg.accel_gate_g * G0 and not log.accel_saturated[i]:
                wn = float(np.linalg.norm(IK @ (w_all[i] - b)))
                sigma = rate_scale * (cfg.accel_sigma_mps2 + cfg.accel_sigma_per_excess * excess
                                      + cfg.accel_speed_mps * wn)
                R = _dcm(q)
                H = np.zeros((3, n))
                H[:, 0:3] = R.T @ up_skew
                if ab:
                    H[:, ia:ia + 3] = np.eye(3)
                update(H, f - R[2, :] * g, np.eye(3) * sigma ** 2)  # R^T (0, 0, g)
                stats["accel_updates"] += 1
            else:
                stats["accel_gated"] += 1
            # Auxiliary events at this sample (and, at the first, before it).
            while j < n_ev and ev.t[j] <= t_imu[i]:
                aux(j, IK @ (w_all[i] - b))
                j += 1
            out_f[i] = pack(q, b, h, vz)
            if outputs.file_backed and i % flush_every == flush_every - 1:
                outputs.flush()
        out_s = None
        if store is not None:
            store.put_state(node, pack(q, b, h, vz))
            out_s = _smooth(store, node + 1, out_f[-1], t_imu, f_all, w_all, g, vert, ab, ia,
                            ih, n, gc, ik, mb, im, outputs)
        outputs.flush()
    except BaseException:
        outputs.close()
        raise
    finally:
        if store is not None:
            store.close()

    fwd, sm = out_f, out_s
    stats["gyro_bias_final_dps"] = np.degrees(fwd[-1, 4:7]).tolist()
    if ab:
        stats["accel_bias_final_mg"] = (fwd[-1, 9:12] * 1000.0 / G0).tolist()
    if gc:
        last = (sm if sm is not None else fwd)[-1, 12:21].reshape(3, 3)
        stats["gyro_sensitivity_final"] = (gyro_sensitivity(last) - np.eye(3)).tolist()
    if mb:
        stats["mag_bias_final_ut"] = (sm if sm is not None else fwd)[-1, 21:24].tolist()
    return FilterResult(
        segment_id=log.segment_id, sample_index=log.sample_index, t=t_imu,
        q_forward=fwd[:, 0:4], q_smoothed=None if sm is None else sm[:, 0:4],
        alt_forward=fwd[:, 7] if vert else None,
        alt_smoothed=sm[:, 7] if (vert and sm is not None) else None,
        vz_forward=fwd[:, 8] if vert else None,
        vz_smoothed=sm[:, 8] if (vert and sm is not None) else None,
        bias_forward=fwd[:, 4:7], bias_smoothed=None if sm is None else sm[:, 4:7],
        stats=stats,
        accel_bias_forward=fwd[:, 9:12] if ab else None,
        accel_bias_smoothed=sm[:, 9:12] if (ab and sm is not None) else None,
        gyro_cal_forward=fwd[:, 12:21].reshape(-1, 3, 3) if gc else None,
        gyro_cal_smoothed=sm[:, 12:21].reshape(-1, 3, 3) if (gc and sm is not None) else None,
        outputs=outputs if outputs.file_backed else None)


def _propagate(q, b, h, vz, ba, K, f0, w0, f1, w1, dt, g, vert):
    """The nominal state propagated over ``dt``, with the IMU interpolated
    linearly from (f0, w0) to (f1, w1) and the gyro corrected by ``K``: the
    new ``q``, ``h`` and ``vz`` (the biases and ``K`` are constant), and the
    start attitude's DCM, the ENU specific force and the bias-corrected raw
    rate for the error-state Jacobian. The forward pass and the smoother's
    recomputed prediction both call this, so they agree exactly."""
    R = _dcm(q)
    u = 0.5 * (w0 + w1) - b
    w = u + K @ u
    fE = R @ (0.5 * (f0 + f1) - ba)
    q = _qnorm(_qmul(q, _qexp(w[0] * dt, w[1] * dt, w[2] * dt)))
    if vert:
        a = fE[2] - g
        h += vz * dt + 0.5 * a * dt * dt
        vz += a * dt
    return q, h, vz, R, fE, u


def _smooth(store, n_nodes, last, t_imu, f_all, w_all, g, vert, ab, ia, ih, n, gc, ik, mb, im,
            outputs):
    """The backward Rauch-Tung-Striebel pass, reading the store in reverse.

    ``x_s,k = x_f,k (+) C_k (x_s,k+1 (-) x_{k+1|k})``, with the prediction
    ``x_{k+1|k}`` recomputed from ``x_f,k`` and the IMU over the node's
    interval rather than stored. Writes and returns the smoothed nominal
    state at every IMU sample, ``outputs.smoothed``."""
    out = outputs.smoothed
    flush_every = store.window

    def imu_at(k):
        """The IMU (f, w) at node k, and its IMU index if it is a sample."""
        t, i = store.node(k)
        if t == t_imu[i]:
            return f_all[i], w_all[i], i
        ta, tb = t_imu[i - 1], t_imu[i]
        u = (t - ta) / (tb - ta)
        return (f_all[i - 1] + u * (f_all[i] - f_all[i - 1]),
                w_all[i - 1] + u * (w_all[i] - w_all[i - 1]), -1)

    xs = tuple(float(x) for x in last)  # smoothed = filtered at the last node
    f1, w1, i1 = imu_at(n_nodes - 1)
    t1 = store.node(n_nodes - 1)[0]
    if i1 >= 0:
        out[i1] = xs
    d = np.empty(n)
    for k in range(n_nodes - 2, -1, -1):
        xf = store.state(k)
        f0, w0, i0 = imu_at(k)
        t0 = store.node(k)[0]
        qf, bf, baf, kf = tuple(xf[0:4]), xf[4:7], xf[9:12], xf[12:21]
        qp, hp, vzp, _, _, _ = _propagate(qf, bf, xf[7], xf[8], baf, kf.reshape(3, 3), f0, w0,
                                          f1, w1, t1 - t0, g, vert)
        d[0:3] = _qlog_global(xs[0:4], qp)
        d[3:6] = np.subtract(xs[4:7], bf)
        if gc:
            d[ik:ik + 9] = np.subtract(xs[12:21], kf)
        if mb:
            d[im:im + 3] = np.subtract(xs[21:24], xf[21:24])
        if ab:
            d[ia:ia + 3] = np.subtract(xs[9:12], baf)
        if vert:
            d[ih] = xs[7] - hp
            d[ih + 1] = xs[8] - vzp
        dx = store.gain(k) @ d
        qk = _qnorm(_qmul(_qexp(dx[0], dx[1], dx[2]), qf))
        bk = bf + dx[3:6]
        bak = baf + dx[ia:ia + 3] if ab else baf
        hk, vzk = (xf[7] + dx[ih], xf[8] + dx[ih + 1]) if vert else (xf[7], xf[8])
        kk = kf + dx[ik:ik + 9] if gc else kf
        mk = xf[21:24] + dx[im:im + 3] if mb else xf[21:24]
        xs = (*qk, *bk, hk, vzk, *bak, *kk, *mk)
        if i0 >= 0:
            out[i0] = xs
            if outputs.file_backed and i0 % flush_every == 0:
                outputs.flush()
        f1, w1, t1 = f0, w0, t0
    return out


class _Column:
    """One per-node array in a file, mapped a window of rows at a time.

    Rows are written in increasing order by the forward pass and read in
    decreasing order by the backward pass; each window is a ``numpy.memmap``
    of ``window`` rows, flushed and unmapped when the access moves outside
    it, so the process keeps at most one window of each column resident."""

    def __init__(self, path: Path, n: int, row: tuple, dtype, window: int):
        self.path, self.n, self.row, self.dtype = path, n, row, np.dtype(dtype)
        self.window = window
        self.row_bytes = self.dtype.itemsize * int(np.prod(row, dtype=np.int64))
        with open(path, "wb") as f:
            f.truncate(max(1, n * self.row_bytes))
        self.map, self.lo, self.hi = None, 0, 0

    def _remap(self, k: int) -> None:
        self.close()
        self.lo = (k // self.window) * self.window
        self.hi = min(self.lo + self.window, self.n)
        self.map = np.memmap(self.path, dtype=self.dtype, mode="r+",
                             offset=self.lo * self.row_bytes, shape=(self.hi - self.lo, *self.row))

    def __setitem__(self, k: int, value) -> None:
        if not self.lo <= k < self.hi or self.map is None:
            self._remap(k)
        self.map[k - self.lo] = value

    def __getitem__(self, k: int):
        if not self.lo <= k < self.hi or self.map is None:
            self._remap(k)
        return np.array(self.map[k - self.lo])

    def close(self) -> None:
        if self.map is not None:
            self.map.flush()
            self.map = None


class NodeStore:
    """The forward pass's per-node record for the smoother.

    Per node: the filtered nominal state (:data:`NOMINAL` doubles), the RTS
    gain to the next node (``n x n``, float64, or float32 with
    :attr:`FilterConfig.gain_float32`), and the node's time and IMU interval
    (a double and an int32). Held in plain arrays when it fits in
    :attr:`FilterConfig.memmap_threshold_mb`, otherwise in ``numpy.memmap``
    files in a temporary directory (under :attr:`FilterConfig.memmap_dir`,
    else the system's), deleted by :meth:`close`."""

    def __init__(self, n_nodes: int, n: int, cfg: FilterConfig):
        self.window = cfg.memmap_window_nodes
        gain_dtype = np.float32 if cfg.gain_float32 else np.float64
        self.bytes_per_node = NOMINAL * 8 + n * n * np.dtype(gain_dtype).itemsize + 8 + 4
        self.nbytes = n_nodes * self.bytes_per_node
        self.file_backed = self.nbytes > cfg.memmap_threshold_mb * 2 ** 20
        self._tmp = None
        if self.file_backed:
            import tempfile
            self._tmp = tempfile.TemporaryDirectory(prefix="tagsim-filter-", dir=cfg.memmap_dir)
            d, w = Path(self._tmp.name), cfg.memmap_window_nodes
            self.states = _Column(d / "states", n_nodes, (NOMINAL,), np.float64, w)
            self.gains = _Column(d / "gains", n_nodes, (n, n), gain_dtype, w)
            self.times = _Column(d / "times", n_nodes, (), np.float64, w)
            self.intervals = _Column(d / "intervals", n_nodes, (), np.int32, w)
        else:
            self.states = np.empty((n_nodes, NOMINAL))
            self.gains = np.empty((n_nodes, n, n), dtype=gain_dtype)
            self.times = np.empty(n_nodes)
            self.intervals = np.empty(n_nodes, dtype=np.int32)

    def describe(self) -> dict:
        return {"file_backed": self.file_backed, "bytes_per_node": self.bytes_per_node,
                "max_mb": round(self.nbytes / 2 ** 20, 1)}

    def put_state(self, k, x):
        self.states[k] = x

    def put_gain(self, k, c):
        self.gains[k] = c

    def put_node(self, k, t, i):
        self.times[k] = t
        self.intervals[k] = i

    def state(self, k):
        return self.states[k]

    def gain(self, k):
        return self.gains[k]

    def node(self, k):
        return float(self.times[k]), int(self.intervals[k])

    def close(self) -> None:
        """Unmap and delete the files, if any."""
        if self._tmp is not None:
            for c in (self.states, self.gains, self.times, self.intervals):
                c.close()
            self._tmp.cleanup()
            self._tmp = None


class OutputStore:
    """The filter's per-sample outputs: the forward and (when smoothing) the
    smoothed nominal state, :data:`NOMINAL` doubles per IMU sample each.

    Plain arrays when they fit in :attr:`FilterConfig.memmap_threshold_mb`,
    otherwise each is a ``numpy.memmap`` over a file in a temporary
    directory (under :attr:`FilterConfig.memmap_dir`), flushed every
    ``memmap_window_nodes`` rows as it is written so that written pages can
    be dropped from memory. The files outlive :func:`run_filter`: they are
    the result, and :class:`FilterResult` holds this store. :meth:`close`,
    or garbage collection of the store, deletes them."""

    def __init__(self, n: int, smoothed: bool, cfg: FilterConfig):
        self.nbytes = (1 + smoothed) * n * NOMINAL * 8
        self.file_backed = self.nbytes > cfg.memmap_threshold_mb * 2 ** 20
        self._tmp = None
        self.smoothed = None
        if self.file_backed:
            import tempfile
            self._tmp = tempfile.TemporaryDirectory(prefix="tagsim-outputs-", dir=cfg.memmap_dir)
            d = Path(self._tmp.name)
            self.forward = np.memmap(d / "forward", dtype=np.float64, mode="w+",
                                     shape=(n, NOMINAL))
            if smoothed:
                self.smoothed = np.memmap(d / "smoothed", dtype=np.float64, mode="w+",
                                          shape=(n, NOMINAL))
        else:
            self.forward = np.empty((n, NOMINAL))
            if smoothed:
                self.smoothed = np.empty((n, NOMINAL))

    def describe(self) -> dict:
        return {"file_backed": self.file_backed, "mb": round(self.nbytes / 2 ** 20, 1)}

    def flush(self) -> None:
        if self.file_backed:
            for a in (self.forward, self.smoothed):
                if a is not None:
                    a.flush()

    def close(self) -> None:
        """Delete the files, if any."""
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp = None


def dataclass_replace(cfg: FilterConfig, **changes) -> FilterConfig:
    from dataclasses import replace
    return replace(cfg, **changes)


# ---------------------------------------------------------------------------
# Output


#: The gyro sensitivity columns of ``FilterForward`` and ``FilterSmoothed``:
#: ``T - I``, row-major.
GYRO_CAL_COLUMNS = tuple(f"gs{r}{c}" for r in "xyz" for c in "xyz")


def write_estimates(path: Path, results: list[FilterResult], cfg: FilterConfig,
                    source: Path | None = None, chunk: int = 1 << 16) -> None:
    """``FilterForward`` and ``FilterSmoothed``: one row per IMU sample,
    ``SegmentId, SampleIndex, ElapsedUs, qw qx qy qz`` (tag frame to ENU),
    ``alt`` (m, relative to the first pressure sample) and ``vz`` (m/s, up),
    NULL without the pressure channel; ``bx by bz`` the gyro bias (dps);
    ``abx aby abz`` the accelerometer bias (mg), NULL unless estimated;
    ``gsxx gsxy ... gszz`` the gyro's estimated sensitivity and misalignment
    ``T - I`` (dimensionless, row-major; measured = ``T`` true + bias), NULL
    unless estimated; and ``FilterRun`` with the configuration and counters."""
    if path.exists():
        path.unlink()
    con = sqlite3.connect(path)
    try:
        for table in ("FilterForward", "FilterSmoothed"):
            con.execute(f"CREATE TABLE {table} (SegmentId INTEGER, SampleIndex INTEGER, "
                        "ElapsedUs INTEGER, qw REAL, qx REAL, qy REAL, qz REAL, alt REAL, "
                        "vz REAL, bx REAL, by REAL, bz REAL, abx REAL, aby REAL, abz REAL, "
                        + ", ".join(f"{c} REAL" for c in GYRO_CAL_COLUMNS) + ")")
        con.execute("CREATE TABLE FilterRun (SegmentId INTEGER, Source TEXT, ConfigJson TEXT, "
                    "StatsJson TEXT)")
        for r in results:
            for table, q, alt, vz, bias, abias, gcal in (
                    ("FilterForward", r.q_forward, r.alt_forward, r.vz_forward, r.bias_forward,
                     r.accel_bias_forward, r.gyro_cal_forward),
                    ("FilterSmoothed", r.q_smoothed, r.alt_smoothed, r.vz_smoothed,
                     r.bias_smoothed, r.accel_bias_smoothed, r.gyro_cal_smoothed)):
                if q is None:
                    continue
                # In chunks, so that file-backed outputs are never all in memory.
                for lo in range(0, len(r.t), chunk):
                    sl = slice(lo, lo + chunk)
                    nn = len(r.t[sl])
                    a = alt[sl] if alt is not None else [None] * nn
                    v = vz[sl] if vz is not None else [None] * nn
                    bd = np.degrees(bias[sl])
                    ad = abias[sl] * (1000.0 / G0) if abias is not None else [(None,) * 3] * nn
                    gd = ((gyro_sensitivity(gcal[sl]) - np.eye(3)).reshape(-1, 9)
                          if gcal is not None else [(None,) * 9] * nn)
                    con.executemany(
                        f"INSERT INTO {table} VALUES ({', '.join('?' * 24)})",
                        ((r.segment_id, int(si), int(round(t * 1e6)), *map(float, qq),
                          None if aa is None else float(aa), None if vv is None else float(vv),
                          *map(float, bb), *(None if x is None else float(x) for x in abb),
                          *(None if x is None else float(x) for x in gg))
                         for si, t, qq, aa, vv, bb, abb, gg in zip(
                             r.sample_index[sl], r.t[sl], q[sl], a, v, bd, ad, gd)))
            con.execute("INSERT INTO FilterRun VALUES (?,?,?,?)",
                        (r.segment_id, str(source.name) if source else None,
                         json.dumps(cfg.as_dict()), json.dumps(r.stats)))
        con.commit()
    finally:
        con.close()


def field_enu_from_ned(ned_ut) -> tuple[float, float, float]:
    return tuple(float(x) for x in R_ENU_NED @ np.asarray(ned_ut, dtype=np.float64))


__all__ = ["FilterConfig", "FilterResult", "ImuTagLog", "Events", "read_log", "simulated_log",
           "build_events", "gyro_sensitivity", "GYRO_CAL_COLUMNS", "mag_conversion_times",
           "estimate_mag_clock",
           "run_filter", "write_estimates", "still_mask", "triad", "isa_altitude_m",
           "poll_offset_s", "field_enu_from_ned", "mag_stored_to_tag", "IMU", "MAG",
           "PRESSURE"]
