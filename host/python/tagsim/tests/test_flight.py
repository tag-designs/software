"""A recorded pattern flight, end to end (needs a log and the importer)."""

from __future__ import annotations

import json
import os
import sqlite3
import subprocess

import pytest

from tagsim.cli import main, truth_path

from conftest import sensorviz_bin


@pytest.fixture(scope="module")
def flight_db(tmp_path_factory, flight_log, import_bin):
    out = tmp_path_factory.mktemp("flight") / "flight.db3"
    os.environ["TAG_LOG_IMPORT_BIN"] = import_bin
    assert main(["run", "--log", str(flight_log), "--odr", "100", "--accel-range", "4",
                 "--gyro-range", "1000", "-o", str(out)]) == 0
    return out


def test_log_and_truth(flight_db, flight_log):
    con = sqlite3.connect(flight_db)
    n = con.execute("SELECT COUNT(*) FROM ImuAccel").fetchone()[0]
    assert n > 100 * 60
    assert con.execute("SELECT COUNT(*) FROM ImuMag").fetchone()[0] == n // 10
    assert con.execute("SELECT value FROM info WHERE fieldname = 'tagsim_source'").fetchone()[0] \
        == flight_log.name
    truth = sqlite3.connect(truth_path(flight_db))
    assert truth.execute("SELECT COUNT(*) FROM TruthPose").fetchone()[0] == n
    assert truth.execute("SELECT COUNT(*) FROM TruthKinematics").fetchone()[0] == n
    name, sha, cons = truth.execute(
        "SELECT SourceLog, SourceSha256, ConsistencyJson FROM SimRun").fetchone()
    assert name == flight_log.name and len(sha) == 64
    report = json.loads(cons)
    print("strapdown consistency:", report)
    assert report["attitude_deg_max"] < 0.1
    assert report["velocity_mps_median"] < 0.01
    # Truth joins the log on ElapsedUs (no clock error simulated).
    row = con.execute("SELECT ElapsedUs, ax FROM ImuAccel WHERE SampleIndex = 5000").fetchone()
    t = truth.execute("SELECT ElapsedUs, ax FROM TruthKinematics WHERE SampleIndex = 5000").fetchone()
    assert row[0] == t[0]
    if abs(t[1]) < 3900:
        assert abs(row[1] - t[1]) <= 0.122 / 2 + 1e-6


def test_sensorviz_loads(flight_db, tmp_path):
    sv = sensorviz_bin()
    if sv is None:
        pytest.skip("sensorviz not found; set SENSORVIZ_BIN")
    done = subprocess.run([sv, "--load-log", str(flight_db), "--capture-screenshot", "imutag",
                           "--screenshot-dir", str(tmp_path), "--no-user-prompts"],
                          capture_output=True, text=True, timeout=120)
    assert done.returncode == 0, done.stderr
    shots = list(tmp_path.glob("*.png"))
    assert shots and shots[0].stat().st_size > 10000
