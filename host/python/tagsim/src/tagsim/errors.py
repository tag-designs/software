"""Datasheet sensor errors: what turns tagsim's ideal signals into measurements.

Phase 2 of design/proposals/imutag-simulator.md, kept to what an orientation
filter's behaviour depends on. Every figure is a named parameter of
:class:`ErrorConfig`, defaulting to the datasheet's typical value (the
proposal's "Datasheet noise and errors"):

- **Accelerometer and gyroscope** (LSM6DSV16X, approximated by the LSM6DSV
  sheet, high-performance mode): white noise of 60 ug/sqrt(Hz) and
  2.8 mdps/sqrt(Hz) over the sheet's bandwidth ``BW = ODR/2``, so
  ``sigma = density * sqrt(ODR/2)`` per sample (0.42 mg and 0.020 dps at
  100 Hz), and a turn-on bias drawn once per run, uniform in +/-12 mg and
  +/-1 dps per axis. Both are added before ``int16`` quantization and
  saturation, as in the part.
- **Sensitivity and misalignment**: the true signal is multiplied by
  ``T = I + diag(s) + M``, drawn once per run, with the scale error ``s``
  uniform in +/-0.3 % per gyro axis (the sheet's ``G_So%``) and, an
  assumption since the LSM6DSV sheet gives no accelerometer figure, +/-1 %
  per accelerometer axis; the off-diagonal ``M`` (cross-axis sensitivity,
  axis misalignment) is uniform in +/-0.5 degree per element for both, also
  assumed (the sheet gives no cross-axis figure).
- **Bias instability**: a first-order Gauss-Markov process per axis, added
  to the turn-on bias. The sheet gives no figure; the defaults (0.003 dps
  and 0.05 mg RMS, 100 s correlation time) are assumptions, to be fitted from
  a bench recording.
- **Vibration** (off by default): band-limited Gaussian noise of a given RMS
  per axis, added to the true specific force and rate before ``T``. It is
  generated at the ODR, so its band is clipped below the Nyquist frequency;
  the sensor's own filtering and aliasing are not modelled.
- **Magnetometer** (BMM350): hard iron V, uniform in +/-25 uT per axis, and a
  symmetric soft-iron correction ``A = I + D + O``, with D uniform in +/-1 %
  (x, y) and +/-3 % (z) and the off-diagonal O uniform in +/-0.5 %. They are
  applied, in the stored axes the host calibrates in, as the inverse of the
  host's correction ``M' = A(M - V)``: ``M = A^-1 M_true + V``. White noise
  of 190 nT (x, y) and 450 nT (z) RMS is then added per conversion.
- **Magnetometer timing**: the BMM350 free-runs at the firmware's rate for
  the ODR (``select_mag_rate`` in ``families/IMUTag/src/sensors.c``: 12.5 Hz
  at 100 Hz, scaling with the ODR), with a random phase and a rate error
  uniform in +/-2 % (the sheet's output-rate tolerance at room temperature).
- **Pressure** (BMP581): 0.41 Pa RMS white noise per conversion, a per-run
  offset uniform in +/-30 Pa, and quantization to 1/64 Pa.

**Why not gnss-ins-sim.** Its ``acc_gen`` and ``gyro_gen`` would do the same
arithmetic for these two terms, but it is not on PyPI, would be pinned to a
git commit, and brings more than tagsim needs. This module is the small
equivalent, with the same meanings:
``b`` (constant bias) is the turn-on bias here, and gnss-ins-sim's white
noise ``vrw``/``arw``, whose per-sample sigma is ``vrw * sqrt(fs)``, equals
this module's datasheet density divided by sqrt(2).

**Determinism.** Every random term draws from its own generator, seeded from
``(seed, stream)``, so a run is reproducible from its seed and configuration,
and changing one term's parameters does not change the others' draws.
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass, field, fields

import numpy as np

#: Independent random streams, by name (the second element of the seed).
_STREAMS = {"draw": 1, "accel": 2, "gyro": 3, "mag": 4, "pressure": 5, "weather": 6,
            "calibration": 7, "sensitivity": 8, "accel_gm": 9, "gyro_gm": 10,
            "vibration": 11}

#: BMM350 rate at 100 Hz IMU ODR; the firmware scales it with the ODR.
MAG_RATE_AT_100HZ = 12.5


def rng(seed: int, stream: str) -> np.random.Generator:
    """The generator for one named random stream of a run."""
    return np.random.default_rng([seed, _STREAMS[stream]])


def mag_rate_hz(odr_hz: int) -> float:
    """The BMM350 free-running rate the firmware picks for an IMU ODR."""
    return MAG_RATE_AT_100HZ * odr_hz / 100.0


@dataclass
class ErrorConfig:
    """Sensor error parameters. Ranges are half-widths of uniform draws."""

    accel_noise_ug_rthz: float = 60.0  #: noise density, over BW = ODR/2
    gyro_noise_mdps_rthz: float = 2.8
    accel_bias_mg: float = 12.0  #: turn-on bias range, per axis
    gyro_bias_dps: float = 1.0
    #: Scale-factor error range, per axis: gyro the sheet's G_So% (0.3 %);
    #: accelerometer assumed (the sheet gives none).
    accel_scale_error: float = 0.01
    gyro_scale_error: float = 0.003
    #: Cross-axis / misalignment range per off-diagonal element, degrees;
    #: assumed (the sheet gives none).
    accel_misalignment_deg: float = 0.5
    gyro_misalignment_deg: float = 0.5
    #: Bias instability, first-order Gauss-Markov: stationary RMS and
    #: correlation time. Assumed; to be fitted from bench data.
    accel_bias_gm_mg: float = 0.05
    gyro_bias_gm_dps: float = 0.003
    bias_gm_tau_s: float = 100.0
    #: Band-limited vibration, RMS per axis (0 disables) and band, Hz.
    vibration_accel_mg: float = 0.0
    vibration_gyro_dps: float = 0.0
    vibration_band_hz: tuple[float, float] = (20.0, 45.0)
    mag_noise_ut: tuple[float, float, float] = (0.19, 0.19, 0.45)  #: RMS, stored axes
    mag_hard_iron_ut: float = 25.0
    mag_soft_iron_diag: tuple[float, float, float] = (0.01, 0.01, 0.03)
    mag_soft_iron_offdiag: float = 0.005
    mag_rate_error: float = 0.02  #: fractional BMM350 rate error range
    pressure_noise_pa: float = 0.41
    pressure_offset_pa: float = 30.0
    pressure_lsb_pa: float = 1.0 / 64.0  #: 0 disables quantization
    #: Sea-level pressure random walk, hPa per sqrt(hour); 0 holds it constant.
    sea_level_walk_hpa_rth: float = 0.0
    #: Calibration table to write: "true" (the drawn V and A) or "perturbed".
    calibration: str = "true"
    cal_perturb_hard_iron_ut: float = 2.0  #: per-axis range added to V if perturbed
    cal_perturb_soft_iron: float = 0.01  #: per-element range added to A if perturbed

    def __post_init__(self) -> None:
        if self.calibration not in ("true", "perturbed"):
            raise ValueError(f"calibration {self.calibration!r}: 'true' or 'perturbed'")

    @classmethod
    def ideal(cls) -> "ErrorConfig":
        """No noise, bias, distortion, rate error, offset or quantization."""
        keep = ("calibration", "vibration_band_hz", "bias_gm_tau_s")
        zero = {f.name: (0.0, 0.0, 0.0) if isinstance(f.default, tuple) else 0.0
                for f in fields(cls) if f.name not in keep}
        return cls(**zero)

    def as_dict(self) -> dict:
        return asdict(self)


@dataclass
class ErrorDraw:
    """The per-run constants drawn from an :class:`ErrorConfig`."""

    accel_bias_mg: np.ndarray  #: tag frame
    gyro_bias_dps: np.ndarray  #: tag frame
    hard_iron_ut: np.ndarray  #: V, stored magnetometer axes
    soft_iron: np.ndarray  #: A, the host's correction matrix (3, 3)
    mag_rate_hz: float  #: the BMM350's actual conversion rate
    mag_phase: float  #: fraction of a conversion period, in [0, 1)
    pressure_offset_pa: float
    cal_hard_iron_ut: np.ndarray  #: V written to the Calibration table
    cal_soft_iron: np.ndarray  #: A written to the Calibration table
    #: T, the sensitivity and misalignment matrices (measured = T true), tag frame
    accel_sensitivity: np.ndarray = field(default_factory=lambda: np.eye(3))
    gyro_sensitivity: np.ndarray = field(default_factory=lambda: np.eye(3))

    def as_dict(self) -> dict:
        return {k: (v.tolist() if isinstance(v, np.ndarray) else v)
                for k, v in asdict(self).items()}


def draw_errors(cfg: ErrorConfig, odr_hz: int, seed: int) -> ErrorDraw:
    """Draw the per-run constants, uniform within each configured range."""
    g = rng(seed, "draw")

    def u(half, shape=3):
        return g.uniform(-1.0, 1.0, shape) * np.asarray(half, dtype=np.float64)

    accel_b, gyro_b = u(cfg.accel_bias_mg), u(cfg.gyro_bias_dps)
    v = u(cfg.mag_hard_iron_ut)
    off = u(cfg.mag_soft_iron_offdiag)  # (xy, xz, yz)
    a = np.eye(3) + np.diag(u(cfg.mag_soft_iron_diag))
    a[0, 1] = a[1, 0] = off[0]
    a[0, 2] = a[2, 0] = off[1]
    a[1, 2] = a[2, 1] = off[2]
    rate = mag_rate_hz(odr_hz) * (1.0 + float(u(cfg.mag_rate_error, None)))
    phase = float(g.uniform(0.0, 1.0))
    p_off = float(u(cfg.pressure_offset_pa, None))
    cal_v, cal_a = v.copy(), a.copy()
    if cfg.calibration == "perturbed":
        c = rng(seed, "calibration")
        cal_v = v + c.uniform(-1.0, 1.0, 3) * cfg.cal_perturb_hard_iron_ut
        p = c.uniform(-1.0, 1.0, (3, 3)) * cfg.cal_perturb_soft_iron
        cal_a = a + (p + p.T) / 2.0
    s = rng(seed, "sensitivity")

    def sensitivity(scale, misalign_deg):
        t = np.eye(3) + np.diag(s.uniform(-1.0, 1.0, 3) * scale)
        off = s.uniform(-1.0, 1.0, (3, 3)) * np.radians(misalign_deg)
        return t + off - np.diag(np.diag(off))

    t_a = sensitivity(cfg.accel_scale_error, cfg.accel_misalignment_deg)
    t_g = sensitivity(cfg.gyro_scale_error, cfg.gyro_misalignment_deg)
    return ErrorDraw(accel_b, gyro_b, v, a, rate, phase, p_off, cal_v, cal_a, t_a, t_g)


def white_sigma(density: float, odr_hz: int) -> float:
    """Per-sample RMS of white noise of a datasheet density over BW = ODR/2."""
    return density * np.sqrt(odr_hz / 2.0)


def gauss_markov(n: int, sigma: float, tau_s: float, odr_hz: float,
                 g: np.random.Generator) -> np.ndarray:
    """``(n, 3)`` first-order Gauss-Markov samples, stationary RMS ``sigma``
    and correlation time ``tau_s``, started from the stationary distribution;
    zeros when ``sigma`` is 0."""
    if sigma <= 0 or n == 0:
        return np.zeros((n, 3))
    phi = math.exp(-1.0 / (odr_hz * tau_s))
    drive = g.standard_normal((n, 3)) * (sigma * math.sqrt(1.0 - phi * phi))
    drive[0] = g.standard_normal(3) * sigma
    # x_k = phi x_{k-1} + drive_k, as a first-order IIR filter.
    from scipy.signal import lfilter
    return lfilter([1.0], [1.0, -phi], drive, axis=0)


def band_noise(n: int, rms: float, band_hz: tuple[float, float], odr_hz: float,
               g: np.random.Generator) -> np.ndarray:
    """``(n, 3)`` Gaussian noise of RMS ``rms`` per axis, confined to
    ``band_hz`` (clipped below the Nyquist frequency); zeros when ``rms`` is 0
    or the band is empty."""
    lo, hi = float(band_hz[0]), min(float(band_hz[1]), 0.5 * odr_hz)
    if rms <= 0 or n < 2 or hi <= lo:
        return np.zeros((n, 3))
    spec = np.fft.rfft(g.standard_normal((n, 3)), axis=0)
    f = np.fft.rfftfreq(n, 1.0 / odr_hz)
    spec[(f < lo) | (f > hi)] = 0.0
    x = np.fft.irfft(spec, n, axis=0)
    std = x.std(axis=0)
    return x * np.where(std > 0, rms / np.where(std > 0, std, 1.0), 0.0)


def measure_imu(accel_mg: np.ndarray, gyro_dps: np.ndarray, odr_hz: int, cfg: ErrorConfig,
                draw: ErrorDraw, seed: int) -> tuple[np.ndarray, np.ndarray]:
    """Accel (mg) and gyro (dps), tag frame, with the sensor errors:
    ``T (true + vibration) + bias + Gauss-Markov + white noise``.

    The result is still unquantized; :func:`tagsim.sensors.stored_imu_from`
    quantizes and saturates it.
    """
    n = len(accel_mg)
    sa = white_sigma(cfg.accel_noise_ug_rthz / 1000.0, odr_hz)
    sg = white_sigma(cfg.gyro_noise_mdps_rthz / 1000.0, odr_hz)
    vib = rng(seed, "vibration")
    a_in = accel_mg + band_noise(n, cfg.vibration_accel_mg, cfg.vibration_band_hz, odr_hz, vib)
    w_in = gyro_dps + band_noise(n, cfg.vibration_gyro_dps, cfg.vibration_band_hz, odr_hz, vib)
    a = (a_in @ draw.accel_sensitivity.T + draw.accel_bias_mg
         + gauss_markov(n, cfg.accel_bias_gm_mg, cfg.bias_gm_tau_s, odr_hz, rng(seed, "accel_gm"))
         + sa * rng(seed, "accel").standard_normal(accel_mg.shape))
    w = (w_in @ draw.gyro_sensitivity.T + draw.gyro_bias_dps
         + gauss_markov(n, cfg.gyro_bias_gm_dps, cfg.bias_gm_tau_s, odr_hz, rng(seed, "gyro_gm"))
         + sg * rng(seed, "gyro").standard_normal(gyro_dps.shape))
    return a, w


def measure_mag(stored_true_ut: np.ndarray, cfg: ErrorConfig, draw: ErrorDraw,
                seed: int) -> np.ndarray:
    """Distorted, noisy field in the stored axes: ``A^-1 M + V + noise``.

    Rows that are NaN (missed conversions) stay NaN.
    """
    m = stored_true_ut @ np.linalg.inv(draw.soft_iron).T + draw.hard_iron_ut
    noise = rng(seed, "mag").standard_normal(m.shape) * np.asarray(cfg.mag_noise_ut)
    return m + noise


def measure_pressure(pressure_hpa: np.ndarray, cfg: ErrorConfig, draw: ErrorDraw,
                     seed: int) -> np.ndarray:
    """Measured pressure (hPa): offset, white noise and 1/64 Pa quantization."""
    pa = pressure_hpa * 100.0 + draw.pressure_offset_pa
    pa = pa + cfg.pressure_noise_pa * rng(seed, "pressure").standard_normal(pa.shape)
    if cfg.pressure_lsb_pa > 0:
        pa = np.rint(pa / cfg.pressure_lsb_pa) * cfg.pressure_lsb_pa
    return pa / 100.0


def sea_level_walk(t_s: np.ndarray, base_hpa: float, cfg: ErrorConfig,
                   seed: int) -> np.ndarray:
    """Sea-level pressure (hPa) at times ``t_s``: constant, or a random walk
    of ``sea_level_walk_hpa_rth`` hPa per sqrt(hour) starting at ``base_hpa``."""
    t_s = np.asarray(t_s, dtype=np.float64)
    if cfg.sea_level_walk_hpa_rth <= 0 or len(t_s) < 2:
        return np.full(t_s.shape, base_hpa)
    dt_h = np.diff(t_s) / 3600.0
    steps = rng(seed, "weather").standard_normal(len(dt_h)) * cfg.sea_level_walk_hpa_rth \
        * np.sqrt(dt_h)
    return base_hpa + np.concatenate([[0.0], np.cumsum(steps)])


def saturation(values: np.ndarray, lsb: float, full_scale: float) -> list[float]:
    """Per-axis fraction of samples that read full scale after quantization."""
    limit = min(round(full_scale / lsb), 32767)
    if len(values) == 0:
        return [0.0] * values.shape[1]
    return (np.abs(np.rint(values / lsb)) >= limit).mean(axis=0).tolist()


__all__ = ["ErrorConfig", "ErrorDraw", "draw_errors", "measure_imu", "gauss_markov", "band_noise", "measure_mag",
           "measure_pressure", "sea_level_walk", "saturation", "white_sigma", "mag_rate_hz",
           "rng"]
