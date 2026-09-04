#!/usr/bin/env python3
"""Hammer a tag with monitor attach/detach cycles and check nothing breaks.

Attaching the monitor connects the debug probe **under reset**. A reset that
lands in the middle of an I2C byte leaves the addressed slave driving SDA low,
waiting for clocks that never arrive, and the master cannot issue a START while
the bus is not idle. Every device on that controller then fails for the rest of
the boot, and nothing recovers on its own, because recovery needs the bus.

On IMUTagNandBmp581 the RV-3028 and the BMM350 share one controller and one
pair of pins, so this presented as two unrelated faults: setting the clock
failed about 13% of the time, and collection aborted on roughly one attach in
three, when the BMM350 whoami -- its first bus access -- failed every retry.
Both are the same wedged bus.

This exercises both paths:

  clock  repeated reset-and-set-clock cycles, counting failures. The RV-3028
         write is the transaction the bus fault used to break.
  storm  attach and detach repeatedly against a *running* tag, then confirm it
         is still RUNNING, stop it, and read the state transition log. An abort
         shows up there as an ABORTED marker, typically EVENT_POWERFAIL, and
         the run's data is checked afterwards so a "survived" tag that recorded
         nothing is not reported as a pass.

Power is deliberately not measured here; tag_lifecycle_check.py owns that.

Examples:
    tag_attach_storm.py --config embedded/tools/power-configs/imutag-400.json
    tag_attach_storm.py --config cfg.json --rtc-cycles 20 --storm-rounds 3
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
import time
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from power_experiment import (  # noqa: E402
    DEFAULT_BIN,
    ExperimentError,
    check_download,
    config_odr_hz,
    download,
    recorded_config,
    run,
    start,
    stop,
    tag_state,
    to_idle,
)

#: Markers in the transition log that mean a run ended on its own.
#:
#: The tag records why it left each state. A run that was stopped by the host
#: ends FINISHED with EVENT_STOPCMD; anything that ends ABORTED, or reports a
#: power failure, ended because something went wrong -- which is exactly what a
#: wedged bus during an attach used to cause.
ABORT_MARKERS = ("ABORTED", "EVENT_POWERFAIL", "EVENT_EXCEPTION")


@dataclass
class Storm:
    """Everything one storm session produced."""

    rtc_attempts: int = 0
    rtc_failures: list[str] = field(default_factory=list)
    rounds: int = 0
    aborts: list[str] = field(default_factory=list)
    sanity: list[str] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)


def tag_info(bin_dir: str, base: str | None, timeout: float,
             verbose: bool) -> str:
    """Read the tag's full status, including the state transition log.

    @param bin_dir Directory holding the host tools.
    @param base    Optional bus:device selector.
    @param timeout Command timeout in seconds.
    @param verbose True to echo commands.
    @return Captured stdout, empty when the command failed.
    """
    argv = [os.path.join(bin_dir, "tag-info")]
    if base:
        argv += ["-b", base]
    return run("info", argv, timeout, verbose).stdout


def transition_log(text: str) -> list[str]:
    """Extract the state transition log entries from tag-info output.

    @param text Captured tag-info stdout.
    @return Marker lines, in order, without their indentation.
    """
    lines: list[str] = []
    seen = False
    for line in text.splitlines():
        if "State transition log" in line:
            seen = True
            continue
        if not seen:
            continue
        s = line.strip()
        # Entries look like "[1] RUNNING  reason=EVENT_STARTTIM  time=...".
        if s.startswith("["):
            lines.append(s)
    return lines


def storm_once(bin_dir: str, base: str | None, cycles: int, on_ms: int,
               off_ms: int, timeout: float, verbose: bool) -> None:
    """Attach and detach repeatedly against whatever the tag is doing.

    @param bin_dir Directory holding the host tools.
    @param base    Optional bus:device selector.
    @param cycles  Attach/detach cycles to perform.
    @param on_ms   Milliseconds to remain attached.
    @param off_ms  Milliseconds to remain detached.
    @param timeout Command timeout in seconds.
    @param verbose True to echo commands.
    @raise ExperimentError when the storm tool itself could not run.
    """
    argv = [os.path.join(bin_dir, "tag-attach-cycle"),
            "--cycles", str(cycles),
            "--on-ms", str(on_ms), "--off-ms", str(off_ms)]
    if base:
        argv += ["-b", base]
    res = run("storm", argv, timeout, verbose)
    # A non-zero exit is not by itself a verdict: the tool attaches under reset
    # and reports transport noise on stderr routinely. The tag's own state and
    # its transition log are the evidence, so only a failure to run at all is
    # fatal here.
    if res.returncode == -1:
        raise ExperimentError(f"tag-attach-cycle did not run: {res.stderr.strip()}")


def main() -> int:
    """Run the clock cycles and storm rounds, then report.

    @return 0 when no cycle failed and no run aborted, 1 otherwise.
    """
    p = argparse.ArgumentParser(
        description="Attach/detach storm and clock-cycle reliability check.")
    p.add_argument("--config", help="protobuf-JSON configuration for the runs")
    p.add_argument("--merge", action="store_true",
                   help="overlay a partial configuration onto the tag's")
    p.add_argument("--bin-dir", default=DEFAULT_BIN,
                   help="directory holding the host tools")
    p.add_argument("--base", help="bus:device selector")
    p.add_argument("--rtc-cycles", type=int, default=10,
                   help="reset-and-set-clock cycles to run")
    p.add_argument("--storm-rounds", type=int, default=1,
                   help="collection runs to storm")
    p.add_argument("--cycles", type=int, default=40,
                   help="attach/detach cycles per round")
    p.add_argument("--on-ms", type=int, default=1500,
                   help="milliseconds attached per cycle")
    p.add_argument("--off-ms", type=int, default=1500,
                   help="milliseconds detached per cycle")
    p.add_argument("--settle", type=float, default=8.0,
                   help="seconds to let a run establish before storming")
    p.add_argument("--timeout", type=float, default=180.0,
                   help="per-command timeout in seconds")
    p.add_argument("--download-timeout", type=float, default=900.0,
                   help="download timeout in seconds")
    p.add_argument("--tolerance", type=float, default=0.25,
                   help="fractional tolerance on the expected sample count")
    p.add_argument("--verbose", action="store_true", help="echo commands")
    args = p.parse_args()

    st = Storm()
    try:
        # Phase 1: the RV-3028 write, repeatedly. This is what used to fail
        # about 13% of the time once the bus was wedged.
        if args.rtc_cycles > 0:
            print(f"[clock] {args.rtc_cycles} reset-and-set-clock cycles")
            for i in range(1, args.rtc_cycles + 1):
                st.rtc_attempts += 1
                try:
                    state = to_idle(args.bin_dir, args.base, args.timeout,
                                    args.verbose, set_rtc=True)
                    if state != "IDLE":
                        st.rtc_failures.append(f"cycle {i}: reached {state}")
                except ExperimentError as e:
                    st.rtc_failures.append(f"cycle {i}: {e}")
                    print(f"      cycle {i}: FAILED: {e}")
            print(f"      {st.rtc_attempts - len(st.rtc_failures)}"
                  f"/{st.rtc_attempts} succeeded")

        # Phase 2: attach storms against a running tag.
        for rnd in range(1, args.storm_rounds + 1):
            print(f"[storm {rnd}/{args.storm_rounds}] start, then "
                  f"{args.cycles} attach/detach cycles")
            to_idle(args.bin_dir, args.base, args.timeout, args.verbose,
                    set_rtc=True)
            start(args.bin_dir, args.config, args.base, args.merge,
                  args.timeout, args.verbose)
            time.sleep(args.settle)

            storm_once(args.bin_dir, args.base, args.cycles, args.on_ms,
                       args.off_ms, args.timeout + args.cycles *
                       (args.on_ms + args.off_ms) / 1000.0, args.verbose)
            st.rounds += 1

            # The tag must still be collecting. An abort during the storm is
            # the fault this exists to catch, and it is visible here before
            # the stop command muddies the final state.
            after = tag_state(tag_info(args.bin_dir, args.base, args.timeout,
                                       args.verbose))
            if after != "RUNNING":
                st.aborts.append(
                    f"round {rnd}: tag is {after or 'in an unknown state'} "
                    "after the storm, not RUNNING -- the run did not survive")
                print(f"      ABORTED: tag is {after}, not RUNNING")
            else:
                print("      still RUNNING")

            final = stop(args.bin_dir, args.base, args.timeout, args.verbose)
            log = transition_log(tag_info(args.bin_dir, args.base,
                                          args.timeout, args.verbose))
            bad = [l for l in log if any(m in l for m in ABORT_MARKERS)]
            for l in bad:
                st.aborts.append(f"round {rnd}: {l}")
            print(f"      final state {final}, {len(log)} log entries, "
                  f"{len(bad)} abort markers")
            for l in log:
                print(f"        {l}")

            # A tag that survived but recorded nothing is not a pass.
            tmp = tempfile.NamedTemporaryFile(suffix=".db3", delete=False)
            tmp.close()
            try:
                res = download(args.bin_dir, tmp.name, args.base,
                               args.download_timeout, args.verbose)
                if not res.ok:
                    st.sanity.append("download failed")
                    st.failures.append(
                        f"round {rnd}: download failed: {res.stderr.strip()}")
                else:
                    cfg = recorded_config(tmp.name)
                    verdict, detail = check_download(
                        tmp.name, args.settle + args.cycles *
                        (args.on_ms + args.off_ms) / 1000.0,
                        config_odr_hz(cfg), args.tolerance)
                    st.sanity.append(verdict)
                    print(f"      data: {verdict}: {detail}")
                    if verdict.lower().startswith("fail"):
                        st.failures.append(f"round {rnd}: data {detail}")
            finally:
                os.unlink(tmp.name)

    except ExperimentError as e:
        st.failures.append(str(e))
        print(f"  aborted: {e}", file=sys.stderr)

    print()
    ok_rtc = st.rtc_attempts - len(st.rtc_failures)
    print(f"  clock cycles : {ok_rtc}/{st.rtc_attempts} succeeded")
    print(f"  storm rounds : {st.rounds} run, {len(st.aborts)} aborted")
    print(f"  data         : {', '.join(st.sanity) or 'not checked'}")

    st.failures = (st.failures + st.aborts
                   + [f"clock {f}" for f in st.rtc_failures])
    print()
    if st.failures:
        print("  FAILED")
        for f in st.failures:
            print(f"    - {f}")
        return 1
    print("  PASSED: every clock cycle succeeded and every run survived")
    return 0


if __name__ == "__main__":
    sys.exit(main())
