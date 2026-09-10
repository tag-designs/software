#!/usr/bin/env python3
"""Fit the PresTag Shutdown-regime average-current law and estimate lifetime.

At sample periods of 10 s and above a PresTag sleeps in Shutdown between
samples, so the resting current does not depend on the period and each sample
costs a fixed packet of charge -- wake, reboot, LPS27 read, external-flash
write -- however far apart the samples are. Average current is therefore linear
in sample rate:

    I_avg(T) = I_rest + Q_cycle / T          [uA, Q in uC, T in s]

with Q_cycle = Q_sample + Q_header/60, the /60 because the internal-flash
header is written once per 60-sample block and is amortised over the block.

This fits that line by least squares from measured (period, current) points,
reports the knee period at which sampling costs as much as resting, and turns
the fit into battery lifetime for one or more cell capacities.

The fit is only valid in the Shutdown regime. Below 10 s the firmware sleeps in
Stop 2 instead, which is a different intercept and a different slope, so points
below 10 s are rejected unless --allow-short is given.

Examples:
    prestag_power_model.py --point 10:3.1 --point 30:1.2 --point 90:0.62
    prestag_power_model.py --point 10:3.1 --point 90:0.62 --capacity 5.5 \
        --capacity 11 --derate 0.75 --target-days 365

Expected magnitudes: a healthy PresTag averages under 1 uA at a 60 s period, so
the whole field range is roughly 0.5-4 uA and Q_cycle is tens of microcoulombs.
A fit returning hundreds of uC, or an average in the tens of uA, is a broken
measurement -- most often a monitor still attached -- not a slow tag.
"""

from __future__ import annotations

import argparse
import sys

#: Sample period below which the firmware sleeps in Stop 2 rather than Shutdown.
#:
#: state_run.c returns STOP2 for `lps_period < 10` and the target's
#: PRESTAG_RUNNING_LONG_SLEEP_MODE (Shutdown on PresTag) otherwise. The two
#: regimes have different intercepts and slopes, so mixing them in one fit
#: produces a line that describes neither.
SHUTDOWN_REGIME_MIN_S = 10.0

#: Hours per day, for turning uA and uAh into days.
HOURS_PER_DAY = 24.0


class ModelError(Exception):
    """Raised when the inputs cannot produce a meaningful fit."""


def parse_point(text: str) -> tuple[float, float]:
    """Parse one ``period:current`` measurement.

    @param[in] text Measurement as ``<period_s>:<current_uA>``, e.g. ``30:13.0``.
    @return Tuple of period in seconds and average current in microamps.
    @raise ModelError When the text is malformed or either value is not
           strictly positive. A zero or negative period has no reciprocal and a
           non-positive current cannot be a measured average.
    """
    if ":" not in text:
        raise ModelError(f"malformed point {text!r}; expected <period_s>:<current_uA>")
    period_text, current_text = text.split(":", 1)
    try:
        period = float(period_text)
        current = float(current_text)
    except ValueError as exc:
        raise ModelError(f"malformed point {text!r}: {exc}") from exc
    if period <= 0.0:
        raise ModelError(f"point {text!r} has a non-positive period")
    if current <= 0.0:
        raise ModelError(f"point {text!r} has a non-positive current")
    return period, current


def fit(points: list[tuple[float, float]]) -> tuple[float, float, float, float]:
    """Least-squares fit of ``I = I_rest + Q_cycle * (1/T)``.

    @details Regresses current on reciprocal period. The intercept is the
             Shutdown resting current and the slope is the charge consumed by
             one sampling cycle, including its amortised share of the
             once-per-60-samples internal-flash header.

    @param[in] points Measured (period_s, current_uA) pairs, at least two with
               distinct periods.
    @return Tuple of resting current in uA, cycle charge in uC, the maximum
            absolute residual in uA, and the coefficient of determination.
    @raise ModelError When fewer than two distinct periods are supplied, which
           leaves the line underdetermined.
    """
    if len(points) < 2:
        raise ModelError("need at least two measurements to fit a line")
    xs = [1.0 / p for p, _ in points]
    ys = [i for _, i in points]
    if len(set(xs)) < 2:
        raise ModelError("need at least two distinct periods; all points share one")

    n = float(len(points))
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    slope = sxy / sxx
    intercept = mean_y - slope * mean_x

    residuals = [y - (intercept + slope * x) for x, y in zip(xs, ys)]
    max_resid = max(abs(r) for r in residuals)
    ss_res = sum(r * r for r in residuals)
    ss_tot = sum((y - mean_y) ** 2 for y in ys)
    # A perfect horizontal line through identical currents has no variance to
    # explain; report 1.0 rather than dividing by zero.
    r_squared = 1.0 if ss_tot == 0.0 else 1.0 - ss_res / ss_tot
    return intercept, slope, max_resid, r_squared


def lifetime_days(capacity_mah: float, current_ua: float) -> float:
    """Convert a capacity and an average current into a lifetime.

    @param[in] capacity_mah Usable cell capacity in mAh.
    @param[in] current_ua Average current in microamps; must be positive.
    @return Lifetime in days, ignoring self-discharge and cutoff voltage.
    @raise ModelError When the current is not positive.
    """
    if current_ua <= 0.0:
        raise ModelError("average current must be positive to estimate lifetime")
    return (capacity_mah * 1000.0) / (current_ua * HOURS_PER_DAY)


def main() -> int:
    """Parse arguments, fit the model, and print the fit and lifetime tables."""
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--point", action="append", default=[], metavar="T:UA",
                   help="a measured sample period in seconds and average "
                        "current in uA, e.g. 30:13.0; repeat for each point")
    p.add_argument("--capacity", action="append", type=float, default=[],
                   metavar="MAH",
                   help="cell capacity in mAh for the lifetime table "
                        "(default: 5.5 and 11)")
    p.add_argument("--derate", type=float, default=1.0, metavar="FRACTION",
                   help="fraction of nominal capacity actually usable, to "
                        "account for self-discharge and cutoff voltage "
                        "(default: 1.0, i.e. an upper bound)")
    p.add_argument("--predict", action="append", type=float, default=[],
                   metavar="T",
                   help="extra sample period in seconds to predict and include "
                        "in the lifetime table; repeat as needed")
    p.add_argument("--target-days", type=float, default=None, metavar="DAYS",
                   help="lifetime target; report the average current each "
                        "capacity may draw to reach it, and flag periods that "
                        "miss it (e.g. 365)")
    p.add_argument("--allow-short", action="store_true",
                   help="accept points below 10 s. They are in the Stop 2 "
                        "regime, not Shutdown, and mixing the two fits a line "
                        "that describes neither")
    args = p.parse_args()

    if not args.point:
        p.error("at least two --point measurements are required")

    try:
        points = [parse_point(t) for t in args.point]
    except ModelError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    short = [t for t, _ in points if t < SHUTDOWN_REGIME_MIN_S]
    if short and not args.allow_short:
        print(f"error: {len(short)} point(s) below {SHUTDOWN_REGIME_MIN_S:.0f} s "
              f"({', '.join(f'{t:g}' for t in short)}) are in the Stop 2 regime, "
              "not Shutdown; drop them or pass --allow-short",
              file=sys.stderr)
        return 2

    if not 0.0 < args.derate <= 1.0:
        print("error: --derate must be in (0, 1]", file=sys.stderr)
        return 2

    try:
        i_rest, q_cycle, max_resid, r_squared = fit(points)
    except ModelError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    capacities = args.capacity or [5.5, 11.0]
    target = args.target_days
    if target is not None and target <= 0.0:
        print("error: --target-days must be positive", file=sys.stderr)
        return 2

    print("PresTag Shutdown-regime average-current model")
    print("  I_avg(T) = I_rest + Q_cycle / T")
    print()
    print("Measurements")
    for period, current in sorted(points):
        modelled = i_rest + q_cycle / period
        print(f"  T = {period:8.2f} s   measured {current:10.3f} uA   "
              f"model {modelled:10.3f} uA   residual {current - modelled:+8.3f} uA")
    print()
    print("Fit")
    print(f"  I_rest        {i_rest:12.4f} uA      resting Shutdown current")
    print(f"  Q_cycle       {q_cycle:12.4f} uC      charge per sampling cycle")
    print(f"  max residual  {max_resid:12.4f} uA")
    print(f"  R^2           {r_squared:12.6f}")

    if i_rest <= 0.0:
        print()
        print("  WARNING: the fitted resting current is not positive. The line "
              "does not describe a physical tag -- suspect a bad point, a "
              "still-attached monitor, or a measurement window that was not a "
              "whole number of 60-sample blocks.")
        knee = None
    else:
        knee = q_cycle / i_rest
        print(f"  T_knee        {knee:12.2f} s       sampling costs as much as resting")
        print("                             below it, longer periods save a lot; "
              "above it, little")

    # The Shutdown floor on this board is the STM32L432 plus the RV3028, the
    # LPS27 powered down and the AT25 in deep power-down, which is well under
    # 1 uA. An intercept above that is not a plausible resting current, so it
    # means a bad point or a state that never actually slept.
    if i_rest >= 1.0:
        print()
        print(f"  WARNING: fitted resting current {i_rest:.3f} uA is at or above "
              "1 uA. The PresTag Shutdown floor should be well below that -- "
              "suspect a point measured with the monitor attached, a pin driven "
              "against a pull-up, or a window that was not a whole number of "
              "60-sample blocks. Cross-check against the directly measured "
              "IDLE current.")

    if max_resid > 0.05 * max(y for _, y in points):
        print()
        print("  WARNING: residuals exceed 5% of the largest measurement. The "
              "points are not on one line -- check for a measurement window "
              "that straddled a header write, or a point from the wrong regime.")

    periods = sorted({t for t, _ in points} | set(args.predict))
    for capacity in capacities:
        usable = capacity * args.derate
        print()
        label = (f"Lifetime, {capacity:g} mAh"
                 + (f" derated to {usable:g} mAh ({args.derate:.0%})"
                    if args.derate != 1.0 else " (nominal, upper bound)"))
        print(label)
        budget_ua = None
        if target is not None:
            # Invert lifetime_days: the average current that exactly reaches the
            # target on this capacity.
            budget_ua = (usable * 1000.0) / (target * HOURS_PER_DAY)
            print(f"  budget for {target:.0f} days: {budget_ua:.4f} uA average")
        for period in periods:
            current = i_rest + q_cycle / period
            if current <= 0.0:
                print(f"  T = {period:8.2f} s   model {current:10.3f} uA   "
                      "lifetime not meaningful")
                continue
            days = lifetime_days(usable, current)
            verdict = ""
            if budget_ua is not None:
                verdict = "  MEETS" if current <= budget_ua else "  MISSES"
            print(f"  T = {period:8.2f} s   {current:10.3f} uA   "
                  f"{days:9.1f} days  ({days / 30.44:6.1f} months){verdict}")
        if budget_ua is not None and i_rest > budget_ua:
            print(f"  NOTE: the resting current alone ({i_rest:.4f} uA) exceeds "
                  f"the {target:.0f}-day budget. No sample period reaches the "
                  "target; the floor has to come down first.")
        if knee is not None:
            ceiling = lifetime_days(usable, i_rest)
            print(f"  ceiling as T -> inf: {ceiling:.1f} days at {i_rest:.3f} uA")

    if args.derate == 1.0:
        print()
        print("Note: these are upper bounds. They ignore self-discharge, the "
              "cutoff voltage above which the tag stops working, temperature, "
              "and cell sag under the per-sample flash-write peak. Re-run with "
              "--derate for a deployment figure.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
