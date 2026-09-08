#!/usr/bin/env python3
"""Check a downloaded PresTag SQLite log for structure *and* plausible values.

A structural check -- rows present, epochs monotonic, count consistent with the
run -- passes happily on a tag that recorded perfectly formatted nonsense. The
value checks here exist because several real failures produce well-formed rows:

  * `lps27GetPressureTemp()` presets its outputs to SHRT_MIN and the caller in
    state_run.c ignores its return value, so a failed sensor read is logged
    silently as -2048.00 hPa / -327.68 C rather than dropped.
  * A stuck bus or a cached register returns the same reading forever. Real
    barometric pressure moves by more than the 1/16 hPa LSB over any useful
    window, so a run with one distinct pressure value did not measure pressure.
  * A wrong conversion constant or a mis-scaled raw value lands far outside the
    range air can actually be at, while remaining a perfectly valid float.

Tables come from host/libraries/tagcore/sqlitelog: Pressure(Epoch, Pressure) in
hPa, Temperature(Epoch, Temperature) in C, Voltage(Epoch, Voltage) in volts, one
Voltage row per 60-sample block. Sample epochs advance by exactly the configured
period from each block header, so a larger step is a real discontinuity --
a hibernation window or a restart -- and is reported rather than failed.

Examples:
    prestag_check_download.py run.db --period 10
    prestag_check_download.py run.db --period 90 --expect-gaps 1
    prestag_check_download.py run.db --period 10 --pressure-min 950
"""

from __future__ import annotations

import argparse
import sqlite3
import sys

#: Value a failed pressure read reaches the log as, in hPa.
#:
#: lps27GetPressureTemp() presets *pressure to SHRT_MIN before touching the bus
#: and returns false on failure; state_run.c logs the sample regardless.
#: SHRT_MIN / 16.0 is exactly -2048.0.
PRESSURE_READ_FAILED_HPA = -2048.0

#: Value a failed temperature read reaches the log as, in degrees Celsius.
#: SHRT_MIN / 100.0.
TEMPERATURE_READ_FAILED_C = -327.68

#: Tolerance for recognising the sentinels above through float/REAL round-trips.
SENTINEL_TOL = 0.05

#: Samples per block header, matching PRESTAG_LOG_SAMPLES.
DATALOG_SAMPLES = 60

#: Plausible supply range for a bench-powered tag, in volts.
#:
#: Below 2.00 V the firmware's own LOGWRITE_BAT threshold would have tripped;
#: above 3.7 V is outside anything the baseboard supplies.
VOLTAGE_MIN_V = 2.0
VOLTAGE_MAX_V = 3.7


class Check:
    """One named pass/fail result with optional detail lines."""

    def __init__(self, name: str):
        """@param[in] name Short label shown in the summary table."""
        self.name = name
        self.ok = True
        self.detail: list[str] = []
        self.skipped = False

    def fail(self, message: str) -> None:
        """Mark the check failed and record why.

        @param[in] message One line describing the failure.
        """
        self.ok = False
        self.detail.append(message)

    def note(self, message: str) -> None:
        """Record an informational line without changing the verdict.

        @param[in] message One line of detail.
        """
        self.detail.append(message)

    def skip(self, message: str) -> None:
        """Mark the check not applicable.

        @param[in] message Why it could not run.
        """
        self.skipped = True
        self.detail.append(message)


def read_series(con: sqlite3.Connection, table: str,
                column: str) -> list[tuple[int, float]]:
    """Read one (Epoch, value) stream in insertion order.

    @details Rows are returned in rowid order rather than sorted by epoch, so a
             non-monotonic epoch is visible as written rather than hidden by the
             query.

    @param[in] con Open database connection.
    @param[in] table Table name.
    @param[in] column Value column name.
    @return List of (epoch, value) pairs, empty when the table is absent.
    """
    try:
        cur = con.execute(f'SELECT Epoch, "{column}" FROM "{table}" ORDER BY rowid')
    except sqlite3.Error:
        return []
    return [(int(e), float(v)) for e, v in cur.fetchall() if v is not None]


def check_presence(pressure: list, temperature: list,
                   voltage: list) -> Check:
    """Verify the three streams exist and are non-trivial."""
    c = Check("tables present and non-empty")
    for name, rows in (("Pressure", pressure), ("Temperature", temperature),
                       ("Voltage", voltage)):
        if not rows:
            c.fail(f"{name} is missing or empty")
        else:
            c.note(f"{name}: {len(rows)} rows")
    if pressure and temperature and len(pressure) != len(temperature):
        c.fail(f"Pressure has {len(pressure)} rows but Temperature has "
               f"{len(temperature)}; they are written together per sample")
    return c


def check_blocks(pressure: list, voltage: list) -> Check:
    """Verify one Voltage row per 60-sample block."""
    c = Check("one header per 60-sample block")
    if not pressure or not voltage:
        c.skip("needs both Pressure and Voltage rows")
        return c
    expected = -(-len(pressure) // DATALOG_SAMPLES)  # ceil
    c.note(f"{len(pressure)} samples -> expect {expected} headers, "
           f"found {len(voltage)}")
    if len(voltage) != expected:
        c.fail(f"header count {len(voltage)} does not match the {expected} "
               f"implied by {len(pressure)} samples; headers and pages may be "
               "desynchronised (see the plan's section 1.7)")
    return c


def check_epochs(pressure: list, period: int | None,
                 expect_gaps: int | None) -> Check:
    """Verify epochs advance monotonically at the configured period."""
    c = Check("epochs monotonic and evenly spaced")
    if len(pressure) < 2:
        c.skip("needs at least two samples")
        return c

    epochs = [e for e, _ in pressure]
    regressions = [i for i in range(1, len(epochs)) if epochs[i] <= epochs[i - 1]]
    if regressions:
        i = regressions[0]
        c.fail(f"{len(regressions)} non-increasing epoch step(s); first at "
               f"sample {i}: {epochs[i - 1]} -> {epochs[i]}")

    if period is None:
        c.note("no --period given; spacing not checked")
        return c

    gaps = []
    odd = 0
    for i in range(1, len(epochs)):
        step = epochs[i] - epochs[i - 1]
        if step == period:
            continue
        if step > period:
            gaps.append((i, epochs[i - 1], epochs[i], step))
        else:
            odd += 1
    if odd:
        c.fail(f"{odd} step(s) shorter than the {period} s period")

    for i, a, b, step in gaps:
        c.note(f"gap at sample {i}: {a} -> {b} ({step} s, "
               f"{step / 60.0:.1f} min) -- hibernation or restart")
    if expect_gaps is not None and len(gaps) != expect_gaps:
        c.fail(f"expected {expect_gaps} gap(s), found {len(gaps)}")
    elif not gaps:
        c.note("no discontinuities")
    return c


def check_values(rows: list, label: str, unit: str, lo: float, hi: float,
                 sentinel: float) -> Check:
    """Verify a stream is inside physical bounds and actually varying.

    @param[in] rows (epoch, value) pairs.
    @param[in] label Stream name for messages.
    @param[in] unit Engineering unit for messages.
    @param[in] lo Inclusive lower bound.
    @param[in] hi Inclusive upper bound.
    @param[in] sentinel Value a failed sensor read produces, reported separately
               from an ordinary out-of-range value because it means the read
               failed and the return code was ignored, not that the reading was
               wrong.
    @return The completed check.
    """
    c = Check(f"{label} plausible")
    if not rows:
        c.skip(f"no {label} rows")
        return c

    values = [v for _, v in rows]
    failed = [v for v in values if abs(v - sentinel) < SENTINEL_TOL]
    if failed:
        c.fail(f"{len(failed)} of {len(values)} sample(s) are the "
               f"failed-read sentinel {sentinel} {unit}: lps27GetPressureTemp() "
               "returned false and state_run.c logged the sample anyway")

    out = [v for v in values if not (lo <= v <= hi)
           and abs(v - sentinel) >= SENTINEL_TOL]
    if out:
        c.fail(f"{len(out)} sample(s) outside {lo}..{hi} {unit}, "
               f"e.g. {out[0]:.3f}")

    lo_v, hi_v = min(values), max(values)
    mean = sum(values) / len(values)
    distinct = len(set(values))
    c.note(f"min {lo_v:.3f}  max {hi_v:.3f}  mean {mean:.3f} {unit}  "
           f"({distinct} distinct value(s))")

    if distinct == 1 and len(values) > 10:
        c.fail(f"every one of {len(values)} samples reads exactly "
               f"{values[0]:.3f} {unit}; a real sensor varies by more than its "
               "LSB over a run, so this is a stuck bus or a cached register, "
               "not a clean signal")
    return c


def check_voltage(voltage: list) -> Check:
    """Verify header supply readings are in a plausible range."""
    c = Check("supply voltage plausible")
    if not voltage:
        c.skip("no Voltage rows")
        return c
    values = [v for _, v in voltage]
    out = [v for v in values if not (VOLTAGE_MIN_V <= v <= VOLTAGE_MAX_V)]
    if out:
        c.fail(f"{len(out)} header(s) outside "
               f"{VOLTAGE_MIN_V}..{VOLTAGE_MAX_V} V, e.g. {out[0]:.3f}")
    c.note(f"min {min(values):.3f}  max {max(values):.3f}  "
           f"mean {sum(values) / len(values):.3f} V")
    return c


def check_count(pressure: list, period: int | None,
                expect_duration: float | None) -> Check:
    """Compare the sample count against the intended run length."""
    c = Check("sample count matches run duration")
    if expect_duration is None or period is None:
        c.skip("needs --period and --expect-duration")
        return c
    if not pressure:
        c.skip("no samples")
        return c
    expected = expect_duration / period
    actual = len(pressure)
    c.note(f"{actual} samples, expected about {expected:.0f} "
           f"({expect_duration:.0f} s / {period} s)")
    # Deliberately loose: collection starts before and ends after the nominal
    # window, and the final block is truncated wherever the run stopped.
    if not 0.8 * expected <= actual <= 1.2 * expected + DATALOG_SAMPLES:
        c.fail(f"{actual} samples is not within 20% of the expected {expected:.0f}")
    return c


def main() -> int:
    """Parse arguments, run every check, and print a verdict."""
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("database", help="SQLite file written by tag-dwnld")
    p.add_argument("--period", type=int, default=None,
                   help="configured sample period in seconds; enables the "
                        "spacing, gap and count checks")
    p.add_argument("--expect-duration", type=float, default=None, metavar="S",
                   help="intended run length in seconds, for the count check")
    p.add_argument("--expect-gaps", type=int, default=None, metavar="N",
                   help="number of epoch discontinuities to require, e.g. 1 for "
                        "a run with a single hibernation window")
    p.add_argument("--pressure-min", type=float, default=900.0, metavar="HPA",
                   help="lower plausible pressure bound (default: 900)")
    p.add_argument("--pressure-max", type=float, default=1100.0, metavar="HPA",
                   help="upper plausible pressure bound (default: 1100; the "
                        "LPS27 itself is specified to 1260)")
    p.add_argument("--temp-min", type=float, default=15.0, metavar="C",
                   help="lower plausible temperature bound (default: 15)")
    p.add_argument("--temp-max", type=float, default=40.0, metavar="C",
                   help="upper plausible temperature bound (default: 40)")
    args = p.parse_args()

    try:
        con = sqlite3.connect(f"file:{args.database}?mode=ro", uri=True)
    except sqlite3.Error as exc:
        print(f"error: cannot open {args.database}: {exc}", file=sys.stderr)
        return 2

    with con:
        pressure = read_series(con, "Pressure", "Pressure")
        temperature = read_series(con, "Temperature", "Temperature")
        voltage = read_series(con, "Voltage", "Voltage")

    checks = [
        check_presence(pressure, temperature, voltage),
        check_blocks(pressure, voltage),
        check_epochs(pressure, args.period, args.expect_gaps),
        check_values(pressure, "pressure", "hPa", args.pressure_min,
                     args.pressure_max, PRESSURE_READ_FAILED_HPA),
        check_values(temperature, "temperature", "C", args.temp_min,
                     args.temp_max, TEMPERATURE_READ_FAILED_C),
        check_voltage(voltage),
        check_count(pressure, args.period, args.expect_duration),
    ]

    print(f"PresTag download check: {args.database}")
    print()
    failed = 0
    for c in checks:
        if c.skipped:
            status = "SKIP"
        elif c.ok:
            status = "PASS"
        else:
            status = "FAIL"
            failed += 1
        print(f"  [{status}] {c.name}")
        for line in c.detail:
            print(f"         {line}")
    print()
    if failed:
        print(f"VERDICT: FAIL ({failed} check(s) failed)")
        return 1
    print("VERDICT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
