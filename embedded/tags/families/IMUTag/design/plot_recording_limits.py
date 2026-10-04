#!/usr/bin/env python3
"""Draw IMUTag recording time against sample rate: battery limit vs flash limit.

Reads the measured 3.7 V sweep (sweep-3v7-20261003.csv, beside this script)
and writes host/docs/src/images/imutag-recording-limits.svg, which both the
user manual's IMUTag overview and power.md embed.

- Battery limit: a 12 mAh cell divided by the measured run current at each rate.
- Flash limit: the 2 Gbit GD5F2GM7RE on IMUTagNandBmp581, 131,072 pages of
  2048 bytes, 150 IMU samples per page (see power.md, Storage-Limited Runtime).

The recording ends at whichever limit is lower. Standard library only; rerun
after a new sweep:

    python3 embedded/tags/families/IMUTag/design/plot_recording_limits.py
"""

import csv
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
CSV = HERE / "sweep-3v7-20261003.csv"
OUT = ROOT / "host/docs/src/images/imutag-recording-limits.svg"

CELL_MAH = 12.0
FLASH_PAGES = 131072
SAMPLES_PER_PAGE = 150
DESIGN_HZ = 400

# Geometry (SVG user units).
W, H = 680, 424
L, R, T, B = 64, 150, 70, 336  # plot box: left, right edge, top, bottom
Y_MAX, Y_STEP = 60, 10


def load():
    rows = []
    with CSV.open(newline="") as f:
        for r in csv.DictReader(f):
            if r["recorded_odr"]:
                hz = int(r["recorded_odr"])
                ua = float(r["current_ua"])
                battery_h = CELL_MAH * 1000.0 / ua
                flash_h = FLASH_PAGES * SAMPLES_PER_PAGE / hz / 3600.0
                rows.append((hz, ua, battery_h, flash_h))
    return sorted(rows)


def x_of(hz, lo, hi):
    return L + (math.log2(hz) - math.log2(lo)) / (math.log2(hi) - math.log2(lo)) * (R - L)


def y_of(h):
    return B - h / Y_MAX * (B - T)


def main():
    rows = load()
    lo, hi = rows[0][0], rows[-1][0]
    R_ = W - R  # right edge of the plot box
    globals()["R"] = R_

    def pts(i):
        return [(x_of(r[0], lo, hi), y_of(r[i])) for r in rows]

    bat, fla = pts(2), pts(3)
    design = next(r for r in rows if r[0] == DESIGN_HZ)
    xd = x_of(DESIGN_HZ, lo, hi)

    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
             'role="img" aria-labelledby="t d">')
    o.append('<title id="t">IMUTag recording time vs sample rate: battery limit and flash limit</title>')
    o.append(f'<desc id="d">At {DESIGN_HZ} Hz the 2 Gbit flash fills after {design[3]:.1f} h, before a 12 mAh '
             f'cell runs out at {design[2]:.1f} h. Below about 300 Hz the battery runs out first.</desc>')
    o.append("""<style>
  svg { --surface:#fcfcfb; --ink:#0b0b0b; --ink2:#52514e; --muted:#898781; --grid:#e1e0d9; --axis:#c3c2b7;
        --s1:#2a78d6; --s2:#eb6834; font-family:system-ui,-apple-system,"Segoe UI",sans-serif; }
  @media (prefers-color-scheme: dark) {
    svg { --surface:#1a1a19; --ink:#ffffff; --ink2:#c3c2b7; --muted:#898781; --grid:#2c2c2a; --axis:#383835;
          --s1:#3987e5; --s2:#d95926; }
  }
  .bg { fill:var(--surface); }
  .title { fill:var(--ink); font-size:15px; font-weight:600; }
  .sub { fill:var(--ink2); font-size:12px; }
  .tick { fill:var(--muted); font-size:11.5px; }
  .lab { fill:var(--ink); font-size:12.5px; font-weight:600; }
  .val { fill:var(--ink2); font-size:12px; }
  .grid { stroke:var(--grid); stroke-width:1; }
  .axis { stroke:var(--axis); stroke-width:1; }
  .s1 { stroke:var(--s1); fill:none; stroke-width:2; stroke-linejoin:round; stroke-linecap:round; }
  .s2 { stroke:var(--s2); fill:none; stroke-width:2; stroke-linejoin:round; stroke-linecap:round; }
  .m1 { fill:var(--s1); stroke:var(--surface); stroke-width:2; }
  .m2 { fill:var(--s2); stroke:var(--surface); stroke-width:2; }
  .design { stroke:var(--ink2); stroke-width:1.25; stroke-dasharray:4 4; }
</style>""")
    o.append(f'<rect class="bg" width="{W}" height="{H}"/>')
    o.append(f'<text class="title" x="{L - 40}" y="26">At 400 Hz the flash fills before the battery runs out</text>')
    o.append(f'<text class="sub" x="{L - 40}" y="46">Recording time, IMUTagNandBmp581, 12 mAh cell, 2 Gbit flash; '
             'currents measured at 3.69 V on 2026-10-03</text>')

    # Grid and y axis.
    for h in range(0, Y_MAX + 1, Y_STEP):
        y = y_of(h)
        o.append(f'<line class="{"axis" if h == 0 else "grid"}" x1="{L}" y1="{y:.1f}" x2="{R_}" y2="{y:.1f}"/>')
        o.append(f'<text class="tick" x="{L - 8}" y="{y + 4:.1f}" text-anchor="end">{h}</text>')
    o.append(f'<text class="tick" x="{L - 40}" y="{(T + B) / 2:.1f}" transform="rotate(-90 {L - 40} {(T + B) / 2:.1f})" '
             'text-anchor="middle">hours of recording</text>')

    # X ticks.
    for r in rows:
        x = x_of(r[0], lo, hi)
        o.append(f'<text class="tick" x="{x:.1f}" y="{B + 18}" text-anchor="middle">{r[0]}</text>')
    o.append(f'<text class="tick" x="{(L + R_) / 2:.1f}" y="{B + 38}" text-anchor="middle">sample rate (Hz)</text>')

    # Design-point line.
    o.append(f'<line class="design" x1="{xd:.1f}" y1="{T - 6}" x2="{xd:.1f}" y2="{B}"/>')
    o.append(f'<text class="val" x="{xd + 6:.1f}" y="{T + 4}">{DESIGN_HZ} Hz design point</text>')

    # Series.
    path = lambda p: "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in p)
    o.append(f'<path class="s1" d="{path(bat)}"/>')
    o.append(f'<path class="s2" d="{path(fla)}"/>')
    for (x, y) in bat:
        o.append(f'<circle class="m1" cx="{x:.1f}" cy="{y:.1f}" r="4.5"/>')
    for (x, y) in fla:
        o.append(f'<circle class="m2" cx="{x:.1f}" cy="{y:.1f}" r="4.5"/>')

    # Values at the design point only (selective labels).
    yb, yf = y_of(design[2]), y_of(design[3])
    o.append(f'<text class="val" x="{xd + 8:.1f}" y="{yb - 9:.1f}">{design[2]:.1f} h</text>')
    o.append(f'<text class="val" x="{xd + 8:.1f}" y="{yf + 17:.1f}">{design[3]:.1f} h</text>')

    # Direct labels at the right end, which double as the legend.
    xe = R_ + 12
    o.append(f'<text class="lab" x="{xe}" y="{bat[-1][1] + 4:.1f}">Battery</text>')
    o.append(f'<text class="lab" x="{xe}" y="{fla[-1][1] + 4:.1f}">Flash</text>')

    # Where the limits cross, interpolated on the log-rate axis between the
    # two measured rates that bracket it.
    for (h0, _, b0, f0), (h1, _, b1, f1) in zip(rows, rows[1:]):
        if (f0 - b0) > 0 >= (f1 - b1):
            t = (f0 - b0) / ((f0 - b0) - (f1 - b1))
            hz_x = 2 ** (math.log2(h0) + t * (math.log2(h1) - math.log2(h0)))
            h_x = b0 + t * (b1 - b0)
            cx, cy = x_of(hz_x, lo, hi), y_of(h_x)
            o.append(f'<text class="val" x="{cx:.1f}" y="{cy - 40:.1f}" text-anchor="middle">limits cross</text>')
            o.append(f'<text class="val" x="{cx:.1f}" y="{cy - 25:.1f}" text-anchor="middle">near {round(hz_x, -1):.0f} Hz</text>')
            o.append(f'<line class="axis" x1="{cx:.1f}" y1="{cy - 20:.1f}" x2="{cx:.1f}" y2="{cy - 8:.1f}"/>')
            break

    # Legend row (identity never by colour alone).
    lx, ly = L, H - 14
    o.append(f'<line class="s1" x1="{lx}" y1="{ly - 4}" x2="{lx + 22}" y2="{ly - 4}"/>'
             f'<circle class="m1" cx="{lx + 11}" cy="{ly - 4}" r="4"/>'
             f'<text class="val" x="{lx + 30}" y="{ly}">Battery limit: 12 mAh / measured current</text>')
    lx2 = lx + 300
    o.append(f'<line class="s2" x1="{lx2}" y1="{ly - 4}" x2="{lx2 + 22}" y2="{ly - 4}"/>'
             f'<circle class="m2" cx="{lx2 + 11}" cy="{ly - 4}" r="4"/>'
             f'<text class="val" x="{lx2 + 30}" y="{ly}">Flash limit: 131,072 pages x 150 samples</text>')
    o.append("</svg>")

    OUT.write_text("\n".join(o) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}")
    for hz, ua, bh, fh in rows:
        print(f"{hz:5d} Hz  {ua:8.1f} uA  battery {bh:5.1f} h  flash {fh:5.1f} h  -> {'flash' if fh < bh else 'battery'} first")


if __name__ == "__main__":
    main()
