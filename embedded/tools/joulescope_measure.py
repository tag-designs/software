#!/usr/bin/env python3
"""Measure tag current with a Joulescope without disturbing the device under test.

The stock ``pyjoulescope_driver`` command-line entry points are unsafe for tags
that keep state across standby. ``measure`` and ``statistics`` both call
``Driver.open(device)`` with no ``mode``, which the driver documents as
equivalent to ``'defaults'``: it pushes the metadata default for every writable
topic. On a JS320 two of those defaults break the measurement:

* ``s/i/range/mode`` defaults to ``0`` (``off``), which opens the current-sense
  path and therefore **cuts power to the DUT** at every open.
* ``s/i/range/select`` -- the manual shunt selection -- also defaults to ``0``
  (``off``). ``statistics`` then sets mode ``5`` (``manual``), so the current
  path stays open for the whole run and every sample reads approximately zero.

For an IMUTag the consequence is not merely a bad number. Runtime state that
must survive standby lives in ``pState``, a mirror of the RTC backup registers,
so a power interruption at each open corrupts exactly the state the firmware
relies on: the observed symptom is a disrupted clock and a tag that draws
milliamps instead of microamps until the host resynchronises it.

This tool therefore:

* opens with ``mode='restore'``, adopting the device's current state and pushing
  nothing;
* reports the pre-existing range configuration instead of assuming it;
* refuses to select range ``0`` on any code path;
* holds one session open across repeated windows rather than reopening;
* restores whatever range configuration it found before exiting.

Average current is computed from the accumulated charge over the window rather
than by averaging the per-window means, so a pulsating load -- a switching
regulator's input current, or a tag's per-sample bursts -- integrates correctly.

Example:
    joulescope_measure.py --duration 30
    joulescope_measure.py --duration 5 --repeat 4 --window 0.5
    joulescope_measure.py --duration 10 --set-range manual:5
"""

from __future__ import annotations

import argparse
import signal
import statistics
import sys
import threading
import time
from contextlib import suppress
from dataclasses import dataclass, field

#: ``s/i/range/mode`` values, from the JS320 topic metadata. ``0`` disconnects
#: the current-sense path and is never written by this tool.
RANGE_MODE_OFF = 0
RANGE_MODE_AUTO = 4
RANGE_MODE_MANUAL = 5

#: Most sensitive manual shunt selection. ``s/i/range/select`` == 0 means off.
SELECT_MIN = 1
SELECT_MAX = 5

#: ``s/stats/scnt`` is documented as a count of 1 Msps samples per block, and
#: setting it that way does produce the requested block duration.
SCNT_PER_SECOND = 1_000_000


@dataclass
class Window:
    """One statistics block returned by the instrument.

    Attributes are in SI units: amps, volts, watts, coulombs, joules.
    """

    sample_id: int
    i_avg: float
    i_min: float
    i_max: float
    v_avg: float
    p_avg: float
    charge: float
    energy: float


@dataclass
class Collector:
    """Accumulates statistics blocks from the driver's callback thread."""

    windows: list[Window] = field(default_factory=list)
    _lock: threading.Lock = field(default_factory=threading.Lock)

    def on_value(self, topic: str, value) -> None:  # noqa: ANN001 - driver type
        """Driver subscription callback.

        @param topic Topic that published, unused; retained for the driver's
                     callback signature.
        @param value Statistics structure with ``signals`` and ``accumulators``.
        """
        try:
            sig = value['signals']
            acc = value['accumulators']
            w = Window(
                sample_id=value['time']['samples']['value'][0],
                i_avg=sig['current']['avg']['value'],
                i_min=sig['current']['min']['value'],
                i_max=sig['current']['max']['value'],
                v_avg=sig['voltage']['avg']['value'],
                p_avg=sig['power']['avg']['value'],
                charge=acc['charge']['value'],
                energy=acc['energy']['value'],
            )
        except (KeyError, TypeError):
            return
        with self._lock:
            self.windows.append(w)

    def snapshot(self) -> list[Window]:
        """Return a copy of the windows collected so far."""
        with self._lock:
            return list(self.windows)


def parse_set_range(text: str | None) -> tuple[int, int] | None:
    """Parse a ``--set-range`` argument into ``(mode, select)``.

    @param text ``None``, ``"auto"``, or ``"manual:N"`` where N is 1-5.
    @return ``None`` to leave the device's range configuration untouched, else
            the mode and manual shunt selection to apply.
    @raise SystemExit on a malformed value, or on any attempt to select 0,
           which would disconnect the DUT.
    """
    if text is None:
        return None
    if text == 'auto':
        return RANGE_MODE_AUTO, 0
    if text.startswith('manual:'):
        try:
            sel = int(text.split(':', 1)[1])
        except ValueError:
            raise SystemExit(f'--set-range: not an integer: {text!r}')
        if not SELECT_MIN <= sel <= SELECT_MAX:
            raise SystemExit(
                f'--set-range manual:{sel} rejected; select must be '
                f'{SELECT_MIN}-{SELECT_MAX} (0 disconnects the DUT)')
        return RANGE_MODE_MANUAL, sel
    raise SystemExit(f"--set-range: expected 'auto' or 'manual:N', got {text!r}")


def describe_mode(mode) -> str:  # noqa: ANN001 - driver returns int or str
    """Render a ``s/i/range/mode`` value for display."""
    names = {0: 'off', 4: 'auto', 5: 'manual', 6: 'test_dir', 7: 'test_seq'}
    try:
        return f'{int(mode)} ({names.get(int(mode), "?")})'
    except (TypeError, ValueError):
        return repr(mode)


def report(windows: list[Window], elapsed: float, window_s: float,
           label: str = '') -> None:
    """Print an aggregate summary for one measurement run.

    Average current is the accumulated-charge delta divided by the interval it
    actually spans. The accumulators are session-cumulative, so the delta
    between the first and last block covers one block less than the elapsed
    wall time; the span is therefore derived from the block count and the
    configured block duration, both known exactly.

    The instrument's own sample-id counter is deliberately not used as the time
    base. On a JS320 it advances 8e6 per 0.5 s block, i.e. at 16 MHz, not at the
    1 Msps that ``s/stats/scnt`` is documented in, and that rate is not
    documented as stable. It is reported as a diagnostic instead.

    The mean of the per-block averages is printed as a cross-check; a material
    difference between the two indicates dropped blocks.

    @param windows  Statistics blocks collected during the run.
    @param elapsed  Wall-clock duration of the run in seconds, for display.
    @param window_s Configured block duration in seconds.
    @param label    Optional prefix, used when repeating runs.
    """
    if len(windows) < 2:
        print(f'{label}insufficient data: {len(windows)} window(s) collected',
              file=sys.stderr)
        return
    span = (len(windows) - 1) * window_s
    d_samples = windows[-1].sample_id - windows[0].sample_id
    d_charge = windows[-1].charge - windows[0].charge
    d_energy = windows[-1].energy - windows[0].energy
    i_int = d_charge / span
    i_mean = statistics.fmean(w.i_avg for w in windows)
    v_mean = statistics.fmean(w.v_avg for w in windows)
    print(f'{label}blocks={len(windows)} elapsed={elapsed:.2f} s '
          f'integrated over {span:.3f} s')
    print(f'{label}  current  (charge/time) : {i_int * 1e6:12.4f} uA')
    print(f'{label}  current  (window mean) : {i_mean * 1e6:12.4f} uA')
    print(f'{label}  current  min / max     : '
          f'{min(w.i_min for w in windows) * 1e6:.4f} / '
          f'{max(w.i_max for w in windows) * 1e6:.4f} uA')
    print(f'{label}  voltage  mean          : {v_mean:12.4f} V')
    print(f'{label}  power    (energy/time) : {d_energy / span * 1e6:12.4f} uW')
    if span > 0 and d_samples > 0:
        print(f'{label}  (sample-id rate         : '
              f'{d_samples / span / 1e6:.3f} MHz, informational)')
    if i_int != 0 and abs(i_mean - i_int) / abs(i_int) > 0.05:
        print(f'{label}  WARNING: integrated and window-mean currents differ by '
              f'>5%; windows may have been dropped', file=sys.stderr)


def _terminate(signum, frame):  # noqa: ANN001 - signal handler signature
    """Turn SIGTERM/SIGINT into an exception so teardown still runs.

    @details Without this a timeout or Ctrl-C kills the process outright and the
             finally block never executes, leaving the instrument mid-session.
    """
    raise KeyboardInterrupt(f"signal {signum}")


def main() -> int:
    """Parse arguments, run the measurement, and restore device state."""
    signal.signal(signal.SIGTERM, _terminate)
    signal.signal(signal.SIGINT, _terminate)
    p = argparse.ArgumentParser(
        description='Measure DUT current with a Joulescope, without pushing '
                    'topic defaults that would power-cycle the DUT.')
    p.add_argument('--duration', type=float, default=30.0,
                   help='Measurement window in seconds (default: 30).')
    p.add_argument('--repeat', type=int, default=1,
                   help='Number of consecutive windows, one session (default: 1).')
    p.add_argument('--window', type=float, default=0.5,
                   help='Statistics block length in seconds (default: 0.5).')
    p.add_argument('--device', default=None,
                   help='Device path; default is the only device found.')
    p.add_argument('--allow-unpowered', action='store_true',
                   help='Measure even when the current range is off. Only '
                        'meaningful when the DUT is powered from a separate '
                        'supply and the sense path is deliberately open; the '
                        'reading does not describe the DUT otherwise.')
    p.add_argument('--set-range', default=None, metavar='SPEC',
                   help="Change the current range: 'auto' or 'manual:N' with "
                        "N in 1-5. Omit to leave the device as found. Never "
                        "accepts 0, which would disconnect the DUT.")
    args = p.parse_args()

    try:
        from pyjoulescope_driver import Driver
    except ImportError:
        print('pyjoulescope_driver is not installed', file=sys.stderr)
        return 2

    requested = parse_set_range(args.set_range)

    d = Driver()
    try:
        devices = d.device_paths()
        if not devices:
            print('no Joulescope found', file=sys.stderr)
            return 1
        if args.device is not None:
            if args.device not in devices:
                print(f'device not found: {args.device}; have {devices}',
                      file=sys.stderr)
                return 1
            device = args.device
        elif len(devices) > 1:
            print(f'multiple devices; use --device: {devices}', file=sys.stderr)
            return 1
        else:
            device = devices[0]

        # 'restore' adopts the device's current state and pushes nothing. Never
        # use 'defaults' (or omit mode) here: see this module's docstring.
        d.open(device, mode='restore')
        saved_mode = saved_select = None
        range_changed = False
        try:
            saved_mode = d.query(f'{device}/s/i/range/mode')
            saved_select = d.query(f'{device}/s/i/range/select')
            print(f'device: {device}')
            print(f'  range/mode as found   : {describe_mode(saved_mode)}')
            print(f'  range/select as found : {saved_select}')
            if int(saved_mode) == RANGE_MODE_OFF and requested is None:
                # Refuse rather than warn. A warning here does not survive a
                # sweep: the caller records the number, the number looks
                # plausible, and every point in the run reads the same because
                # it reflects the sense path rather than the tag -- an entire
                # measurement session was lost to exactly this. There is no
                # useful measurement to take with the output off, so failing is
                # never the wrong call.
                print('ERROR: range mode is off, so the DUT is not powered '
                      'through the sense path and any average taken now '
                      'describes the instrument, not the tag. Pass '
                      '--set-range auto to power it, or --allow-unpowered if '
                      'the DUT is deliberately powered from elsewhere.',
                      file=sys.stderr)
                if not args.allow_unpowered:
                    return 2

            if requested is not None:
                mode, select = requested
                range_changed = True
                if mode == RANGE_MODE_MANUAL:
                    d.publish(f'{device}/s/i/range/select', select)
                d.publish(f'{device}/s/i/range/mode', mode)
                print(f'  range set to          : {describe_mode(mode)}'
                      + (f' select={select}' if mode == RANGE_MODE_MANUAL else ''))

            scnt = max(1, int(round(SCNT_PER_SECOND * args.window)))
            d.publish(f'{device}/s/stats/scnt', scnt)
            d.publish(f'{device}/s/stats/ctrl', 1)

            for run in range(args.repeat):
                c = Collector()
                # Hold one reference to the bound method: each attribute access
                # creates a distinct object, and the driver matches
                # subscriptions by identity, so unsubscribe would not find it.
                callback = c.on_value
                topic = f'{device}/s/stats/value'
                d.subscribe(topic, 'pub', callback)
                t0 = time.time()
                try:
                    while time.time() - t0 < args.duration:
                        time.sleep(0.02)
                finally:
                    elapsed = time.time() - t0
                    # A failed unsubscribe must not discard the measurement.
                    with suppress(Exception):
                        d.unsubscribe(topic, callback)
                label = f'[{run + 1}/{args.repeat}] ' if args.repeat > 1 else ''
                report(c.snapshot(), elapsed, args.window, label)
        finally:
            with suppress(Exception):
                d.publish(f'{device}/s/stats/ctrl', 0)
            #
            # Restore the range ONLY if this run changed it.
            #
            # The previous teardown republished the values it had read, but
            # s/i/range/select reads back as 0 on a device left in auto, and 0
            # is the "off" selection that disconnects the current path and cuts
            # DUT power. Writing it unconditionally on every exit was the one
            # place this tool did the exact thing it exists to avoid, and it
            # left a tag unpowered mid-erase.
            #
            # When --set-range was not given the range was never touched, so the
            # correct teardown is to touch nothing. When it was given, restore
            # the mode first and only write a selection that is a real shunt.
            if range_changed:
                with suppress(Exception):
                    # Never restore "off" for either field. Faithfully putting
                    # back what was found is wrong when what was found was a
                    # disconnected current path: it re-cuts DUT power on the way
                    # out, which is the failure this tool exists to avoid. If
                    # the device was found off and this run turned it on, leave
                    # it on -- an unexpectedly powered DUT is recoverable, an
                    # unexpectedly dead one is not.
                    if saved_mode is not None and int(saved_mode) != RANGE_MODE_OFF:
                        d.publish(f'{device}/s/i/range/mode', saved_mode)
                    if saved_select:
                        d.publish(f'{device}/s/i/range/select', saved_select)
            with suppress(Exception):
                d.close(device)
    finally:
        d.finalize()
    return 0


if __name__ == '__main__':
    sys.exit(main())
