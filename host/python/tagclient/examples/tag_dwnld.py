#!/usr/bin/env python3
"""Python counterpart of tag-dwnld: download a FINISHED or ABORTED tag's data
log. It writes the raw pages, not tag-dwnld's decoded SQLite or text: one JSON
object per line, a header (info, config, status, state log) then one line per
page, each the tag's Ack in protobuf JSON."""

from __future__ import annotations

import json
from pathlib import Path

from google.protobuf import json_format

from _common import TagState, fail, open_tag, parser, run, state_name, wait_for_state, wait_settled, pb

# Ack payloads that carry log data; anything else ends the log.
DATA_LOGS = {"bittag_data_log", "prestag_data_log", "prestag_raw_data_log",
             "bitprestag_data_log", "compasstag_data_log", "bittag_ng_data_log",
             "imu_data_log", "imu_raw_data_log", "uiuctag_data_log"}

# sizeof(t_PresTagRawBlock) in include/prestag_log_format.h: epoch, voltage and
# temperature (12 bytes) and 60 four-byte raw samples.
PRESTAG_RAW_BLOCK_BYTES = 12 + 60 * 4


def records_in(page) -> int:
    """How far one page advances the log index.

    This mirrors what tagcore's log writers return for each payload (see
    host/libraries/tagcore/sqlitelog/): a BitTag page holds one record per data
    entry, a PresTag raw page one per raw block, and every other log type one
    per page.
    """
    kind = page.WhichOneof("payload")
    if kind == "bittag_data_log":
        return len(page.bittag_data_log.data)
    if kind == "prestag_raw_data_log":
        return len(page.prestag_raw_data_log.samples) // PRESTAG_RAW_BLOCK_BYTES
    return 1


def as_dict(message) -> dict:
    return json_format.MessageToDict(message, preserving_proto_field_name=True)


def main() -> int:
    p = parser(__doc__)
    p.add_argument("-o", "--output", type=Path, default=Path("tag-download.jsonl"),
                   help="output file (default: tag-download.jsonl)")
    p.add_argument("-s", "--stop", action="store_true",
                   help="stop a running tag before downloading")
    args = p.parse_args()

    with open_tag(args) as tag:
        status = wait_settled(tag, args.settle_timeout)
        if args.stop and status.state in (TagState.RUNNING, TagState.HIBERNATING):
            tag.stop()
            status = wait_for_state(tag, {TagState.FINISHED, TagState.ABORTED}, 30, poll=2)
        if status is None or status.state not in (TagState.FINISHED, TagState.ABORTED):
            fail(f"Can't download from {state_name(status.state) if status else 'unknown'}; "
                 "stop the tag first (--stop)")

        total_count = status.external_data_count or status.internal_data_count
        if total_count <= 0:
            print("No log records to download")
            return 0

        header = {"tag_sha": tag.git_sha(), "info": as_dict(tag.get_info()),
                  "config": as_dict(tag.get_config()), "status": as_dict(status),
                  "states": [as_dict(s) for s in tag.state_log()]}
        index = requests = pages = 0
        with args.output.open("w") as out:
            out.write(json.dumps(header, separators=(",", ":")) + "\n")
            while True:
                page = tag.get_data_log(index)
                requests += 1
                if page.error_message:
                    fail(page.error_message)
                if page.err == pb.tag.Ack.NODATA and index < total_count:
                    index += 1          # an unreadable entry; skip it, as tag-dwnld does
                    continue
                if page.WhichOneof("payload") not in DATA_LOGS:
                    break               # end of log
                out.write(json.dumps(as_dict(page), separators=(",", ":")) + "\n")
                pages += 1
                index += records_in(page)
                print(f"\r{index}/{total_count}", end="", flush=True)
        print(f"\nDownloaded {index} records ({pages} pages) in {requests} requests "
              f"to {args.output}")
    return 0


if __name__ == "__main__":
    run(main)
