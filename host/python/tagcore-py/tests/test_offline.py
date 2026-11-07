"""Offline checks of the tagcore binding: no tag or base is touched.

Each argument error must be raised before anything attaches, so these run on
any machine with the package built (maturin develop).
"""

import pytest

import tagcore
from tagcore import recovery, swd


def test_api_is_present():
    """The documented names exist."""
    for name in ("bases", "open", "Session", "ExternalFlash"):
        assert hasattr(swd, name)
    for name in ("capture", "rescue_erase_external"):
        assert hasattr(recovery, name)
    assert issubclass(tagcore.RecoveryError, Exception)
    assert tagcore.RecoveryError.__module__ == "tagcore"


def test_session_methods():
    """A Session and an ExternalFlash carry the documented methods."""
    for name in ("read", "read_word", "write", "regions", "read_region",
                 "halt", "run", "close", "external_flash", "uid", "mcu_name", "attach"):
        assert hasattr(swd.Session, name)
    for name in ("read", "read_page", "erase_sector", "program", "close",
                 "jedec", "sr1", "size", "sector_size", "writable", "page_bytes"):
        assert hasattr(swd.ExternalFlash, name)


def test_bases_is_a_list():
    """bases() lists without attaching."""
    assert isinstance(swd.bases(), list)


def test_bad_exit_is_refused():
    """An unknown exit is a ValueError, raised before the base is opened."""
    with pytest.raises(ValueError):
        swd.open(exit="sideways")


def test_unknown_base_is_refused():
    """A base that is not on USB raises RecoveryError."""
    with pytest.raises(tagcore.RecoveryError, match="no base"):
        swd.open(base="250:250")


def test_rescue_needs_a_capture_decision():
    """rescue_erase_external refuses unless told capture= or no_capture."""
    with pytest.raises(ValueError):
        recovery.rescue_erase_external("AT25XE_PresTagv3-RW")
    with pytest.raises(ValueError):
        recovery.rescue_erase_external("AT25XE_PresTagv3-RW", capture="x", no_capture=True)


def test_unknown_loader_is_refused():
    """A loader that is neither a file nor a known name raises RecoveryError."""
    with pytest.raises(tagcore.RecoveryError, match="no loader"):
        recovery.rescue_erase_external("no-such-loader", no_capture=True, loader_dirs=["/nonexistent"])
