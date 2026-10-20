"""tag-server checks on a real tag: opt-in, and they change no tag state.

Run with TAG_SERVER_HARDWARE=1 and exactly one tag base connected. They read
information, status and memory, and hold the session idle on keepalives; they
never start, stop, erase or write.
"""

from __future__ import annotations

import os
import time

import grpc
import pytest

from tagclient import TagClient, TagServerError, pb

pytestmark = pytest.mark.skipif(
    os.environ.get("TAG_SERVER_HARDWARE") != "1",
    reason="set TAG_SERVER_HARDWARE=1 with one tag base connected")

L4_DBGMCU_IDCODE = 0xE0042000
U3_DBGMCU_IDCODE = 0xE0044000


def plausible_idcode(value: int) -> bool:
    return value not in (0, 0xFFFFFFFF) and (value & 0xFFF) != 0


@pytest.fixture
def tag(start_server):
    target, _ = start_server("--attach", "--enable-debug")
    with TagClient(target) as client:
        yield client


def test_information(tag):
    assert tag.is_attached()
    info = tag.get_info()
    assert info.board_desc
    assert len(tag.git_sha()) == 40
    assert 1.5 < tag.voltage() < 5.0
    status = tag.get_status()
    assert pb.tag.TagState.Name(status.state) != "STATE_UNSPECIFIED"


def test_state_log(tag):
    states = list(tag.state_log())
    assert all(s.HasField("status") for s in states)


def test_raw_call_matches_typed(tag):
    ack = tag.call(pb.tag.Req(get_status=pb.tag.Empty()))
    assert ack.err == pb.tag.Ack.OK
    assert ack.status.state == tag.get_status().state


def test_read_idcode(tag):
    for address in (L4_DBGMCU_IDCODE, U3_DBGMCU_IDCODE):
        try:
            if plausible_idcode(tag.read_u32(address)):
                return
        except TagServerError:
            continue
    pytest.fail("no plausible DBGMCU ID code")


def test_memory_range_checks(tag):
    for address, length in ((0x20000002, 4), (0x20000000, 3), (0xFFFFFFFC, 8)):
        with pytest.raises(TagServerError) as e:
            tag.read_memory(address, length)
        assert e.value.code == grpc.StatusCode.INVALID_ARGUMENT


def test_session_survives_idle(tag):
    # The server's keepalive must hold the session past the U3 timeout
    # (5-10 s); L4's is 60 s.
    time.sleep(15)
    tag.get_status()
    assert tag.get_monitor_stats().rpc_calls > 0
