#!/usr/bin/env python3
"""Python counterpart of tag-info: print a tag's information, configuration,
status and state-transition log, or all of it as one JSON object (--json)."""

from __future__ import annotations

import json
import time

from google.protobuf import json_format

from _common import open_tag, parser, run, state_name, pb


def marker_time(millis: int) -> str:
    seconds = millis // 1000
    if seconds <= 0:
        return f"{seconds}  <-- clock not set"
    return time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(seconds))


def as_dict(message) -> dict:
    # The field names tagcore uses when it stores these messages.
    return json_format.MessageToDict(message, preserving_proto_field_name=True)


def main() -> int:
    p = parser(__doc__)
    p.add_argument("-j", "--json", action="store_true",
                   help="print one JSON object, for recording rather than reading")
    args = p.parse_args()

    with open_tag(args) as tag:
        info = tag.get_info()
        if args.json:
            record = {"tag_sha": tag.git_sha(), "info": as_dict(info),
                      "config": as_dict(tag.get_config()),
                      "status": as_dict(tag.get_status())}
            print(json.dumps(record, separators=(",", ":")))
            return 0

        print("Tag SHA:", tag.git_sha())
        print(info)
        print(tag.get_config())
        status = tag.get_status()
        print("Current state:", state_name(status.state))
        if status.debug_message:
            print("Firmware report:", status.debug_message)

        print("State transition log:")
        entries = 0
        for index, entry in enumerate(tag.state_log()):
            s = entry.status
            print(f"  [{index}] {state_name(s.state)}  "
                  f"reason={pb.tag.State.Event.Name(entry.transition_reason)}  "
                  f"time={marker_time(s.millis)}")
            if entry.transition_detail:
                print(f"        detail={entry.transition_detail:#x}")
            print(f"        internal_pages={s.internal_data_count} "
                  f"external_pages={s.external_data_count} "
                  f"vdd={s.voltage:g} temp={s.temperature:g}")
            entries += 1
        print(f"  entries: {entries}" if entries else
              "  (empty -- nothing recorded since the last reset)")
    return 0


if __name__ == "__main__":
    run(main)
