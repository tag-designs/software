"""The DataFlash reader's scaling agrees with the log's own unit records."""

from __future__ import annotations

import numpy as np
import pytest

from tagsim.dataflash import _FORMATS, DataFlashLog

FIELDS = {"ORGN": ["Lat", "Lng", "Alt"], "XKF1": ["VN", "VE", "VD", "PN", "PE", "PD"],
          "XKF6": ["VN", "VE", "VD", "PN", "PE", "PD"], "GPS": ["Lat", "Lng", "Alt"],
          "XKQ1": ["Q1", "Q2", "Q3", "Q4"]}
EXPECTED_UNITS = {"Lat": "deglatitude", "Lng": "deglongitude", "Alt": "m", "VN": "m/s",
                  "VE": "m/s", "VD": "m/s", "PN": "m", "PE": "m", "PD": "m"}


def test_units_match_format_scaling(flight_log):
    log = DataFlashLog(flight_log)
    if not log.units("XKF1"):
        pytest.skip("log carries no FMTU/UNIT/MULT records")
    for name, cols in FIELDS.items():
        units = log.units(name)
        fmt = log.formats[name]
        for col in cols:
            unit, mult = units[col]
            scale = _FORMATS[fmt.format[fmt.columns.index(col)]][1] or 1.0
            if col in EXPECTED_UNITS:
                assert unit == EXPECTED_UNITS[col], (name, col, unit)
            assert mult is None or np.isclose(mult, scale, rtol=1e-6), (name, col, mult, scale)
    q = log.messages("XKQ1")
    norm = np.sqrt(sum(q[c].astype(float) ** 2 for c in ["Q1", "Q2", "Q3", "Q4"]))
    np.testing.assert_allclose(norm, 1.0, atol=1e-3)
