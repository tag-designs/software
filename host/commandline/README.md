# Command-Line Tools

Command-line host tools live here. Most hardware-facing tools link the Qt-free
`tagcore` target from `../libraries/tagcore`. `dataprocessing` is the current
exception: it reuses the existing `sensoranalysis` math library, so it is built
when the Qt/sensor-analysis host stack is enabled.

Distributed tools -- these are the ones in `host_cli_install_targets` in
`../CMakeLists.txt`, the ones documented in the user guide, and the ones that
reach a release:

- `dataprocessing`: copy SQLite logs and materialize derived/calibrated sensor
  streams. The initial implementation supports CompassTag calibrated vectors
  and canonical orientation streams.
- `tag-dwnld`: download tag logs using the shared tag log writer interface.
- `tag-info`: inspect tag/base information.
- `tag-reset`: reset a tag.
- `tag-start`: start logging.
- `tag-stop`: stop logging and print the resulting tag status.
- `tag-cal`: calibration helper.
- `tag-test`: run RTC checks and tag self-tests. Despite the name this is a
  field tool -- it is what confirms a freshly programmed board works -- so it
  ships.

Developer and bench tools -- built from this directory, never installed. They
exist to exercise hardware and firmware during development, they carry no user
guide pages, and adding one to the install list is what put an unsigned,
undocumented `tag-attach-cycle.app` into every macOS release until it was
caught by the signature check in `host/tools/release-macos.sh`:

- `tag-test-example`: minimal example that sets the tag RTC, waits, and prints
  the measured clock error. A worked example of the tagcore API, not a tool.
- `tag-monitor-test`: exercises the debug monitor interface through the link
  adapter.
- `tag-attach-cycle`: repeatedly attaches and detaches a tag over USB, to shake
  out enumeration and attach-path faults.
- `tag-peek`: read arbitrary memory through the monitor interface.
- `tag-cal-write`: write an identity (no-op) calibration constant set, to
  unblock a mass-erased CompassTag. See below.
- `qtmonitor-fixture-capture`: capture `TagInfo`, default `Config`, `Status`,
  and voltage from a real tag into the fixture JSON consumed by qtmonitor
  documentation screenshot automation.

All of the tag-attached tools share `-d`/`--debug`, `-b`/`--base BUS:DEVICE`
and `-h`/`--help`.

Keep direct tag-operation tools independent of Qt so they remain lightweight and
usable in scripts. Processing tools may link host analysis libraries when that
avoids duplicating sensor math.

Example qtmonitor fixture capture preserving the real tag identity:

```sh
qtmonitor-fixture-capture \
  --id compasstag \
  --label CompassTag \
  --state idle \
  --fallback-config embedded/proto-c/compasstag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/compasstag.json \
  --print-summary
```

Example sanitized capture for screenshots that should not show a real device
UUID:

```sh
qtmonitor-fixture-capture \
  --id prestag \
  --label PresTag \
  --state idle \
  --sanitize \
  --fallback-config embedded/proto-c/prestag-proto-c/default-config.json \
  --output host/docs/fixtures/qtmonitor/prestag.json
```

`--state` names the captured status slot in the fixture; it does not drive the
tag into that state. Put the tag in the desired state before running the tool.

## tag-capture

Captures a tag's state over SWD **without booting its firmware**: the backup
registers (where the tag keeps its `pState`), the reset flags, option bytes,
flash ECC registers, RTC, unique ID, OTP and the whole internal flash. Run it
first on a returned tag, before `tag-info` or any other tool here. Those
attach a monitor, which lets the firmware boot, and the boot clears the reset
flags and can rewrite `pState` and the marker log.

```sh
build-host/bin/tag-capture --reason "returned from site 3, would not start"
```

It writes `captures/capture-YYYYmmdd-HHMMSS/` with one `.bin` per region and a
`manifest.json` holding the decoded registers and each file's SHA-256. The tag
is then reset and boots normally. `--sram` also captures SRAM, which is rarely
useful because Shutdown and Standby do not keep it. `--leave-halted` leaves the
core stopped. External flash is not captured yet. See
[SWD Capture and Recovery Library](../libraries/tagcore/design/swd-recovery.md).

## tag-xflash

Reads a tag's external flash over SWD, again **without booting its firmware**.
It halts the tag at its reset vector, downloads an external-flash loader from
`embedded/loaders` into SRAM, and calls the loader's `Init` and `Read` from the
host. CubeProgrammer is not involved, and internal flash is not written.

```sh
build-host/bin/tag-xflash dump \
    --loader build-host/embedded/loaders/AT25XE_PresTagv3/AT25XE_PresTagv3/build/AT25XE_PresTagv3.stldr \
    -o xflash.bin
```

A loader with a `Serve()` entry point (the loaders in this tree) is driven
through it, and the tool prints the part's JEDEC ID and status register as
found. Any other `.stldr` is driven through the STM32CubeProgrammer entry
points; `--st` forces that path. `--offset` and `--length` read part of the
device; by default the whole part is read, its size taken from the loader. The loader must match the board; for now
it is chosen by hand. Downloading the loader overwrites the start of SRAM1, so
run `tag-capture` first if SRAM matters. A 4 MiB part takes about a minute.
See [Loader Runtime Design](../../embedded/loaders/design/loader-runtime.md).

## tag-cal-write

Several CompassTag-family state handlers refuse `Start` with "Device must be
calibrated" (`config.c`, `sensorsHaveCalibration()`) until at least one
magnetometer calibration entry is present in flash. A mass-erase reflash
(`STM32_Programmer_CLI -e all`, or any full-chip erase) wipes that flash
region along with everything else, which then blocks `tag-start` on an
otherwise-healthy board with no other symptom.

`tag-cal-write` writes a single identity calibration entry (`M' = A(M-V)`
with `A` = identity, `V` = 0, `B` = 1) so the tag will start again:

```sh
build-host/bin/tag-cal-write
build-host/bin/tag-reset --set-rtc
build-host/bin/tag-start --start-now
```

This unblocks power/lifecycle testing on a freshly reflashed board without a
physical rotation-based calibration pass. It is **not** a real calibration --
magnetometer output taken under it is uncalibrated raw data passed straight
through. Run the interactive `tag-cal` (or qtcalibrate) for a real
calibration before trusting compass output from a board this was used on.

## DataProcessing

`dataprocessing` is a post-processing tool for SQLite logs. It keeps the input
database untouched, copies it to a new output path, writes derived tables and
stream metadata, and records processing provenance in a `ProcessingRun` table.

Current processors:

- `compass-calibrated`: writes `CompassCalibrated` with raw acceleration copied
  beside calibrated magnetometer x/y/z values.
- `compass-orientation`: writes `CompassOrientation` with canonical
  magnetic-frame yaw, pitch, roll, dip, field strength, acceleration magnitude,
  and quaternion columns.

Example:

```sh
dataprocessing \
  --input host/docs/fixtures/sensorviz/compasstag.db3 \
  --output /tmp/compasstag-processed.db3 \
  --processor compass-calibrated \
  --processor compass-orientation \
  --if-exists replace \
  --print-summary
```

Use `--list-processors` to see whether a log contains the inputs needed by the
current processors. Heading is not materialized; downstream tools can convert
the stored yaw to a display heading by applying their own declination and
mounting convention.
