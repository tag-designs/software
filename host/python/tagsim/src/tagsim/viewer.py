"""A minimal ArduPilot DataFlash log of the truth and an estimate, for
UAV Log Viewer.

UAV Log Viewer (``ArduPilot/UAVLogViewer``) reads DataFlash ``.bin`` files
with its JsDataflashParser. Its ``dataflashDataExtractor.js`` takes:

- the **trajectory** from ``POS``, ``AHR2``, ``SIM`` or ``GPS``: ``Lat`` and
  ``Lng`` as ``int32`` degrees x 1e7 (format ``L``), ``Alt`` in metres, one
  point per 200 ms;
- the **attitude** from ``ATT`` or ``AHR2`` (``Roll``, ``Pitch``, ``Yaw``,
  degrees), or from the ``XKQ1``/``NKQ1`` quaternions (``Q1``..``Q4``);
- the start time from ``GPS`` (``Status``, ``GWk``, ``GMS``), and the vehicle
  model from a ``MSG`` containing "ArduPlane";
- ``time_boot_ms`` from each message's ``TimeUS``.

The file written here therefore carries:

- ``POS`` and ``ATT``: the truth position and attitude, the viewer's
  defaults;
- ``AHR2``: an estimate's attitude, flown along the truth position, so
  choosing ``AHR2`` as both trajectory and attitude source shows the estimate
  flying the same path (``AHR2`` is ArduPilot's own secondary estimate, which
  the viewer already offers beside ``ATT``);
- ``XKQ1``: optionally a second estimate's attitude, as the airframe
  quaternion (FRD to NED, ``Q1`` scalar first), which the viewer offers as
  a quaternion attitude source; it has no position, so it is flown along
  ``POS``. ``tagsim view --estimate both`` puts the filter in ``AHR2`` and
  the compass here;
- ``GPS``, ``MSG`` and ``FMTU``/``UNIT``/``MULT`` so the viewer labels time
  and units.

Attitude is that of the airframe (FRD to NED, ArduPilot's roll, pitch, yaw),
recovered from the tag attitude through the run's mounting. Position goes
from the local ENU frame back to latitude and longitude about the EKF origin
with the same flat-earth approximation the simulator used.

Times are the source log's own ``TimeUS``, so the file lines up with the
source flight in the viewer.
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

#: (type, name, format, columns, units, multipliers) for each message written.
_MESSAGES = [
    (129, "MSG", "QZ", "TimeUS,Message", "s-", "F-"),
    (130, "GPS", "QBIHBLLf", "TimeUS,Status,GMS,GWk,NSats,Lat,Lng,Alt", "s---SDDm", "F-C--GG0"),
    (131, "POS", "QLLf", "TimeUS,Lat,Lng,Alt", "sDDm", "FGG0"),
    (132, "ATT", "Qfff", "TimeUS,Roll,Pitch,Yaw", "sddh", "F000"),
    (133, "AHR2", "QfffLLf", "TimeUS,Roll,Pitch,Yaw,Lat,Lng,Alt", "sddhDDm", "F000GG0"),
    (137, "XKQ1", "Qffff", "TimeUS,Q1,Q2,Q3,Q4", "s----", "F0000"),
    (134, "UNIT", "QbZ", "TimeUS,Id,Label", "s--", "F--"),
    (135, "MULT", "Qbd", "TimeUS,Id,Mult", "s--", "F--"),
    (136, "FMTU", "QBNN", "TimeUS,FmtType,UnitIds,MultIds", "s---", "F---"),
]
_UNITS = {"-": "", "s": "s", "d": "deg", "h": "degheading", "D": "deglatitude",
          "m": "m", "S": "satellites"}
_MULTS = {"-": 0.0, "0": 1.0, "F": 1e-6, "C": 1e-3, "G": 1e-7}
_PACK = {"Q": "Q", "B": "B", "b": "b", "H": "H", "I": "I", "L": "i", "f": "f", "d": "d",
         "Z": "64s", "N": "16s"}


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
        for row in zip(*columns):
            self.chunks.append(head + struct.pack(fmt, *row))

    def save(self, path: Path) -> None:
        Path(path).write_bytes(b"".join(self.chunks))


def body_quaternion(q_enu_tag: np.ndarray, mounting: str) -> np.ndarray:
    """The airframe's attitude quaternion, FRD body to NED, scalar first."""
    q_ned_enu = dcmtoq(R_ENU_NED.T)
    q_tb = qconj(dcmtoq(mounting_matrix(mounting)))
    q_nb = qmul(qmul(q_ned_enu, q_enu_tag), q_tb)
    return q_nb * np.where(q_nb[:, :1] < 0, -1.0, 1.0)


def body_euler_deg(q_enu_tag: np.ndarray, mounting: str) -> np.ndarray:
    """ArduPilot roll, pitch, yaw (deg, yaw in [0, 360)) of the airframe."""
    q_nb = body_quaternion(q_enu_tag, mounting)
    xyzw = np.concatenate([q_nb[:, 1:], q_nb[:, :1]], axis=1)
    yaw, pitch, roll = Rotation.from_quat(xyzw).as_euler("ZYX", degrees=True).T
    return np.stack([roll, pitch, yaw % 360.0], -1)


def enu_to_geodetic(enu: np.ndarray, origin: tuple[float, float, float]) -> np.ndarray:
    """Latitude, longitude (deg) and altitude (m) of local ENU points."""
    lat0, lon0, alt0 = origin
    lat = lat0 + np.degrees(enu[:, 1] / _EARTH_RADIUS_M)
    lon = lon0 + np.degrees(enu[:, 0] / (_EARTH_RADIUS_M * np.cos(np.radians(lat0))))
    return np.stack([lat, lon, alt0 + enu[:, 2]], -1)


def write_viewer_log(path: Path, time_us: np.ndarray, q_true: np.ndarray, pos_enu: np.ndarray,
                     origin: tuple[float, float, float], start_unix: float, mounting: str,
                     est_time_us: np.ndarray | None = None, q_est: np.ndarray | None = None,
                     est_pos_enu: np.ndarray | None = None, label: str = "truth",
                     rate_hz: float = 25.0, second_time_us: np.ndarray | None = None,
                     q_second: np.ndarray | None = None) -> int:
    """Write the viewer log; returns the number of attitude records.

    ``time_us`` is the source log's TimeUS of each truth row and
    ``start_unix`` the UTC time of the first row. Truth is decimated to
    ``rate_hz``; the estimate, if given, is written as ``AHR2`` at its own
    rows, and a second estimate (``q_second`` at ``second_time_us``) as
    ``XKQ1``. Estimates are tag frame to ENU, like the truth.
    """
    t = np.asarray(time_us, dtype=np.float64)
    step = max(1, int(round(1e6 / rate_hz / np.median(np.diff(t)))))
    k = np.arange(0, len(t), step)
    tu = t[k].astype(np.uint64)
    geo = enu_to_geodetic(pos_enu[k], origin)
    lat = np.rint(geo[:, 0] * 1e7).astype(np.int64)
    lon = np.rint(geo[:, 1] * 1e7).astype(np.int64)
    eul = body_euler_deg(q_true[k], mounting)

    w = DataFlashWriter()
    w.write("MSG", int(tu[0]), f"ArduPlane V4.0.9 (tagsim {label})".encode())
    # GPS at 5 Hz: start time and a GPS trajectory.
    g = np.arange(0, len(k), max(1, int(round(rate_hz / 5))))
    gps_s = start_unix + (t[k][g] - t[0]) * 1e-6 - GPS_EPOCH_UNIX + GPS_LEAP_SECONDS
    week = np.floor(gps_s / 604800.0).astype(np.int64)
    gms = np.rint((gps_s - week * 604800.0) * 1000.0).astype(np.int64)
    w.write_many("GPS", [tu[g], np.full(len(g), 3), gms, week, np.full(len(g), 12),
                         lat[g], lon[g], geo[g, 2]])
    w.write_many("POS", [tu, lat, lon, geo[:, 2]])
    w.write_many("ATT", [tu, eul[:, 0], eul[:, 1], eul[:, 2]])
    if q_est is not None:
        te = np.asarray(est_time_us, dtype=np.float64)
        ge = enu_to_geodetic(est_pos_enu, origin)
        ee = body_euler_deg(q_est, mounting)
        ok = np.all(np.isfinite(ee), axis=1)
        w.write_many("AHR2", [te[ok].astype(np.uint64), ee[ok, 0], ee[ok, 1], ee[ok, 2],
                              np.rint(ge[ok, 0] * 1e7).astype(np.int64),
                              np.rint(ge[ok, 1] * 1e7).astype(np.int64), ge[ok, 2]])
    if q_second is not None:
        qb = body_quaternion(q_second, mounting)
        ok = np.all(np.isfinite(qb), axis=1)
        w.write_many("XKQ1", [np.asarray(second_time_us, dtype=np.float64)[ok].astype(np.uint64),
                              *qb[ok].T])
    w.save(path)
    return len(k)


__all__ = ["DataFlashWriter", "write_viewer_log", "body_euler_deg", "body_quaternion",
           "enu_to_geodetic"]
