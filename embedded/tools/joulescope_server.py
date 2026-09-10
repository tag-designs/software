#!/usr/bin/env python3
"""Hold the Joulescope open and serve measurements over a Unix socket.

Every invocation of joulescope_measure.py opens and closes the instrument.
Across a sweep that is dozens of USB open/close cycles, and across a working
day it was several hundred. Two failure modes came out of that churn: the
device wedging so that its topic tree disappears and only a physical power
cycle recovers it, and the DUT supply being left switched off, which presents
downstream as "Unable to get core ID" from the debug probe because the target
is unpowered.

This opens the device once and keeps it open until told to stop. Power is
explicit state, not something saved and restored around each run, so a killed
client cannot leave the tag unpowered.

    joulescope_server.py --start          # background, holds the device
    joulescope_server.py --status
    joulescope_server.py --stop

Clients speak one JSON object per line over the socket; see handle().
joulescope_measure.py --use-server routes through here, so existing scripts
keep working.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import threading
import time
from contextlib import suppress

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from joulescope_measure import (  # noqa: E402
    Collector,
    RANGE_MODE_AUTO,
    RANGE_MODE_OFF,
    SCNT_PER_SECOND,
)

#: Default socket path. Under /tmp so it does not land in the repository.
DEFAULT_SOCKET = "/tmp/joulescope_server.sock"


class Server:
    """Owns the instrument for the lifetime of the process."""

    def __init__(self, sock_path: str, window: float = 0.5):
        """@param sock_path Unix socket to listen on."""
        self.sock_path = sock_path
        self.driver = None
        self.device = None
        self.stop = threading.Event()
        self.collector = None
        self.window = window

    def open_device(self) -> None:
        """Open the instrument and ensure the DUT is powered.

        @raise RuntimeError when no device is present.
        @post The current range mode is auto, so the DUT is powered.
        """
        from pyjoulescope_driver import Driver

        self.driver = Driver()
        paths = self.driver.device_paths()
        if not paths:
            raise RuntimeError("no Joulescope found")
        self.device = paths[0]
        self.driver.open(self.device, mode="restore")
        time.sleep(0.5)
        mode = self.driver.query(f"{self.device}/s/i/range/mode")
        if int(mode) == RANGE_MODE_OFF:
            self.driver.publish(f"{self.device}/s/i/range/mode", RANGE_MODE_AUTO)
            time.sleep(0.5)

    def start_stream(self, window: float) -> None:
        """Subscribe once, for the life of the server.

        @param window Statistics block length in seconds.

        @details Subscribing per measurement stacked subscriptions: an
                 unsubscribe that silently failed left the previous callback
                 attached, so every block was counted twice and the reported
                 current halved. One subscription, taken at startup, cannot
                 drift that way.
        """
        scnt = max(1, int(round(SCNT_PER_SECOND * window)))
        self.driver.publish(f"{self.device}/s/stats/scnt", scnt)
        self.driver.publish(f"{self.device}/s/stats/ctrl", 1)
        self.window = window
        self.collector = Collector()
        self.driver.subscribe(f"{self.device}/s/stats/value", "pub",
                              self.collector.on_value)

    def measure(self, duration: float, window: float,
                trace: bool = False) -> dict:
        """Measure by taking a delta across the running stream.

        @param duration Seconds to accumulate.
        @param window   Requested block length; a change restarts the stream.
        @param trace    Also return trace_ua, the mean current of every block.
        @return Dict with current_ua, voltage_v, blocks and span_s, or an
                error key when the DUT is unpowered or too few blocks arrived.
        """
        mode = int(self.driver.query(f"{self.device}/s/i/range/mode"))
        if mode == RANGE_MODE_OFF:
            return {"error": "range mode is off; DUT is not powered"}
        # A different window is honoured by ignoring it, not by restarting the
        # stream: restarting races inside pyjoulescope_driver's publish
        # callback and has both killed the server and wedged the instrument.
        # The average is charge over span, so block length changes only the
        # resolution of trace_ua. Start the server with --window to change it.
        requested_window = window

        before = self.collector.snapshot()
        t0 = time.time()
        time.sleep(duration)
        elapsed = time.time() - t0
        after = self.collector.snapshot()

        new_blocks = after[len(before):]
        if len(new_blocks) < 2:
            return {"error": f"only {len(new_blocks)} blocks in {elapsed:.1f}s"}
        charge = new_blocks[-1].charge - new_blocks[0].charge
        # Span from the block count, which is exact; elapsed is wall clock and
        # includes the partial blocks at each end.
        span = (len(new_blocks) - 1) * self.window
        resp = {
            "current_ua": (charge / span) * 1e6,
            "voltage_v": sum(w.v_avg for w in new_blocks) / len(new_blocks),
            "blocks": len(new_blocks),
            "span_s": span,
            "elapsed_s": round(elapsed, 2),
            "window_s": self.window,
        }
        if abs(requested_window - self.window) > 1e-9:
            resp["note"] = (f"measured with the server's {self.window} s blocks, "
                            f"not the requested {requested_window} s")
        if trace:
            # Per-block mean current, so a caller can see whether a high
            # average is a steady load or a duty cycle; the aggregate alone
            # cannot tell a tag that never sleeps from one that sleeps and
            # wakes every couple of seconds.
            resp["trace_ua"] = [
                round((b.charge - a.charge) / self.window * 1e6, 2)
                for a, b in zip(new_blocks, new_blocks[1:])
            ]
        return resp

    def handle(self, req: dict) -> dict:
        """Dispatch one request.

        Commands: measure {duration, window}, power {on}, status, quit.

        @param req Decoded request object.
        @return Response object; always JSON-serialisable.
        """
        cmd = req.get("cmd")
        if cmd == "measure":
            return self.measure(float(req.get("duration", 10.0)),
                                float(req.get("window", 0.5)),
                                bool(req.get("trace", False)))
        if cmd == "power":
            want = RANGE_MODE_AUTO if req.get("on", True) else RANGE_MODE_OFF
            self.driver.publish(f"{self.device}/s/i/range/mode", want)
            time.sleep(0.4)
            return {"range_mode": int(self.driver.query(
                f"{self.device}/s/i/range/mode"))}
        if cmd == "status":
            return {"device": self.device,
                    "range_mode": int(self.driver.query(
                        f"{self.device}/s/i/range/mode"))}
        if cmd == "quit":
            self.stop.set()
            return {"stopping": True}
        return {"error": f"unknown command {cmd!r}"}

    def serve(self) -> int:
        """Listen until told to quit.

        @return Process exit status.
        @post The socket file is removed, the DUT is left powered, and the
              device is closed. The power step is explicit: closing the driver
              with the current range off opens the sense path and removes the
              target's supply, which downstream looks like a dead tag --
              "Unable to get core ID" from the debug probe, or a download that
              silently does nothing. This used to be claimed and not done.
        """
        with suppress(FileNotFoundError):
            os.unlink(self.sock_path)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(self.sock_path)
        srv.listen(4)
        srv.settimeout(1.0)
        print(f"joulescope_server: holding {self.device} on {self.sock_path}",
              flush=True)
        try:
            while not self.stop.is_set():
                try:
                    conn, _ = srv.accept()
                except socket.timeout:
                    continue
                with conn:
                    data = conn.makefile("rwb")
                    line = data.readline()
                    if not line:
                        continue
                    try:
                        resp = self.handle(json.loads(line))
                    except Exception as e:                    # noqa: BLE001
                        resp = {"error": f"{type(e).__name__}: {e}"}
                    try:
                        data.write((json.dumps(resp) + "\n").encode())
                        data.flush()
                    except (BrokenPipeError, ConnectionResetError):
                        # The client gave up; that is its problem, not a
                        # reason to drop the device and every other client.
                        #
                        # flush() has to be inside this guard, not after it:
                        # the writer is buffered, so write() only fills the
                        # buffer and it is flush() that touches the socket and
                        # raises. With flush() outside, a client that timed out
                        # during a long measurement killed the server as it
                        # tried to reply -- which is how a 900 s window took the
                        # instrument down with it.
                        pass
        finally:
            srv.close()
            with suppress(FileNotFoundError):
                os.unlink(self.sock_path)
            self.stop_stream()
            self.leave_dut_powered()
            with suppress(Exception):
                self.driver.close(self.device)
            with suppress(Exception):
                self.driver.finalize()
        return 0

    def stop_stream(self) -> None:
        """Undo everything start_stream() turned on, in reverse order.

        @details Closing the driver while the instrument is still streaming and
                 a callback is still attached leaves the JS320 wedged: it keeps
                 enumerating, nothing holds its USB handle, and the next
                 jsdrv_open() times out with its topic tree gone, recoverable
                 only by physically replugging it. The teardown used to close
                 the device without unsubscribing or clearing stats/ctrl, which
                 is how a routine release cost a power cycle.
        """
        if self.collector is not None:
            with suppress(Exception):
                self.driver.unsubscribe(f"{self.device}/s/stats/value",
                                        self.collector.on_value)
        with suppress(Exception):
            self.driver.publish(f"{self.device}/s/stats/ctrl", 0)
        # Let the final blocks drain before the device is closed underneath the
        # stream; the teardown is not worth racing.
        time.sleep(0.3)

    def leave_dut_powered(self) -> bool:
        """Force the current range back to auto before releasing the device.

        @details The DUT is supplied through the sense path, so a range of off
                 unpowers it. Whoever picks the instrument up next -- the
                 desktop UI, another script, a person -- should find a live
                 target, and a tag that has silently lost power wastes far more
                 time than this costs.

        @return true when the range reads back as something other than off.
        """
        try:
            self.driver.publish(f"{self.device}/s/i/range/mode", RANGE_MODE_AUTO)
            time.sleep(0.2)
            mode = int(self.driver.query(f"{self.device}/s/i/range/mode"))
        except Exception as e:                                  # noqa: BLE001
            print(f"joulescope_server: could not restore DUT power: {e}",
                  file=sys.stderr, flush=True)
            return False
        if mode == RANGE_MODE_OFF:
            print("joulescope_server: WARNING the DUT is left UNPOWERED "
                  "(range mode off); power-cycle the instrument",
                  file=sys.stderr, flush=True)
            return False
        print(f"joulescope_server: released, DUT left powered (range mode {mode})",
              flush=True)
        return True


def request(sock_path: str, req: dict, timeout: float = 300.0) -> dict:
    """Send one request to a running server.

    @param sock_path Socket to connect to.
    @param req       Request object.
    @param timeout   Seconds to wait for the reply.
    @return Decoded response, or an error object when the server is absent.
    """
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(sock_path)
    except (FileNotFoundError, ConnectionRefusedError, socket.timeout):
        return {"error": "no server; start it with joulescope_server.py --start"}
    with s:
        f = s.makefile("rwb")
        f.write((json.dumps(req) + "\n").encode())
        f.flush()
        line = f.readline()
    return json.loads(line) if line else {"error": "no reply"}


def main() -> int:
    """Start, query or stop the server.

    @return 0 on success, 1 on error.
    """
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--socket", default=DEFAULT_SOCKET)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--start", action="store_true", help="run in the foreground")
    g.add_argument("--status", action="store_true")
    g.add_argument("--stop", action="store_true")
    g.add_argument("--measure", type=float, metavar="SECONDS")
    p.add_argument("--window", type=float, default=0.5,
                   help="statistics block length; with --start it is fixed for "
                        "the server's lifetime, because changing it later "
                        "races inside the driver and wedges the instrument")
    args = p.parse_args()

    if args.start:
        srv = Server(args.socket, args.window)
        srv.open_device()
        srv.start_stream(args.window)
        return srv.serve()
    if args.status:
        print(json.dumps(request(args.socket, {"cmd": "status"})))
        return 0
    if args.stop:
        print(json.dumps(request(args.socket, {"cmd": "quit"})))
        return 0
    r = request(args.socket, {"cmd": "measure", "duration": args.measure,
                              "window": args.window})
    print(json.dumps(r))
    return 1 if "error" in r else 0


if __name__ == "__main__":
    sys.exit(main())
