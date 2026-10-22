"""Glitch repair on a synthetic DataFlash log: a spike is bridged, a lasting
step is split at. No recorded log is needed; the file is written here."""

from __future__ import annotations

import struct

import numpy as np

from tagsim.ardupilot import load_flight
from tagsim.rotations import qexp, qmul

_FORMATS = {  # name: (type, format, columns)
    "XKQ1": (200, "Qffff", "TimeUS,Q1,Q2,Q3,Q4"),
    "XKQ2": (201, "Qffff", "TimeUS,Q1,Q2,Q3,Q4"),
    "XKF1": (202, "Qffffff", "TimeUS,VN,VE,VD,PN,PE,PD"),
    "XKF6": (203, "Qffffff", "TimeUS,VN,VE,VD,PN,PE,PD"),
    "STAT": (204, "QB", "TimeUS,isFlying"),
    "ORGN": (205, "QLLe", "TimeUS,Lat,Lng,Alt"),
    "GPS": (206, "QBIH", "TimeUS,Status,GMS,GWk"),
}
_STRUCT = {"Q": "Q", "f": "f", "B": "B", "L": "i", "e": "i", "I": "I", "H": "H"}


def _record(name, *values) -> bytes:
    t, fmt, _ = _FORMATS[name]
    return b"\xa3\x95" + bytes([t]) + struct.pack("<" + "".join(_STRUCT[c] for c in fmt), *values)


def write_log(path, t, q, v, p) -> None:
    """A minimal log: both EKF cores identical, flying throughout."""
    out = bytearray()
    for name, (t_id, fmt, cols) in _FORMATS.items():
        length = 3 + struct.calcsize("<" + "".join(_STRUCT[c] for c in fmt))
        out += b"\xa3\x95\x80" + struct.pack("<BB4s16s64s", t_id, length, name.encode(),
                                             fmt.encode(), cols.encode())
    out += _record("ORGN", 0, 0, 0, 10000)
    for k, tk in enumerate(t):
        us = int(round(tk * 1e6))
        for xkq, xkf in (("XKQ1", "XKF1"), ("XKQ2", "XKF6")):
            out += _record(xkq, us, *q[k])
            out += _record(xkf, us, *v[k], *p[k])
        if k % 5 == 0:
            out += _record("STAT", us, 1)
            out += _record("GPS", us, 3, 100_000_000 + int(tk * 1000), 2270)
    path.write_bytes(bytes(out))


def test_spike_bridged_and_lasting_step_split(tmp_path):
    dt = 0.04
    t = np.arange(0.0, 60.0, dt)
    yaw_rate = np.radians(20.0)
    q = [np.array([1.0, 0, 0, 0])]
    for _ in t[1:]:
        q.append(qmul(q[-1], qexp(np.array([0.0, 0.0, yaw_rate * dt]))))
    q = np.array(q)
    step_at, spike_at = 450, 1000          # 18.0 s and 40.0 s
    jump = qexp(np.array([0.0, 0.0, np.radians(48.0)]))
    q[step_at:] = qmul(jump, q[step_at:])  # stays turned: a lasting step
    q[spike_at] = qmul(jump, q[spike_at])  # one sample only: a spike
    v = np.tile([20.0, 0.0, 0.0], (len(t), 1))
    p = np.cumsum(v * dt, axis=0)
    log = tmp_path / "synthetic.bin"
    write_log(log, t, q, v, p)

    flight = load_flight(log, pad_s=0.0)
    kinds = {(r.kind, round(r.t_start, 2)) for r in flight.repairs}
    assert ("split: lasting step", 18.0) in kinds
    assert ("bridge", 40.0) in kinds
    # The longest piece, after the step, is the one kept.
    assert flight.t[0] > 18.0
    traj, _ = flight.trajectory()
    tt = np.arange(traj.t_start, traj.t_end, 0.005)
    _, w = traj.attitude(tt)
    peak = np.degrees(np.linalg.norm(w, axis=1)).max()
    assert peak < 25.0, peak
