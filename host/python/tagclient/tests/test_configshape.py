"""tagclient.configshape: shapes, file shapes and the merge.

Mirrors tagcore's config_shape_check (host/libraries/tagcore/test), so the
Python and C++ merges follow one set of rules. Needs no tag-server.
"""

from __future__ import annotations

import pytest
from google.protobuf import json_format

from tagclient import configshape, pb

Config = pb.tagdata.Config
T = pb.tagdata

#: ConfigFieldVisibility member -> path, and the retired generator's output:
#: the members that were true, per tag type. The same frozen table is in
#: config_shape_check.cc.
MEMBER_PATHS = {
    "active_interval": "active_interval", "hibernate": "hibernate",
    "period": "period", "start_delay": "start_delay", "bittag_log": "bittag_log",
    "adxl362": "adxl362", "adxl362_range": "adxl362.range",
    "adxl362_freq": "adxl362.freq", "adxl362_filter": "adxl362.filter",
    "adxl362_act_thresh_g": "adxl362.act_thresh_g",
    "adxl362_inact_thresh_g": "adxl362.inact_thresh_g",
    "adxl362_inactive_sec": "adxl362.inactive_sec",
    "adxl362_accel_type": "adxl362.accel_type",
    "adxl362_data_format": "adxl362.data_format",
    "adxl362_channels": "adxl362.channels",
    "adxl362_active_sec": "adxl362.active_sec",
    "lsm6": "lsm6", "lsm6_odr": "lsm6.odr", "lsm6_accel_rng": "lsm6.accel_rng",
    "lsm6_gyro_rng": "lsm6.gyro_rng",
}
GENERATOR_TABLE = {
    T.BITPRESTAG: {"active_interval", "hibernate", "adxl362", "adxl362_act_thresh_g",
                   "adxl362_inact_thresh_g", "adxl362_inactive_sec"},
    T.BITTAG: {"active_interval", "hibernate", "bittag_log", "adxl362", "adxl362_range",
               "adxl362_freq", "adxl362_filter", "adxl362_act_thresh_g",
               "adxl362_inact_thresh_g", "adxl362_inactive_sec"},
    T.BITTAGNG: {"active_interval", "hibernate", "adxl362", "adxl362_act_thresh_g",
                 "adxl362_inactive_sec", "adxl362_accel_type"},
    T.BITTAG_LE: {"active_interval", "hibernate", "bittag_log", "adxl362",
                  "adxl362_act_thresh_g", "adxl362_inact_thresh_g", "adxl362_inactive_sec"},
    T.COMPASSTAG: {"active_interval", "hibernate"},
    T.IMUTAG: {"start_delay", "lsm6", "lsm6_odr", "lsm6_accel_rng", "lsm6_gyro_rng"},
    T.PRESTAG: {"active_interval", "hibernate", "period"},
    T.UIUCTAG: {"active_interval", "hibernate", "adxl362", "adxl362_act_thresh_g",
                "adxl362_inactive_sec"},
}


def parse(text: str) -> Config:
    return json_format.Parse(text, Config())


def restore(tag: Config, text: str) -> Config:
    """Merge a file into a copy of ``tag``, as Restore does."""
    out = Config()
    out.CopyFrom(tag)
    configshape.merge(out, parse(text), configshape.for_file(text))
    return out


@pytest.mark.parametrize("tag_type", sorted(GENERATOR_TABLE))
def test_shapes_match_generator(tag_type):
    shape = configshape.for_tag(tag_type)
    for member, path in MEMBER_PATHS.items():
        assert (path in shape) == (member in GENERATOR_TABLE[tag_type]), \
            f"{T.TagType.Name(tag_type)}: {path}"


def test_lookup():
    assert configshape.for_tag(T.TAG_UNSPECIFIED) == frozenset()
    assert "lsm6.accel_rng" in configshape.for_tag(T.IMUTAG)  # JSON says accelRng
    assert "active_interval" not in configshape.for_tag(T.IMUTAG)
    assert "hibernate.end_epoch" in configshape.for_tag(T.PRESTAG)


def test_from_json():
    shape = configshape.from_json('{"period": 0, "adxl362": {"range": "R2G"}}')
    assert {"period", "adxl362", "adxl362.range"} <= shape     # zero kept
    assert "adxl362.freq" not in shape
    assert "lsm6.accel_rng" in configshape.from_json('{"lsm6": {"accelRng": "R4G"}}')
    for bad in ['{"bogus": 1}', '{"lsm6": {"range": 1}}', "{not json"]:
        with pytest.raises(ValueError):
            configshape.from_json(bad)


def test_merge():
    ng = parse('{"tag_type": "BITTAGNG", "adxl362": {"accel_type": "AdxlType_367",'
               ' "act_thresh_g": 0.5, "inactive_sec": 6}}')
    out = restore(ng, '{"adxl362": {"act_thresh_g": 0.3}}')
    assert out.adxl362.act_thresh_g == pytest.approx(0.3)
    assert out.adxl362.inactive_sec == 6
    assert out.adxl362.accel_type == T.Adxl362.AdxlType_367

    pres = parse('{"tag_type": "PRESTAG", "period": 60}')
    assert restore(pres, '{"period": 0}').period == 0            # stated zero

    le = parse('{"tag_type": "BITTAG_LE", "adxl362": {"act_thresh_g": 0.5}}')
    assert restore(le, '{"adxl362": {"freq": "S400"}}').adxl362.freq == 0

    imu = parse('{"tag_type": "IMUTAG", "start_delay": 1,'
                ' "lsm6": {"odr": "S100", "accel_rng": "R4G", "gyro_rng": "R1000dps"}}')
    out = restore(imu, '{"tag_type": "PRESTAG", "period": 60, "start_delay": 0,'
                       ' "bittag_log": "BITTAG_UNSPECIFIED",'
                       ' "active_interval": {"start_epoch": 0, "end_epoch": 2147483647},'
                       ' "hibernate": [{"start_epoch": 0, "end_epoch": 0}]}')
    assert out.tag_type == T.IMUTAG
    assert out.start_delay == 1                  # PresTag has no start_delay: padding
    assert not out.HasField("active_interval") and out.period == 0
    assert len(out.hibernate) == 0
    assert restore(imu, '{"start_delay": 0}').start_delay == 0   # no tag_type

    bittag = parse('{"tag_type": "BITTAG", "bittag_log": "BITTAG_BITSPERMIN"}')
    assert restore(bittag, '{"tag_type": "BITTAGNG", "bittag_log": "BITTAG_UNSPECIFIED"}'
                   ).bittag_log == T.BITTAG_BITSPERMIN
    assert restore(bittag, '{"tag_type": "BITTAG_LE", "bittag_log": "BITTAG_BITPERSEC"}'
                   ).bittag_log == T.BITTAG_BITPERSEC

    bt = parse('{"tag_type": "BITTAG", "bittag_log": "BITTAG_BITSPERMIN",'
               ' "active_interval": {"start_epoch": 100, "end_epoch": 200},'
               ' "hibernate": [{"start_epoch": 1, "end_epoch": 2},'
               '               {"start_epoch": 3, "end_epoch": 4}]}')
    out = restore(bt, '{"active_interval": {"start_epoch": 150}}')
    assert (out.active_interval.start_epoch, out.active_interval.end_epoch) == (150, 200)

    out = restore(bt, '{"hibernate": [{"start_epoch": 10, "end_epoch": 20},'
                      ' {"start_epoch": 30, "end_epoch": 40},'
                      ' {"start_epoch": 50, "end_epoch": 60}]}')
    assert len(out.hibernate) == 2
    assert out.hibernate[0].start_epoch == 10 and out.hibernate[1].end_epoch == 40
    out = restore(bt, '{"hibernate": [{"start_epoch": 10, "end_epoch": 20}]}')
    assert len(out.hibernate) == 2
    assert out.hibernate[0].end_epoch == 20 and out.hibernate[1].start_epoch == 3

    assert restore(bt, "{}").SerializeToString() == bt.SerializeToString()
