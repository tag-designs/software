#!/usr/bin/env python3
"""Python counterpart of tag-start: start an IDLE tag with its stored
configuration, or one read from a protobuf-JSON file, and wait for it to leave
IDLE."""

from __future__ import annotations

from pathlib import Path

from google.protobuf import json_format

from tagclient import configshape

from _common import TagState, fail, open_tag, parser, run, state_name, wait_for_state, wait_settled, pb


def main() -> int:
    p = parser(__doc__)
    p.add_argument("-c", "--config", type=Path,
                   help="program this protobuf-JSON configuration before starting")
    p.add_argument("--merge", action="store_true",
                   help="overlay --config onto the tag's stored configuration: "
                        "only fields the tag supports are copied, zeros included")
    p.add_argument("--set-rtc", action="store_true",
                   help="synchronise the tag clock from the server's before starting")
    p.add_argument("--start-now", action="store_true",
                   help="force start_delay to zero")
    p.add_argument("--start-timeout", type=float, default=10,
                   help="seconds to wait for the tag to leave IDLE (default: 10)")
    args = p.parse_args()

    with open_tag(args) as tag:
        status = wait_settled(tag, args.settle_timeout)
        if status.state in (TagState.RUNNING, TagState.CONFIGURED):
            print("Start skipped: tag is already", state_name(status.state))
            return 0
        if status.state != TagState.IDLE:
            fail(f"Start skipped: tag is {state_name(status.state)}; "
                 "a start is accepted only in IDLE")

        if args.set_rtc:
            tag.set_rtc()
            print("RTC synchronized")

        # Start from what the tag holds, never from an empty Config.
        config = tag.get_config()
        if args.config:
            text = args.config.read_text()
            parsed = json_format.Parse(text, pb.tagdata.Config())
            if args.merge:
                # As tag-start --merge and qtmonitor's Restore: copy only the
                # fields the tag supports and the file speaks for.
                configshape.merge(config, parsed, configshape.for_file(text))
            else:
                config = parsed
        if args.start_now:
            config.start_delay = 0
        print("Starting with configuration:")
        print(config)
        tag.start(config)

        status = wait_for_state(tag, {s for s in TagState.values() if s != TagState.IDLE},
                                args.start_timeout)
        if status is None:
            print(f"Start accepted, but no status read succeeded in "
                  f"{args.start_timeout:g} s")
            return 0
        print("State:", state_name(status.state))
        if status.state not in (TagState.RUNNING, TagState.CONFIGURED):
            fail(f"Start did not lead to RUNNING or CONFIGURED: {state_name(status.state)}")
    return 0


if __name__ == "__main__":
    run(main)
