"""tag-server and TagClient checks that need no tag.

None of these attaches. ListDevices and Attach enumerate USB devices, which
does not disturb a connected base.
"""

from __future__ import annotations

import os
import subprocess

import grpc
import pytest

from tagclient import TagClient, TagServerError, pb


def test_not_attached(server):
    target, _ = server
    with TagClient(target) as tag:
        assert tag.is_attached() is False


@pytest.mark.parametrize("call", ["get_info", "get_config", "get_status",
                                  "voltage", "git_sha", "stop"])
def test_calls_need_attach(server, call):
    target, _ = server
    with TagClient(target) as tag:
        with pytest.raises(TagServerError) as e:
            getattr(tag, call)()
    assert e.value.code == grpc.StatusCode.FAILED_PRECONDITION
    assert e.value.message == "not attached"
    assert e.value.ack_err is None
    assert e.value.transport_failed is False


def test_raw_call_needs_attach(server):
    target, _ = server
    with TagClient(target) as tag:
        with pytest.raises(TagServerError) as e:
            tag.call(pb.tag.Req(get_status=pb.tag.Empty()))
    assert e.value.code == grpc.StatusCode.FAILED_PRECONDITION


def test_debug_calls_refused_without_flag(server):
    target, _ = server
    with TagClient(target) as tag:
        for call in (lambda: tag.read_memory(0x20000000, 4),
                     lambda: tag.write_memory(0x20000000, b"\0\0\0\0"),
                     tag.count_bittag_log_headers):
            with pytest.raises(TagServerError) as e:
                call()
            assert e.value.code == grpc.StatusCode.PERMISSION_DENIED
            assert "--enable-debug" in e.value.message


def test_debug_calls_need_attach(debug_server):
    target, _ = debug_server
    with TagClient(target) as tag:
        with pytest.raises(TagServerError) as e:
            tag.read_memory(0x20000000, 4)
    assert e.value.code == grpc.StatusCode.FAILED_PRECONDITION
    assert "not attached" in e.value.message


def test_attach_to_missing_base(server):
    target, _ = server
    with TagClient(target) as tag:
        with pytest.raises(TagServerError) as e:
            tag.attach(bus=250, address=250)
        assert tag.is_attached() is False
    assert e.value.code == grpc.StatusCode.NOT_FOUND


def test_list_devices(server):
    target, _ = server
    with TagClient(target) as tag:
        devices = tag.list_devices()
    for device in devices:
        assert device.vid and device.pid


def test_stats_without_tag(server):
    target, _ = server
    with TagClient(target) as tag:
        tag.reset_link_stats()
        tag.reset_monitor_stats()
        assert tag.get_link_stats().usb_in_calls == 0
        assert tag.get_monitor_stats().rpc_calls == 0


def test_tcp_listen(start_server):
    target, _ = start_server(tcp=True)
    with TagClient(target) as tag:
        assert tag.is_attached() is False


def test_clean_shutdown(start_server):
    _, server = start_server()
    if server.socket_path:            # Unix socket (not on Windows)
        assert server.socket_path.exists()
    assert server.stop() == 0
    if server.socket_path:
        assert not server.socket_path.exists()


@pytest.mark.skipif(os.name == "nt", reason="Unix sockets only")
def test_second_server_on_same_socket_refused(start_server):
    _, first = start_server()
    result = subprocess.run(
        [first.process.args[0], "--listen", f"unix:{first.socket_path}"],
        capture_output=True, text=True, timeout=10)
    assert result.returncode != 0
    assert "another server" in result.stderr
    assert first.process.poll() is None
