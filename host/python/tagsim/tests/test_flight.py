"""A recorded pattern flight, end to end (needs a log and the importer)."""

from __future__ import annotations

import json
import os
import sqlite3
import subprocess

import numpy as np
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
    name, sha, cons, config = truth.execute(
        "SELECT SourceLog, SourceSha256, ConsistencyJson, ConfigJson FROM SimRun").fetchone()
    assert name == flight_log.name and len(sha) == 64
    report = json.loads(cons)
    print("strapdown consistency:", report)
    assert report["attitude_deg_max"] < 0.1
    assert report["velocity_mps_median"] < 0.01
    # Truth joins the log on ElapsedUs (no clock error simulated), and the
    # stored value is truth plus the drawn bias, within the noise.
    bias = json.loads(config)["error_draw"]["accel_bias_mg"][0]
    row = con.execute("SELECT ElapsedUs, ax FROM ImuAccel WHERE SampleIndex = 5000").fetchone()
    t = truth.execute("SELECT ElapsedUs, ax FROM TruthKinematics WHERE SampleIndex = 5000").fetchone()
    assert row[0] == t[0]
    if abs(t[1]) < 3900:
        assert abs(row[1] - t[1] - bias) <= 5 * 0.42 + 0.122 / 2
    # One magnetometer truth row per log row, none missed at 100 Hz.
    assert truth.execute("SELECT COUNT(*), SUM(Missed) FROM TruthMagSamples").fetchone() \
        == (n // 10, 0)


def test_calibrated_field_is_wmm(flight_db, dataprocessing_bin, tmp_path):
    """With the true constants in the Calibration table, dataprocessing's
    calibrated field magnitude is the WMM magnitude; uncorrected, it is not."""
    out = tmp_path / "proc.db3"
    done = subprocess.run([dataprocessing_bin, "-i", str(flight_db), "-o", str(out),
                           "-p", "imu-compass-orientation"], capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    field = np.array([r[0] for r in sqlite3.connect(out).execute(
        "SELECT field FROM ImuCompassOrientation")])
    truth = sqlite3.connect(truth_path(flight_db))
    wmm = json.loads(truth.execute("SELECT ConfigJson FROM SimRun").fetchone()[0])["wmm"]
    raw = np.array(sqlite3.connect(flight_db).execute("SELECT mx, my, mz FROM ImuMag").fetchall())
    raw_mag = np.linalg.norm(raw, axis=1)
    print(f"field: WMM {wmm['total_ut']:.3f} uT; raw {raw_mag.min():.2f}-{raw_mag.max():.2f}; "
          f"calibrated median {np.median(field):.3f}, 1-99 % "
          f"{np.percentile(field, 1):.3f}-{np.percentile(field, 99):.3f}")
    assert abs(np.median(field) - wmm["total_ut"]) < 0.05
    assert np.percentile(field, 99) - np.percentile(field, 1) < 2.0
    assert raw_mag.max() - raw_mag.min() > 5.0


def test_score_and_view(flight_db, dataprocessing_bin, tmp_path):
    """The Python port of the compass matches dataprocessing's quaternions,
    and the flight scores and exports."""
    from tagsim.compass import solve_orientation
    from tagsim.dataflash import DataFlashLog

    proc = tmp_path / "proc.db3"
    subprocess.run([dataprocessing_bin, "-i", str(flight_db), "-o", str(proc),
                    "-p", "imu-compass-orientation"], check=True, capture_output=True)
    con = sqlite3.connect(flight_db)
    accel = np.array(con.execute("SELECT ax, ay, az FROM ImuAccel ORDER BY SampleIndex").fetchall())
    mag = np.array(con.execute("SELECT SampleIndex, mx, my, mz FROM ImuMag ORDER BY SampleIndex")
                   .fetchall())
    cal = json.loads(con.execute("SELECT Constants FROM Calibration").fetchone()[0])["magnetometer"]
    a = np.array([[cal.get(f"a{i}{j}", float(i == j)) for j in range(3)] for i in range(3)])
    v = np.array([cal.get(f"v{i}", 0.0) for i in range(3)])
    est = np.array(sqlite3.connect(proc).execute(
        "SELECT qw, qx, qy, qz FROM ImuCompassOrientation ORDER BY SampleIndex").fetchall())
    i = mag[:, 0].astype(int)
    q = solve_orientation((mag[:, 1:] - v) @ a.T, accel[i[:, None] + np.arange(10)].mean(axis=1))
    diff = np.minimum(np.abs(q - est).max(axis=1), np.abs(q + est).max(axis=1))
    assert diff.max() < 1e-5

    score_db, view = tmp_path / "s.db3", tmp_path / "v.bin"
    assert main(["score", "--truth", str(truth_path(flight_db)), "--estimate", str(proc),
                 "-o", str(score_db)]) == 0
    s = sqlite3.connect(score_db)
    assert s.execute("SELECT Undecoded FROM ScoreRun").fetchone()[0] == 0
    n_flying = s.execute("SELECT SUM(Flying), COUNT(*) FROM ScoreSeries").fetchone()
    assert 0 < n_flying[0] < n_flying[1]
    assert main(["view", "--truth", str(truth_path(flight_db)), "--score", str(score_db),
                 "-o", str(view)]) == 0
    log = DataFlashLog(view)
    assert len(log.messages("AHR2")["TimeUS"]) == n_flying[1]
    assert len(log.messages("ATT")["TimeUS"]) > 1000


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


def test_filter_storage_modes(flight_db, tmp_path):
    """The smoother's storage and the outputs on a real flight: file-backed
    (memory-mapped) and in-memory give identical results, the files are
    deleted afterwards (the outputs' when the result is closed),
    and float32 gains (the default) change the smoothed estimate by less
    than 0.001 degree, 1 mm and 1 mm/s against float64 ones."""
    from tagsim.navfilter import FilterConfig, field_enu_from_ned, read_log, run_filter
    from tagsim.score import attitude_errors, load_truth

    log = read_log(flight_db)[0]
    field = field_enu_from_ned(load_truth(truth_path(flight_db)).field_ned_ut)
    mem = run_filter(log, FilterConfig(field_enu_ut=field))
    assert not mem.stats["storage"]["file_backed"]
    mapped = run_filter(log, FilterConfig(field_enu_ut=field, memmap_threshold_mb=0,
                                          memmap_dir=str(tmp_path), memmap_window_nodes=4096))
    assert mapped.stats["storage"]["file_backed"] and mapped.stats["outputs"]["file_backed"]
    # The smoother's storage is gone; the outputs' files stay with the result.
    assert [p.name for p in tmp_path.iterdir()] == [mapped.outputs._tmp.name.split("/")[-1]]
    for a, b in ((mem.q_smoothed, mapped.q_smoothed), (mem.alt_smoothed, mapped.alt_smoothed),
                 (mem.bias_smoothed, mapped.bias_smoothed), (mem.q_forward, mapped.q_forward),
                 (mem.accel_bias_smoothed, mapped.accel_bias_smoothed),
                 (mem.gyro_cal_smoothed, mapped.gyro_cal_smoothed),
                 (mem.vz_forward, mapped.vz_forward)):
        np.testing.assert_array_equal(a, b)
    mapped.close()
    assert list(tmp_path.iterdir()) == []
    f64 = run_filter(log, FilterConfig(field_enu_ut=field, gain_float32=False))
    angle = attitude_errors(mem.q_smoothed, f64.q_smoothed)["total_deg"]
    print(f"float32 gains: max {angle.max():.2e} deg, "
          f"{np.abs(mem.alt_smoothed - f64.alt_smoothed).max():.2e} m, "
          f"{np.abs(mem.vz_smoothed - f64.vz_smoothed).max():.2e} m/s")
    assert angle.max() < 1e-3
    assert np.abs(mem.alt_smoothed - f64.alt_smoothed).max() < 1e-3
    assert np.abs(mem.vz_smoothed - f64.vz_smoothed).max() < 1e-3
