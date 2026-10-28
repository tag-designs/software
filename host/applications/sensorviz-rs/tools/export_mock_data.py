#!/usr/bin/env python3
"""Export a sensorviz fixture log as mock data for the browser page.

    export_mock_data.py <log.db3> <name> <out.js>

Reads the log's `streams` table and, for every scalar stream, its time column
(epoch seconds, or ElapsedUs as seconds) and value column, and writes
`window.SVZ_MOCK_DATA[<name>]` with each stream's metadata and its times and
values as base64 little-endian Float64 and Float32 arrays -- the layout
sensordata keeps -- plus the log's info rows and, for an elapsed-time log, the
collection start. The mock engine (ui/mock-engine.js) computes envelopes and
hover values from these as the engine will from sensordata. Mock data only:
nothing here is shipped or used by the app.
"""

import base64
import json
import sqlite3
import struct
import sys


def b64(fmt, values):
    """Pack values with struct format char fmt ('d' or 'f'), little-endian, as base64."""
    return base64.b64encode(struct.pack("<%d%s" % (len(values), fmt), *values)).decode()


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    path, name, out = sys.argv[1:]
    db = sqlite3.connect(path)
    db.row_factory = sqlite3.Row
    streams = []
    elapsed = False
    for s in db.execute("select * from streams where stream_kind = 'scalar'"):
        tcol, table, vcol = s["time_column"], s["table_name"], s["value_column"]
        rows = db.execute(f'select "{tcol}", "{vcol}" from "{table}" order by rowid').fetchall()
        rows = [r for r in rows if r[0] is not None and r[1] is not None]
        if not rows:
            continue
        scale = 1e-6 if tcol == "ElapsedUs" else 1.0
        elapsed = elapsed or tcol == "ElapsedUs"
        streams.append({
            "id": s["stream_id"], "label": s["display_name"], "units": s["units"],
            "group": s["group_id"] or "", "groupName": s["group_name"] or "",
            "table": table, "n": len(rows),
            "t": b64("d", [r[0] * scale for r in rows]),
            "v": b64("f", [float(r[1]) for r in rows]),
        })
    info = [[r[0], r[1]] for r in db.execute("select fieldname, value from info")]
    start = None
    if elapsed:
        try:
            row = db.execute("select * from ImuHeader limit 1").fetchone()
            for key in ("EpochS", "Epoch", "StartEpoch"):
                if row is not None and key in row.keys():
                    start = row[key]
        except sqlite3.Error:
            pass
    data = {"name": path.rsplit("/", 1)[-1], "domain": "elapsed" if elapsed else "epoch",
            "start": start, "info": info, "streams": streams}
    with open(out, "w") as f:
        f.write("// Mock data exported by tools/export_mock_data.py; not shipped.\n")
        f.write("window.SVZ_MOCK_DATA = window.SVZ_MOCK_DATA || {};\n")
        f.write(f"window.SVZ_MOCK_DATA[{json.dumps(name)}] = ")
        json.dump(data, f, separators=(",", ":"))
        f.write(";\n")
    print(out, len(streams), "streams", sum(s["n"] for s in streams), "samples")
    return 0


if __name__ == "__main__":
    sys.exit(main())
