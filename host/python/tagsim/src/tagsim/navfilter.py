"""A basic multi-rate post-processing filter for IMUTag logs: attitude,
gyro bias, and relative altitude with vertical velocity.

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
selects). By default the filter subtracts the **mean** age, half a nominal
conversion period, and treats the rest as noise that grows with the turn
rate; :attr:`FilterConfig.mag_age_s` sets the subtracted age explicitly, and
is the hook for a later per-sample age correction.

**Filter.** An error-state EKF over the nominal state

- ``q``: attitude, tag frame to ENU (as tagsim's ``TruthPose``);
- ``b``: gyro bias, rad/s, tag frame;
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

A forward pass stores, at every event, the predicted and filtered nominal
states and the RTS gain; a backward Rauch-Tung-Striebel pass then smooths
the whole record.
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
    #: Age (s) subtracted from each magnetometer event's poll time; None:
    #: the mean, half the nominal BMM350 period at the log's ODR.
    mag_age_s: float | None = None
    #: SD (s) of the age left after that; None: a uniform age's over one
    #: nominal period. Multiplied by the turn rate and the field, it is the
    #: magnetometer noise the unrecorded age adds.
    mag_age_sigma_s: float | None = None
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
    smooth: bool = True

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


def poll_offset_s(odr_hz: float) -> float:
    """Firmware poll time after the auxiliary slot's stamp: the superframe's
    last IMU sample, nine IMU periods after its first."""
    return (SUPERFRAME - 1) / odr_hz


def mag_age_s(cfg: FilterConfig, odr_hz: float) -> float:
    """The magnetometer age subtracted from its poll time."""
    from .errors import mag_rate_hz

    return cfg.mag_age_s if cfg.mag_age_s is not None else 0.5 / mag_rate_hz(odr_hz)


def build_events(log: ImuTagLog, cfg: FilterConfig) -> Events:
    """Magnetometer and pressure events, ordered by the time they apply at.

    Times are formed in whole microseconds, as ``ElapsedUs`` is, so an event
    that falls on an IMU sample has exactly that sample's time."""
    off_us = round(poll_offset_s(log.odr_hz) * 1e6)

    def us(t):
        return np.rint(np.asarray(t) * 1e6)

    parts = [(np.full(len(log.t_mag), MAG),
              (us(log.t_mag) + off_us - round(mag_age_s(cfg, log.odr_hz) * 1e6)) * 1e-6,
              log.t_mag,
              np.arange(len(log.t_mag)))]
    if cfg.use_pressure:
        parts.append((np.full(len(log.t_pressure), PRESSURE),
                      (us(log.t_pressure) + off_us) * 1e-6, log.t_pressure,
                      np.arange(len(log.t_pressure))))
    kind, t, t_log, idx = (np.concatenate(x) for x in zip(*parts))
    order = np.lexsort((kind, t))
    return Events(kind[order].astype(np.int64), t[order], t_log[order], idx[order])


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


def run_filter(log: ImuTagLog, cfg: FilterConfig) -> FilterResult:
    """Forward error-state EKF over the event stream, then RTS smoothing."""
    vert = cfg.use_pressure and len(log.t_pressure) > 0
    n = 8 if vert else 6
    odr = log.odr_hz
    t_imu, f_all, w_all = log.t_imu, log.accel_mps2, log.gyro_rps
    n_imu = len(t_imu)
    ev = build_events(log, dataclass_replace(cfg, use_pressure=vert))
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
    boot = None
    if not start_still and cfg.bootstrap:
        boot = run_filter(log, dataclass_replace(cfg, use_pressure=False, bootstrap=False,
                                                 smooth=True))
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
    if boot is not None:
        q = tuple(float(x) for x in boot.q_smoothed[0])
        b = boot.bias_smoothed[0].copy()
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
    if vert:
        P[6, 6] = s_h ** 2
        P[7, 7] = s_vz ** 2 if (start_still or first.sum() >= 3) else 5.0 ** 2

    # Noise.
    rate_scale = math.sqrt(odr / 100.0)
    q_theta = math.radians(cfg.gyro_noise_dps_rthz) ** 2
    q_bias = math.radians(cfg.gyro_bias_walk_dps_rts) ** 2
    q_vz = cfg.accel_vertical_noise_mps2_rthz ** 2
    if cfg.mag_age_sigma_s is not None:
        age_s = cfg.mag_age_sigma_s
    else:
        from .errors import mag_rate_hz
        age_s = 1.0 / (mag_rate_hz(odr) * math.sqrt(12.0))
    r_p = cfg.pressure_sigma_m ** 2

    # Storage, one node per IMU sample and per auxiliary event.
    n_nodes = n_imu + len(ev.t)
    xs_f = np.empty((n_nodes, 9))  # q(4), b(3), h, vz after the node's updates
    xs_p = np.empty((n_nodes, 9))  # predicted into the node
    gains = np.empty((n_nodes, n, n)) if cfg.smooth else None  # C_k: node k -> k+1
    imu_node = np.empty(n_imu, dtype=np.int64)
    stats = {"accel_updates": 0, "accel_gated": 0, "mag_updates": 0, "mag_gated": 0,
             "pressure_updates": 0, "still_fraction": float(still.mean()),
             "start_still": start_still, "g_mps2": g,
             "gyro_bias_init_dps": np.degrees(b0).tolist()}
    I_n = np.eye(n)

    def pack(q, b, h, vz):
        return (q[0], q[1], q[2], q[3], b[0], b[1], b[2], h, vz)

    def inject(dx):
        nonlocal q, b, h, vz
        q = _qnorm(_qmul(_qexp(dx[0], dx[1], dx[2]), q))
        b = b + dx[3:6]
        if vert:
            h += dx[6]
            vz += dx[7]

    def update(H, r, R):
        nonlocal P
        PHt = P @ H.T
        S = H @ PHt + R
        K = np.linalg.solve(S, PHt.T).T
        inject(K @ r)
        A = I_n - K @ H
        P = A @ P @ A.T + K @ R @ K.T

    def predict(f0_, w0_, f1_, w1_, dt, saturated, node):
        """Propagate over dt with the IMU interpolated linearly from (f0, w0)
        to (f1, w1), then store the prediction and the previous node's gain."""
        nonlocal q, P, h, vz
        R = _dcm(q)
        w = 0.5 * (w0_ + w1_) - b
        fm = 0.5 * (f0_ + f1_)
        fE = R @ fm
        q = _qnorm(_qmul(q, _qexp(w[0] * dt, w[1] * dt, w[2] * dt)))
        F = np.eye(n)
        F[0:3, 3:6] = -R * dt
        Qd = np.zeros((n, n))
        Qd[0:3, 0:3] = np.eye(3) * (q_theta * dt)
        Qd[3:6, 3:6] = np.eye(3) * (q_bias * dt)
        if vert:
            a = fE[2] - g
            h += vz * dt + 0.5 * a * dt * dt
            vz += a * dt
            F[6, 7] = dt
            F[7, 0] = fE[1] * dt
            F[7, 1] = -fE[0] * dt
            F[6, 0] = 0.5 * fE[1] * dt * dt
            F[6, 1] = -0.5 * fE[0] * dt * dt
            qv = q_vz * dt + (cfg.accel_saturated_mps2 * dt) ** 2 * saturated
            Qd[7, 7] = qv
            Qd[6, 6] = qv * dt * dt / 3.0
            Qd[6, 7] = Qd[7, 6] = qv * dt / 2.0
        Pp = F @ P @ F.T + Qd
        if gains is not None:
            gains[node - 1] = np.linalg.solve(Pp, F @ P).T
        P = 0.5 * (Pp + Pp.T)
        xs_p[node] = pack(q, b, h, vz)

    up_skew = _skew(np.array([0.0, 0.0, g]))
    field_skew = _skew(field)
    n_ev = len(ev.t)
    j = 0  # next auxiliary event
    node = 0  # the current node: an instant at which updates are applied
    xs_p[0] = pack(q, b, h, vz)

    def aux(j, w):
        _aux_update(ev, j, log, cfg, q, field, field_norm, field_skew, age_s, w, update,
                    vert, h, alt_ref, r_p, stats, n)

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
                    xs_f[node] = pack(q, b, h, vz)
                    node += 1
                    predict(fc, wc, fe, we, te - tc, sat, node)
                    tc, fc, wc = te, fe, we
                aux(j, 0.5 * (wc + w_b) - b)
                j += 1
            xs_f[node] = pack(q, b, h, vz)
            node += 1
            predict(fc, wc, f_b, w_b, tb - tc, sat, node)
        # Accelerometer update at the IMU sample.
        f = f_all[i]
        excess = abs(float(np.linalg.norm(f)) - g)
        if excess < cfg.accel_gate_g * G0 and not log.accel_saturated[i]:
            wn = float(np.linalg.norm(w_all[i] - b))
            sigma = rate_scale * (cfg.accel_sigma_mps2 + cfg.accel_sigma_per_excess * excess
                                  + cfg.accel_speed_mps * wn)
            R = _dcm(q)
            H = np.zeros((3, n))
            H[:, 0:3] = R.T @ up_skew
            update(H, f - R[2, :] * g, np.eye(3) * sigma ** 2)  # R^T (0, 0, g)
            stats["accel_updates"] += 1
        else:
            stats["accel_gated"] += 1
        # Auxiliary events at this sample (and, at the first, before it).
        while j < n_ev and ev.t[j] <= t_imu[i]:
            aux(j, w_all[i] - b)
            j += 1
        imu_node[i] = node
    xs_f[node] = pack(q, b, h, vz)
    n_nodes = node + 1

    xs_s = None
    if gains is not None:
        xs_s = xs_f[:n_nodes].copy()
        for k in range(n_nodes - 2, -1, -1):
            d = np.empty(n)
            d[0:3] = _qlog_global(tuple(xs_s[k + 1, 0:4]), tuple(xs_p[k + 1, 0:4]))
            d[3:6] = xs_s[k + 1, 4:7] - xs_p[k + 1, 4:7]
            if vert:
                d[6:8] = xs_s[k + 1, 7:9] - xs_p[k + 1, 7:9]
            dx = gains[k] @ d
            qk = _qnorm(_qmul(_qexp(dx[0], dx[1], dx[2]), tuple(xs_f[k, 0:4])))
            xs_s[k, 0:4] = qk
            xs_s[k, 4:7] = xs_f[k, 4:7] + dx[3:6]
            if vert:
                xs_s[k, 7:9] = xs_f[k, 7:9] + dx[6:8]

    fwd = xs_f[imu_node]
    sm = xs_s[imu_node] if xs_s is not None else None
    stats["gyro_bias_final_dps"] = np.degrees(fwd[-1, 4:7]).tolist()
    return FilterResult(
        segment_id=log.segment_id, sample_index=log.sample_index, t=t_imu,
        q_forward=fwd[:, 0:4], q_smoothed=None if sm is None else sm[:, 0:4],
        alt_forward=fwd[:, 7] if vert else None,
        alt_smoothed=sm[:, 7] if (vert and sm is not None) else None,
        vz_forward=fwd[:, 8] if vert else None,
        vz_smoothed=sm[:, 8] if (vert and sm is not None) else None,
        bias_forward=fwd[:, 4:7], bias_smoothed=None if sm is None else sm[:, 4:7],
        stats=stats)


def _aux_update(ev, j, log, cfg, q, field, field_norm, field_skew, age_s, w, update, vert, h,
                alt_ref, r_p, stats, n):
    """One magnetometer or pressure update at the current state."""
    if ev.kind[j] == MAG:
        m = log.mag_ut[ev.index[j]]
        if abs(float(np.linalg.norm(m)) - field_norm) > cfg.mag_gate_ut:
            stats["mag_gated"] += 1
            return
        R = _dcm(q)
        pred = R.T @ field
        H = np.zeros((3, n))
        H[:, 0:3] = R.T @ field_skew
        sigma = cfg.mag_sigma_ut + field_norm * float(np.linalg.norm(w)) * age_s
        update(H, m - pred, np.eye(3) * sigma ** 2)
        stats["mag_updates"] += 1
    elif vert:
        z = float(isa_altitude_m(log.pressure_hpa[ev.index[j]], cfg.sea_level_hpa)) - alt_ref
        H = np.zeros((1, n))
        H[0, 6] = 1.0
        update(H, np.array([z - h]), np.array([[r_p]]))
        stats["pressure_updates"] += 1


def dataclass_replace(cfg: FilterConfig, **changes) -> FilterConfig:
    from dataclasses import replace
    return replace(cfg, **changes)


# ---------------------------------------------------------------------------
# Output


def write_estimates(path: Path, results: list[FilterResult], cfg: FilterConfig,
                    source: Path | None = None) -> None:
    """``FilterForward`` and ``FilterSmoothed``: one row per IMU sample,
    ``SegmentId, SampleIndex, ElapsedUs, qw qx qy qz`` (tag frame to ENU),
    ``alt`` (m, relative to the first pressure sample) and ``vz`` (m/s, up),
    NULL without the pressure channel; ``bx by bz`` the gyro bias (dps); and
    ``FilterRun`` with the configuration and counters."""
    if path.exists():
        path.unlink()
    con = sqlite3.connect(path)
    try:
        for table in ("FilterForward", "FilterSmoothed"):
            con.execute(f"CREATE TABLE {table} (SegmentId INTEGER, SampleIndex INTEGER, "
                        "ElapsedUs INTEGER, qw REAL, qx REAL, qy REAL, qz REAL, alt REAL, "
                        "vz REAL, bx REAL, by REAL, bz REAL)")
        con.execute("CREATE TABLE FilterRun (SegmentId INTEGER, Source TEXT, ConfigJson TEXT, "
                    "StatsJson TEXT)")
        for r in results:
            for table, q, alt, vz, bias in (
                    ("FilterForward", r.q_forward, r.alt_forward, r.vz_forward, r.bias_forward),
                    ("FilterSmoothed", r.q_smoothed, r.alt_smoothed, r.vz_smoothed,
                     r.bias_smoothed)):
                if q is None:
                    continue
                nn = len(r.t)
                a = alt if alt is not None else [None] * nn
                v = vz if vz is not None else [None] * nn
                bd = np.degrees(bias)
                con.executemany(
                    f"INSERT INTO {table} VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
                    ((r.segment_id, int(si), int(round(t * 1e6)), *map(float, qq),
                      None if aa is None else float(aa), None if vv is None else float(vv),
                      *map(float, bb))
                     for si, t, qq, aa, vv, bb in zip(r.sample_index, r.t, q, a, v, bd)))
            con.execute("INSERT INTO FilterRun VALUES (?,?,?,?)",
                        (r.segment_id, str(source.name) if source else None,
                         json.dumps(cfg.as_dict()), json.dumps(r.stats)))
        con.commit()
    finally:
        con.close()


def field_enu_from_ned(ned_ut) -> tuple[float, float, float]:
    return tuple(float(x) for x in R_ENU_NED @ np.asarray(ned_ut, dtype=np.float64))


__all__ = ["FilterConfig", "FilterResult", "ImuTagLog", "Events", "read_log", "simulated_log",
           "build_events",
           "run_filter", "write_estimates", "still_mask", "triad", "isa_altitude_m",
           "poll_offset_s", "field_enu_from_ned", "mag_stored_to_tag", "IMU", "MAG",
           "PRESSURE"]
