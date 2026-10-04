---
type: procedure
status: current
summary: The shared procedure for measuring a tag's current with the Joulescope -- rig setup, the server, flashing and attaching, taking a trustworthy number, supply voltage, the qualification tools, and what to record.
---

# Power Testing

Measure with `joulescope_server.py` held open for the whole session, the
Joulescope desktop app and qtmonitor detached, the supply set and confirmed
before the tag is connected, and the tag flashed with `mode=UR`. Measure after
an attach and detach, not from a cold boot. Read the `charge/time` figure, never
the window mean, over windows that hold a whole number of the target's
repeating units, and take two or more windows that agree. Record the supply
voltage with every number: a current means nothing at another voltage.

Every target-specific choice -- window length, thresholds, config, which states
to measure -- is in that target's plan:

| Target | Plan | Results |
| --- | --- | --- |
| BitTag | [power-test-plan.md](../../embedded/tags/BitTag/design/power-test-plan.md) | [power-results.md](../../embedded/tags/BitTag/design/power-results.md) |
| CompassTag family | [power-test-plan.md](../../embedded/tags/families/CompassTag/design/power-test-plan.md) | [power-results.md](../../embedded/tags/families/CompassTag/design/power-results.md) |
| PresTag family | [power-test-plan.md](../../embedded/tags/families/PresTag/design/power-test-plan.md) | [power-results.md](../../embedded/tags/families/PresTag/design/power-results.md) |
| IMUTag family | release check below | [power.md](../../embedded/tags/families/IMUTag/design/power.md) |

When a firmware change is what you are measuring, start from
[verifying-firmware.md](verifying-firmware.md), which says when a measurement is
required and how to bisect a bad one. To qualify a release, follow
[the release procedure](../release/release-procedure.md).

## 1. Before measuring

1. **Ask the user to detach the Joulescope desktop app and qtmonitor, and wait
   for confirmation.** Both spoil the result, each in its own way, and neither
   failure shows in the output:
   - the desktop app holds the instrument, so the scripts cannot open it;
   - qtmonitor holds the monitor, which keeps `isMonitorEnabled()` true, so the
     tag never sleeps at all. On the L432, `tagPowerEnterTerminalSleep()`
     returns without sleeping. On the U375 the idle hook
     (`tagPowerEnterIdleMode()`) falls back to plain Sleep, and the Stop 3
     terminal sleep returns while `monitorIsAttached()`. A held monitor just
     reads as a plausible-looking high average.
2. **Never use the `joulescope-js220` MCP server.** It holds the device for the
   life of the session, which blocks every script (`jsdrv_open` times out), and
   it cannot be released without killing the process. Kill any stray
   `joulescope-mcp` process before starting, or every open fails with
   `jsdrv_open timed out` and no explanation.

   ```sh
   pgrep -af 'qtmonitor|joulescope-mcp'          # must be empty
   ```

3. **Set the supply for the target, and confirm it on the Joulescope before the
   tag is connected.** The bench is often left at 3.7 V for IMUTag work.
   BitTag has no regulator and runs at 2.5 V, and its ADXL362's
   absolute-maximum supply is 3.6 V, so connecting it at 3.7 V risks damaging
   it. See section 5.

## 2. The Joulescope server

`joulescope_measure.py` opens and closes the instrument on every run when used
on its own. Across a sweep that is dozens of USB open/close cycles, and it has
caused two failures:

- the instrument wedges so that its topic tree disappears;
- the DUT supply is left switched off. Downstream this looks like
  `Unable to get core ID` from the debug probe, because the target is
  unpowered.

So run the server for the whole session. It holds the device open and treats
power as explicit state:

```sh
<python-with-pyjoulescope> embedded/tools/joulescope_server.py --start &
embedded/tools/joulescope_server.py --status   # device held, range mode not 0
embedded/tools/joulescope_measure.py --use-server --duration 10 --window 0.5
embedded/tools/joulescope_server.py --stop
```

`--use-server` prints the same lines as the direct path, so scripts that scrape
`current  (charge/time)` keep working. Checked against the direct path on the
same build when the server was added, the two agreed: 5.3368 uA and 5.3333 uA
(commit 0755c36d).

The interpreter must be able to import `pyjoulescope_driver`, which usually
means a virtualenv rather than the system python. `power_experiment.py` finds
one automatically and prints which it chose. Pass `--measure-python` or set
`JOULESCOPE_PYTHON` to choose one yourself.

Rules for the server:

- **Start one server, once, and leave it running.** Cycling it is what wedges
  the instrument.
- **Do not change the statistics window on a running server.** It races inside
  the driver's publish callback and has wedged the instrument. The server's
  0.5 s block is enough to count wakes and to measure the charge of each event,
  because one wake fills one block.
- **A killed session leaves the tag unpowered.** The clean release only runs on
  a normal exit. A hard kill skips it, the DUT loses its supply through the
  open sense path, and any run in progress dies. The transition log then reads
  `ABORTED reason=EVENT_POWERFAIL`. After any crash, assume the run died, and
  check.
- **`s/i/range/mode = 0` means the sense path is open and the DUT is
  unpowered.** A tag in that state looks dead (`Unable to get core ID`,
  `initial DEMCR read failed`). Starting the server puts the range back on auto.

### Recovering a wedged instrument

Symptom: the instrument enumerates but its whole topic tree reads `NOT_FOUND`,
or `jsdrv_open` times out. **Repeated `USBDEVFS_RESET` ioctls** recovered it
without a replug or root. One reset did nothing; three in a row, 5 s apart,
worked. Re-read `busnum`/`devnum` from `/sys/bus/usb/devices/*/` between
attempts, because the device renumbers. Afterwards the range mode reads 0
(above), and starting the server restores it. Evidence: the
[PresTag results](../../embedded/tags/families/PresTag/design/power-results.md),
2026-09-09 ~20:15.

## 3. Flashing and attaching

- **Program with `mode=UR`** (connect under reset). A hotplug connect fails with
  `Unable to get core ID` even on a healthy target, because resting states
  power the debug port down.
- **Stop and reset a tag before programming it.** Programming clears neither
  the state markers in internal flash nor `pState` in the RTC backup
  registers. A freshly programmed image therefore resumes whatever the tag was
  doing and goes back to sleep within milliseconds. The next connection then
  meets a sleeping part with its debug port down and fails with
  `Unable to get core ID` at a perfectly good target voltage, which looks like
  a rig fault.

  ```sh
  build-host/bin/tag-stop     # RUNNING -> FINISHED; it polls, because an ack is not a completion
  build-host/bin/tag-reset    # erase -> IDLE
  cmake --build <build-dir> --target <Target>-download
  ```

- **Start the image with a debugger jump, `-g 0x08000000`, after programming or
  after any erase.** An L432 tag has been found running the STM32
  system-memory bootloader: PC at `0x1FFF....`, ~14 mA, RTC registers reading
  zero. No reset recovered it. Two things did: a debugger jump, which never
  consults boot selection, and a power-on reset.

  ```sh
  STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -g 0x08000000
  ```

  The download targets end with that jump and must keep doing so; replacing it
  with `-rst` put the tag straight back into the ROM. Incident: the
  [PresTag campaign](../../embedded/tags/families/PresTag/design/investigations/2026-09-prestag-stop2-and-power-campaign.md), §2.0a.
- `stm32_programmer_select.py` connects with `reset=HWrst` and retries four
  times, two seconds apart. A first attempt on a sleeping target still fails
  now and then.
- **The first monitor attach to a sleeping tag often fails** with
  `initial DEMCR read failed`, `couldn't fetch monitor buffer length` or
  `empty or invalid acknowledgement`. Retry. On BitTag the failed attach also
  wakes the part, which then sits at ~376 uA until the next attach. A tag found
  at hundreds of uA straight after a failed attach is probably not a sleep
  fault: reset it, let it settle, and measure again. See the
  [BitTag plan](../../embedded/tags/BitTag/design/power-test-plan.md) and
  [its first qualification](../../embedded/tags/BitTag/design/investigations/2026-10-bittag-first-qualification.md),
  where the ~376 uA was measured.
- **Every attach connects under reset**, and the reset is a real event. Do not
  poll state during a run: a PresTag run polled eight times recorded 3 samples
  instead of ~30. Read the epochs back from the download instead, or watch the
  tag with a current trace, which needs no attach.
- **`tag-info` output contains binary.** Piped straight into `grep`, it gives
  `binary file matches` and an empty parse. Use `strings` first, or `grep -a`.
- **Check the configuration from the data, not from the host's echo.**
  `tag-start` prints the configuration it requested, not the one that was
  programmed. On PresTag a stale stored configuration has silently survived a
  reset-and-start (see the PresTag [TODO](../../embedded/tags/families/PresTag/TODO.md)).

## 4. Taking the measurement

- **Measure after an attach and a clean detach, not from a cold boot.** On the
  L432 a debug-domain register that outlived the debug session once left every
  terminal state about 1000x high, and only after a debugger had attached. A
  cold-boot number cannot see that class of fault. Every tool here attaches, so
  its numbers are already post-attach numbers. A true never-attached baseline
  needs all power removed and no probe ever connected. History:
  [CompassTag standby-after-attach](../../embedded/tags/families/CompassTag/design/investigations/2026-09-compasstag-standby-after-attach.md).
- **Take `charge/time`, never the window mean.** Tag loads are strongly
  duty-cycled. A PresTag at 90 s runs about 1 in 10,000, so a mean of
  per-window statistics is set by whichever windows happened to hold a pulse.
  Only the charge integral means anything.
- **Leave the Joulescope auto-ranging.** A range that resolves a few hundred nA
  clips a milliamp wake, and a range that captures the wake cannot see the
  floor. `joulescope_measure.py` leaves the range as it finds it unless given
  `--set-range`, so check that it is on auto rather than assuming. Auto-range
  transitions can distort the shape of a trace while conserving its charge, so
  trust the charge, not the per-phase attribution of a tail.
- **Make each window a whole number of the load's repeating unit.** That unit
  is the minute alarm for a once-a-minute waker, and 60 samples for PresTag
  (its header cadence). A window that is not a whole number of units contains
  N or N+1 events depending on where it opened. The target's plan states the
  unit.
- **Take two or more windows per point, and require them to agree.** One window
  hides both a still-attached monitor and a tag waking on a period you did not
  expect. When bisecting a flaky fault, use three or four trials per point:
  single-point bisects on this bench have produced confident, wrong
  conclusions.
- **Be as suspicious of a surprisingly low reading as of a high one.** Tens of
  nA more likely means the sense path is not carrying the tag's current than
  that the tag is extraordinary.
- **On the L432, measure Stop depth only after a power-on reset, using only the
  host tools.** A debugger session leaves the debug port powered until a POR or
  a completed Shutdown. With it up, Stop modes keep the system domain alive and
  read as Sleep-level current. `STM32_Programmer_CLI` hotplug sessions do this,
  and the CLI itself has wedged after repeated use until a USB reset. Take no
  debugger reads until the measurement is done. Shutdown and Standby resting
  numbers are not affected.
- **Classify sleep from averages of 1 ms or longer, never per sample.** See
  [debugging a tag](debugging-a-tag.md).

To measure one state directly:

```sh
build-host/bin/tag-reset --set-rtc               # -> IDLE, clock set
embedded/tools/joulescope_measure.py --use-server --duration 45 --repeat 2
```

## 5. Supply voltage

**Say what voltage you measured at, every time.** How current depends on
voltage is set by the board's regulator:

- **Buck converter (SMPS)**, as on the shipping IMUTagNandBmp581 board: the
  converter draws roughly constant *power*, so input current scales inversely
  with supply voltage. A figure moves between supplies by the voltage ratio.
- **LDO**: input current does not depend on voltage. The IMUTag LDO build was
  left behind by the SMPS version, so its figures do not bound the SMPS board.
- **No regulator**, as on BitTag: the parts run straight off the cell, no
  scaling applies, and a number holds only at the voltage it was taken at.

The release check's run-current bound is the worked example. It was 850 uA
against a healthy 750 uA at the ~3.29 V bench supply. It was then rebased by
the voltage ratio to **760 uA** against a healthy ~665 uA at a 3.7 V cell. The
evidence is in [IMUTag power](../../embedded/tags/families/IMUTag/design/power.md).
If you move the bench supply without rebasing the bound, the bound either
catches nothing or fails good builds.

## 6. The qualification tools

All are in `embedded/tools/`. Pass `--use-server` to every tool that offers it;
check `--help`.

| Tool | Use it for | Notes |
| --- | --- | --- |
| `tag_lifecycle_check.py` | The default after any firmware change: walks idle (clock set) -> running -> stopped -> idle again and measures every resting state | Compares idle before the run with idle after it: the same state reached by two histories, which is where a state-dependent fault shows. `--idle-max-ua` defaults to 100 uA, a "did it sleep at all" bound for IMUTag. Set a target's own bound from its plan. `--settle` defaults to 12 s. It has no attach retry, so it cannot drive a BitTag yet |
| `tag_attach_storm.py` | Repeated reset-and-set-clock cycles and attach/detach storms against a running tag. Checks that the run survives and records usable data | Measures no power, so it complements the life-cycle check rather than replacing it. Pass `--keep-download <dir>`, or each round's database goes to a temporary file that is deleted. Pass `--stop-on-failure` to keep a failed tag for [capture](../../embedded/tools/tag_capture_state.py) |
| `tag_release_check.py` | Qualifying an image: builds (unless `--skip-build`), flashes, measures idle four times, walks the life cycle, runs three attach-storm sets, and keeps every log, database and the ELF in a timestamped directory with one pass/fail | Records whether the tree was dirty. Defaults are IMUTag's: `--target IMUTagNandBmp581`, `imutag-400.json`, `--run-max-ua 760`. Procedure: [release procedure](../release/release-procedure.md) |
| `power_experiment.py` | One configured run, measured, then downloaded and checked | `--settle` defaults to 5 s, sized for a tag that starts collecting at once. Its rate check reads `lsm6.odr`, so it switches itself off for any tag without one, and only the monotonic-timestamp check runs |
| `joulescope_measure.py` | A single state, by hand | `--repeat` for agreeing windows. `--use-server` always |
| `joulescope_server.py` | Holding the instrument for a session | Section 2 |

Why each of these exists:

- **Measure every resting state, not one.** A power sweep measures the run,
  which is the state where a fault is hardest to see. An idle regression that
  left a tag at 1036 uA hid inside a genuine 400 Hz run current of about
  970 uA and survived a full sweep. The sweep also reset the tag without
  setting the clock, so the tag never entered the state that was broken. The
  life-cycle check measures idle with the clock set, because that is how a
  prepared tag is actually left.
- **Bound run current, not just idle.** Run current sets battery life during a
  deployment. On IMUTag it has twice moved by about 200 uA between builds that
  differed only in code layout. Four consecutive release checks recorded such a
  regression and passed, because only the resting states were bounded.
- **Qualify on hardware before shipping, every time.** A change to the
  state-machine path once shipped a 240x idle regression to main. A clean
  build, a hardware feature test and a full attach storm all passed it; only an
  idle measurement caught it, days late. Later the same day's tree slept in
  IDLE and stalled in FINISHED, and only the life-cycle walk saw that.

## 7. Recording a session

Results go in the target's `power-results.md`, which is append-only. A
measurement that turns out to be wrong gets a later entry saying so; it is
never edited out. Record for every session:

- the release tag, commit and image SHA-256, read from the build manifest for
  a release rather than from notes. For a development build, record the git
  hash and **whether the tree was dirty**: a dirty-tree result is not
  reproducible, so say so rather than leaving the field blank;
- the exact target built. Sibling targets share code but are distinct images,
  and nothing checks that the board matches the target;
- the board label and the UUID that `tag-info` or `tag-test` reports. One
  board is not a spread, so do not generalise from one UUID;
- the measured supply voltage;
- the config used;
- the interpreter used for the Joulescope, and confirmation that the server
  was used, not the desktop app or the MCP server;
- confirmation that qtmonitor and the desktop app were detached;
- the directory holding the session's own evidence: logs, databases, the
  flashed manifest. The write-up points at it; it does not replace it.

A release qualification also goes onto the release page. See *Publishing the
qualification* in [the release procedure](../release/release-procedure.md).
