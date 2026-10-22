#!/usr/bin/env python3
"""Offline check of tag-txt-import on anonymised fixtures of each .txt variant.

Runs the importer on every file in ``txt-import/`` and asserts what its
conversion report must say: the variant, the records written, each repair,
and that the round trip back to text passed. Also checks that the written
log has the rows the report claims, and that a file the importer refuses is
refused with the right exit status.

Usage, from the repository root, after building ``tag-txt-import``::

    python3 host/commandline/test/txt_import_check.py <build-dir>/Release/bin/tag-txt-import

Exit status 0 when every check passes. No test framework; it prints each
fixture's result and stops at the first failure. The fixtures are trimmed
from the legacy collection, with UUIDs and repository URLs replaced; see
``README.md`` beside this script.
"""

from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent / "txt-import"

#: fixture -> (variant, records written, other report fields that must match)
EXPECT = {
    "c1-fivemin.txt": ("C", 4, {}),
    "c1-fivemin-crlf.txt": ("C", 4, {"crlf_lines": 94}),
    "c1-duplicate-record.txt": ("C", 4, {"exact_duplicates_removed": 1}),
    "c1-implausible-time.txt": ("C", 3, {"implausible_times_dropped": 1}),
    "c2-min.txt": ("C", 1, {}),
    "c3-packed-word.txt": ("C", 3, {"packed_words_checked": 3}),
    "b-fivemin-9x7.txt": ("B", 3, {"repair_fivemin_9x7": True}),
    "d-prestag.txt": ("D", 2, {"prestag_voltage_pairs_averaged": 2}),
    "e-no-header.txt": ("E", 3, {}),
    "header-only.txt": ("C", 0, {"header_only": True}),
}

#: tables and the row count per record for each tag type
ROWS = {"BITTAG": ("Activity", None), "PRESTAG": ("Pressure", None)}


def check(importer: str, fixture: Path, out: Path) -> None:
    variant, records, fields = EXPECT[fixture.name]
    db, report_path = out / (fixture.stem + ".db3"), out / (fixture.stem + ".json")
    done = subprocess.run([importer, str(fixture), "-o", str(db), "--report", str(report_path)],
                          capture_output=True, text=True)
    assert done.returncode == 0, f"{fixture.name}: exit {done.returncode}: {done.stderr[-400:]}"
    report = json.loads(report_path.read_text())
    assert report["roundtrip"] == "passed", f"{fixture.name}: {report['roundtrip']}"
    assert report["variant"] == variant, f"{fixture.name}: variant {report['variant']}"
    assert report["records_written"] == records, \
        f"{fixture.name}: {report['records_written']} records, expected {records}"
    for key, value in fields.items():
        assert report[key] == value, f"{fixture.name}: {key} = {report[key]}, expected {value}"

    con = sqlite3.connect(db)
    info = dict(con.execute("SELECT fieldname, value FROM info"))
    assert info["source"] == "txt-import" and info["txt_roundtrip"] == "passed"
    assert info["txt_variant"] == variant and len(info["txt_source_sha256"]) == 64
    volts = con.execute("SELECT COUNT(*) FROM Voltage").fetchone()[0]
    assert volts == records, f"{fixture.name}: {volts} voltage rows"
    if info["tagtype"] == "PRESTAG":
        # TC: is a second voltage: stored with V: as one mean, no core rows.
        tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master")}
        assert "CoreTemperature" not in tables, f"{fixture.name}: has CoreTemperature"
        n = con.execute("SELECT COUNT(*) FROM Pressure").fetchone()[0]
        assert n == con.execute("SELECT COUNT(*) FROM Temperature").fetchone()[0] and \
            (n > 0) == (records > 0)
    else:
        cores = con.execute("SELECT COUNT(*) FROM CoreTemperature").fetchone()[0]
        assert cores == records, f"{fixture.name}: {cores} core temperature rows"
        n = con.execute("SELECT COUNT(*) FROM Activity").fetchone()[0]
        per = {"BITTAG_BITSPERMIN": 10, "BITTAG_BITSPERFIVEMIN": 7}.get(info.get("bittag_log"), 0)
        assert n == records * per, f"{fixture.name}: {n} activity rows"
    con.close()
    print(f"ok  {fixture.name}: variant {variant}, {records} records, "
          f"{report['roundtrip_exact_lines']} lines exact, "
          f"{report['roundtrip_within_precision_lines']} within precision")


def check_failed(importer: str, out: Path) -> None:
    """Exports run together fail (exit 1) rather than being guessed at."""
    fixture = HERE / "e-concatenated.txt"
    db = out / "failed.db3"
    done = subprocess.run([importer, str(fixture), "-o", str(db)], capture_output=True, text=True)
    assert done.returncode == 1, f"{fixture.name}: exit {done.returncode}, expected 1"
    assert "exports run together" in done.stderr, done.stderr
    assert not db.exists(), f"{fixture.name}: wrote a log"
    print(f"ok  {fixture.name}: fails, as it should")


def check_variant_a(importer: str, out: Path) -> None:
    """Variant A is translated with btviz's decode, checked here independently.

    btviz (host/applications/btdataviz/mainwindow.cpp): for i = 5 .. 1, the
    activity at epoch - i*60 is ((word >> (i-1)*6) & 63) / 0.6 percent. The
    fixture's tag clock had reset when it was read (read time in 1979), which
    must not make its 2019 records look implausible.
    """
    fixture = HERE / "a-rtcv4.txt"
    db, report_path = out / "a.db3", out / "a.json"
    done = subprocess.run([importer, str(fixture), "-o", str(db), "--report", str(report_path)],
                          capture_output=True, text=True)
    assert done.returncode == 0, f"{fixture.name}: exit {done.returncode}: {done.stderr[-400:]}"
    report = json.loads(report_path.read_text())
    assert report["variant"] == "A" and report["roundtrip"].startswith("translated"), report
    assert report["records_written"] == 6 and report["implausible_times_dropped"] == 0, report
    expected = []
    for line in fixture.read_text().splitlines():
        if line.startswith("\t") and "temperature" not in line:
            _, epoch, word = line.strip().split(",")
            w = int(word, 16)
            expected += [(int(epoch) - i * 60, ((w >> (i - 1) * 6) & 63) / 0.6)
                         for i in range(5, 0, -1)]
    con = sqlite3.connect(db)
    rows = con.execute("SELECT Epoch, Activity FROM Activity ORDER BY rowid").fetchall()
    assert rows == expected, f"{fixture.name}: activity rows differ from btviz's decode"
    assert con.execute("SELECT Epoch, Voltage FROM Voltage").fetchall() == [(1554924000, 2.67)]
    assert con.execute("SELECT Temperature FROM CoreTemperature").fetchall() == [(36.9,)]
    con.close()
    print(f"ok  {fixture.name}: variant A, 6 words translated as btviz decodes them")


def check_refused(importer: str, out: Path) -> None:
    """A file that is not a log is refused with exit status 2."""
    cases = {
        "notes.txt": "Experiment notes, not a log\n",
        "empty.txt": "",
    }
    for name, text in cases.items():
        path = out / name
        path.write_text(text)
        done = subprocess.run([importer, str(path), "-o", str(out / "refused.db3")],
                              capture_output=True, text=True)
        assert done.returncode == 2, f"{name}: exit {done.returncode}, expected 2"
        assert not (out / "refused.db3").exists(), f"{name}: wrote a log"
        print(f"ok  {name}: refused")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    importer = sys.argv[1]
    missing = sorted((set(EXPECT) | {"a-rtcv4.txt", "e-concatenated.txt"})
                     - {p.name for p in HERE.glob("*.txt")})
    assert not missing, f"fixtures missing: {missing}"
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        for name in sorted(EXPECT):
            check(importer, HERE / name, out)
        check_failed(importer, out)
        check_variant_a(importer, out)
        check_refused(importer, out)
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
