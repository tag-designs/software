---
type: design
status: current
summary: How Tag Programmer's engine works - one thread that owns all state, the message loop and its deadline timers, worker threads for slow work, the station's checks and attach path, the programming session's step machine, and the hazards a maintainer must not reintroduce.
---

# Tag Programmer engine

The engine is one thread that owns every piece of state: the checks, the base
and tag connection, the firmware choice, the record file, the open prompt and
the programming session. The UI cannot touch any of it. It sends commands and
queries, and receives the whole view state after every change. Slow work
(programming, flash read-back, downloads) runs on worker threads, but its
result is applied on the engine thread, so no state is ever shared between
threads and nothing needs a lock.

The engine is a port of qtprogram's `Station` and `ProgramSession`
([batch station design](../../qtprogram/design/batch-station.md)). Its texts,
checks and record rows are qtprogram's, with the differences listed in
[the UI/engine contract](ui-engine-contract.md#deliberate-differences-from-qtprogram).
The contract also defines the view state, commands, queries and prompts; this
document describes what is behind them.

## Layout

| File | Holds |
| --- | --- |
| `core/src/engine/mod.rs` | `Engine` (the UI's handle), `spawn`, the loop, `Msg`, the log forwarding (`UiLogger`) |
| `core/src/engine/station.rs` | `Station`: checks, base and tag, firmware and catalog, record, prompts, commands and queries |
| `core/src/engine/session.rs` | The program and retest step machine, as more `impl Station` |
| `core/src/engine/view.rs` | The view-state JSON built from `Station` |
| `core/src/model/` | Toolkit-free model: ELF reading, flash plan, identity record, image, release catalog, record CSV, settings |
| `app/src/main.rs` | The Tauri shell: creates the engine, forwards `command`/`query`, emits `state` and `log` events |

`session.rs` is a second `impl Station` block, not a separate object. A
session needs the tag, the base, the record and the checks, all of which the
station owns. Keeping one owner avoids handing out references across the
message loop. The session's own fields live in `SessionState`
(`Station::session`).

## The loop

```text
            UI thread(s)                     engine thread                worker threads
 command ──► Msg::Command ──────────────►  Station::command
 query   ──► Msg::Query(reply) ─────────►  Station::query ──► reply
                                           fire_timers(now)
                                           view_state() != last? ──► Event::State
                                     ◄──── Msg::Done(closure) ◄──────── spawn_work result,
                                                                        progress
```

`run` blocks in `recv_timeout` until the earliest deadline from
`Station::next_deadline`. After each message, or when the wait times out, it
fires every due timer and rebuilds the view state. It emits the state only
when it differs from the last one sent. The UI therefore receives a whole,
consistent state, never a partial update, and an idle station emits nothing.

There are four timers, each an `Option<Instant>` deadline rather than a
thread:

| Timer | Period | Runs while |
| --- | --- | --- |
| base poll | 1 s | no base is chosen or present: watches USB for one |
| status poll | 400 ms | a tag is attached and idle: reads `Status`, detects a vanished tag after 5 misses |
| attach retry | 200 ms, 10 tries | the firmware did not answer the first attach |
| session tick | 1 s | a session is running a step that waits on the tag |

A timer is cancelled by setting it to `None`. Nothing else needs cleaning up.

### Queries block the caller, not the engine

`Engine::query` sends a reply channel and waits up to 10 s for the answer;
after that it returns `null`. The Tauri `query` command runs it in
`spawn_blocking`, so the webview's thread never waits on the engine.

### What runs on the engine thread

Monitor calls on the tag run on the engine thread: attach, `TagInfo`,
`Status`, stop, erase, set RTC and test. They are short (milliseconds) when
the firmware answers. A call that gets no answer waits for tagcore's monitor
timeout: on an STM32L4 tag a protobuf request allows 65000 poll iterations,
about 120 s; on an STM32U3 tag every call gives up after 2.5 s. The engine is stuck for that
long, and so is every query (each returns `null` after 10 s). The PresTag
SetRtc hang (`embedded/tags/TODO.md`) showed this. Moving monitor calls onto a
worker would need a second owner for `Tag`; it has not been done.

### What runs on workers

`Station::spawn_work(work, apply)` runs `work` on a new thread and posts
`Msg::Done(apply(result))` back. The closure runs on the engine thread with
`&mut Station`, exactly like a command. Workers run:

- the connect check (`tagprobe::connect_check`) at attach;
- programming (`tagprobe::program`). Its progress callback posts its own
  `Msg::Done` per event, which updates the session's progress bar;
- the Retest flash read-back (`tagprobe::read_memory`);
- the release catalog refresh and release downloads.

If a worker's job panics, `spawn_work` catches it and posts
`worker_panicked` instead of the result. That clears every busy flag, ends a
running session as `FAILED: internal error (...)` and tells the operator, so
the station never stays busy until a restart.

While a worker owns the base, `worker_busy` is set. Polls, attaches and
commands that would touch the USB device are refused until it clears. The
monitor link (`Tag`) and probe-rs must never hold the base at the same time,
so the session detaches the tag before it starts the programmer and
reattaches afterwards.

A new thread per job is deliberate: jobs are rare and long. probe-rs is safe
to call from any thread **only because tagprobe opens ST-LINKs through
`StLinkFactory`**. probe-rs's general `Lister` also enumerates CMSIS-DAP
probes through hidapi, and on macOS hidapi's `IOHIDManager` is bound to the
first thread that used it. Through `Lister`, the second program of a session
aborted the process. See
[tagprobe's design](../../../libraries/tagprobe-rs/design/architecture.md).

### Deferring work to the next pass

A closure posted with `Msg::Done` from the engine thread itself runs on the
next pass of the loop, after the current view state has been emitted.
`begin_checks` uses this to open the record file. On macOS the first `open()`
in `~/Documents` by a new app identity waits for the user's answer to a
privacy prompt. Opening the record inline would have left the window blank
(the CI build "bounced" in the Dock). Instead the Record check reads "Opening
…", the window draws, and the open happens on the next pass.

## Logging

`UiLogger` is a `log` logger that forwards every record at or above the log
pane's level to the UI as `Event::Log`. tagcore and tagprobe log through the
same facade, so their messages reach the pane as qtprogram's did. The sink is
a process-wide static because `log` allows one logger per process. The level
is an atomic so that `log::Log::enabled` reads it without a lock.

Two kinds of log message are deliberately repetitive or deliberately quiet:

- each session tick writes one DEBUG line (step, tag state, erase sectors,
  test status), so a slow step can be diagnosed from a saved log afterwards
  instead of by attaching (which would reset the tag);
- tagcore rate-limits failed debug-register reads: the first is an ERROR, the
  rest are DEBUG, and a WARN gives the count on recovery. After probe-rs
  releases a field base, a burst of reads failing with ST-LINK status `0x81`
  is normal while the tag boots.

## The station

### Checks

Eight checks gate programming, in display order: Programmer, Base, SWD,
Supply, Tag, Tag state, Firmware, Record. Each has a level (pending, ok,
warn, fail) and a text. `can_program` returns the first reason programming
is not allowed:

- a session or worker is running;
- Programmer, Base, SWD or Record is not ok;
- Supply has failed;
- the firmware has failed, is pending, or has no usable image.

The program dialog shows that reason instead of opening.

Supply fails above 3.6 V on an STM32L432 (its absolute maximum) and warns
below 1.8 V. The voltage is the base's measurement of the tag's supply. It
comes from the connect check, so it is fresh at every attach.

### Start-up

`begin` runs once, on the UI's `start` command, sent when the page has loaded. If the operator has never
answered whether the station may attach automatically, it raises the
`autoAttach` prompt first. Every automatic attach resets whatever tag is in
the base, so this needs consent. `begin_checks` then:

1. sets the Programmer check to the probe-rs version;
2. looks for the base;
3. refreshes the release catalog;
4. loads the chosen image;
5. opens the record, deferred as above.

### Base and tag

```text
check_base ──► Base ok ──► (auto attach) ──► attach
attach ──► worker: connect_check ──► SWD ok, Supply ──► attach_tag(10)
attach_tag ──► Tag ok, TagInfo, Status, detect target ──► status poll
           └─► no answer ──► retry every 200 ms ──► "No firmware answers" (blank board)
```

The base is re-found by its saved ST-LINK serial every time, because a
replugged base has a new USB address. With one base on USB, no serial is
needed. The base's identity (`F6 99`, `tagprobe::identify`) chooses the probe
driver: Stock when it answers, FieldSafe when it does not.

**Every attach resets the tag**: connect check, monitor attach and probe-rs
session all go through reset. The station never attaches on its own except
after consent (automatic attach) or the operator's Attach. The status poll
uses the existing connection; it does not attach. Next Tag forgets the tag
and waits for Attach rather than polling for a new board, since each poll
would reset it.

### Firmware

A release from the catalog or a hand-chosen `.elf` is loaded as a
`FirmwareImage`: the ELF, its identity record, its flash plan and its SHA-256.
For a release the image is checked against the manifest. The tag type comes
from the attached tag's `TagInfo.source_path` (`detect_target`). It is
detected again whenever a release is chosen or downloaded
(`reload_for_catalog`), since the list of targets depends on the release. The
probe-rs part is chosen by `tagprobe::chip_for` from the device ID and a flash
size: the tag's own (`intflashsz`) when it answers, otherwise the image's
`__flash0_size__`. This is how an STM32L432KB (128 KB, the UIUCTag) is told
from a KC.

### Record

The record is qtprogram's append-only CSV, one row per attempt that reached
the programmer and one per retest. Before every Program and Retest the station
checks that it can still be appended to, before a tag is touched. A spreadsheet holding it locked is found
before the erase, not after. `label_conflict` warns when the label typed is
recorded for another UUID. For a blank board, which has no UUID yet, any
recorded use of the label counts. The UI asks for confirmation before going
on.

## The session

A session programs one tag (`program`) or retests it (`retest`). It is a step
machine ticked once a second while it waits on the tag:

```text
program:  Stopping ─┬─(FINISHED/ABORTED)─► Erasing ─(IDLE)─► Stopping
                    └─(IDLE)───────────► Programming ─► Connecting ─► Testing ─► Done
                                           (worker)        │
                                                           └─(no answer, 6 s)─► PowerCycle ─(Retry)─► Connecting
retest:   [Reading (worker), if IDLE and an image matches] ─► Connecting ─► Testing ─► Done
```

- **Stopping** stops a running tag. A tag in FINISHED or ABORTED is told to
  erase its log; its own firmware does the erase, because only the firmware
  that wrote the log knows its layout. The step gives up after 30 ticks.
- **Erasing** shows the tag's own sector count. When the tag reports a total
  of 0, the bar uses the log size (IMUTag pages) or the external flash size,
  as qtprogram did. With no total at all, the bar is the busy indicator.
- **Programming** detaches the monitor and runs `tagprobe::program` on a
  worker. Erase, program and verify happen in one probe-rs session, so no
  reset falls between them.
- **Connecting** reattaches and reads `TagInfo`. After 6 ticks without an
  answer the session moves to **PowerCycle** and waits for the operator.
  After a mass erase the L432 can stay in its bootloader until power is
  removed. tagprobe clears `FLASH_SR.PEMPTY`, so this is now rare.
- **Testing** compares the tag's git hash with the image's (`MISMATCH`
  otherwise), sets the clock and starts the self-tests. It ends when the tag
  leaves TEST, or after 120 ticks.

The session ends with the tag **IDLE**. It never starts the tag. A start
belongs to a deployment, and on a PresTag a start leaves the RTC
write-protected until reset, so a later SetRtc hangs the firmware
(`embedded/tags/TODO.md`).

`finish` writes the record row, reads the tag's info again, and resumes the
status poll. Anything but OK raises a warning alert. Cancel is accepted while
the session is stopping, erasing or waiting for a power cycle. It is refused
while the programmer runs, because an interrupted flash write is worse than
any wait.

## Hazards

Changes that have already gone wrong once, or that the bench rules forbid:

- **Do not let an attached tag go quiet.** A UIUCTag with fw-v0.6 or earlier
  shuts down 5 s after the last request ([monitor interface](../../../../docs/shared/monitor-interface.md#sleep-and-reset-interaction)),
  and a sleeping tag answers nothing until it is reset. The 400 ms status
  poll is what keeps it awake. The app opts out of macOS App Nap for this
  reason (`stay_awake` in `app/src/main.rs`): App Nap stretched the poll past
  the limit while the window was in the background. A poll that fails after
  a gap of 4 s or more forgets the tag at once and says it went to sleep, and
  `wake_tag` attaches again at the start of a session rather than failing.
- **Do not start the tag** at the end of a session, and do not send SetRtc
  to a started tag.
- **Do not open probes through `probe_rs::probe::list::Lister`.** It
  enumerates HID, which is single-threaded on macOS (see above).
- **Do not attach on a timer.** Every attach resets the tag. Polling uses the
  existing connection; finding a new tag waits for Attach or consent.
- **Do not open user files inline during start-up.** A macOS privacy prompt
  blocks the engine thread and blanks the window.
- **Do not hold the base from tagcore and probe-rs at once.** Detach the
  monitor before any tagprobe call, and keep `worker_busy` set until it
  returns.

## Testing without hardware

- `cargo test -p tagprogram-core` covers the model: ELF, flash plan,
  identity, record CSV, settings and catalog, plus the view-state clock text.
- `examples/smoke.rs` runs the engine headless with scratch settings and
  automatic attach off. It finds and identifies the base but never attaches.
- `examples/bench.rs` is qtprogram's `--bench-program`. It programs or
  retests one tag through the same engine and prints every step and the
  record row; it needs hardware.
- `ui/mock-engine.js` drives the page in a plain browser with canned states.
