#!/usr/bin/env python3
"""Plot qtcalibrate quality metrics against sample count for two retention runs.

Replaying one capture with and without --leverage-retention produces two logs
whose DEBUG lines carry the whole trajectory, not just the end state. Four
numbers across two policies do not read as a table; as curves against sample
count the divergence, and where it starts, are visible at a glance.

Usage:

    plot_retention_comparison.py INHERITED.log LEVERAGE.log [-o OUT.png]

Needs matplotlib. Colours are the first two categorical slots of the house
palette, which validate for colour-vision deficiency as a pair; line style
carries the same distinction so identity never rests on colour alone.
"""

import argparse
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Categorical slots 1 and 2. Validated as a pair: worst CVD Delta E 24.7
# (protan), normal-vision 33.6, both well clear of the 8 and 15 floors.
INHERITED_COLOR = "#2a78d6"
LEVERAGE_COLOR = "#eb6834"
INK = "#1a1a19"
MUTED = "#6b6a63"
GRID = "#e4e3dd"
SURFACE = "#fcfcfb"

QUALITY = re.compile(
    r"magquality: coverage (\d+)/(\d+) isotropy ([\d.]+)"
    r"(?: attitude ([\d.]+) dip ([-\d.]+) \+/- ([\d.]+) deg"
    r" \(p95 ([\d.]+), (\d+) of (\d+) in gate\))?")
RETENTION = re.compile(r"retention: (\d+) evictions")


def read_log(path):
    """Metric rows in order, each paired with the eviction count at that tick."""
    rows = []
    evictions = 0
    pending = None
    for line in open(path, errors="replace"):
        q = QUALITY.search(line)
        if q:
            pending = q
            continue
        r = RETENTION.search(line)
        if r and pending is not None:
            evictions = int(r.group(1))
            rows.append({
                "coverage": int(pending.group(1)),
                "patches": int(pending.group(2)),
                "isotropy": float(pending.group(3)),
                "attitude": float(pending.group(4)) if pending.group(4) else None,
                "dip_spread": float(pending.group(6)) if pending.group(6) else None,
                "held": int(pending.group(9)) if pending.group(9) else None,
                "evictions": evictions,
            })
            pending = None
    if not rows:
        raise SystemExit("no magquality/retention pairs found in %s" % path)
    return rows


def sample_axis(rows, buffer_size):
    """Samples seen at each tick.

    The gate line reports how many samples the buffer holds. That rises to
    MAGBUFFSIZE and then stops, and every later add evicts one, so holdings
    plus evictions is the total seen -- exact at both ends and across the
    join.

    Counting ticks instead does not work: the quality tick is not fired once
    per sample. On these captures it fires every second sample, which
    compresses the pre-fill half of the axis by about two and puts the
    buffer-full marker at roughly twice the sample number where the buffer
    really filled. The tick count is kept only as a fallback for a log with
    no accelerometer in it, which has no gate line to read.
    """
    axis = []
    for i, row in enumerate(rows):
        held = row["held"]
        if held is None:
            held = min(buffer_size, i + 1)
        axis.append(held + row["evictions"])
    return axis


def settled_window(runs, key, start):
    """Axis limits from the settled part of a metric, ignoring the transient.

    Everything before the buffer fills is startup: the calibration is still
    forming and the metric swings over a range that dwarfs the difference
    being compared. Scaling to what comes after keeps the comparison legible
    whatever the absolute level happens to be.
    """
    values = [r[key] for _, rows, _, _ in runs
              for x, r in zip(sample_axis(rows, start), rows)
              if r[key] is not None and x >= start]
    if not values:
        return None
    lo, hi = min(values), max(values)
    pad = max((hi - lo) * 0.12, 0.05)
    return (lo - pad, hi + pad)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inherited")
    parser.add_argument("leverage")
    parser.add_argument("-o", "--out", default="retention-comparison.png")
    parser.add_argument("--buffer", type=int, default=650,
                        help="MAGBUFFSIZE (default 650)")
    args = parser.parse_args()

    runs = [("Inherited nearest-pair", read_log(args.inherited),
             INHERITED_COLOR, "-"),
            ("Leverage and Cook's distance", read_log(args.leverage),
             LEVERAGE_COLOR, "--")]

    # Dip spread is clipped. Before a calibration exists the inclination is
    # meaningless and runs to thirty degrees and beyond; left unclipped that
    # transient occupies the whole axis and hides the steady state, which is
    # the only part being compared. The window is taken from the data rather
    # than fixed, because correcting the accelerometer moved the steady state
    # by more than a degree and a fixed window emptied the panel.
    panels = [
        ("isotropy", "Evenness of direction", None),
        ("dip_spread", "Dip spread (degrees, settled range)",
         settled_window(runs, "dip_spread", args.buffer)),
        ("coverage", "Patches occupied", (0, runs[0][1][0]["patches"] * 1.05)),
        ("attitude", "Attitude diversity", None),
    ]

    fig, axes = plt.subplots(2, 2, figsize=(11, 7), sharex=True,
                             facecolor=SURFACE)
    fig.suptitle("Calibration sample retention, one capture replayed both ways",
                 fontsize=13, color=INK, y=0.98)

    for ax, (key, label, ylim) in zip(axes.flat, panels):
        ax.set_facecolor(SURFACE)
        for name, rows, color, style in runs:
            xs = sample_axis(rows, args.buffer)
            ys = [r[key] for r in rows]
            pairs = [(x, y) for x, y in zip(xs, ys) if y is not None]
            if pairs:
                ax.plot([p[0] for p in pairs], [p[1] for p in pairs],
                        color=color, linestyle=style, linewidth=2.0,
                        label=name, solid_capstyle="round")
        # Where the buffer fills: before this the two runs are the same code
        # and must coincide, which makes any later divergence real.
        ax.axvline(args.buffer, color=MUTED, linewidth=1.0, linestyle=":")
        ax.set_title(label, fontsize=10, color=INK, loc="left")
        ax.grid(True, color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("left", "bottom"):
            ax.spines[side].set_color(GRID)
        ax.tick_params(colors=MUTED, labelsize=9)
        if ylim:
            ax.set_ylim(*ylim)

    for ax in axes[1]:
        ax.set_xlabel("samples collected", fontsize=9, color=MUTED)

    axes[0][0].annotate("buffer full", xy=(args.buffer, 0.02),
                        xycoords=("data", "axes fraction"),
                        xytext=(6, 0), textcoords="offset points",
                        fontsize=8, color=MUTED)

    handles, labels = axes[0][0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=2, frameon=False,
               fontsize=10, labelcolor=INK, bbox_to_anchor=(0.5, -0.01))

    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    fig.savefig(args.out, dpi=144, facecolor=SURFACE)
    print("wrote %s" % args.out)
    for name, rows, _, _ in runs:
        last = rows[-1]
        print("  %-30s coverage %d/%d  isotropy %.3f  dip +/- %.2f  "
              "evictions %d"
              % (name, last["coverage"], last["patches"], last["isotropy"],
                 last["dip_spread"] if last["dip_spread"] else float("nan"),
                 last["evictions"]))


if __name__ == "__main__":
    sys.exit(main())
