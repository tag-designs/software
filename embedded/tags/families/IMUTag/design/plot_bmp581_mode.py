#!/usr/bin/env python3
"""Draw IMUTag run current against sample rate for the two BMP581 power modes.

Reads the 2026-11-03 sweeps beside this script,
sweep-3v7-20261103-bmp581-continuous.csv and sweep-3v7-20261103-bmp581-normal.csv,
and writes bmp581-mode-current.svg, which power.md embeds.

The two images differ in one instruction: the power mode passed to
bmp5_set_power_mode() in bmp581_config_continuous_device(). Standard library
only; rerun after a new pair of sweeps:

    python3 embedded/tags/families/IMUTag/design/plot_bmp581_mode.py
"""

import csv
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
CONTINUOUS = HERE / "sweep-3v7-20261103-bmp581-continuous.csv"
NORMAL = HERE / "sweep-3v7-20261103-bmp581-normal.csv"
OUT = HERE / "bmp581-mode-current.svg"

# BMP581 ODR chosen by select_pressure_rate() in IMUTag sensors.c, per IMU rate.
PRESSURE_ODR = {100: 25, 200: 25, 400: 50, 800: 100, 1600: 200}

# Geometry (SVG user units).
W, H = 680, 440
L, R, T, B = 72, 120, 70, 340  # plot box: left inset, right inset, top, bottom
Y_MAX, Y_STEP = 1200, 200


def load(path):
    """Return {rate_hz: current_ua} for the rate points of one sweep CSV."""
    out = {}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            if r["recorded_odr"]:
                out[int(r["recorded_odr"])] = float(r["current_ua"])
    return out


def main():
    cont, norm = load(CONTINUOUS), load(NORMAL)
    rates = sorted(cont)
    lo, hi = rates[0], rates[-1]
    right = W - R

    def x_of(hz):
        return L + (math.log2(hz) - math.log2(lo)) / (math.log2(hi) - math.log2(lo)) * (right - L)

    def y_of(ua):
        return B - ua / Y_MAX * (B - T)

    pc = [(x_of(hz), y_of(cont[hz])) for hz in rates]
    pn = [(x_of(hz), y_of(norm[hz])) for hz in rates]
    s100 = cont[100] - norm[100]

    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
             'role="img" aria-labelledby="t d">')
    o.append('<title id="t">IMUTag run current vs sample rate, BMP581 CONTINUOUS and NORMAL modes</title>')
    o.append(f'<desc id="d">NORMAL mode draws {s100:.0f} uA less than CONTINUOUS at 100 Hz '
             f'({cont[100]:.0f} against {norm[100]:.0f} uA); the saving shrinks as the pressure ODR rises '
             f'and is gone at 1600 Hz, where the ODR is 200 Hz.</desc>')
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
  .val { fill:var(--ink2); font-size:11.5px; }
  .grid { stroke:var(--grid); stroke-width:1; }
  .axis { stroke:var(--axis); stroke-width:1; }
  .gap { stroke:var(--muted); stroke-width:1; stroke-dasharray:2 3; }
  .s1 { stroke:var(--s1); fill:none; stroke-width:2; stroke-linejoin:round; stroke-linecap:round; }
  .s2 { stroke:var(--s2); fill:none; stroke-width:2; stroke-linejoin:round; stroke-linecap:round; }
  .m1 { fill:var(--s1); stroke:var(--surface); stroke-width:2; }
  .m2 { fill:var(--s2); stroke:var(--surface); stroke-width:2; }
</style>""")
    o.append(f'<rect class="bg" width="{W}" height="{H}"/>')
    o.append(f'<text class="title" x="{L - 48}" y="26">NORMAL mode saves about 100 uA at low rates, '
             'nothing at 1600 Hz</text>')
    o.append(f'<text class="sub" x="{L - 48}" y="46">Run current, IMUTagNandBmp581, BMP581 power mode only; '
             'measured at 3.69 V on 2026-11-03</text>')

    # Grid and y axis.
    for ua in range(0, Y_MAX + 1, Y_STEP):
        y = y_of(ua)
        o.append(f'<line class="{"axis" if ua == 0 else "grid"}" x1="{L}" y1="{y:.1f}" x2="{right}" y2="{y:.1f}"/>')
        o.append(f'<text class="tick" x="{L - 8}" y="{y + 4:.1f}" text-anchor="end">{ua}</text>')
    o.append(f'<text class="tick" x="{L - 52}" y="{(T + B) / 2:.1f}" '
             f'transform="rotate(-90 {L - 52} {(T + B) / 2:.1f})" text-anchor="middle">run current (uA)</text>')

    # X ticks: IMU rate, with the pressure ODR beneath it.
    for hz in rates:
        x = x_of(hz)
        o.append(f'<text class="tick" x="{x:.1f}" y="{B + 18}" text-anchor="middle">{hz}</text>')
        o.append(f'<text class="tick" x="{x:.1f}" y="{B + 33}" text-anchor="middle">({PRESSURE_ODR[hz]})</text>')
    o.append(f'<text class="tick" x="{(L + right) / 2:.1f}" y="{B + 54}" text-anchor="middle">'
             'IMU sample rate, Hz (BMP581 ODR, Hz)</text>')

    # The saving at each rate, drawn as a dotted span with its value.
    for hz, (xc, yc), (_, yn) in zip(rates, pc, pn):
        d = cont[hz] - norm[hz]
        if d > 5:
            o.append(f'<line class="gap" x1="{xc:.1f}" y1="{yc:.1f}" x2="{xc:.1f}" y2="{yn:.1f}"/>')
            # Below the NORMAL point, which is clear of both lines because the
            # segments leaving each point all rise; the first, where the series
            # label sits below, goes beside the middle of its span instead.
            if hz == rates[0]:
                xt, yt, anchor = xc + 7, (yc + yn) / 2 + 4, "start"
            else:
                xt, yt, anchor = xc + 6, yn + 22, "middle"
            o.append(f'<text class="val" x="{xt:.1f}" y="{yt:.1f}" '
                     f'text-anchor="{anchor}">-{d:.0f} uA</text>')

    path = lambda p: "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in p)
    o.append(f'<path class="s1" d="{path(pc)}"/>')
    o.append(f'<path class="s2" d="{path(pn)}"/>')
    for (x, y) in pc:
        o.append(f'<circle class="m1" cx="{x:.1f}" cy="{y:.1f}" r="4.5"/>')
    for (x, y) in pn:
        o.append(f'<circle class="m2" cx="{x:.1f}" cy="{y:.1f}" r="4.5"/>')

    # Direct labels at the left end, where the lines are furthest apart.
    o.append(f'<text class="lab" x="{pc[0][0] + 8:.1f}" y="{pc[0][1] - 18:.1f}">CONTINUOUS</text>')
    o.append(f'<text class="lab" x="{pn[0][0] + 8:.1f}" y="{pn[0][1] + 20:.1f}">NORMAL</text>')
    # End values.
    o.append(f'<text class="val" x="{pc[-1][0] + 10:.1f}" y="{pc[-1][1] - 4:.1f}">{cont[hi]:.0f} / {norm[hi]:.0f}</text>')

    # Legend row (identity never by colour alone).
    lx, ly = L, H - 12
    o.append(f'<line class="s1" x1="{lx}" y1="{ly - 4}" x2="{lx + 22}" y2="{ly - 4}"/>'
             f'<circle class="m1" cx="{lx + 11}" cy="{ly - 4}" r="4"/>'
             f'<text class="val" x="{lx + 30}" y="{ly}">CONTINUOUS: converts at its maximum rate</text>')
    lx2 = lx + 290
    o.append(f'<line class="s2" x1="{lx2}" y1="{ly - 4}" x2="{lx2 + 22}" y2="{ly - 4}"/>'
             f'<circle class="m2" cx="{lx2 + 11}" cy="{ly - 4}" r="4"/>'
             f'<text class="val" x="{lx2 + 30}" y="{ly}">NORMAL: converts at the ODR</text>')
    o.append("</svg>")

    OUT.write_text("\n".join(o) + "\n", encoding="utf-8")
    print(f"wrote {OUT.name}")
    for hz in rates:
        d = cont[hz] - norm[hz]
        print(f"{hz:5d} Hz  ODR {PRESSURE_ODR[hz]:3d}  CONTINUOUS {cont[hz]:7.1f}  NORMAL {norm[hz]:7.1f}  "
              f"saving {d:6.1f} uA ({100 * d / cont[hz]:4.1f} %)")


if __name__ == "__main__":
    main()
