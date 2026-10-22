"""An ArduPilot DataFlash log of the truth and an estimate, for UAV Log Viewer
and FlightCoach.

The file imitates a log from the autopilot that flew the source flights,
ArduPlane 4.0.9: every message is written with that version's own type id,
format, columns, units and multipliers (taken from the source logs), so
anything that reads those logs reads this one.

Two readers set what it carries:

- **UAV Log Viewer** (``ArduPilot/UAVLogViewer``, JsDataflashParser and
  ``dataflashDataExtractor.js``): the trajectory from ``POS``, ``AHR2`` or
  ``GPS``; the attitude from ``ATT`` or ``AHR2``; the start time from
  ``GPS`` (``GWk``, ``GMS``); the aeroplane model from a ``MSG`` containing
  "ArduPlane"; and at least one ``MODE``, since the plot and the 3D track are
  coloured by flight mode and a log without one fails to display.
- **FlightCoach** (``flightdata.Flight.from_log``, through pymavlink):
  ``PARM`` with ``AHRS_EKF_TYPE``, ``ATT`` with its ``Des*`` columns, ``POS``,
  ``ORGN`` (required), and velocity from ``XKF1``.

So the file has:

- ``POS``, ``ATT`` and ``XKF1``: the truth position, attitude and velocity,
  the readers' defaults (``ATT``'s demanded attitude is the truth too, and
  its errors zero);
- ``AHR2``: the estimate's attitude, flown along the truth position, so
  choosing ``AHR2`` as both trajectory and attitude source shows the estimate
  flying the same path (``AHR2`` is ArduPilot's own secondary estimate, which
  UAV Log Viewer already offers beside ``ATT``);
- ``GPS`` at 5 Hz, ``ORGN`` (the EKF origin), ``PARM`` (``AHRS_EKF_TYPE`` 3),
  ``MODE`` (MANUAL, as the pattern flights were flown), ``MSG``, and
  ``FMTU``/``UNIT``/``MULT``.

Attitude is that of the airframe (FRD to NED, ArduPilot's roll, pitch, yaw),
recovered from the tag attitude through the run's mounting. Position goes
from the local ENU frame back to latitude and longitude about the EKF origin
with the same flat-earth approximation the simulator used.

Times are the source log's own ``TimeUS``, so the file lines up with the
source flight in either viewer.
"""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

from .ardupilot import GPS_EPOCH_UNIX, GPS_LEAP_SECONDS
from .rotations import dcmtoq, qconj, qmul
from .sensors import R_ENU_NED, mounting_matrix

_EARTH_RADIUS_M = 6378137.0
_HEAD = b"\xa3\x95"

#: ArduPlane 4.0.9's definitions of the messages written:
#: (type, name, format, columns, units, multipliers).
_MESSAGES = [
    (81, "XKF1", "QccCfffffffccce", "TimeUS,Roll,Pitch,Yaw,VN,VE,VD,dPD,PN,PE,PD,GX,GY,GZ,OH",
     "sddhnnnnmmmkkkm", "FBBB0000000BBBB"),
    (129, "PARM", "QNf", "TimeUS,Name,Value", "s--", "F--"),
    (130, "GPS", "QBIHBcLLeffffB",
     "TimeUS,Status,GMS,GWk,NSats,HDop,Lat,Lng,Alt,Spd,GCrs,VZ,Yaw,U",
     "s---SmDUmnhnh-", "F---0BGGB000--"),
    (134, "MSG", "QZ", "TimeUS,Message", "s-", "F-"),
    (141, "AHR2", "QccCfLLffff", "TimeUS,Roll,Pitch,Yaw,Alt,Lat,Lng,Q1,Q2,Q3,Q4",
     "sddhmDU????", "FBBB0GG????"),
    (166, "ATT", "QccccCCCC", "TimeUS,DesRoll,Roll,DesPitch,Pitch,DesYaw,Yaw,ErrRP,ErrYaw",
     "sddddhhdh", "FBBBBBBBB"),
    (188, "MODE", "QMBB", "TimeUS,Mode,ModeNum,Rsn", "s---", "F---"),
    (199, "POS", "QLLfff", "TimeUS,Lat,Lng,Alt,RelHomeAlt,RelOriginAlt", "sDUmmm", "FGG000"),
    (210, "ORGN", "QBLLe", "TimeUS,Type,Lat,Lng,Alt", "s-DUm", "F-GGB"),
    (218, "FMTU", "QBNN", "TimeUS,FmtType,UnitIds,MultIds", "s---", "F---"),
    (219, "UNIT", "QbZ", "TimeUS,Id,Label", "s--", "F--"),
    (220, "MULT", "Qbd", "TimeUS,Id,Mult", "s--", "F--"),
]
_UNITS = {"-": "", "?": "UNKNOWN", "d": "deg", "h": "degheading", "D": "deglatitude",
          "U": "deglongitude", "k": "deg/s", "m": "m", "n": "m/s", "s": "s",
          "S": "satellites"}
_MULTS = {"-": 0.0, "?": 1.0, "0": 1.0, "B": 1e-2, "F": 1e-6, "G": 1e-7}
_PACK = {"Q": "Q", "B": "B", "M": "B", "b": "b", "H": "H", "I": "I", "L": "i", "f": "f",
         "d": "d", "c": "h", "C": "H", "e": "i", "Z": "64s", "N": "16s"}

#: ArduPilot's plane mode number for MANUAL.
_MODE_MANUAL = 0


def _fmt_record(type_id: int, name: str, fmt: str, columns: str) -> bytes:
    length = 3 + struct.calcsize("<" + "".join(_PACK[c] for c in fmt))
    return _HEAD + struct.pack("<BBB4s16s64s", 128, type_id, length, name.encode(),
                               fmt.encode(), columns.encode())


class DataFlashWriter:
    """Writes the message types in ``_MESSAGES``, described by FMT records."""

    def __init__(self) -> None:
        self.chunks = [_HEAD + struct.pack("<BBB4s16s64s", 128, 128, 89, b"FMT", b"BBnNZ",
                                           b"Type,Length,Name,Format,Columns")]
        self.types = {}
        for type_id, name, fmt, cols, _, _ in _MESSAGES:
            self.chunks.append(_fmt_record(type_id, name, fmt, cols))
            self.types[name] = (type_id, "<" + "".join(_PACK[c] for c in fmt))
        for uid, label in _UNITS.items():
            self.write("UNIT", 0, ord(uid), label.encode())
        for mid, mult in _MULTS.items():
            self.write("MULT", 0, ord(mid), mult)
        for type_id, _, _, _, units, mults in _MESSAGES:
            self.write("FMTU", 0, type_id, units.encode(), mults.encode())

    def write(self, name: str, *values) -> None:
        type_id, fmt = self.types[name]
        self.chunks.append(_HEAD + bytes([type_id]) + struct.pack(fmt, *values))

    def write_many(self, name: str, columns: list[np.ndarray]) -> None:
        type_id, fmt = self.types[name]
        head = _HEAD + bytes([type_id])
        for row in zip(*(np.asarray(c).tolist() for c in columns)):
            self.chunks.append(head + struct.pack(fmt, *row))

    def save(self, path: Path) -> None:
        Path(path).write_bytes(b"".join(self.chunks))


def body_q_ned(q_enu_tag: np.ndarray, mounting: str) -> np.ndarray:
    """The airframe's attitude quaternion (w x y z), body FRD to NED."""
    q_ned_enu = dcmtoq(R_ENU_NED.T)
    q_tb = qconj(dcmtoq(mounting_matrix(mounting)))
    return qmul(qmul(q_ned_enu, q_enu_tag), q_tb)


def body_euler_deg(q_enu_tag: np.ndarray, mounting: str) -> np.ndarray:
    """ArduPilot roll, pitch, yaw (deg, yaw in [0, 360)) of the airframe."""
    q_nb = body_q_ned(q_enu_tag, mounting)
    xyzw = np.concatenate([q_nb[:, 1:], q_nb[:, :1]], axis=1)
    yaw, pitch, roll = Rotation.from_quat(xyzw).as_euler("ZYX", degrees=True).T
    return np.stack([roll, pitch, yaw % 360.0], -1)


def enu_to_geodetic(enu: np.ndarray, origin: tuple[float, float, float]) -> np.ndarray:
    """Latitude, longitude (deg) and altitude (m) of local ENU points."""
    lat0, lon0, alt0 = origin
    lat = lat0 + np.degrees(enu[:, 1] / _EARTH_RADIUS_M)
    lon = lon0 + np.degrees(enu[:, 0] / (_EARTH_RADIUS_M * np.cos(np.radians(lat0))))
    return np.stack([lat, lon, alt0 + enu[:, 2]], -1)


def _cdeg(deg: np.ndarray) -> np.ndarray:
    """Signed centidegrees (format ``c``)."""
    return np.clip(np.rint(np.asarray(deg) * 100.0), -32768, 32767).astype(np.int64)


def _cdeg_heading(deg: np.ndarray) -> np.ndarray:
    """Unsigned centidegrees in [0, 36000) (format ``C``)."""
    return (np.rint(np.mod(deg, 360.0) * 100.0).astype(np.int64)) % 36000


def _deg7(deg: np.ndarray) -> np.ndarray:
    """Degrees x 1e7 (format ``L``)."""
    return np.rint(np.asarray(deg) * 1e7).astype(np.int64)


def write_viewer_log(path: Path, time_us: np.ndarray, q_true: np.ndarray, pos_enu: np.ndarray,
                     origin: tuple[float, float, float], start_unix: float, mounting: str,
                     est_time_us: np.ndarray | None = None, q_est: np.ndarray | None = None,
                     est_pos_enu: np.ndarray | None = None, label: str = "truth",
                     rate_hz: float = 25.0, vel_enu: np.ndarray | None = None) -> int:
    """Write the viewer log; returns the number of attitude records.

    ``time_us`` is the source log's TimeUS of each truth row and
    ``start_unix`` the UTC time of the first row. ``vel_enu`` is the truth
    velocity (m/s); without it, velocity is differenced from ``pos_enu``.
    Truth is decimated to ``rate_hz``; the estimate, if given, is written at
    its own rows.
    """
    t = np.asarray(time_us, dtype=np.float64)
    step = max(1, int(round(1e6 / rate_hz / np.median(np.diff(t)))))
    k = np.arange(0, len(t), step)
    n = len(k)
    tu = t[k].astype(np.uint64)
    if vel_enu is None:
        vel_enu = np.gradient(pos_enu, t * 1e-6, axis=0)
    p, v = pos_enu[k], vel_enu[k]
    geo = enu_to_geodetic(p, origin)
    lat, lon = _deg7(geo[:, 0]), _deg7(geo[:, 1])
    eul = body_euler_deg(q_true[k], mounting)
    roll, pitch, yaw = _cdeg(eul[:, 0]), _cdeg(eul[:, 1]), _cdeg_heading(eul[:, 2])
    zeros = np.zeros(n, dtype=np.int64)

    w = DataFlashWriter()
    w.write("MSG", int(tu[0]), f"ArduPlane V4.0.9 (tagsim {label})".encode())
    w.write("PARM", int(tu[0]), b"AHRS_EKF_TYPE", 3.0)
    w.write("MODE", int(tu[0]), _MODE_MANUAL, _MODE_MANUAL, 0)
    w.write("ORGN", int(tu[0]), 0, int(_deg7(origin[0])), int(_deg7(origin[1])),
            int(round(origin[2] * 100.0)))

    # GPS at 5 Hz: the start time, and a GPS trajectory.
    g = np.arange(0, n, max(1, int(round(rate_hz / 5))))
    gps_s = start_unix + (t[k][g] - t[0]) * 1e-6 - GPS_EPOCH_UNIX + GPS_LEAP_SECONDS
    week = np.floor(gps_s / 604800.0).astype(np.int64)
    gms = np.rint((gps_s - week * 604800.0) * 1000.0).astype(np.int64)
    ng = len(g)
    w.write_many("GPS", [tu[g], np.full(ng, 3), gms, week, np.full(ng, 12), np.full(ng, 100),
                         lat[g], lon[g], np.rint(geo[g, 2] * 100.0).astype(np.int64),
                         np.hypot(v[g, 0], v[g, 1]),
                         np.degrees(np.arctan2(v[g, 0], v[g, 1])) % 360.0,
                         -v[g, 2], np.zeros(ng), np.ones(ng, dtype=np.int64)])

    w.write_many("POS", [tu, lat, lon, geo[:, 2], geo[:, 2] - geo[0, 2], geo[:, 2] - origin[2]])
    w.write_many("ATT", [tu, roll, roll, pitch, pitch, yaw, yaw, zeros, zeros])
    # XKF1 is NED: north, east, down from ENU.
    w.write_many("XKF1", [tu, roll, pitch, yaw, v[:, 1], v[:, 0], -v[:, 2], -v[:, 2],
                          p[:, 1], p[:, 0], -p[:, 2], zeros, zeros, zeros, zeros])

    if q_est is not None:
        te = np.asarray(est_time_us, dtype=np.float64)
        ge = enu_to_geodetic(est_pos_enu, origin)
        qn = body_q_ned(q_est, mounting)
        ee = body_euler_deg(q_est, mounting)
        ok = np.all(np.isfinite(ee), axis=1)
        w.write_many("AHR2", [te[ok].astype(np.uint64), _cdeg(ee[ok, 0]), _cdeg(ee[ok, 1]),
                              _cdeg_heading(ee[ok, 2]), ge[ok, 2], _deg7(ge[ok, 0]),
                              _deg7(ge[ok, 1]), qn[ok, 0], qn[ok, 1], qn[ok, 2], qn[ok, 3]])
    w.save(path)
    return n


__all__ = ["DataFlashWriter", "write_viewer_log", "body_q_ned", "body_euler_deg",
           "enu_to_geodetic"]
