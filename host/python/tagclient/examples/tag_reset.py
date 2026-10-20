#!/usr/bin/env python3
"""Python counterpart of tag-reset: stop the tag if it is running, erase it,
and wait for IDLE. This discards the tag's log."""

from __future__ import annotations

from _common import TagState, fail, open_tag, parser, run, state_name, wait_for_state, wait_settled


def main() -> int:
    p = parser(__doc__)
    p.add_argument("--set-rtc", action="store_true",
                   help="synchronise the tag clock from the server's once IDLE")
    p.add_argument("--reset-timeout", type=float, default=180,
                   help="seconds to wait for the erase to finish (default: 180)")
    args = p.parse_args()

    with open_tag(args) as tag:
        status = wait_settled(tag, args.settle_timeout)
        print("State:", state_name(status.state))

        if status.state in (TagState.RUNNING, TagState.HIBERNATING):
            tag.stop()
            status = wait_for_state(tag, {TagState.FINISHED, TagState.ABORTED}, 30, poll=2)
            if status is None:
                fail("no status read succeeded after the stop")
            print("State:", state_name(status.state))

        if status.state in (TagState.FINISHED, TagState.ABORTED):
            tag.erase()
            # The erase sweeps storage in batches; how long depends on how much
            # data is present, so poll rather than guess.
            status = wait_for_state(tag, {TagState.IDLE}, args.reset_timeout, poll=0.25)
            if status is None or status.state != TagState.IDLE:
                last = state_name(status.state) if status else "unknown"
                fail(f"Reset did not reach IDLE within {args.reset_timeout:g} s; "
                     f"last state {last}")

        if args.set_rtc:
            tag.set_rtc()
            print("RTC synchronized")
        print("Final state:", state_name(status.state))
    return 0


if __name__ == "__main__":
    run(main)
