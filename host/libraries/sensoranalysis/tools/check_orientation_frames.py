#!/usr/bin/env python3
"""Check a qtcalibrate capture against the magnetometer axis convention.

The angle between the calibrated magnetic field and gravity is a property of
the site, not of the sample, so a correct pairing of the two sensors gives a
nearly constant inclination however the tag was turned. A large spread means
the two vectors are not in the same frame.

CompassProcessor::computeOrientation converts both sensors to NWU with three
lines that look like independent sign conventions:

    accel = QVector3D(accel[0], -accel[1], accel[2]);
    mag   = QVector3D(mag[0], mag[1], -mag[2]);
    qacc  = QQuaternion(q0, q2, q1, q3);          // note q2 before q1

Together those compose into exactly one signed axis permutation of the
magnetometer relative to the accelerometer -- HOST_ASSUMED_ALIGNMENT below --
and nothing else. This script verifies that equivalence and reports whether a
capture agrees with it, so a tag whose sensors are mounted differently is
caught instead of silently producing wrong headings.

Usage:

    check_orientation_frames.py CAPTURE.json [CAPTURE.json ...]
    check_orientation_frames.py --self-test

Captures must have been stopped, so the solver's constants are in the file.
Standard library only.
"""

import argparse
import itertools
import json
import math
import random
import sys

# The magnetometer-to-accelerometer alignment that computeOrientation encodes.
# Read it as: the host expects the magnetometer's +y to lie along the
# accelerometer's +x, its -x along +y, and its -z along +z.
HOST_ASSUMED_ALIGNMENT = ((1, 0, 2), (1, -1, -1))

# Acceleration within this fraction of one g is mostly gravity. Outside it the
# accelerometer is measuring motion too, so the gravity direction -- and the
# inclination that depends on it -- is not trustworthy. This gates the
# measurement only; it is not a reason to drop the magnetometer reading.
DEFAULT_GATE = 0.10
ONE_G_MILLI_G = 1000.0

# A capture agreeing with the host convention should sit far below this; one
# that does not will be an order of magnitude above it.
SPREAD_LIMIT_DEG = 10.0


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return None if n == 0.0 else [c / n for c in v]


def label_of(perm, sign):
    return ",".join("%s%s" % ("-" if sign[i] < 0 else "+", "xyz"[perm[i]])
                    for i in range(3))


def inclinations(samples, perm, sign):
    """Inclination per sample, in degrees, under one axis alignment.

    With the magnetometer relabelled into the accelerometer's frame, levelling
    is a rotation that leaves the angle between the two vectors alone, so the
    inclination is just asin(m_hat . a_hat). No quaternion is needed.
    """
    out = []
    for mag, accel in samples:
        m, a = unit(mag), unit(accel)
        if m is None or a is None:
            continue
        mm = [sign[i] * m[perm[i]] for i in range(3)]
        dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(mm, a))))
        out.append(math.degrees(math.asin(dot)))
    return out


def robust(values):
    v = sorted(values)
    n = len(v)
    med = v[n // 2]
    return med, 1.4826 * sorted(abs(x - med) for x in v)[n // 2]


def search(samples):
    scored = []
    for perm in itertools.permutations(range(3)):
        for sign in itertools.product((1, -1), repeat=3):
            med, mad = robust(inclinations(samples, perm, sign))
            scored.append((mad, med, perm, sign))
    scored.sort()
    return scored


def load_capture(path, gate):
    with open(path) as handle:
        doc = json.load(handle)
    calibration = doc.get("calibration")
    if not calibration or "offset" not in calibration:
        return None, "no calibration block (was the capture stopped?)"
    offset, mapping = calibration["offset"], calibration["mapping"]

    def apply(raw):
        v = [raw["mx"] - offset[0], raw["my"] - offset[1], raw["mz"] - offset[2]]
        return [sum(mapping[i][j] * v[j] for j in range(3)) for i in range(3)]

    kept, offered = [], 0
    for sample in doc.get("samples", []):
        if not sample.get("has_accel"):
            continue
        offered += 1
        a = sample["accel"]
        av = [a["ax"], a["ay"], a["az"]]
        if abs(math.sqrt(sum(c * c for c in av)) - ONE_G_MILLI_G) > gate * ONE_G_MILLI_G:
            continue
        kept.append((apply(sample["mag"]), av))
    if not kept:
        return None, "no samples passed the acceleration gate"
    return (kept, offered, len(doc.get("samples", []))), None


def report(path, gate):
    loaded, problem = load_capture(path, gate)
    if problem:
        print("  skipped: %s" % problem)
        return None
    samples, offered, total = loaded
    print("  %d samples, %d with accelerometer, %d within %.0f mg of one g"
          % (total, offered, len(samples), gate * ONE_G_MILLI_G))

    perm, sign = HOST_ASSUMED_ALIGNMENT
    med, mad = robust(inclinations(samples, perm, sign))
    agrees = mad <= SPREAD_LIMIT_DEG
    print("  host convention %-12s inclination %7.2f°   spread %6.2f°   %s"
          % (label_of(perm, sign), med, mad, "OK" if agrees else "DISAGREES"))

    scored = search(samples)
    print("  best alignments in this capture:")
    for mad_i, med_i, p, s in scored[:3]:
        mark = "  <- host convention" if (p, s) == HOST_ASSUMED_ALIGNMENT else ""
        print("    %-12s inclination %7.2f°   spread %6.2f°%s"
              % (label_of(p, s), med_i, mad_i, mark))
    if not agrees:
        print("  This tag does not match the host convention. See"
              " docs/shared/ for the axis contract.")
    return agrees


def self_test():
    """Plant a known misalignment in synthetic data and recover it."""
    def rotation(a, b, c):
        ca, sa, cb, sb, cc, sc = (math.cos(a), math.sin(a), math.cos(b),
                                  math.sin(b), math.cos(c), math.sin(c))
        rz = [[ca, -sa, 0], [sa, ca, 0], [0, 0, 1]]
        ry = [[cb, 0, sb], [0, 1, 0], [-sb, 0, cb]]
        rx = [[1, 0, 0], [0, cc, -sc], [0, sc, cc]]
        def mm(A, B):
            return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)]
                    for i in range(3)]
        return mm(mm(rz, ry), rx)

    def body(R, v):
        return [sum(R[j][i] * v[j] for j in range(3)) for i in range(3)]

    ok = True
    for true_dip in (67.0, 20.0, -45.0):
        for perm, sign in (((0, 1, 2), (1, 1, 1)), HOST_ASSUMED_ALIGNMENT,
                           ((2, 0, 1), (-1, 1, -1))):
            random.seed(5)
            r = math.radians(true_dip)
            world_mag = [math.cos(r), 0.0, -math.sin(r)]
            world_up = [0.0, 0.0, 1.0]
            samples = []
            for _ in range(300):
                R = rotation(*(random.uniform(0, 2 * math.pi) for _ in range(3)))
                m = body(R, world_mag)
                a = body(R, world_up)
                # Scramble the magnetometer by the inverse of (perm, sign), so
                # that applying (perm, sign) during the search recovers it.
                scrambled = [0.0, 0.0, 0.0]
                for i in range(3):
                    scrambled[perm[i]] = sign[i] * m[i]
                samples.append(([c * 46.0 for c in scrambled],
                                [c * 1000.0 for c in a]))
            mad, med, got_p, got_s = search(samples)[0]
            good = mad < 1e-6 and abs(abs(med) - abs(true_dip)) < 1e-6
            ok = ok and good
            print("    dip %+6.1f° planted %-12s -> found %-12s "
                  "inclination %+7.3f° spread %.1e  %s"
                  % (true_dip, label_of(perm, sign), label_of(got_p, got_s),
                     med, mad, "ok" if good else "FAILED"))
    print("  self-test: %s" % ("the search recovers a planted alignment exactly"
                               if ok else "FAILED"))
    return ok


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", nargs="*")
    parser.add_argument("--gate", type=float, default=DEFAULT_GATE,
                        help="acceleration gate as a fraction of one g"
                             " (default %.2f)" % DEFAULT_GATE)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test or not args.captures:
        print("Self-test: plant a known alignment, recover it")
        ok = self_test()
        if args.self_test:
            return 0 if ok else 1
        parser.error("give at least one capture, or --self-test")

    verdicts = []
    for path in args.captures:
        print()
        print(path)
        verdicts.append(report(path, args.gate))
    return 0 if all(v for v in verdicts if v is not None) else 1


if __name__ == "__main__":
    sys.exit(main())
