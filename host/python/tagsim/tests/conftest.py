"""Shared fixtures.

The importer comes from ``TAG_LOG_IMPORT_BIN`` (or ``tag-log-import`` on
PATH), and ``dataprocessing`` from ``DATAPROCESSING_BIN`` (or PATH); recorded flights from ``TAGSIM_LOG_DIR`` (default
the sibling data checkout, ``<repo>/../data/ardupilot/pattern-2023``). Tests that need either
skip without it.
Recorded logs belong to a third party and carry GPS positions: they are
never copied into the repository.
"""

from __future__ import annotations

import os
import shutil
from pathlib import Path

import pytest

PREFERRED_FLIGHT = "00000173.BIN"


@pytest.fixture(scope="session")
def import_bin() -> str:
    found = os.environ.get("TAG_LOG_IMPORT_BIN") or shutil.which("tag-log-import")
    if not found or not Path(found).exists():
        pytest.skip("tag-log-import not found; set TAG_LOG_IMPORT_BIN")
    return found


@pytest.fixture(scope="session")
def flight_log() -> Path:
    default = Path(__file__).resolve().parents[4] / "../data/ardupilot/pattern-2023"
    log_dir = Path(os.environ.get("TAGSIM_LOG_DIR", default)).expanduser()
    preferred = log_dir / PREFERRED_FLIGHT
    if preferred.exists():
        return preferred
    found = sorted(log_dir.glob("*.BIN")) + sorted(log_dir.glob("*.bin"))
    if not found:
        pytest.skip(f"no ArduPilot logs in {log_dir}; set TAGSIM_LOG_DIR")
    return found[0]


@pytest.fixture(scope="session")
def dataprocessing_bin() -> str:
    """``DATAPROCESSING_BIN``, else dataprocessing on PATH."""
    found = os.environ.get("DATAPROCESSING_BIN") or shutil.which("dataprocessing")
    if not found or not Path(found).exists():
        pytest.skip("dataprocessing not found; set DATAPROCESSING_BIN")
    return found


def sensorviz_bin() -> str | None:
    """``SENSORVIZ_BIN``, else sensorviz on PATH."""
    found = os.environ.get("SENSORVIZ_BIN") or shutil.which("sensorviz")
    return found if found and Path(found).exists() else None
