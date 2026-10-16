---
type: proposal
status: proposed
summary: Test the command line tools without hardware by linking them against a fixture-driven fake of tagcore's Tag class in a check-only build; shipped binaries are unchanged, and the runtime fake backend the Python binding needs is left to that work.
---

# A Fake Tag for Testing the Command Line Tools

The command line tools that talk to a tag -- `tag-info`, `tag-dwnld`,
`tag-start`, `tag-stop`, `tag-reset` and the rest -- can only be run against
real hardware, so nothing checks their behaviour between bench sessions.
This proposal adds a fake of tagcore's `Tag` class, driven by fixture files,
that a check-only build links the tools against in place of the real one. The
shipped tools and the libraries they link are unchanged.

## What is already covered, and what is not

Only three host applications talk to a tag, and the two that do so through the
monitor protocol already run on fixture data:

| Application | Talks to a tag | Hardware-free mode |
| --- | --- | --- |
| `qtmonitor` | yes | `--fake-fixture`, from `host/docs/fixtures/qtmonitor/` |
| `qtcalibrate` | yes | `--replay-capture`, a recorded calibration session |
| `qtprogram` | only by running STM32CubeProgrammer | none needed: it wraps another tool |
| `sensorviz`, `btviz`, `dataprocessing` | no; they read files | load a fixture log |

Those modes do two jobs at once: they exercise each application's real windows
and they generate the user guide's screenshots, so both stay maintained. The
log decoders behind `tag-dwnld` are covered too, by the offline assertion
programs in [`test/`](../../test/README.md), which feed synthetic `Ack`
messages through the SQLite writer.

What nothing covers is the command line tools' own logic: their exit
statuses, their output, the order of calls they make, and how they handle a
tag that refuses or fails a request. That is not hypothetical. Until
2026-10-16 `tag-info` exited 0 when it could not attach, and
[`tag_rebuild_check.py`](../../../../../embedded/tools/tag_rebuild_check.py)
decides its `attach_after_capture` step on that exit status, so the step could
pass on a failed attach. `tag-info --json` is a contract too:
[`flash_release.py`](../../../../../embedded/tools/flash_release.py) records it
for every tag it programs.

## Where the fake goes

`Tag` ([`tagclass.h`](../../tagclass.h)) owns a `TagMonitor`, which speaks the
monitor protocol over a `LinkAdapt`, the ST-LINK USB link. A fake could replace
any of the three.

- **`LinkAdapt`.** It deals in memory and debug-register reads and writes,
  core registers and reset. A fake there has to behave as the firmware's monitor
  executing on the core, on two target families: a Cortex-M emulator, or a
  replay of recorded register traffic that breaks whenever the protocol moves.
  Worth it only for testing `TagMonitor` itself.
- **`TagMonitor`, at `Rpc(Req, Ack)`.** The protocol boundary, and the most
  faithful seam: `Tag`'s request building and reply decoding would run
  unchanged. But nothing in tagcore is virtual and `Tag` holds its
  `TagMonitor` by value, so it needs an interface extracted from `TagMonitor`
  and `Tag` reworked to hold one -- a change to the shipped code path for the
  sake of tests.
- **`Tag`, replaced at link time.** A second implementation of the same
  `tagclass.h`, compiled into a separate library that check-only builds of the
  tools link instead of the real one. No interface, no virtual calls, and no
  change to any shipped binary. It does not exercise `Tag`'s own
  request building -- but that is thin, and not what is untested.

**Chosen: the link-time fake of `Tag`.** It tests what is untested -- the tools
-- for the least change. The runtime seam at `TagMonitor` is the right design
for a different job; see "The Python binding", below.

## The fake

`fake_tag.cc` implements `Tag`'s public methods from one fixture file, named by
an environment variable:

```sh
TAGCORE_FAKE_TAG=host/docs/fixtures/qtmonitor/bittag.json tag-info-fake
```

- **The fixture is the existing `qtmonitor` fixture,** which already holds
  what most tools ask for: `info` (`TagInfo`), `config` (`Config`), `voltage`,
  and a `Status` per state under `statuses`. The fake reads that schema as it
  stands and adds optional sections a tool may need:
  - `state` -- the state the fake starts in (default `idle`);
  - `state_log` -- the `StateLog` pages `GetStateLog()` returns;
  - `data_log` -- the `Ack` pages `GetDataLog()` returns, so `tag-dwnld` can be
    run end to end;
  - `git_sha` -- what `GitSha()` returns;
  - `fail` -- methods that report failure, with the `DebugMessage()` text, to
    drive the error paths;
  - `settle` and `erase_polls` -- how many `GetStatus()` calls report
    `STATE_UNSPECIFIED` after attach, and how many report the erase still in
    progress, so the tools' settling and polling loops are exercised.

  `qtmonitor` ignores fields it does not know, so one fixture serves both.
- **`Available()`** lists one base, whose `UsbDev` names the fixture, and
  `Attach()` succeeds unless the fixture lists it under `fail`.
- **State.** `Start()` moves `idle` to `running`, `Stop()` `running` to
  `finished`, and `Erase()` `finished` or `aborted` to `idle`; anything else
  fails, as the firmware refuses it. `GetStatus()` returns the fixture's
  `Status` for the current state, with the state field set. These rules model
  the firmware only as far as the tools depend on them; the fake tests the
  tools, never the firmware, and a rule that drifts from the firmware is a bug
  in the fake.
- **Not modelled.** Calibration and self-tests (`Calibrate()`, `Test()`,
  `ReadCalibration()`, `WriteCalibration()`) return the fixture's answer if it
  has one and fail otherwise. Memory reads (`ReadMemWord()`) fail.

The fake needs no hardware, but it does carry `Tag`'s `TagMonitor` member,
because both implementations share one header and so one layout. That member
is constructed and never used; constructing it calls `libusb_init()`, which
succeeds on any machine whether or not a device is present.

## Building it

1. **Split `tagclass.cc` out of `tagcore`.** `tagcore` keeps everything else;
   `tagclass.cc` moves to its own small library, `tagcore_tag`, which `tagcore`'s
   users link as before. Without the split, a link-time replacement depends on
   which archive members a linker chooses to pull, which differs between
   linkers and fails quietly when it fails.
2. **`tagcore_fake`,** built only with the existing `BUILD_TAGCORE_CHECKS`
   option: `fake_tag.cc` plus the fixture reader.
3. **`host_cli_tool()`** gains a `FAKE` variant, so each tool built from
   [`host/commandline`](../../../../commandline/CMakeLists.txt) on `Tag` also
   builds as `<tool>-fake` when the option is on. The fake variants are never
   installed, and so never packaged.

Tools that work below `Tag` -- `tag-capture`, `tag-rebuild`, `tag-xflash` and
`tag-sramcall` drive the SWD session and the SRAM loaders directly -- are out
of scope; their hardware-free checks, if any, belong with the recovery code.

## The checks

A Python script beside the existing checks, `test/command_line_check.py`, runs
the fake tools through scenarios and asserts on their exit status and output,
in the established pattern: run by hand, no harness. A first set:

| Scenario | Expectation |
| --- | --- |
| `tag-info` on each fixture | exit 0; every `TagInfo` field printed; the state |
| `tag-info` with `Attach` failing | exit 1, and the reason on stderr |
| `tag-info --json` | exit 0; one JSON object with `info`, `config`, `status` that round-trips to the fixture's messages |
| `tag-start` from `idle`, then `tag-stop` | both exit 0; the fake ends `finished` |
| `tag-reset` from `running` | stops, erases and polls until `idle`; exit 0 |
| `tag-reset` with `settle` past its timeout | exit 1, naming `STATE_UNSPECIFIED` |
| `tag-dwnld` on a fixture with a `data_log` | exit 0; a SQLite log whose rows match the fixture's pages |

It runs anywhere the host tools build, which makes it a natural step in CI's
host builds later; this proposal does not add it to CI.

## The Python binding

The [Python interface proposal](python-interface.md) asks for something this
does not provide: a fake *backend* beneath `Tag`, injected at run time, that
can produce malformed payloads, timeouts and transport failures, to test the
binding and a shared download service. That is the `TagMonitor` seam above,
and the binding is the work that justifies it. When it is built, its fake
backend should read the same fixture files, so the fixtures written for these
checks carry over; the link-time fake can then be retired in favour of running
the real `Tag` over the fake backend.

## Phases

1. Split `tagclass.cc` into `tagcore_tag`; check every host target still links
   on macOS, Windows and Linux.
2. `fake_tag.cc` and the fixture reader, with `tag-info-fake` as the first
   tool, and the `tag-info` scenarios.
3. The remaining tools and scenarios; a fixture with a `data_log`, captured
   from a real BitTag, for `tag-dwnld`.
