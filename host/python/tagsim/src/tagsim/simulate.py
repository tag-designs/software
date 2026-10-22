"""From a trajectory to an IMUTag download, a SQLite log and its truth file.

:func:`simulate` samples a :class:`~tagsim.trajectory.Trajectory` through the
sensor model and packs the pages. :func:`write_download` writes them in the
JSONL format of ``host/python/tagclient/examples/tag_dwnld.py``, with a
synthetic header, and :func:`run_import` turns that into a genuine IMUTag
SQLite log with ``tag-log-import``. :func:`write_truth` writes what the tag
could not know into ``<name>.truth.db3``.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from . import __version__
from .pages import Page, Segment, ack_dict, pack
from .sensors import IdealSignals, SensorConfig, ideal_signals, stored_aux, stored_imu
from .trajectory import Trajectory

BOARD = "IMUTagNandBmp581"
#: 256 MiB SPI NAND, as on the IMUTagNandBmp581 board.
EXTFLASH_BYTES = 256 * 1024 * 1024
_ODR_NAMES = {100: "S100", 200: "S200", 400: "S400", 800: "S800", 1600: "S1600"}
_ACCEL_NAMES = {2: "R2G", 4: "R4G", 8: "R8G", 16: "R16G"}
_GYRO_NAMES = {125: "R125dps", 250: "R250dps", 500: "R500dps", 1000: "R1000dps",
               2000: "R2000dps", 4000: "R4000dps"}


@dataclass
class Simulation:
    """Everything one run produced, before it is written."""

    cfg: SensorConfig
    signals: IdealSignals
    imu: np.ndarray  #: stored int16 samples (n, 6)
    aux: np.ndarray  #: stored float32 auxiliary slots (n / 10, 4)
    start_unix: float  #: UTC of sample 0
    pages: list[Page]
    provenance: dict = field(default_factory=dict)

    @property
    def end_unix(self) -> float:
        return self.start_unix + len(self.imu) / self.cfg.odr_hz


def simulate(traj: Trajectory, cfg: SensorConfig, start_unix: float,
             t_first: float | None = None, duration_s: float | None = None,
             origin_alt_m: float = 0.0) -> Simulation:
    """Sample ``traj`` from ``t_first`` (default its start) for whole superframes.

    ``start_unix`` is the UTC time of the first sample. The run stops at the
    last whole superframe inside the trajectory, or after ``duration_s``.
    """
    if t_first is None:
        t_first = traj.t_start
    span = traj.t_end - t_first
    if duration_s is not None:
        span = min(span, duration_s)
    n_sf = int(np.floor(span * cfg.odr_hz / 10 - 1e-9))
    if n_sf <= 0:
        raise ValueError("trajectory too short for one superframe")
    sig = ideal_signals(traj, t_first, n_sf, cfg, origin_alt_m)
    imu = stored_imu(sig, cfg)
    aux = stored_aux(sig)
    temps = sig.temperature_c[::15]
    pages = pack([Segment(imu, aux, start_unix, cfg.odr_hz, temps)])
    return Simulation(cfg, sig, imu, aux, start_unix, pages)


def synthetic_uuid(seed: int, source: str) -> str:
    """A 24-hex-digit UUID, like a real STM32 one, derived from the run."""
    return hashlib.sha256(f"tagsim:{seed}:{source}".encode()).hexdigest()[:24].upper()


def download_header(sim: Simulation, uuid: str, ppm_clock_error: float = 0.0) -> dict:
    """The JSONL header: info, config, status, state log and calibration."""
    from google.protobuf import json_format
    from tagclient import pb

    td, tg = pb.tagdata, pb.tag
    cfg = sim.cfg
    info = td.TagInfo(
        tag_type=td.IMUTAG, board_desc=BOARD, uuid=uuid, intflashsz=1024,
        extflashsz=EXTFLASH_BYTES, firmware=f"{BOARD} (tagsim {__version__})",
        gitrepo="tagsim", githash="tagsim", source_path="/host/python/tagsim",
        build_time="tagsim", ppm_clock_error=ppm_clock_error)
    config = td.Config(tag_type=td.IMUTAG)
    config.lsm6.odr = td.Lsm6dsv.ODR.Value(_ODR_NAMES[cfg.odr_hz])
    config.lsm6.accel_rng = td.Lsm6dsv.ACCEL.Value(_ACCEL_NAMES[cfg.accel_range_g])
    config.lsm6.gyro_rng = td.Lsm6dsv.GYRO.Value(_GYRO_NAMES[cfg.gyro_range_dps])

    def state(at_unix: float, s, event) -> dict:
        st = tg.State(transition_reason=event)
        st.status.millis = int(round(at_unix * 1000))
        st.status.state = s
        st.status.voltage = 3.7
        return st

    start = int(np.floor(sim.start_unix))
    states = [state(start - 60, tg.CONFIGURED, tg.State.EVENT_STARTCMD),
              state(start, tg.RUNNING, tg.State.EVENT_STARTTIM),
              state(sim.end_unix, tg.FINISHED, tg.State.EVENT_STOPCMD)]
    status = tg.Status(state=tg.FINISHED, external_data_count=sim.pages[-1].index + 1,
                       voltage=3.7, millis=int(round(sim.end_unix * 1000)))
    cal = td.CalibrationConstants(timestamp=start - 3600)
    m = cal.magnetometer
    m.a00 = m.a11 = m.a22 = 1.0
    cal.accelerometer.SetInParent()

    def d(msg):
        return json_format.MessageToDict(msg, preserving_proto_field_name=True)

    return {"tag_sha": "tagsim", "info": d(info), "config": d(config), "status": d(status),
            "states": [d(s) for s in states], "calibration": [d(cal)]}


def write_download(path: Path, header: dict, pages: list[Page]) -> None:
    """Write the JSONL download: header line, then one Ack per page."""
    with open(path, "w") as out:
        out.write(json.dumps(header, separators=(",", ":")) + "\n")
        for page in pages:
            out.write(json.dumps(ack_dict(page), separators=(",", ":")) + "\n")


def import_binary() -> str:
    """``tag-log-import``: ``$TAG_LOG_IMPORT_BIN``, else the one on PATH."""
    found = os.environ.get("TAG_LOG_IMPORT_BIN") or shutil.which("tag-log-import")
    if not found:
        raise FileNotFoundError("tag-log-import not found; set TAG_LOG_IMPORT_BIN")
    return found


def run_import(jsonl: Path, db: Path, info: dict[str, str] | None = None) -> str:
    """Run ``tag-log-import`` on ``jsonl``, writing ``db``; returns its output."""
    cmd = [import_binary(), str(jsonl), "-o", str(db)]
    for key, value in (info or {}).items():
        cmd += ["--info", f"{key}={value}"]
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"tag-log-import failed: {done.stderr.strip()}")
    return done.stdout.strip()


def write_truth(path: Path, sim: Simulation, seed: int, config: dict,
                source: dict | None = None, repairs: list | None = None,
                consistency: dict | None = None) -> None:
    """Write the truth sidecar.

    Tables are keyed like the log's: ``SegmentId``, ``SampleIndex`` and the
    true ``ElapsedUs`` since sample 0 (equal to the log's own ``ElapsedUs``
    while no clock error is simulated or reported).
    """
    if path.exists():
        path.unlink()
    sig, cfg = sim.signals, sim.cfg
    n = len(sim.imu)
    idx = np.arange(n)
    elapsed = (idx * (1_000_000 // cfg.odr_hz)).astype(np.int64)
    zeros = np.zeros(n, dtype=np.int64)
    con = sqlite3.connect(path)
    try:
        con.executescript("""
            CREATE TABLE TruthPose (SegmentId INTEGER, SampleIndex INTEGER, ElapsedUs INTEGER,
                e REAL, n REAL, u REAL, ve REAL, vn REAL, vu REAL,
                qw REAL, qx REAL, qy REAL, qz REAL);
            CREATE TABLE TruthKinematics (SegmentId INTEGER, SampleIndex INTEGER, ElapsedUs INTEGER,
                ax REAL, ay REAL, az REAL, gx REAL, gy REAL, gz REAL);
            CREATE TABLE TruthAtmosphere (SegmentId INTEGER, SampleIndex INTEGER, ElapsedUs INTEGER,
                Altitude REAL, SeaLevelPressure REAL, Pressure REAL, Temperature REAL);
            CREATE TABLE SimRun (ToolVersion TEXT, Seed INTEGER, ConfigJson TEXT,
                SourceLog TEXT, SourceSha256 TEXT, RepairsJson TEXT, ConsistencyJson TEXT,
                StartUnix REAL, CreatedUtc TEXT);
            CREATE TABLE units (TableName TEXT, ColumnName TEXT, Unit TEXT, Meaning TEXT);
        """)
        pose = np.column_stack([sig.pos_enu, sig.vel_enu, sig.q_enu_tag])
        con.executemany("INSERT INTO TruthPose VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (tuple(int(x) for x in r[:3]) + tuple(float(x) for x in r[3:])
                         for r in zip(zeros, idx, elapsed, *pose.T)))
        kin = np.column_stack([sig.accel_mg, sig.gyro_dps])
        con.executemany("INSERT INTO TruthKinematics VALUES (?,?,?,?,?,?,?,?,?)",
                        (tuple(int(x) for x in r[:3]) + tuple(float(x) for x in r[3:])
                         for r in zip(zeros, idx, elapsed, *kin.T)))
        ia = idx[::10]
        con.executemany("INSERT INTO TruthAtmosphere VALUES (?,?,?,?,?,?,?)",
                        ((0, int(i), int(e), float(a), cfg.sea_level_hpa, float(p), float(t))
                         for i, e, a, p, t in zip(ia, elapsed[::10], sig.altitude_m,
                                                  sig.pressure_hpa, sig.temperature_c)))
        source = source or {}
        con.execute("INSERT INTO SimRun VALUES (?,?,?,?,?,?,?,?,?)", (
            __version__, seed, json.dumps(config, sort_keys=True), source.get("name"),
            source.get("sha256"), json.dumps(repairs or []), json.dumps(consistency or {}),
            sim.start_unix, datetime.now(timezone.utc).isoformat(timespec="seconds")))
        con.executemany("INSERT INTO units VALUES (?,?,?,?)", [
            ("TruthPose", "e,n,u", "m", "position, local ENU about the EKF origin"),
            ("TruthPose", "ve,vn,vu", "m/s", "velocity, ENU"),
            ("TruthPose", "qw,qx,qy,qz", "", "attitude, tag frame to ENU"),
            ("TruthKinematics", "ax,ay,az", "mg", "ideal specific force, tag frame"),
            ("TruthKinematics", "gx,gy,gz", "dps", "ideal body rate, tag frame"),
            ("TruthAtmosphere", "Altitude", "m", "AMSL"),
            ("TruthAtmosphere", "SeaLevelPressure,Pressure", "hPa", "ISA"),
            ("TruthAtmosphere", "Temperature", "C", "ISA"),
        ])
        con.commit()
    finally:
        con.close()
