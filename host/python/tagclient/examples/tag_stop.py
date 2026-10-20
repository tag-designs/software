#!/usr/bin/env python3
"""Python counterpart of tag-stop: stop a running tag and wait for FINISHED
or ABORTED."""

from __future__ import annotations

from _common import TagState, fail, open_tag, parser, run, state_name, wait_for_state, wait_settled


def main() -> int:
    p = parser(__doc__)
    p.add_argument("--stop-timeout", type=float, default=30,
                   help="seconds to wait for FINISHED or ABORTED (default: 30)")
    args = p.parse_args()

    with open_tag(args) as tag:
        wait_settled(tag, args.settle_timeout)
        tag.stop()
        status = wait_for_state(tag, {TagState.FINISHED, TagState.ABORTED},
                                args.stop_timeout, poll=2)
        if status is None or status.state not in (TagState.FINISHED, TagState.ABORTED):
            last = state_name(status.state) if status else "unknown"
            fail(f"Tag did not stop within {args.stop_timeout:g} s; last state {last}")
        print("State:", state_name(status.state))
    return 0


if __name__ == "__main__":
    run(main)
