"""Pages and the importer: what tagsim packs is what the decoder reads back."""

from __future__ import annotations

import sqlite3

import numpy as np
import pytest

from tagsim.pages import (CHECKPOINT_PAGES, HEADER_RESYNC, HEADER_RESYNC_STORAGE_SKIP,
                          MISSING_AUX_BITS, PAGE_BYTES, Segment, aux_bits, pack, unpack)
from tagsim.sensors import SensorConfig
from tagsim.simulate import Simulation, download_header, run_import, write_download

CFG = SensorConfig(odr_hz=200, accel_range_g=8, gyro_range_dps=2000)


def make_segments(rng):
    """Two segments: 21 pages (the last partial), then a RESYNC_STORAGE_SKIP
    segment of 3.5 pages starting at the next checkpoint group, page 24."""
    segs = []
    for k, (n_sf, flags) in enumerate([(20 * 15 + 4, 0),
                                       (52, HEADER_RESYNC | HEADER_RESYNC_STORAGE_SKIP)]):
        imu = rng.integers(-32768, 32768, size=(n_sf * 10, 6)).astype(np.int16)
        aux = rng.normal(0, 30, size=(n_sf, 4)).astype(np.float32)
        aux[:, 0] = 1000 + aux[:, 0]
        aux[rng.random(n_sf) < 0.1, 0] = np.nan          # missing pressure
        aux[rng.random(n_sf) < 0.1, 1:] = np.nan         # missing magnetometer
        aux[rng.random(n_sf) < 0.05, 2] = np.nan         # one axis missing drops the row
        segs.append(Segment(imu, aux, 1.7e9 + 100.0 * k + 0.37, CFG.odr_hz, 21.5 + k, flags))
    return segs


def test_pack_layout():
    segs = make_segments(np.random.default_rng(3))
    pages = pack(segs)
    assert all(len(p.data) == PAGE_BYTES for p in pages)
    # Flags only on the first page of a checkpoint group.
    for p in pages:
        assert p.flags == 0 or p.index % CHECKPOINT_PAGES == 0
    second = [p for p in pages if p.flags]
    assert len(second) == 1 and second[0].index == 24
    # Partial last pages: 4 and 7 superframes, the rest erased.
    assert pages[20].index == 20 and len(unpack(pages[20].data)[1]) == 40
    assert pages[21].index == 24
    assert len(unpack(pages[-1].data)[1]) == 70
    assert pages[-1].data.endswith(b"\xff" * 136 * 8)
    # Exact NaN pattern, and values survive.
    hdr, imu, aux = unpack(pages[0].data)
    assert np.array_equal(imu, segs[0].imu[:150])
    assert np.array_equal(aux, aux_bits(segs[0].aux[:15]))
    assert np.all(aux_bits(np.float32([np.nan])) == MISSING_AUX_BITS)
    assert hdr[2] == 2150


def test_round_trip(tmp_path, import_bin):
    rng = np.random.default_rng(7)
    segs = make_segments(rng)
    pages = pack(segs)
    imu = np.concatenate([s.imu for s in segs])
    aux = np.concatenate([s.aux for s in segs])
    sim = Simulation(CFG, None, imu, aux, segs[0].start_unix, pages)
    jsonl, db = tmp_path / "rt.jsonl", tmp_path / "rt.db3"
    write_download(jsonl, download_header(sim, "0" * 24), pages)
    run_import(jsonl, db)

    con = sqlite3.connect(db)
    q = lambda sql: con.execute(sql).fetchall()
    accel = np.array(q("SELECT ax, ay, az FROM ImuAccel ORDER BY rowid"))
    gyro = np.array(q("SELECT gx, gy, gz FROM ImuGyro ORDER BY rowid"))
    assert accel.shape == (len(imu), 3)
    np.testing.assert_array_equal(accel, imu[:, :3] * CFG.accel_lsb_mg)
    np.testing.assert_array_equal(gyro, imu[:, 3:] * CFG.gyro_lsb_dps)

    # Auxiliary slots: rows only where the value is present.
    seg_of_sf = np.concatenate([np.full(len(s.aux), k) for k, s in enumerate(segs)])
    idx_of_sf = np.concatenate([np.arange(len(s.aux)) * 10 for s in segs])
    has_p = ~np.isnan(aux[:, 0])
    has_m = ~np.isnan(aux[:, 1:]).any(axis=1)
    pres = q("SELECT SegmentId, SampleIndex, Pressure FROM ImuPressure ORDER BY rowid")
    assert [(int(a), int(b)) for a, b, _ in pres] == list(zip(seg_of_sf[has_p], idx_of_sf[has_p]))
    np.testing.assert_array_equal([r[2] for r in pres], aux[has_p, 0].astype(np.float64))
    mag = np.array(q("SELECT mx, my, mz FROM ImuMag ORDER BY rowid"))
    np.testing.assert_array_equal(mag, aux[has_m, 1:].astype(np.float64))

    # Segments and events.
    segrows = q("SELECT SegmentId, HeaderIndex, Event, FirstSampleIndex, ConfiguredOdrHz, Epoch "
                "FROM ImuSegment ORDER BY SegmentId")
    assert [r[:5] for r in segrows] == [(0, 0, "COLLECTION_START", 0, 200),
                                        (1, 21, "RESYNC_STORAGE_SKIP", 0, 200)]
    assert segrows[1][5] == int(segs[1].start_unix)
    assert q("SELECT Event FROM ImuEvent") == [("RESYNC_STORAGE_SKIP",)]
    assert q("SELECT COUNT(*) FROM ImuHeader") == [(len(pages),)]
    # The second segment starts at its own epoch, after the first ends.
    first = q("SELECT MIN(ElapsedUs) FROM ImuAccel WHERE SegmentId = 1")[0][0]
    assert first == pytest.approx(100.0e6, abs=2000)
    temps = q("SELECT DISTINCT Temperature FROM ImuTemperature ORDER BY 1")
    assert temps == [(21.5,), (22.5,)]
