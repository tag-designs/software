#!/usr/bin/env python3
"""Generate a PresTag protobuf-JSON configuration for a scheduled test.

The schedule fields a PresTag honours are absolute epochs, and the tests in
embedded/tags/families/PresTag/design/power-test-plan.md are written in terms of
offsets from "now" -- start five minutes out, hibernate from +25 to +45 minutes.
This turns those offsets into a file tag-start can program, and prints the
absolute epochs it chose so the run can be checked against them afterwards.

Two things this exists to get right, both of which have a wrong-looking
alternative that appears to work:

  * `start_delay` is an IMUTag mechanism. PresTag's writeConfig() reads only
    active_interval.start_epoch, so `tag-start --start-now` does nothing on this
    family. Start time is set here or not at all.
  * A period of zero is rejected outright by writeConfig(), and the tag stays
    IDLE with no useful diagnostic, so it is rejected here instead.

Program the result without --merge, so the tag runs exactly this schedule:

    prestag_mkconfig.py --period 10 --start-in 300 --out /tmp/p10.json
    build-host/bin/tag-reset --set-rtc
    build-host/bin/tag-start -c /tmp/p10.json

Examples:
    # immediate open-ended run at 10 s
    prestag_mkconfig.py --period 10 --out /tmp/p10.json

    # 90 min run, hibernating from +25 min to +45 min
    prestag_mkconfig.py --period 10 --run-for 5400 \
        --hibernate 1500:2700 --out /tmp/hib.json
"""

from __future__ import annotations

import argparse
import json
import sys
import time

#: Largest int32, used as "no scheduled end" in active_interval.end_epoch.
#:
#: Running() compares `sconfig.stop < timestamp`, so a far-future stop is how an
#: open-ended run is expressed; there is no separate "run forever" flag.
FOREVER = 2147483647

#: Sample period below which Running() sleeps in Stop 2 rather than Shutdown.
SHUTDOWN_REGIME_MIN_S = 10

#: Number of hibernation windows the stored configuration has room for.
#:
#: t_storedconfig declares hibernate[2], and readConfig() always reports two, so
#: a third window would be silently dropped somewhere between host and flash.
MAX_HIBERNATE_WINDOWS = 2


class ConfigError(Exception):
    """Raised when the requested schedule cannot be represented."""


def parse_window(text: str) -> tuple[int, int]:
    """Parse one ``start:end`` hibernation window, in seconds from now.

    @param[in] text Window as ``<start_offset_s>:<end_offset_s>``.
    @return Tuple of start and end offsets in seconds.
    @raise ConfigError When the text is malformed or the window does not end
           after it starts, which would never be entered.
    """
    if ":" not in text:
        raise ConfigError(f"malformed window {text!r}; expected <start_s>:<end_s>")
    a, b = text.split(":", 1)
    try:
        start, end = int(a), int(b)
    except ValueError as exc:
        raise ConfigError(f"malformed window {text!r}: {exc}") from exc
    if end <= start:
        raise ConfigError(f"window {text!r} ends at or before it starts")
    return start, end


def build(period: int, now: int, start_in: int | None, run_for: int | None,
          windows: list[tuple[int, int]]) -> dict:
    """Build the protobuf-JSON configuration body.

    @param[in] period Sample period in seconds; must be at least 1.
    @param[in] now Epoch the offsets are measured from.
    @param[in] start_in Seconds until collection should start, or None to start
               as soon as CONFIGURED next polls.
    @param[in] run_for Seconds until collection should stop, measured from
               @p now, or None for an open-ended run.
    @param[in] windows Hibernation windows as offsets from @p now.
    @return Configuration as a dict ready to serialise.
    @raise ConfigError When the period is out of range, too many windows are
           given, or the stop precedes the start.
    """
    if period < 1:
        raise ConfigError("period must be at least 1 s; writeConfig() rejects 0 "
                          "and enableTicker() ignores anything below 1")
    if period > 65536:
        raise ConfigError("period must be at most 65536 s; the RTC wakeup "
                          "register is 16-bit")
    if len(windows) > MAX_HIBERNATE_WINDOWS:
        raise ConfigError(f"at most {MAX_HIBERNATE_WINDOWS} hibernation windows "
                          f"fit in the stored configuration; got {len(windows)}")

    start_epoch = 0 if start_in is None else now + start_in
    end_epoch = FOREVER if run_for is None else now + run_for
    if run_for is not None and start_in is not None and end_epoch <= start_epoch:
        raise ConfigError("the run stops at or before it starts")

    hibernate = [{"start_epoch": now + s, "end_epoch": now + e}
                 for s, e in windows]
    while len(hibernate) < MAX_HIBERNATE_WINDOWS:
        hibernate.append({"start_epoch": 0, "end_epoch": 0})

    return {
        "tag_type": "PRESTAG",
        "active_interval": {"start_epoch": start_epoch, "end_epoch": end_epoch},
        "hibernate": hibernate,
        "period": period,
    }


def main() -> int:
    """Parse arguments, build the configuration, and write it out."""
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--period", type=int, required=True,
                   help="sample period in seconds (1-65536)")
    p.add_argument("--start-in", type=int, default=None, metavar="S",
                   help="start collection this many seconds from now; omit to "
                        "start as soon as CONFIGURED next polls, which is "
                        "within about 60 s")
    p.add_argument("--run-for", type=int, default=None, metavar="S",
                   help="stop collection this many seconds from now; omit for "
                        "an open-ended run")
    p.add_argument("--hibernate", action="append", default=[], metavar="S:E",
                   help="hibernation window as offsets in seconds from now, "
                        f"e.g. 1500:2700; at most {MAX_HIBERNATE_WINDOWS}")
    p.add_argument("--out", default="-",
                   help="file to write, or - for stdout (default: -)")
    args = p.parse_args()

    now = int(time.time())
    try:
        windows = [parse_window(w) for w in args.hibernate]
        cfg = build(args.period, now, args.start_in, args.run_for, windows)
    except ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    text = json.dumps(cfg, indent=2) + "\n"
    if args.out == "-":
        sys.stdout.write(text)
    else:
        with open(args.out, "w") as fh:
            fh.write(text)
        print(f"wrote {args.out}")

    def stamp(epoch: int) -> str:
        return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(epoch))

    # Print the absolute epochs so the run can be checked against what was
    # actually programmed, rather than against what was intended.
    print(f"  now             {now}  {stamp(now)}", file=sys.stderr)
    ai = cfg["active_interval"]
    if ai["start_epoch"] == 0:
        print("  start           0  (as soon as CONFIGURED polls, <= ~60 s)",
              file=sys.stderr)
    else:
        print(f"  start           {ai['start_epoch']}  {stamp(ai['start_epoch'])}",
              file=sys.stderr)
    if ai["end_epoch"] == FOREVER:
        print("  stop            open-ended", file=sys.stderr)
    else:
        print(f"  stop            {ai['end_epoch']}  {stamp(ai['end_epoch'])}",
              file=sys.stderr)
    for i, w in enumerate(cfg["hibernate"]):
        if w["end_epoch"] > w["start_epoch"]:
            print(f"  hibernate[{i}]    {w['start_epoch']} .. {w['end_epoch']}  "
                  f"{stamp(w['start_epoch'])} .. {stamp(w['end_epoch'])}",
                  file=sys.stderr)

    if args.period < SHUTDOWN_REGIME_MIN_S:
        print(f"  note: period < {SHUTDOWN_REGIME_MIN_S} s sleeps in Stop 2 "
              "between samples, not Shutdown. This is a bench configuration.",
              file=sys.stderr)
    print(f"  note: one 60-sample block is {60 * args.period} s; an unbiased "
          "power window is a whole number of these.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
