"""Shared plumbing for the tagclient examples.

Each example is a Python counterpart of a command-line tool in
host/commandline, talking to the tag through tag-server instead of USB.
"""

from __future__ import annotations

import argparse
import sys
import time
from contextlib import contextmanager
from typing import Iterator

from tagclient import TagClient, TagServerError, pb

TagState = pb.tag.TagState


def parser(description: str) -> argparse.ArgumentParser:
    """An argument parser with the options every example shares."""
    p = argparse.ArgumentParser(description=description)
    p.add_argument("--server", help="tag-server address, e.g. unix:///tmp/tag-server.sock "
                   "(default: $TAG_SERVER, then tag-server's own default)")
    p.add_argument("-b", "--base", metavar="BUS:DEVICE",
                   help="base to attach to if the server is not attached "
                   "(default: the only base)")
    p.add_argument("--settle-timeout", type=float, default=10,
                   help="seconds to wait after attach for a definite state "
                   "(default: 10)")
    return p


@contextmanager
def open_tag(args: argparse.Namespace) -> Iterator[TagClient]:
    """Connect to the server and make sure a tag is attached.

    If this attaches, leaving the block detaches again; a tag the server was
    already holding (tag-server --attach) is left attached.
    """
    with TagClient(args.server) as tag:
        if not tag.is_attached():
            bus = address = 0
            if args.base:
                bus, address = (int(x) for x in args.base.split(":"))
            tag.attach(bus, address)
        yield tag


def state_name(state: int) -> str:
    return TagState.Name(state)


def wait_settled(tag: TagClient, timeout: float):
    """Status once the tag reports a definite state.

    Attaching connects under reset, so the first status can be
    STATE_UNSPECIFIED while the tag boots; the C++ tools wait the same way.
    """
    deadline = time.monotonic() + timeout
    while True:
        status = tag.get_status()
        if status.state != TagState.STATE_UNSPECIFIED:
            return status
        if time.monotonic() >= deadline:
            fail(f"tag still reports STATE_UNSPECIFIED after {timeout:g} s; "
                 "it is not merely settling")
        time.sleep(1)


def wait_for_state(tag: TagClient, wanted: set[int], timeout: float, poll: float = 1.0):
    """Poll until the state is in ``wanted``; returns the last Status read.

    Start, stop and erase are posted, not performed: the tag's state machine
    acts on them later, so the state changes a little after the call returns.
    """
    deadline = time.monotonic() + timeout
    while True:
        try:
            status = tag.get_status()
            if status.state in wanted:
                return status
        except TagServerError:
            status = None  # the tag did not answer in time; keep polling
        if time.monotonic() >= deadline:
            return status
        time.sleep(poll)


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def run(main) -> None:
    """Run ``main()``, reporting a TagServerError the way the C++ tools do."""
    try:
        sys.exit(main() or 0)
    except TagServerError as e:
        detail = f" (Ack {e.ack_err_name})" if e.ack_err_name else ""
        fail(f"{e.message}{detail}")
    except KeyboardInterrupt:
        sys.exit(1)
