"""Fixtures that run a fresh tag-server for each test.

Set TAG_SERVER_BIN to the tag-server executable; the tests are skipped
without it. Hardware tests also need TAG_SERVER_HARDWARE=1 and one tag base.
"""

from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import tempfile
from pathlib import Path

import pytest


def server_binary() -> str:
    path = os.environ.get("TAG_SERVER_BIN")
    if not path or not Path(path).is_file():
        pytest.skip("set TAG_SERVER_BIN to the tag-server executable")
    return path


class RunningServer:
    def __init__(self, args: list[str], socket_path: Path | None):
        self.socket_path = socket_path
        # On Windows the server is stopped with Ctrl-Break, which needs it in
        # its own process group so the event does not reach pytest too.
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        self.process = subprocess.Popen(
            [server_binary(), *args], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, creationflags=flags)
        line = self.process.stdout.readline()
        if "listening" not in line:
            self.process.kill()
            raise RuntimeError(f"tag-server did not start: {line!r} "
                               f"{self.process.stderr.read()!r}")
        self.banner = line.strip()

    def stop(self) -> int:
        """Stop the server the way an operator would; returns its exit code."""
        if os.name == "nt":
            self.process.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            self.process.send_signal(signal.SIGTERM)
        return self.process.wait(timeout=10)


def _short_tmpdir() -> Path:
    # A Unix socket path must fit in about 100 bytes, so avoid the long
    # per-test directories.
    return Path(tempfile.mkdtemp(prefix="ts-", dir="/tmp" if os.name != "nt" else None))


@pytest.fixture
def start_server():
    """Start tag-server with extra arguments; returns (target, server)."""
    started = []
    dirs = []

    def start(*args: str, tcp: bool = False):
        if tcp or os.name == "nt":
            with socket.socket() as s:
                s.bind(("127.0.0.1", 0))
                port = s.getsockname()[1]
            server = RunningServer(["--listen", f"127.0.0.1:{port}", *args], None)
            target = f"127.0.0.1:{port}"
        else:
            d = _short_tmpdir()
            dirs.append(d)
            path = d / "s.sock"
            server = RunningServer(["--listen", f"unix:{path}", *args], path)
            target = f"unix://{path}"
        started.append(server)
        return target, server

    yield start
    for server in started:
        if server.process.poll() is None:
            server.stop()
    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)


@pytest.fixture
def server(start_server):
    """tag-server, not attached, without --enable-debug."""
    return start_server()


@pytest.fixture
def debug_server(start_server):
    """tag-server, not attached, with --enable-debug."""
    return start_server("--enable-debug")
