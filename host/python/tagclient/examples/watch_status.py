#!/usr/bin/env python3
"""Print the tag's state, voltage and log counts at a fixed interval until
Ctrl-C, as qtmonitor's status tab does. The server keeps the monitor session
alive between polls, however long the interval."""

from __future__ import annotations

import time

from _common import open_tag, parser, run, state_name


def main() -> int:
    p = parser(__doc__)
    p.add_argument("-i", "--interval", type=float, default=1.0,
                   help="seconds between polls (default: 1)")
    args = p.parse_args()

    with open_tag(args) as tag:
        try:
            while True:
                s = tag.get_status()
                print(f"{time.strftime('%H:%M:%S')}  {state_name(s.state):<12} "
                      f"{tag.voltage():.2f} V  internal={s.internal_data_count} "
                      f"external={s.external_data_count}", flush=True)
                time.sleep(args.interval)
        except KeyboardInterrupt:
            pass  # Ctrl-C is how this ends
    return 0


if __name__ == "__main__":
    run(main)
