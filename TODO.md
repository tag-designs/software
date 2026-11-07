---
type: worklist
status: current
summary: "The repository's one list of open work, by area -- host (applications, command line, libraries), embedded (tags, bases, loaders) and repository-wide (build, shared contracts, licensing)."
---

# TODO

All open work in this repository, in one list. It follows the tree: **Host**,
**Embedded**, then **Repository** for what no single directory owns. Each
section names the directory it covers. Add an item to the section that owns
the code; delete it when it is done, never strike it through.

## Host

### Applications

#### sensorViz

Directory: [`host/applications/sensorviz/`](host/applications/sensorviz/).

Current behavior and its known limitations are in [README.md](host/applications/sensorviz/README.md).

##### Near term

- If plots with several shared axes still read as crowded, consider one
  stacked band per group (a QCustomPlot axis rect each, sharing the time axis,
  with cursors and markers spanning all bands), with fixed x/y/z colours in
  every band. The group membership and legend in `PlotSpec` carry over.
- The first right-hand Auto axis reuses QCustomPlot's `yAxis2`, whose range
  carries over from earlier rebuilds, so pressure shown alone on the right
  can start at 0. Decide whether Auto should rescale from the data alone.
- Keep validating CompassTag and IMUTag derived streams against real logs and
  against specialized/reference tooling where available.
- Warn the user when a load succeeds but an expected stream or derived stream is
  skipped, for example when CompassTag data lacks calibration metadata needed
  for heading/orientation streams.
- Add new stream display defaults to `defaultDisplayForStream()` as new tag
  streams appear.
- Improve transform-specific configuration structure if more transforms are
  added.
- Consider a small shared helper for building mirrored menu-bar/context-menu
  sections if more actions are added.
- Decide whether metadata-box position should remain session-only or become a
  saved preference.
- Add richer IMUTag display transforms once the desired analysis views are
  clearer.
- Implement the dialog screenshot suite. The blocking dialogs in
  `stream_actions.cpp`, `controls.cpp`, and `transforms.cpp` first need a
  refactor into reusable dialog builders so they can be shown modelessly,
  captured, and closed without user input.
- Support files processed by `dataprocessing`; the open decisions are in
  [the command-line TODO](#sensorviz-handling-of-processed-files).

##### Larger refactors

- What a non-Qt UI needs is in `sensorviz_core`, behind `Session`
  (toolkit-neutral core, phases A-F). Still in the Qt layer, by design or
  until the Qt app retires: menus and dialogs, the `PlotView` renderer, the
  QML compass panel (and its nearest-sample lookup), and the activity filter,
  which is not ported.
- Generalize record-set transforms before adding more multi-column sensor
  families.
- Add optional session/project files if users need to persist graph title,
  ranges, sea-level pressure, declination, metadata-box position, or other
  current-view state.
- Add light GUI regression tests around menu enable/visibility rules and
  preference JSON round trips.

#### Tag View

Directory: [`host/applications/sensorviz-rs/`](host/applications/sensorviz-rs/).

Current behaviour and how it differs from the Qt sensorViz are in
[README.md](host/applications/sensorviz-rs/README.md).

##### Not in v4.0

- **The activity filter.** The Qt sensorViz's **Configuration ▸ Activity
  Filter** was left in its Qt layer when the core was made toolkit-neutral
  ([proposal](host/applications/sensorviz/design/proposals/toolkit-neutral-core.md), revised
  2026-10-28), so Tag View does not have it. v4.0 ships Tag View alone and
  releases without it (decided 2026-10-31,
  [release plan](host/design/proposals/rust-default-release.md)). If it is
  wanted back, port it into the core as a transform in the registry, with
  its time constant as a declared parameter, and check it against the Qt
  app with `session_cli`, which the legacy build (`BUILD_LEGACY_HOST`)
  still provides.

### Command line

Directory: [`host/commandline/`](host/commandline/).

#### dataprocessing

Design: [design/dataprocessing.md](host/commandline/design/dataprocessing.md).

##### Tests

There are none yet. Unit tests:

- Compass calibration application matches existing SensorViz/qtcalibrate
  expectations.
- Compass orientation output is deterministic for a small fixture.
- Downstream heading conversion examples match SensorViz and qtcalibrate
  display conventions.
- Missing calibration and malformed record sets fail with actionable errors.

Fixture tests:

- Use `host/docs/fixtures/sensorviz/compasstag.db3` as an initial CompassTag
  input.
- Confirm the input file hash does not change.
- Confirm output SQLite integrity check passes.
- Confirm output stream metadata references existing output tables and columns.
- Confirm rerun behavior for `--if-exists fail`, `replace`, and `keep`.

Manual review, once SensorViz support exists:

- Load the processed output in SensorViz.
- Compare processed orientation streams against current SensorViz live-derived
  streams for the same fixture.

##### Further processors

| Processor | Inputs | Outputs |
| --- | --- | --- |
| `altitude` | pressure stream, optional sensor temperature | altitude stream |
| `activity-filter` | activity stream | filtered activity stream |
| `imu-magnitudes` | IMUTag accelerometer/gyroscope/magnetometer axes | magnitude streams |
| `imu-kalman-attitude` | IMU accel/gyro/mag streams, timing metadata, filter config | attitude/orientation streams and filter diagnostics |

##### IMUTag orientation post-processing

**Blocked on real data.** Filter design waits for logs collected on the
prototype tags now at fab; synthetic logs cannot show real motion, mag timing
or disturbance.

A Python prototype now exists in tagsim (`tagsim filter`,
[design](host/python/tagsim/design/navigation-filter.md)): a multi-rate
error-state EKF over attitude, gyro bias, relative altitude and vertical
velocity, with an RTS smoother, scored on simulated pattern flights in
[compass-baseline-results.md](host/python/tagsim/design/compass-baseline-results.md).
Its simulated gyro has no scale-factor or misalignment error, so it does not
lift the block below.

This needs a design before more processors are added. A download is the whole
deployment, so orientation can be estimated offline with a forward EKF over
the 400 Hz accel and gyro, corrected by the 40 Hz magnetometer, and a backward
(RTS) smoothing pass. That would replace `imu-compass-orientation`'s
single-sample eCompass.

**Rule until then (2026-10-21):**

- SensorViz computes orientation live only from a true eCompass record set:
  CompassTag `compass_raw`, where each row is a simultaneous accelerometer and
  magnetometer reading.
- SensorViz never pairs IMUTag `ImuAccel` with `ImuMag` itself. It shows
  IMUTag orientation only from a precomputed `imu-compass-orientation`
  output.
- That processor stays as it is.

Findings from 2026-10-21, measured on a synthetic full 2 Gbit log of 19.66M IMU
and 1.97M mag samples:

- **The superframe mean is a decimation step, not a gravity estimate.** A
  10-sample boxcar at 400 Hz is -3 dB near 18 Hz, so 0.5-10 Hz animal motion
  passes almost unchanged. 50 mg of dynamic acceleration tilts "down" by about
  3 degrees, roughly 6 degrees of heading at 65 degrees of dip.
  [design/dataprocessing.md](host/commandline/design/dataprocessing.md#pairing-imutag-samples)
  overstates the mean as suppressing motion.
- **Magnetometer timing is not logged.** The BMM350 free-runs at 50 Hz and is
  read after each IMU block (`embedded/tags/families/IMUTag/src/sensors.c`). Its
  sample age therefore varies by up to 20 ms against the 40 Hz superframe. A
  filter needs a model of that age, or firmware that logs it.
- **Costs.**
  - `solveOrientation()`: 134 ns per pair.
  - Reading a stored `ImuCompassOrientation` row: 136 ns. The table adds
    170 MB.
  - Reading raw rows: `ImuMag` 82 ns/row, `ImuAccel` 122 ns/row (2.4 s for the
    full log).
  - A full-rate filter must read both 19.7M-row tables, about 4.8 s, and run
    sequentially.
  - So a stored, provenance-tagged filter output is worth keeping, even though
    a stored single-sample eCompass result is not.

Open decisions:

- Filter: forward EKF plus RTS smoother, or a complementary filter. Choose the
  state (attitude, gyro bias, possibly mag disturbance) and how its
  configuration is recorded in `ProcessingRun`.
- Mag timing: model the unknown sample age, or have firmware log it.
- Whether the writer should store a superframe-mean accel beside each `ImuMag`
  row. This is on hold; it mainly serves the single-sample method and a 40 Hz
  overview stream.
- Where the filter lives: `sensoranalysis` core, Qt-free, with the same
  bit-compatibility rules as the compass core.
- What SensorViz shows: stored filter output only, or also a live
  single-sample eCompass for comparison. See
  [SensorViz handling of processed files](#sensorviz-handling-of-processed-files).
- Heading conversion (declination, mounting) stays downstream, per
  [decision 0030](docs/decisions/0030-host-orientation-stored-as-quaternion-only.md).

##### Shared math

- Promote any remaining CompassTag orientation helpers from SensorViz into
  `sensoranalysis` when they are algorithmic rather than presentational. (The
  calibration-constant conversions already live there:
  `CompassCalibration::fromMagnetometerJson()` and
  `fromMagnetometerConstants()`.)

##### SensorViz handling of processed files

Plan this once the output database contract is stable. SensorViz should load
materialized processed streams through normal stream metadata. Open decisions:

- SensorViz now prefers a processed file's stored orientation quaternions over
  re-deriving them from raw CompassTag rows. Should it offer the live
  derivation as well, for comparing a processed file against current code?
- Should it show both raw/live-derived and processed streams, with labels that
  make provenance clear?
- Should SensorViz expose processing configuration from `ProcessingRun` in the
  File Info tab?

Until these are answered, DataProcessing keeps raw streams untouched and uses
stable processed stream ids that SensorViz can display as ordinary data.

### Libraries

#### tagcore

Directory: [`host/libraries/tagcore/`](host/libraries/tagcore/).

##### Verify the monitor side channel on hardware

Phase 1 of the [tag server proposal](host/libraries/tagcore/design/proposals/tag-server.md) added, in
[`tagmonitor.h`](host/libraries/tagcore/tagmonitor.h) and [`tagclass.h`](host/libraries/tagcore/tagclass.h):

- one recursive lock over every SWD link call in `TagMonitor`;
- `ReadMem` and `WriteMem`, exposed as `Tag::ReadMemory` and `Tag::WriteMemory`,
  returning `MemAccess`;
- `KeepAlive`, which writes `host_activity` on U3 and sends `get_status` on L4;
- `Tag::LastError`.

`tag-peek` now uses `Tag::ReadMemory`.
[`tag-side-channel-check`](host/commandline/tag-side-channel-check.cc) runs
the read-only items below and prints PASS or FAIL for each. Use
`--idle <s> --interval <s>` for the keepalive check, and add `--no-keepalive`
for its control run. Three opt-in options change tag memory or state and need
the operator's agreement: `--write-test <addr>`, `--start-running` and
`--unplug <s>`.

Passed on 2026-10-20 on a BitTag V6 (STM32L4, ID code `0x10016435`):

- ID code and 4 KB reads while running;
- the range checks;
- 827 `GetStatus` calls and 374 reads interleaved from two threads with no
  failures;
- `LastError` clean after a successful RPC;
- reads refused after `Detach`;
- `tag-info`;
- the L4 keepalive: 90 s idle with `KeepAlive` every 20 s kept the session,
  and the control run lost it.

Passed on 2026-10-20 on a BitTag V6 (STM32L4, UUID `2035374D303150190059002F`,
BitTag at `56e5e6a0`, through a TagBase v7):

- a `WriteMemory` of two patterns to `0x20000814`, the padding between `.data`
  and `.bss`, read back correctly, and the original was restored. The map came
  from a rebuild of `56e5e6a0` whose image matched the tag's flash word for
  word. The word's contents drift between sessions, since SRAM1 is not
  retained in Standby;
- `tag-start`, `Start` on the running tag refused with `err == PERM` and
  `transport_failed == false`, `tag-stop`, `tag-dwnld` with a dump identical
  to `main`'s, and `tag-reset`;
- attaching after a timed-out session: 3 of 3 clean on the branch and 1 of 1
  on `main`, so the noisy detection after a timeout did not reproduce;
- unplugging the base during a `GetStatus` loop: after 14 good calls,
  `transport_failed == true` and `err == UNSPECIFIED`;
- `qtmonitor`, checked by the operator. BitTag has no calibration, so
  `qtcalibrate` does not apply.

Passed on 2026-10-20 on an IMUTag breakout (STM32U3, ID code `0x10016454`,
UUID `00303143433650090059002E`, IMUTagNandBmp581 at `8f1350da`, 3.3 V):

- every read-only check, including `monitor_shared` writes refused with
  `Protected`, and 1720 `GetStatus` calls and 291 reads from two threads with
  no failures;
- the U3 keepalive: 30 s idle with `KeepAlive` every 2 s kept the session
  (15 calls, none failed); the control lost it within 12 s;
- `tag-info`, including straight after a timed-out session, with no link
  errors on the next attach;
- a `WriteMemory` of two patterns to `0x20001070`, the alignment gap between
  `.data` and `.bss` in that build, read back correctly, and the original
  `0x90CC87D6` was restored;
- `tag-dwnld` of a FINISHED log to SQLite: 121 records, with a dump identical
  to one made by `main`'s `tag-dwnld`;
- `tag-reset` from FINISHED, `tag-start`, `tag-stop`, `tag-dwnld` (4 records)
  and `tag-reset` again, each reaching the expected state;
- `Start` on the RUNNING tag refused with `err == PERM`,
  `transport_failed == false` and "Monitor request not permitted in current
  tag state";
- unplugging the base during a `GetStatus` loop: after 27 good calls,
  `transport_failed == true` and `err == UNSPECIFIED`;
- `qtmonitor` attach, status updates, start and stop, and `qtcalibrate` attach
  and status, checked by the operator.

Still to check, then delete each item:

- **Noisy detach from a running U3 tag.** Detaching from a RUNNING tag logs a
  burst of `linkadapt.cc` `error return 81` read failures, 41 in one run. They
  come from the MONITORSTOP status poll in `CallU3`: once the firmware handles
  the stop it returns to low power, and each poll fails until the 100 ms
  timeout, which the code already treats as a successful detach. This is
  unchanged from `main`. Quieten it by ending the poll at the first failed
  read once the kick has been delivered, or by logging those reads at debug
  level.

##### Verify tag-server on hardware and other platforms

[`tag-server`](host/tagserver/README.md) and
[`tagclient`](host/python/tagclient/README.md) pass their hardware-free tests
on macOS arm64: errors, the debug switch, attaching to a missing base, TCP,
shutdown, and a second server on a busy socket.

Passed on 2026-10-20, all 22 tests including the hardware ones
(`TAG_SERVER_HARDWARE=1`), which read information, status, the state log and
memory and hold a 15 s idle session on the server's keepalive:

- on the L4 BitTag V6 (UUID `2035374D303150190059002F`, BitTag at `56e5e6a0`);
- on the U3 IMUTag breakout (ID code `0x10016454`, UUID
  `00303143433650090059002E`, IMUTagNandBmp581 at `8f1350da`), where a
  `write_u32` to `0x20000010` through the server was also refused with
  `PERMISSION_DENIED` and the session survived.

On both, the server detached and removed its socket on `SIGTERM`.

Also passed on 2026-10-20 on the same U3 IMUTag, through the server, with the
operator's agreement:

- `set_rtc` and `start` from IDLE to RUNNING;
- a second `start` refused with `FAILED_PRECONDITION`, `ack_err_name ==
  "PERM"`, `transport_failed == False` and "Monitor request not permitted in
  current tag state";
- `stop` after 30 s, reaching FINISHED;
- a download of all 17 pages through `get_data_log`, advancing the index by
  one per raw page as `tag-dwnld` does. `tag-dwnld` also found 17 records, and
  each page's epoch, subsecond ticks and temperature matched its `ImuHeader`
  rows;
- `erase` back to IDLE;
- `write_u32` of two patterns to `0x20001070` read back correctly, and the
  original `0x90CC97D6` was restored.

The [Python examples](host/python/tagclient/examples/README.md) then ran a
full cycle on the same tag, with the server holding it (`--attach`):

- `tag_info` (text and `--json`), and `tag_peek` of the ID code and
  `monitor_shared`;
- `tag_start --set-rtc --start-now`, then a second `tag_start` that skipped;
- `watch_status`;
- `tag_stop`;
- `tag_dwnld`: its 205 pages matched `tag-dwnld`'s 205 records header for
  header;
- `tag_reset`, back to IDLE.

Passed on 2026-10-20 on **Linux x86_64** (Ubuntu, GCC 15.2.0, CMake 4.2.3,
`linux-vcpkg` preset, Python 3.14.4 with grpcio 1.84.0 and protobuf 7.36.2),
following the [how-to](host/tagserver/README.md#building-and-testing): the
build is clean apart from 29 warnings inside the fetched libuv; 16 tests pass
without a tag, and all 22 with `TAG_SERVER_HARDWARE=1` on the L4 CompassTag
(UUID `203633324B4250060022005E`, CompassTagAT25 at `169cda2c`). The Python
examples then ran a full cycle on it, each attaching and detaching itself:
`tag_peek` of the ID code (`0x10016435`), `tag_start --set-rtc --start-now`,
120 s untouched, `tag_stop` to FINISHED, `tag_dwnld` of its one page (3
samples at 30 s), and `tag_reset --set-rtc` to IDLE. `tag_dwnld`'s progress
read `1/25`: CompassTag's `external_data_count` is its write cursor in 16-bit
words, not pages, so it is no use as the hint for that family.

Passed on 2026-10-20 on **Windows 11 x64** (Visual Studio 2026, MSVC 19.50,
CMake 4.2.3, `windows-vcpkg-static` preset with `x64-windows-static-md`,
Python 3.14.5 with grpcio 1.84.0 and protobuf 7.36.2), following the
[how-to](host/tagserver/README.md#building-and-testing), after three MSVC
workarounds for grpcxx v0.7.0 now in `tagserver/CMakeLists.txt` and `main.cc`:
15 tests pass without a tag, and 21 with `TAG_SERVER_HARDWARE=1` on the L4
BitTag V6 (UUID `2035374D303150190059002F`, BitTag at `56e5e6a0`), the busy
Unix socket test being skipped. `test_clean_shutdown` covers Ctrl-Break.

#### tagcore-rs

Directory: [`host/libraries/tagcore-rs/`](host/libraries/tagcore-rs/).

The design is [SWD Capture and Recovery Library](host/libraries/tagcore-rs/design/swd-recovery.md). The
capture, loader and SRAM-call code is Rust, in `src/recovery`; the offline
rebuild, `CaptureSource`, is still C++ in `host/libraries/tagcore/recovery`.
The unbuilt pieces (Python binding, identification without an identity record,
rescue) are in
[their proposal](host/libraries/tagcore/design/proposals/recovery-identification-rescue-python.md).
The capture additions still to build are D3 in
[Tag Firmware: Next Release TODO](#common):
- the RV3028 EEOffset for tags already deployed;
- internal-flash ECC faults.

##### Open work

- **A NAND rescue.** `rescue-erase` is NOR only. The IMUTag's GD5F has no
  writable loader, and a NAND erase must skip factory-marked bad blocks (and
  leave the firmware's bad-block map, which lives in internal flash, to be
  rebuilt by `tag-test`).
- **The GD5F logical block count (2008) is a constant in the IMUTag capture
  decoder.** Move it into the identity record's numbers, so the record carries
  every constant the decoder multiplies by.
- **A differential test for `data_logAck()` drift.** Compile the real
  `data_logAck()` against stubs, as `families/PresTag/test/datalog_sim.c`
  already does. Dump the simulated flash as a capture directory with the
  encoded Acks, and require `CaptureSource` to produce the same Acks byte for
  byte. Today a logic change that keeps every struct is caught only by
  `tag_rebuild_check.py run` on hardware.
- **SRAM erased by the attach.** If a tag's option bytes clear SRAM on system
  reset, no capture can recover its SRAM, because this rig must attach under
  reset. Setting the "not erased" options on every tag at provisioning would
  close this. That is a policy decision, still to be made.

#### tagprobe-rs

Directory: [`host/libraries/tagprobe-rs/`](host/libraries/tagprobe-rs/).

No open items.

The virgin STM32U375 boot option bytes, recorded here on 2026-10-31, are now
fixed by the tag firmware at its first start and need no host change; see
[Restart Recovery](embedded/tags/common/core/design/restart-recovery.md#stm32u375-boot-option-bytes).

## Embedded

### Tags

#### Common

Directory: [`embedded/tags/`](embedded/tags/).

##### Release follow-ups

Background:
[the ADC and power-floor investigation](embedded/tags/design/investigations/2026-10-adc-and-the-power-floor.md).

**Firmware changes go in as one batch (decided 2026-11-01).** Most items in
this worklist change the image of most tags, and every changed image needs
requalifying. Collect them -- including the TIM2 prescaler port, the stored
configuration's own page, the state-marker overflow and reset markers, PresTag
F2/F3 and the L4 monitor's remaining checks -- and qualify once, rather than
landing and measuring them one at a time. Host tool fixes are not held back.

- **Monitor version 0x21 for the mailbox images rides in the batch (decided
  2026-11-04).** IMUTag (`handlersU3.c`) and the L4 mailbox images publish
  `DEBUGVERSION_MAILBOX` (0x21); DebugMonitor images keep `DEBUGVERSION`
  (0x20) and are unchanged (see `docs/shared/monitor-interface.md`, Monitor
  versions). Hosts up to v4.0.1 refuse a 0x21 IMUTag, so the host release that
  accepts it must ship first.

- **STM32U375 boot option bytes ride in the batch (verified 2026-11-05).**
  Every U375 image sets `nSWBOOT0 = 0`, `nBOOT0 = 1` at its first start on a
  new board and resets once (`tagFixBootOptionBytes()` in
  `common/core/src/main.c`; see
  [Restart Recovery](embedded/tags/common/core/design/restart-recovery.md#stm32u375-boot-option-bytes)).
  Qualification should include one never-flashed board.

- **Give the stored configuration its own page (part 2).** Part 1 of
  [the proposal](embedded/tags/design/proposals/stored-config-write-is-unchecked.md) landed
  in `dee8654d` and shipped in `fw-v0.6`: every target reads the write back with
  `storedConfigVerify()` and aborts the start on a mismatch. That makes a
  refused write loud; it does not make it succeed, because `sconfig` shares its
  page and cannot be erased first. Part 2 places it on its own page and erases
  before programming, as IMUTag does. It is a linker change per target and needs
  measuring afterwards.
- **Explain the 11% gap** between PresTag's fitted `I_rest` (0.1122 µA) and its
  measured `IDLE` (0.1261 µA). September had the two within 1.1%.

Open work only. Delete an item when it is done; put what was learned in an
investigation or decision record. Items marked *found by reading code* have
not been reproduced on a tag.

##### Defects

- **An L4 session that times out leaves DMA1's clock on; it costs nothing
  measurable (low priority).** The host sets `RCC_AHB1ENR.DMA1EN` for every L4
  session, after `MONITORSTART` on the DebugMonitor and before the request on
  the mailbox (it must, or a request can be dropped while the tag sleeps), and
  clears it only in its own detach. Neither `handlersL4.c` nor
  `handlersL4Mailbox.c` clears a bit it did not set, so a session that ends on
  the firmware's watchdog or heartbeat (the host died, was unplugged, or went
  quiet) leaves it on.
  - **Why it is free:** the L432 tags rest and run in Standby or Shutdown,
    and every wake from either resets RCC; in Stop the bus clocks are gated
    regardless of the enable bit. It only costs while the tag sleeps with its
    clocks running, which is the awake time between a session ending and the
    next Standby.
  - **Measured 2026-11-04, 2.496 V:** UIUCTag mailbox, 15 s idle session:
    idle 0.1543 uA afterwards. CompassTag `eb165461`, DebugMonitor: `DMA1EN`
    read set (`0x101`) through a quiet session, the watchdog ended it
    between 45 and 60 s, no detach, and the tag rested at 0.219 uA against
    0.2206 uA before.
  - **If it is ever fixed**, the firmware can take the clock on connect
    whenever none of its own code holds a DMA1 stream, and clear it on any
    disconnect.

- **On an L4 tag, the first request after an attach waits for an erase to
  finish.** Seen on CompassTagAT25 (2026-11-03, 2.50 V, branch
  `sync-monitor-commands`): attach to a tag in sRESET, and a stop or erase is
  answered only after the sweep ends, 6.6 s later, although within a session
  the sweep yields to a request after one sector. Status reads are answered at
  once, from the cached status. On IMUTag (U3) the same request was answered
  in 0.23 s. The reply is still correct (PERM, then IDLE), so an erase sent in
  a new session to an erasing L4 tag never continues it; it finds the erase
  done. Not yet explained; the sweep's event check, the L4 attach sequence and
  the boot's first pass are the places to look.

- **The L4 DebugMonitor handler can clear the MON_REQ of a request still
  pending.** Step 1 of
  [l4-monitor-robustness.md](embedded/tags/common/core/design/proposals/l4-monitor-robustness.md)
  is on branch `bittag-prestag-fixes`: a request that arrives while one is in
  flight leaves `MON_REQ` alone, and a plain `get_status` while the main thread
  is in its guarded region is answered from cached state in the handler.
  On 2026-11-01 BitTag and PresTag passed `tag-test` with resting current
  unchanged, and a PresTag Tag Monitor session stayed live through Start, Stop,
  Erase and Synchronize Clock. On 2026-11-02 the UIUCTag breakout (main
  `12af820d`, on
  `tag-breakout-base-l432-v1` (fw `be8f9062`, a dirty build of 2026-10-28)) passed `tag-test` six times out of
  six with no 0x81 burst, and Tag Monitor's `session_check` passed every step
  with a clean log; resting current 0.1507 µA against 0.1521 µA before
  ([UIUCTag power results](embedded/tags/UIUCTag/design/power-results.md)). Still to verify,
  per the proposal: `tag-test` on CompassTag. Step 2 (the mailbox) waits on
  whether bursts remain; none have been seen since.
  Observed 2026-10-27 on a UIUCTag breakout (fw `7c508f46` plus the
  session-clock fix), reading `common/core/src/handlersL4.c` and `monitor.c`
  after bursts of base 0x81 errors during Synchronize Clock and Self-Test:
  - **The handler.** `DebugMon_Handler` latches a PROTOBUF request
    (`monitor_pending`, DCRDR = 0) and leaves MON_REQ set until the main
    thread runs it in `monitorServicePending()` at a cooperative point. A
    second PROTOBUF that arrives while one is pending takes the `else`
    branch, `monitorClearRequest()`, and clears the pending request's
    MON_REQ. The host then takes that request as done and reads a reply
    nobody wrote, and `monitorServicePending()` later finds `monitor_pending`
    with MON_REQ clear and drops the request. The clear is right when no
    session is open; while a request is pending it should leave MON_REQ
    alone.
  - **Why a request stays pending.** Synchronize Clock runs
    `SetTimeUnixSec()` inside `proto_eval()` in the main thread: it sets the
    internal RTC, re-initialises it (`tagRtcInit()`), applies the clock
    correction, and writes the RV3028 over I2C, all while the host waits for
    MON_REQ to clear. Self-Test's tests run in the main thread straight after
    the request is accepted.
  - **The 0x81s themselves.** `tag-test` shows the host's DEMCR reads failing
    at the SWD level (base 0x81) for most of the time the main thread spends
    on those operations: 172 failed reads inside one call's wait, about 330 in
    the run. The handler race does not explain an access failing outright.
    **Explained and fixed (2026-10-27):** `DBGMCU_CR.DBG_SLEEP` was clear,
    so the idle thread's Sleep-mode WFI stopped HCLK and held off every SWD
    access while the main thread waited on I2C or a test delay, past the
    base's 25 WAIT retries. The host now sets `DBG_SLEEP` after
    `MONITORSTART` (tagcore, both libraries); see
    `embedded/bases/design/investigations/2026-10-0x81-dbg-sleep.md`. The
    handler race above is separate and still open.
  - **An empty reply during a self-test.** With the host change below in
    place, the Rust `tag-test` (ALL_PASSED) still logged one `GetStatus
    failed: Tag returned an empty or invalid acknowledgement`: a call that
    completed with DCRDR = 0. Possible causes, not yet told apart:
    - the handler race above;
    - a DEMCR read inside a 0x81 burst that came back without MON_REQ;
    - `monitorServicePending()` answering 0 because `monitor_enabled` was
      false.
  - **Host side, done:** tagcore's `TagMonitor::Rpc()` now waits for an L4
    tag's MON_REQ and MON_PEND to clear before writing the request buffer
    (`WaitL4MonitorIdle()`, and `wait_l4_idle()` in tagcore-rs),
    so a request a failed call left pending is no longer overwritten; qtmonitor pauses its polls for 1.5 s after
    Synchronize Clock and Self-Test.
- **The download clock switch's TIM2 prescaler writes do nothing** in the
  BitPresTag, UIUCTag and CompassTag `datalog.c`. `fast_msi()`/`slow_msi()`
  raise MSI 12x around one page of `data_logAck()` and write
  `STM32_ST_TIM->PSC` to compensate. The prescaler is buffered until an update
  event, which ChibiOS's free-running 32-bit TIM2 does not reach, so the tick
  runs 12x fast for each page: every RTOS sleep in that window, the external
  flash's wake delay included, is cut to a twelfth. The same pattern in the
  monitor session made the 60 s watchdog fire after 5 s on UIUCTag (removed
  2026-10-26, see `docs/shared/monitor-interface.md`). PresTag is fixed on
  branch `bittag-prestag-fixes` by `st_load_prescaler()` in
  `families/PresTag/src/datalog.c`, which forces the update and restores the
  counter, and a PresTag download was identical to the previous image's on
  hardware (2026-11-01, [results](embedded/tags/families/PresTag/design/power-results.md)).
  Port it to the other three.
- **The state-marker log can fill, silently.** `recordState()` stops
  writing once `sEpoch[]` is full (24 slots on L4, 20 on U3), with no record
  outside a scratchpad, and reset recovery then restores a stale state after a
  power loss. Brown-out restarts and repeated erase requests write markers
  with no limit. Faults and power loss while running already end the run with
  an ABORTED marker; a fault during hibernation leaves none. Proposal (closing
  markers always fit, the schedule is reserved, unscheduled markers use the
  spare, the first refusal writes an overflow marker):
  [A budget for the state-marker log](embedded/tags/common/core/design/proposals/state-marker-budget.md).
- **BitPresTag's reattach path is unchecked.** An L432 reattach that lands
  while the tag is awake boots as a power init, `deviceInit()` turns the
  ticker, alarms and sensors off, and recovery resumes with
  `Running(T_CONT, POWERFAIL)`. PresTag (a406eda7), UIUCTag (4458a749),
  CompassTag and BitTag (2026-11-04) re-arm there, and the common
  `Hibernating()` re-arms its hourly alarm on the same reason. BitPresTag is
  largely abandoned in favour of UIUCTag and was not checked. None of the
  fixes reproduced the fault on the bench for CompassTag or BitTag, which sleep
  at once after a start; see their power results.
- **The `debug_log` module prevents low-power entry on STM32U375** (1.71 mA;
  low priority).
  This is a fault in the module itself, separate from the flash-flag and layout
  faults; see the warning in `IMUTagNandBmp581/project.mk`.
- **IMUTagNandBmp581 RTC sync.** `tag-start --set-rtc` intermittently failed
  with "RTC sync failed while writing tag clock", and boots often reported
  `rtcInitializedAtBoot` and `clockTrusted` false. The I2C bus clear
  ([0005](docs/decisions/0005-i2c-clear-a-stuck-bus-at-session-start-boot-and-standby.md))
  removed the stuck-bus cause. A single failure in 30 was still seen on
  2026-09-07 ([0007](docs/decisions/0007-u375-run-sleep-is-stop-2.md)).
  Confirm whether a residual RV-3028 write fault remains.
  **Not reproduced on 2026-11-07:** 1000 consecutive `tag-reset --set-rtc`
  cycles on an IDLE tag (UUID `00303143433650090059002E`, `fw` `3a49003b`,
  3.6931 V, 1 s apart) all synchronized, and the clock read at each attach
  was within 0.1 s of the host's. At the old rate of about 1 in 120, 0 in 1000
  has a probability near 0.0002, so the fault is gone from this image or
  needs conditions the loop lacks: a set right after a run, an erase or a
  Stop 2 wake. Idle current 6.73 µA before, 6.88 µA after. Close the item
  unless a gate run sees it again.

##### STM32U375 low power

- **Open question: which monitor predicate guards the terminal sleep.**
  `tagPowerEnterStop3()` refuses only while `monitorIsAttached()`. The retired
  Standby path, the idle hook and the RUNNING wait use the broader
  `isMonitorEnabled()`, which on U3 also covers `monitorAttachGraceActive()`.
  During an attach grace the tag can therefore enter Stop 3 but not returned
  STOP. Decide whether that is intended
  ([STM32U375 Low Power](embedded/tags/common/core/design/u375-low-power.md#monitor-guards)).
- **Move the flash error-flag clear to where errors occur.**
  `tagPowerClearFlashErrorFlags()` runs before every terminal sleep and clears
  any latched error without reporting it. Clear and report the flags at the
  datalog flash operation that latched them, where the error can be attributed
  to an access. Measure idle before and after: this touches the power path.
- **Scratchpad retention through Stop 3 is unverified.** `tagScratchRetain()`
  (`PWR_CR1_RRSB3`) is called only from `tagPowerEnterStandby()`, which is
  unused. Check whether the scratchpad survives the Stop 3 terminal sleep and
  the reset that follows it, and whether the retention bit is needed there.
- **Dead code in `pwr-u375.c`.** `tagPowerRestoreClocksAfterStop3()`,
  `tagPowerRestoreFlashAfterStop3()` and `tagPowerPostStop3WakeEventI()` are
  never called, and `TAG_STM32U3_STOP3_CLEAR_WAKE_FLAGS` is defined but never
  read. Remove them or document them as unused. This changes the image, so
  measure idle and run current after the change.
- **Standby bias for interrupt lines.** On both IMUTagNand boards `WKUP1`,
  `LSM_TRG`, `LPS_DRDY` and `BMM_INT` (and `LMS_TRIG_2` on v1) are left
  unbiased "while measuring whether interrupt pulldowns fight latched
  active-high outputs". The outcome was not recorded. Decide the bias and
  update `embedded/boards/IMUTagNandv*/cfg/board-customizations.json` and the
  boards' `standby-pins.md`.
- **LPTIM-backed system timer.** Not built; see
  [the proposal](embedded/tags/common/core/design/lptim-system-timer.md).
- **Validate `stopMilliseconds()` on L432 hardware**, if not already done:
  scope or log 1, 2, 10 and 100 ms delays and confirm none returns early, and
  confirm that monitor-attached behaviour uses `chThdSleepMilliseconds()`.

##### Restart recovery

- **Clean up the reset recovery code in `StateMachine()`** (user request,
  2026-11-02). The recovery branch at the top of `StateMachine()`
  (`common/core/src/state_machine.c`) has grown one bench fault at a time and
  is now most of the function. It has:
  - two parallel recovery paths, selected by `TAG_MONITOR_RESET_RECOVERY`;
  - four overlapping ways of deciding why the tag reset: `reattachReset()`,
    `externalResetAtBoot`, `backupStateValidAtBoot` and
    `monitor_reset_recovery`;
  - trace bookkeeping (`TAG_RECOVERY_TRACE`, about 50 references in the
    branch) and retained run diagnostics woven through the decisions.

  Reduce it to one path with one reset classification, and move tracing out
  of the decision logic. Write down the cases the path must handle first:
  standby wake, attach reset, field power loss, brownout, exception. Then
  check each against the code rather than preserving every branch. UIUCTag
  behaves differently from BitTag after an attach, and nothing in its
  `restoreLog()` explains why. The synchronous
  [monitor commands](embedded/tags/common/core/design/monitor-commands.md) removed one reason
  for the complexity: the main loop now serves no request before the recovery
  pass, so a request can no longer race it.
- **The monitor-attach recovery branch adopts retained state without
  cross-checking the marker log.** Nothing depends on that now, but if
  `pState->state` is ever wiped or corrupted, it will again outrank durable
  flash evidence.
- **Acquisition-phase sentinels** are not implemented. Add retained sentinels
  around sensor read, header write, external data write and cursor commit, so
  recovery can decide whether the final pre-reset page is complete, abandoned
  or should be hidden. Download timing would then combine `vddHeader` anchors,
  state markers for discontinuities, and the sentinel state.
- **Consider a full erase sweep** when a run did not finish with clearly
  recoverable boundaries.
- **Exercise checked flash reads on hardware.** Reset a tag repeatedly during
  internal header writes, and verify that recovery and download stop at the
  last checked-readable header instead of entering the exception path. If an
  ECC-faulted double-word can be produced on a bench unit, check that a
  guarded read returns an ECC error and an unguarded read still takes the
  ordinary exception path.
- **Host erase-progress fallback.** Once deployed firmware reliably reports
  `Status.erase_sectors_total_plus_one`, remove the IMUTag page-size fallback
  (`kImuDataLogPageBytes`) from `qtmon` and `qtprogram`, so they no longer
  need to know `DATALOG_SAMPLES * sizeof(t_DataLog)`.

##### IMUTagNand and IMUTagNandBmp581 NAND bring-up

- Investigate the GD5F2GM7RE `B9h` extreme-low-power command, and the wake
  sequence and timing needed before the shared NAND driver can use it
  (IMUTagNandBmp581).
- Exercise the NAND erase path with a provisioned map present.
- Confirm the tag refuses to start collection when the NAND map is absent.
- Test page-cache write behaviour across NAND page boundaries.
- Verify that normal reprogramming and erase flows preserve calibration, the
  NAND map, and the dedicated configuration pages.
- Test behaviour with simulated or forced factory bad-block markers.
- Handle ECC status on NAND page reads.
- Provide a "bad page" marker or equivalent read result, so host download can
  skip unreadable pages instead of failing the whole download.
- Decide where bad-page observations live after runtime reads: a transient RAM
  table, the debug log only, a persisted table, or appended download metadata.
- Add a way to inspect bad-page information during download, by dumping the
  table or through a monitor or debug command.
- Confirm downloader behaviour when sparse pages are skipped: sample count,
  timestamp continuity, and log metadata.
- Keep first-boot map provisioning read-only with respect to NAND factory
  markers.
- Confirm that logical-to-physical map validation rejects erased, unsorted or
  out-of-range entries.
- Confirm that erase always checks physical bad-block markers before issuing a
  NAND block erase.
- Decide whether runtime program or erase failures retire blocks, report an
  error only, or both.
- Keep qtmonitor line-buffering behaviour under longer debug streams.
- Consider quieting duplicate NAND ID logs once bring-up stabilizes.
- Add host-side checks that distinguish an absent map, a bad NAND ID, an
  unreadable page, and erased or no-data cases.

##### Offline reconstruction and release tooling

The fixes landed after `fw-v0.0.3`, and how each was verified, are in
[the next-release investigation](embedded/tags/design/investigations/2026-10-next-release-fixes-verified.md).
All five distributed targets passed qualification against the `fw-v0.6`
image on 2026-10-06/08; each target's results log has the figures.

###### Offline reconstruction layout

Specified in
[Offline Log Reconstruction, Decisions and plan](docs/investigations/2026-10-offline-log-reconstruction.md#decisions-and-plan)
and the records
[0021](docs/decisions/0021-offline-rebuild-tag-identity-record.md),
[0020](docs/decisions/0020-offline-rebuild-session-facts-in-stored-config.md)
and [0017](docs/decisions/0017-offline-rebuild-build-time-layout-descriptor.md).

- **Identity record (B1):** add the compiler version. The build-options digest
  does not cover the toolchain: an image built on another machine had the same
  digest as a local build of the same source, but was 460 bytes smaller, with
  every code address shifted. Also add the GD5F logical block count.
- **B1 on hardware:** families other than PresTag, CompassTagAT25, UIUCTag and
  IMUTagNandBmp581 are not yet checked; BitTag remains.
- **Session facts (B2), still to do:** the effective sample settings as values
  (sample period, ODR, ranges), and optionally the nanopb-encoded `Config`
  beside the raw struct, which removes the per-family `readConfig()` mapping
  from the offline path. Give new members fixed-width integer types (short-enum
  ABI), and check the larger struct still fits its page or region, especially
  on L4, where `sconfig` shares `.persistent`.
- **Build-time layout descriptor (B3).** Not built.
  - Compile a small C program for each target, with that target's flags and
    `-fshort-enums`. It emits a JSON descriptor: `sizeof` and member offsets
    for every stored struct, the same constants as B1, and the symbol
    addresses read from the linked ELF.
  - Install the descriptor beside the ELF and record its hash in the build
    manifest.
  - Copy the `.proto` files, or a `FileDescriptorSet`, into the package.
  - Generate B1's record and the descriptor from the same definitions.
  - Verify: for each fw-v0.0.3 target the descriptor agrees with the struct
    sizes and offsets established by hand in Offline Log Reconstruction, and a
    reproducible build regenerates it byte-identically.

###### Release qualification

- **Functional checks beyond the gate:** the CompassTag resume test, the
  full-flash download test, and a `tag-capture` of each target to confirm the
  identity record and session facts read back from the capture.
- **Make `tag_release_check.py` cover every tag.** It has profiles for
  IMUTag, BitTag, PresTag and UIUCTag: config, sleep bound, run floor and
  bound, run, rest and idle windows, settle and storms, from each target's
  qualified figures, and a sample-count check for each through
  `config_odr_hz()`. BitTag, PresTag (2026-11-01) and UIUCTag (2026-11-02)
  passed end to end with no hand steps
  ([BitTag](embedded/tags/BitTag/design/power-results.md),
  [PresTag](embedded/tags/families/PresTag/design/power-results.md),
  [UIUCTag](embedded/tags/UIUCTag/design/power-results.md)). Still to do:
  - BitTag's run bound rests on one board; a second ran 13% lower, inside it;
  - CompassTag: a config, a profile and a rate;
  - it checks neither calibration nor `tag-test`, so both are closed by hand.

###### Host counterparts

These ship separately but are needed for the firmware changes to be useful.

- **D1, offline rebuild:** decoders exist for all five distributed targets
  ([0018](docs/decisions/0018-offline-rebuild-capture-backed-source.md)).
  Still to do: the fw-v0.0.3 layouts, and the GD5F logical block count in the
  identity record.
- **D2:** the identity record is read at the per-MCU address and chooses the
  loader and decoder (tagcore-rs `recovery/identity.rs`, `mcu.rs`). Still to
  do: without the record, fall back to hash or string identification.
- **D3, capture additions:** the EEOffset over I2C for tags already deployed;
  NAND pages read through on-die ECC, with their status; internal-flash ECC
  faults.
- **D5:** decoders should read CompassTag `temp10` as signed for fw-v0.0.3
  offline decoding, and read the new `sconfig` members by layout version.

##### Cleanup

- **The start-time comment in `Configured()` describes only IMUTag**
  (`common/core/src/state_machine.c`, the `TAG_CONFIGURED_IMMEDIATE_START`
  block). It says `config_tmp.start = (start_delay - 1) * 60 + timestamp`, and
  argues from that that the immediate-start test is a relative comparison
  against the same clock and so need not depend on `clockTrusted`. Only IMUTag
  (`families/IMUTag/src/config.c`) computes the start that way. BitTag,
  BitTagNG, PresTag, BitPresTag, CompassTag and UIUCTag all take the absolute
  `config->active_interval.start_epoch`. For them the start is a wall-clock
  time, and the test does depend on the clock being right. Correct the comment,
  and decide whether the `clockTrusted` reasoning still holds for
  absolute-start tags. Related host point: `tag-start --start-now` only zeroes
  `start_delay`, so it changes nothing on those tags; they start at once only
  because their configs set `start_epoch` to 0. Found 2026-11-03, when a
  BitTag "delayed start" built on `start_delay` started immediately.
- **Move sensor orchestration out of `state_run.c`.** Some older tags still
  configure sensors directly from `state_run.c`, which crowds the acquisition
  state with device setup. The CompassTag family started a family-owned
  `sensors.c`; the name is historical and means "how this family uses its
  sensors", not a reusable driver. When touching an older `state_run.c`, move
  sensor configuration, orientation transforms, calibration handling and
  sensor wake/sleep flow into a tag- or family-owned orchestration file with a
  clearer name. Reusable drivers stay under `common/sensors`.
- **RTC descriptors.** The default RV3028 binding is still a weak board default
  in common code. Tags with non-default RTC wiring should move the complete
  `TagRtcDevice` descriptor into tag or family board support, so that one
  place owns the register bus and the board power callbacks. That also removes
  the remaining global `rtcOn()`/`rtcOff()` glue.
- **I2C backends:** build one mixed development target that puts the software
  and hardware backends on different buses.
- **BMM350 self-test enum.** The monitor test table maps the BMM350 to the
  legacy `RUN_MMC5633` request until the protobuf test enum gains a
  BMM350-specific request and result.

#### CompassTag

Directory: [`embedded/tags/families/CompassTag/`](embedded/tags/families/CompassTag/).

- ~~**Measure plain `CompassTag` (MX25R) after an attach.**~~ **Dropped
  2026-10-05: the MX25R path is abandoned.** It reproduced the
  Standby-after-attach fault on its own board and was never measured after the
  fix; `CompassTagAT25` was, on production hardware
  ([results](embedded/tags/families/CompassTag/design/power-results.md), 2026-09-24). They share `pwr-l432.c`
  with no override, so the fix applies to the MX25R image by construction --
  which was never a substitute for a measurement, and now does not need to be.
  **No MX25R measurement is owed.** The `CompassTag` target still builds; it
  is simply not a qualification target, and nothing should block on it.
- **Re-confirm the never-attached cold baseline after the fix.** Remove all
  power, including any cell, and measure before a probe ever touches the
  board. The 376 nA figure is the pre-fix cold measurement.
- **The ABORTED-on-first-boot fix (`379e3f1`) has not been recorded as verified
  on hardware.**
- **`isMonitorEnabled()`'s `MONCONNECTED` latch.** STM32L4 has no
  `TAG_MONITOR_MAILBOX`, and the latch bug noted in `handlers.c` is real. It
  was ruled out as the cause of the Standby fault, because a clean
  `MONITORSTOP` measurably did not fix the stuck current, but it is still open.
- **Host `TagMonitor::Call()` `MONITORSTOP` path.** It reports success after a
  blind 5 ms sleep instead of polling for completion, unlike every other
  operation. Changing it needs a validation cycle across every tag.

#### IMUTag

Directory: [`embedded/tags/families/IMUTag/`](embedded/tags/families/IMUTag/).

##### Power

- **Record the deployed cell voltage and chemistry.** It is not recorded in
  this tree. The current figures in [power.md](embedded/tags/families/IMUTag/design/power.md) and the
  release gate's `--run-max-ua` bound assume a 3.7 V cell; the SMPS board's
  input current scales with supply voltage, so a different cell moves both.
- **Check for sample-synchronous supply noise on the SMPS board.** The load is
  modulated at the sample rate: every sample event is a current burst. In
  Power-Save Mode the TPS62840's PFM pulse rate varies with load current, so its
  pulse timing can correlate with sampling and put rail ripple on every sample.
  Synchronous artifacts do not average out and land in the band of interest
  (for songbird flight, wingbeat fundamentals and their low harmonics). A DC
  field from the inductor calibrates out as a hard-iron offset; a supply
  artifact locked to the sample clock does not. Test: log on an LDO board and
  an SMPS board under matched conditions and compare BMM350 and LSM6DSV16X
  noise floors and spectra in sensorViz, looking for structure at and around
  the sample rate and its subharmonics.
- **Explain the idle split.** Idle on the same image and board fell into two
  tight populations, 6.7122 uA (2026-10-02) and 5.44-5.52 uA (2026-10-03),
  split by day rather than by procedure. Candidates: `FLASH_PWR` standby state
  (settle against the schematic, not by numeric match) or bench temperature
  (record it on the next idle measurement). Worth 17 days of shelf life on a
  12 mAh cell. Until then quote shelf life from the 6.71 uA population.
- **Explain why measured 3.7 V run current falls below the projection**,
  increasingly with load (-0.17% at 100 Hz to -0.74% at 1600 Hz), before the
  scaled figure is used at a rate that was not measured.
- **Measure `IMUTagNand` at 3.7 V.** Only `IMUTagNandBmp581` has been measured;
  `IMUTagNand` sleeps in Stop 1 during runs, not Stop 2.

##### Sample timing

- Report the applied STM32 `CALP/CALM` values with the download. Only the
  RV-3028 offset in ppm (`TagInfo.ppm_clock_error`) is reported now.
- Decide whether the `millis` field of `t_DataHeader` should be renamed in a
  future binary-format revision; it holds 1/1024 s ticks and flags, not
  milliseconds.

#### PresTag

Directory: [`embedded/tags/families/PresTag/`](embedded/tags/families/PresTag/).

- **F3: the config write can still be refused.** Part 1 of
  [the proposal](embedded/tags/design/proposals/stored-config-write-is-unchecked.md)
  landed in `dee8654d` (`fw-v0.6`): `writeStoredConfig()` (`src/config.c`)
  now reads the write back with `storedConfigVerify()` and the start aborts on
  a mismatch, so a stale `sconfig` is a loud refusal rather than a tag running
  9 s when `tag-start` printed `period: 10`. The write itself can still fail:
  `sconfig` shares its page, so it cannot be erased first. Part 2, its own
  page, is tracked in [the firmware worklist](#common). Still open here:
  `erasePersistent()` does not check that its erase happened.
- **F2: make `godown(STOP2)` sleep or refuse.** On the L432,
  `tagPowerEnterTerminalSleep()` handles only Standby and Shutdown and returns
  silently for anything else. So a period under 10 s, where `Running()` returns
  `STOP2`, never sleeps: it measured 530.7 µA flat. Only the bench is affected.
  Implement it, or reject it explicitly.
- **T4: brownout recovery with an odd page count.** This is the regression test
  for the log-cursor fix on the restart path
  ([investigation](embedded/tags/families/PresTag/design/investigations/2026-09-prestag-log-cursor-round-up.md)).
  It needs a genuine brownout. A probe reset takes a different path and would
  pass while the defect stood. Not run.
- **Re-measure HIBERNATING and repeat H3** since `060a566` made `ALARM_HOUR`
  hourly. The recorded 0.3769 µA and the five-wakes-in-295 s trace describe the
  once-a-minute alarm. Hibernation exit can now take up to an hour after the
  window closes. Check C5's exit expectation against that.
- **Compare absolute pressure against a local station reading.** Only
  plausibility and self-consistency have been shown so far.
- **State the cell's self-discharge figure** so the derated lifetime is not a
  placeholder. It used a 0.75 derating with no datasheet behind it.
- **Measure B1, B4 and B5** (1 s, 15 s and 30 s) if extra fit leverage is
  wanted. They are optional; the field fit already rests on three measured
  points.

#### UIUCTag

Directory: [`embedded/tags/UIUCTag/`](embedded/tags/UIUCTag/).

These come from sessions on the shared breakout board, UUID
`2036354B3032500800520028` -- the same physical unit the CompassTag log calls
`CompassTagAT25Breakout`. The "never wakes" report from the same sessions was
retracted; see
[the investigation](embedded/tags/UIUCTag/design/investigations/2026-10-uiuctag-wake-fault-retracted.md).
UIUCTag was qualified on `fw-v0.6` on 2026-10-08
([power results](embedded/tags/UIUCTag/design/power-results.md)).

- **`tag-test` on `RUN_ALL` was intermittent; probably fixed, not yet
  attributed.** On 2026-11-02, main `12af820d` on the same
  `tag-breakout-base-l432-v1` (fw `be8f9062`, a dirty build of 2026-10-28)
  passed **six of six** with no 0x81 at all (the host still logs the first
  failure of a burst at ERROR, so none occurred). The L4 monitor fix, the
  host's SWD enter retry (2026-10-31), the host's `DBG_SLEEP` (2026-10-27) and
  possibly the base build changed since the failures below, so this does not
  say which one fixed it. Six of six against a 4-in-6 rate is p ≈ 0.09;
  delete this item after another clean dozen. Note that the base reports
  some failed accesses as successes (next item), so "no 0x81" is weaker
  evidence than it looks.

  Re-measured on
  `fw-v0.6`, 2026-10-06: **four of six attempts passed** `ALL_PASSED`,
  including the ADXL367 self-test. The two failures died with
  `LIBUSB_ERROR_TIMEOUT` and "empty or invalid acknowledgement" immediately
  after `Running test RUN_ALL` and before the first `test: start request=3`,
  so before any device test ran.

  **The `read debug register 0xe000edfc failed status=0x81` errors are not the
  signal.** They appear 61-64 times in every attempt, including all four that
  passed. The first report treated them as evidence; they are
  background noise on this rig. What distinguishes a failure is only the USB
  timeout at the first transaction of the sequence, which points at the link
  rather than at the tag.

- **While the core sleeps during a monitor session, the debug port cannot
  reach the bus matrix unless a DMA clock is on, and reads return zeros as
  successes.** Every `tag-test` prints `GetStatus failed: Tag returned
  NANOPB` once, then finishes `ALL_PASSED`. Traced on 2026-11-02 with
  throwaway probes (none committed), on `tag-breakout-base-l432-v1`:
  - **Not the monitor.** An image with the L4 monitor fix reverted fails the
    same way. The tag answers `08 08`, `errAck(NANOPB)`: `proto_eval()` could
    not decode the request, because the first word of `ProtoBuf` was zero
    while the host had written `12 00` (`get_status`).
  - **The host's write never lands, and nothing says so.** Read back straight
    after writing, the buffer is `00 00 00 00`; a second write and read give
    the same. The base reported both as OK, including to the host's
    last-read/write-status check.
  - **Every system-bus access fails, not just SRAM.** In the same window
    `RCC_CR`, `RCC_CFGR` and `PWR_CR1` read 0 (with OK status), while the
    core-private registers answer: `DBGMCU_CR` = 1 (`DBG_SLEEP`, set by the
    host), `SCB_SCR` = 0 (no `SLEEPDEEP`), `DHCSR` = `0x01050001` (core in
    Sleep).
  - **Any sleep in the main thread does it; the ADXL367 is incidental.** A
    test that only calls `chThdSleepMilliseconds(1500)` fails the same way;
    the real ADXL367 test is simply the only one longer than the host's 1 s
    poll. With the accelerometer's USART2 bus up, or the sensor measuring,
    nothing changes. Ordinary session idle polls cleanly because on L4 the
    main loop never blocks during a session (`godown()` returns and the loop
    yields), so the core only reaches the BitPresTag idle hook's `__WFI()`
    while a test sleeps. A `Test` request's own reply was once read as zeros
    for the same reason: the test, and its sleep, starts the moment the
    reply is written.
  - **Not Stop mode.** `stopMilliseconds()` falls back to the RTOS sleep while
    the monitor is attached, and `SCB_SCR` = 0.
  - **The idle `__WFI()` is the necessary condition.** With the BitPresTag
    idle hook changed to skip `__WFI()` while `DEMCR.VC_CORERESET` is set, a
    1.5 s sleep-only test and the real ADXL367 test passed 6 of 6 with every
    request read back intact; with the hook as shipped, every one failed.
  - **Contradicts 2026-10-27.** That investigation read `PWR_CR1 = 0x500`
    and made 48 clean status reads across sleeps with `DBG_SLEEP` set, on the
    same base model (`../../bases/design/investigations/2026-10-0x81-dbg-sleep.md`).
    The cause is below.

  **Not the base build:** a clean `tag-breakout-base-l432-v1` (`5c6ddaf6`,
  flashed 2026-11-02) fails identically. Also
  ruled out: the Sleep clock gates (`RCC_AHB1SMENR` = `0x00011303` and
  `RCC_AHB2SMENR` = `0x0005229F`, both reset values, so SRAM1, SRAM2 and
  flash are clocked in Sleep; `APB1SMENR1` = `0xF7E6CE31`, PWR clocked;
  `ProtoBuf` is in SRAM1 at `0x20001738`; and `RCC_CR`, which no Sleep
  gate turns off, reads 0 in the same window), how `DBG_SLEEP` is set (the
  host and the 2026-10-27 tool write the same bit), and a missing barrier
  after the `WFI`: an idle hook of `__WFI(); __ISB();` (idle thread
  `wfi; isb sy`) still failed 6 of 6 with the sleep-only test. As expected,
  since the reads fail for the whole sleep and the `ISB` runs only after
  wake-up. The one recorded
  difference from 2026-10-27 is the supply: 3.28 V then, 2.496 V now. 2.5 V
  is this tag's operating voltage, so the tag must work there.

  **Cause found: the bus matrix stops in Sleep when no DMA clock is on.**
  Probed 2026-11-02 with a throwaway host build:
  - **No bus fault.** `CTRL/STAT` read straight after a lost request is
    `0xF0000F40`: `READOK` set, `STICKYERR`, `WDATAERR` and `STICKYORUN`
    clear. The access port reports the zeros as successful reads. `CSW` is
    `0x23000052` (`DeviceEn` set) inside and outside the window. So the base
    is not hiding a fault; there is none to report.
  - **Everything beyond the core's private bus reads zero.** That covers flash
    at `0x08000000` and its alias at `0`, SRAM2 by both its code-bus
    (`0x10000000`) and system-bus (`0x2000C000`) addresses, SRAM1, and
    `RCC`. Only `DBGMCU` and `SCB` answer. It is the whole path to the STM32
    bus matrix, not the system bus alone.
  - **Turning on DMA1's clock removes it.** With the host setting
    `RCC_AHB1ENR.DMA1EN` (`0x100` → `0x101`) at the first call, 6 of 6
    `tag-test` runs had no lost request and no NANOPB; the two controls run
    with the same build lost one each. `DMA1SMEN` is set at reset, so DMA1 is
    then a bus master clocked in Sleep, and the matrix stays up for the debug
    port. Plain `DBG_SLEEP` keeps the core's clock but evidently not the
    matrix's when the CPU is its only enabled master. This tag never enables
    a DMA (`RCC_AHB1ENR` = `0x100`, flash only). What the 2026-10-27 image
    had enabled was not recorded.
  - **The flash Sleep clock is not it.** `FLASHSMEN` is already set
    (`RCC_AHB1SMENR` bit 8).

  Fixes, separable:
  - **Firmware (batch):** enable DMA1's clock while a monitor session is
    open and disable it when the session closes. This is the shipped
    equivalent of the probe. Skipping the idle `__WFI()` during a session
    also worked (6 of 6), at the cost of run current instead of Sleep.
  - **Host (done 2026-11-02):**
    `l4.rs` `keep_bus_matrix_in_sleep()` sets `DMA1EN` after
    MONITORSTART if it is clear, and `release_l4_dma_clock()` clears it
    again before MONITORSTOP. `tag-test` then passed 6 of 6 with no NANOPB,
    and `tag_release_check.py` passed with the run at 0.55 µA and rest at
    0.15 µA, unchanged from the morning's check, so the clock does not
    outlive the session.
    The C++ `tagcore` library, not released since v4.0, is unchanged.
    `rpc.rs` `write_request()` also reads every L4 request back, rewrites it
    up to twice, and otherwise fails with "RPC request did not reach the
    tag". Checked with the DMA fix disabled: the lost request is reported
    in those words instead of as NANOPB.
  - **Base:** nothing to fix for this fault. The access port reports success,
    so a `STICKYERR` check would not have caught it.

  The L4 mailbox in
  [the monitor proposal](embedded/tags/common/core/design/proposals/l4-monitor-robustness.md)
  would not avoid this on its own, since its block is in SRAM too; it needs
  the bus matrix kept up the same way. Delete this item once the firmware
  change is in the batch.

- **One `Info failed` in two of three UIUCTag release checks, not reproduced
  by hand.** The `tag_release_check.py` runs of 2026-11-02 at 07:14 and 10:55
  each log one `[state-check stderr] Info failed`, with and without the
  host's `DMA1EN` fix, so it is not the bus-matrix fault. It comes from the
  wait-for-RUNNING poll after `tag-start` (`power_experiment.py` `start()`):
  attach succeeded and `GetTagInfo` failed. The next poll succeeded and the
  runs passed. A third check at 14:35 did not show it, and 61 replays did not
  either: 11 by hand, then 5 with `-d` and 5 without, both with the
  Joulescope server running and the harness's reset, 75 s settle and 120 s
  measurement, then 40 fast cycles. So it is rare, not tied to the server or
  to debug logging.

  `tag-info` used to print a bare "Info failed"; it now adds the reason, as
  `tag-test` does, so the next check that hits it says why. It lands in the
  same place as the lost stop in [the shared TODO](#common) -- the first
  session after a start -- which may or may not be a coincidence.

- **Confirm the internal-ADC fix holds here.** This board is what exposed the
  `adcVDD()` defects: it reported `vdd=3.77-3.96` and `temp=92.5-111.4` while
  the rail was 2.4960 V and the room about 25 C. Both outputs come from one
  call and the temperature is computed from the voltage, so one unsettled
  VREFINT sample corrupted both. The same code reads correctly on BitTag and
  CompassTag, so the margin was board-dependent rather than absent. After the
  fix this board read 2.48-2.49 V and 23.9-25.3 C; that was two samples, and
  wants confirming over a longer run and against the BMP581's own temperature.
- **Find the shortest sampling time that still reads correctly.** 640.5 cycles
  was chosen to remove the clock dependency, not because it was measured to be
  necessary. The settling delay may be doing all the work. Sweep it on this
  board; the harness is `adc_sweep.sh` from the 2026-10-04 session.

### Bases

Directory: [`embedded/bases/`](embedded/bases/).

- **One failed read can fail the next several block reads.** With the parity
  bug of [2026-11](embedded/bases/design/investigations/2026-11-breakout-parity-bit.md) on
  `tag-breakout-base-l432-v1`, 112 single parity errors left 25 block reads
  failed twice, in two bursts of 5 and 9 within a few milliseconds, each
  ended only by a host retry. So the base's recovery after a failed `DRW`
  read (`errorClear()`, the abort, the dummy `RDBUFF` read) does not always
  return the AP to a usable state. Find out why with the serial log, by
  injecting a fault; any real link error would cascade the same way.
- **`tag-breakout-base-l432-v1/src/ll_swd_spi.c` is not built.** The target
  compiles `ll_swd.c`. A note in that directory recorded that `ll_swd_spi.c`
  did not work properly on macOS with ChibiOS 21.11. Fix it or remove it, and
  remove `src/ll_swd_spi.c-back` beside it.
- **Unused local sources.** `tag-breakout-base-l432-u375-1v8/src/ll_swd.c` and
  `tag-base-c071/src/stlink-local.c` and `src/ll_swd.c` are not in their
  targets' `project.mk`.
- **Hybrid SPI SWD for a faster F042 base.** The F042 bases still bitbang
  everything. In the optimized `tag-breakout-base-l432-v1/src/ll_swd.c`, every
  shift-out width is a multiple of 8 bits (8, 16, 24, 32) and the odd widths
  are all shift-in (5, 9, 24), so SPI can carry every shift-out phase. That is
  the split `tag-base-c071` and `tag-breakout-base-l432-u375-1v8` already use.
  **Not pursued (2026-11-07):** only `tag-breakout-base-l432-v1` has its SWD
  lines on SPI1 pins (SWCLK PA5 = SCK, SWDIO PA7 = MOSI, SWDIO_IN PA6 =
  MISO). `bittag-base-v7` and `bittag-base-jlcpcb-v3` use PA1 and PB7, which
  have no SPI function; `tag-breakout-base-jlcpcb32-v1` has SWCLK on PA5 but
  SWDIO on PA4 (SPI1_NSS). The gain on one bench base is not worth it, and
  future bases will be derived from `tag-breakout-base-l432-u375-1v8`, which
  already uses SPI.
- **Hot SWD helpers in RAM.** On a future base with a faster MCU and SRAM
  headroom, consider placing only the hottest bitbang helpers in RAM. The F042
  base is not a candidate: its build uses essentially all of its SRAM.

### Loaders

Directory: [`embedded/loaders/`](embedded/loaders/).

Runtime rules are in
[Loader Runtime Design](embedded/loaders/design/loader-runtime.md).

- **The ST entry points do not refresh the watchdog.** `Serve()` does: it
  writes `IWDG_KR = 0xAAAA` while it waits for commands. The ST entry points do
  not. A tag whose option bytes select the hardware watchdog
  (`FLASH_OPTR.IWDG_SW` = 0) has it running from reset, and the debug freeze
  covers only a halted core. A long run of loader code would then be cut off by
  a reset, a mass erase above all. The bench PresTag uses the software watchdog,
  so it is unaffected. The fix is an `IWDG_KR = 0xAAAA` write in the poll loops,
  which has no effect when the watchdog is not running; see
  [SWD Capture and Recovery Library](host/libraries/tagcore-rs/design/swd-recovery.md#mcu-reference).
- **`MassErase` is untested.** It has not been established whether `-e all`
  with a loader loaded also erases internal flash, and it was not tried on a
  tag with firmware worth keeping. Sector erase is tested, through the
  programmer's erase-before-write.

## Repository

### Build

Directory: [`docs/build/`](docs/build/).

Open items for the model in
[Tag Firmware Build Reproducibility](docs/build/firmware-reproducibility.md). Delete an
item when it is done.

#### Provenance and qualification

- **Qualification does not name the image it measured.** This is the gap that
  matters. `tag_release_check.py` writes `results.json` with a verdict per check
  and the commit of the working tree it ran in, but no field for the SHA-256 of
  the image on the tag. The release names its images by hash and the board
  database names what was programmed, but the qualification between them names
  only a commit. Until it records the hash, the operator writes the release tag
  and image SHA-256 into the result directory by hand
  ([release procedure](docs/release/release-procedure.md#reading-and-keeping-the-result)).
- **The dirty flag is not carried into the image.** `version.h` defines
  `GIT_DIRTY`, `GIT_DIRTY_STR`, `CHIBIOS_SHA` and `NANOPB_RUNTIME_VERSION`, but no
  firmware source references them, so a returned tag cannot report over the
  monitor that its tree had uncommitted changes; only the build manifest says
  so, and only if it was kept. Left deliberately: a new string in the image
  shifts its layout, and STM32U375 Standby entry depends on layout, so wiring
  these in needs its own commit and its own bench measurement.
- **Bench-built images have nowhere to put their manifests.** A bench build
  writes a manifest into its build tree and nothing collects it: no archive
  location, no naming convention. This matters only if a bench-flashed tag
  reaches the field, which the rule that field tags are programmed from a
  release exists to prevent.
- **Most of the archive is still manual.** The firmware release workflow
  attaches the images and manifests. The `.map`, `project.mk`, linker script,
  `compile_commands.json` and the board's external loader are not collected.

#### Generators and pins

- **Move to nanopb 0.4.9.2?** 0.4.9.1 is pinned because it generated the tree's
  existing output. The newer LTS bugfix release is worth taking deliberately
  and separately. The pin lives in three places that must move together: the
  vendored runtime directory, `requirements.txt`'s protobuf (which must match
  the `protoc` that distribution ships), and `NANOPB_URL`/`NANOPB_SHA256` in
  `generated-sources-reproduce.yml`.
- **Record the `config-gen` protobuf version in `tools_text`.** It is pinned in
  `embedded/proto-c/requirements.txt` and identical on every machine by
  construction, so recording it is honest. It was left out so that the manifest
  churn belongs to one commit.
- **Record `fmpp` in the build.** The per-image build manifest does not record
  the `fmpp` version; that is a small addition. Recording it in the board
  `inputs.sha256` is a larger one: it needs `fmpp` and its JRE pinned the way
  `config-gen`'s protobuf is, and the pin and the manifest entry have to land
  together, in a commit that regenerates every board. See
  [decision 0015](docs/decisions/0015-build-pin-fmpp-and-its-rendering-environment.md).
- **The old board generation path has no reproducibility support.**
  `generate_board_files` -- the static flow that renders `cfg/*.ftl` from a
  checked-in `board.chcfg` -- has no manifest, no committed output and no
  staleness check. `TagSteval`, `bittag-base-jlcpcb-v3` and
  `tag-breakout-base-jlcpcb32-v1` use it (as does `bittag-base-jlcpcb-v2`,
  which is not configured); none is behind a distributed tag. If one ever is, configure reports it as a
  distributed board that does not commit its generated files, and the remedy it
  suggests (create `generated/` and regenerate) does nothing on this path. Port
  the board to `generate_configured_board_files` first.

#### Scope

- **Do host tools need any of this?** They are out of scope: they release on a
  different clock and their dependencies are pinned by the vcpkg baseline. But
  the `dataprocessing` path also produces records that outlive their build.

### Shared contracts

Directory: [`docs/shared/`](docs/shared/).

#### Monitor transport

- **Clear `VC_CORERESET` on the host's timeout-resume path.**
  `host/libraries/tagcore/tagmonitor.cc` has two resume paths after a failed
  monitor call. The reset path clears the flag. The timeout path is taken when
  the timeout probe has halted the target, and it resumes with
  `(demcr | MON_EN | VC_CORERESET)`, leaving the flag set. Since
  [decision 0006](docs/decisions/0006-monitor-u3-attachment-is-the-shared-session.md)
  a stale flag can no longer stop a U3 tag sleeping. It still leads
  `monitorResetRecoveryActive()` to read the next boot as a monitor attach. Test
  across L4 and U3 targets before changing it, because both transports read
  that bit.

#### Data-log download

- **Decide how a download handles missing blocks.** The host asks for a page
  at an index and advances by the records the page held, until a page comes
  back with nothing in it. Today:
  - **End of log:** most tags return an `Ack` with no payload; BitTag returns an
    empty `bittag_data_log`.
  - **Missing block:** `NODATA` while the index is below the status's data count
    is skipped one index at a time, by `tag-dwnld`
    (`host/commandline/dwnld.cc`) and qtmonitor
    (`host/applications/qtmon/abstractdownload.cpp`) alike. Past the count it
    ends the log.
  - **The count is only a hint,** and the tools disagree on it. `tag-dwnld`
    refuses to download when it is zero, though firmware paths reset it (for
    example IMUTag's `state_run.c` on a flash error). qtmonitor stops when the
    index reaches it, so a low count truncates the download silently.

  Proposed: the tag marks a hole with a new `Ack.Err` value, `DATASKIPPED`;
  any other reply without records ends the log, and every downloader follows
  one tagcore rule that reads no count. See
  [data-log end and skipped pages](docs/shared/design/proposals/data-log-end-and-skip.md).

### Licensing

Directory: [`LICENSES/`](LICENSES/).

Follow-ups from the third-party inventory of 2026-10-06; see
[README.md](LICENSES/README.md). The one component that shipped with a known licensing
gap, SOLPOS, was used only by btviz, which was removed for v4.0 with its
notice.

The MotionCal gap in qtcalibrate closed on 2026-10-10. `quality.c` went first,
replaced by owned arithmetic in `host/libraries/sensoranalysis`; then
`magcal.h`, rewritten from the Freescale solver it declares. `magcal.c` and
`matrix.c` stay, now in `host/libraries/sensoranalysis/core/magcal`, and those are BSD-3-Clause with a notice of their own
(`host/third-party/freescale-magcal.txt`).

#### Worth reducing

- **Project source files carry no license header.** The root `LICENSE` covers
  them, but an `SPDX-License-Identifier: MIT` line in each file would make
  that explicit to anyone who copies a single file.
