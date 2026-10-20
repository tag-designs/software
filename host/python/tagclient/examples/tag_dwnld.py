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

# sizeof(t_PresTagRawBlock) in include/prestag_log_format.h: epoch, voltage and
# temperature (12 bytes) and 60 four-byte raw samples.
PRESTAG_RAW_BLOCK_BYTES = 12 + 60 * 4


def records_in(page) -> int:
    """How many records a page holds, which is how far it advances the index.

    In tag-dwnld and qtmonitor the log writer reports this as it decodes the
    page (host/libraries/tagcore/sqlitelog/): one per BitTag data entry, one
    per PresTag raw block, and one per page for every other log type.
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

        # Only a hint, for progress and for skipping missing blocks: the log
        # ends when a page comes back without log data.
        expected = status.external_data_count or status.internal_data_count

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
                if page.err == pb.tag.Ack.NODATA and index < expected:
                    index += 1          # a missing block inside the log; skip it
                    continue
                # Nothing came back: no payload (most tags) or one with no
                # records (BitTag returns an empty bittag_data_log).
                records = records_in(page) if page.WhichOneof("payload") else 0
                if records == 0:
                    break
                out.write(json.dumps(as_dict(page), separators=(",", ":")) + "\n")
                pages += 1
                index += records
                print(f"\r{index}/{expected}", end="", flush=True)
        print(f"\nDownloaded {index} records ({pages} pages) in {requests} requests "
              f"to {args.output}")
    return 0


if __name__ == "__main__":
    run(main)
