---
type: design
status: current
summary: The monitor's state-changing commands (Start, Stop, Erase, Self-test, Calibrate) are synchronous - proto_eval() runs the transition before it replies, and the reply is the resulting Status, PERM with the state unchanged, or NXIO - with the main loop that makes it safe, the Erase and Self-test split, and how the host reads the reply and falls back for older firmware.
---

# Monitor Commands

**Every state-changing monitor command is synchronous.** Start, Stop, Erase,
Self-test and Calibrate run their state transition inside `proto_eval()`,
before the reply is encoded, so the reply says what happened:

- **OK with the `Status`** the tag reached;
- **PERM**, with the state unchanged, when the state does not allow the
  command;
- **NXIO** when a start or a calibrate ran but did not reach its state.

Erase and Self-test are long, so they reply on *entering* sRESET or TEST. The
sweep or the test runs on the following state-machine passes, and the host
polls until the tag is IDLE again.

The host reads the `Status` in the reply as the answer. Older firmware answered
a bare OK and acted on a later pass; the host recognises that reply and falls
back to polling. Why the design is this one, and the patches it replaced, is in
[decision 0043](../../../../../docs/decisions/0043-synchronous-monitor-commands.md).

The code is shared by every tag on both families: `monitor.c`
(`proto_eval()`, `commandAck()`, `monitor_request_allowed()`),
`state_machine.c` (`StateMachineCommand()`, `SelfTest()`, `Reset()`), and the
main loop in `main.c`.

## The commands

| Command | Allowed from | Transition run in `proto_eval()` | OK reply | Other replies |
| --- | --- | --- | --- | --- |
| Start | IDLE | `writeConfig()`, then `Configured(T_INIT)` | CONFIGURED or RUNNING: a tag due to start at once may already be RUNNING | NXIO with a message when the configuration does not store; NXIO with the Status when the start ends anywhere else (a device-setup failure aborts) |
| Stop | CONFIGURED, RUNNING, HIBERNATING | `Finished(T_INIT)` | FINISHED | |
| | EXCEPTION | `Aborted(T_INIT)` | ABORTED | |
| | CALIBRATE | `Calibrating(T_EXIT)`, then `Idle(T_INIT)` | IDLE | |
| Erase | FINISHED, ABORTED | `resetEnter()`: sRESET recorded, no sweep yet | sRESET | |
| | sRESET | `resetEnter()`: the sweep continues from where it is | sRESET | |
| Self-test | IDLE | `SelfTest(T_INIT)`: devices initialised, TEST, `test_status` TEST_RUNNING | TEST | |
| Calibrate | IDLE, on tags built with `SENSOR_CALIBRATION` | `Calibrating(T_INIT)` | CALIBRATE | NXIO with the Status if the tag did not reach CALIBRATE |

In any other state the command is refused with PERM and nothing changes. A tag
built without `SENSOR_CALIBRATION` refuses Calibrate.

Two checks decide whether a command is allowed:

1. **`monitor_request_allowed()`** runs first, for every request, and refuses
   a command in the wrong state with PERM and the message *"Monitor request
   not permitted in current tag state"*. Set RTC and the log reads are gated
   here too.
2. **`StateMachineCommand()`** is the authority: it reads `pState->state` and
   either runs the transition or returns false, which `commandAck()` answers
   with PERM and the unchanged Status.

The two allow the same states. Keep them in step: a state the gate refuses
never reaches the state machine.

## How a request is served

1. The monitor interrupt latches the request and signals the main thread with
   `EVT_MONITOR_SERVICE`: the DebugMonitor exception on an STM32L4, the kick
   interrupt on an STM32U3 ([monitor interface](../../../../../docs/shared/monitor-interface.md)).
2. The main loop calls `monitorServicePending()`, which runs `proto_eval()` on
   the main thread, the thread that owns the state machine.
3. For a command, `proto_eval()` calls `commandAck()`, which calls
   `StateMachineCommand()` and then encodes `statusAck()` or the error.
4. `monitorServicePending()` publishes the reply (DCRDR on L4,
   `monitor_shared.result` on U3), releases the host and returns true.

The transition and the reply happen in one step, on the thread that owns the
state, so nothing can come between them. There are no event bits for monitor
work and no queue: `MonitorCommand` (`core_runtime.h`) names the command for
the length of one call.

### The main loop

```text
loop:
    sleepmode = StateMachine(events)
    if a monitor session is open:
        if monitorServicePending() served a request:
            continue                      # straight back to the state machine
    else:
        godown(sleepmode)
    wait for, or collect, the next events
```

Two properties of the loop make synchronous commands safe:

- **The state machine runs before any request is served.** Every attach
  resets the tag, and the first `StateMachine()` pass after a boot is reset
  recovery, which restores the clock, the log cursors and the state from the
  retained markers. A command is therefore never carried out on
  pre-recovery state.
- **A served request goes straight back to the state machine.** A command
  changes the state, so the sleep mode the pass before it chose is stale. The
  loop never sleeps on it: the next pass runs first and chooses again.
  `godown()` runs only with no session open.

In RUNNING on an STM32U3, the loop waits for hardware events, and for monitor
events while a session is open; in every other state it never blocks. See
[U375 low power](u375-low-power.md).

## Erase and Self-test

Both are split so the reply comes before the long work:

- **Erase.** `resetEnter()` sets sRESET and records the state marker;
  `Reset(T_CONT)` sweeps on the following passes, a sector at a time, and
  hands the main thread back whenever an event is pending, so the host's
  status polls are answered during the sweep. A stop sent mid-sweep is
  refused with PERM: returning to IDLE with sectors still dirty would let a
  run start on un-erased flash. The tag reaches IDLE when the sweep is done.

  **An erase sent during the sweep continues it.** `resetEnter()` keeps the
  sweep's position when the tag is already in sRESET with the sweep started,
  so a host that finds a tag erasing, or resends an erase, gets an sRESET
  reply and the sweep goes on rather than starting over. After an attach the
  boot's first pass resumes the sweep before any request is served. On an
  STM32L4 tag the first request of a new session is then answered only when
  the sweep has ended, so there it finds the erase done and replies PERM
  ([tags worklist](../../../TODO.md)).
- **Self-test.** `SelfTest(T_INIT)` initialises the devices, sets TEST and
  `test_status` TEST_RUNNING, and sets the RAM flag `test_pending`. The next
  pass, `SelfTest(T_CONT)`, runs `test()` if the flag is set, then returns to
  IDLE with the result in `test_status`.

**A reset during a test abandons it.** `test_pending` is ordinary RAM, so a
reset clears it while TEST survives in `pState`. The next `SelfTest(T_CONT)`
then finds no flag and returns to IDLE without running the test, leaving
`test_status` at TEST_RUNNING. The host reads that as a test that did not
finish. Rerunning instead would restart the test on every attach, and a test
that itself reset the tag would loop for ever.

While the test runs, the main thread is busy, so monitor requests wait for it.
The host pauses its polls after starting a test (Tag Monitor's
`QUIET_AFTER_BUSY`).

## Calibrate

Calibrate enters CALIBRATE from IDLE; Stop ends it, through
`Calibrating(T_EXIT)`, and the tag returns to IDLE. **Calibration does not
survive an attach.** The reset that attaching causes returns the tag to IDLE,
so a calibration session, and any test of it, must stay in one monitor
session from Calibrate to Stop.

## The host

`Tag::start`, `stop`, `erase`, `test` and `calibrate` in `tagcore-rs`
(`host/libraries/tagcore-rs/src/tag.rs`) return
`Result<Option<Status>, TagError>`:

- **`Ok(Some(status))`**: the firmware is synchronous, and `status` is the
  state the command reached.
- **`Ok(None)`**: a bare OK from older firmware, which queued the command. The
  outcome must be read from later status polls.
- **`Err`**: the transport failed or the firmware refused. A refusal carrying a
  Status names the state, as in *"Tag returned PERM (tag is IDLE)"*.

The tools use the reply where it changes what they do, and keep the polling
path for older firmware:

- **`Tag::stop_confirmed`**, used by `tag-stop`, `tag-reset` and `tag-dwnld`,
  returns a stopped Status from the reply at once. For a bare OK it polls for
  FINISHED or ABORTED and resends the stop while the tag is still acquiring
  (`STOP_RESEND_AFTER`), because an STM32L4 tag running the older firmware can
  accept a stop and lose it.
- **`tag-start`** takes the reply as the result and skips its wait for the tag
  to leave IDLE.
- **`tag-test`** reports that the test started, and a test that ended IDLE
  with TEST_RUNNING as one cut short by a reset.
- **Tag Monitor** shows a synchronous stop at once and resends nothing; for a
  bare OK it watches for the stop and resends as before.
- **Tag Programmer** still sends one stop and resends only while the tag reads
  as acquiring, which never happens after a synchronous stop.
- **An erase in sRESET.** `tag-reset`, Tag Monitor's Erase and Tag
  Programmer send an erase to a tag they find erasing, to continue it. Older
  firmware refuses that erase with PERM but sweeps on regardless, so
  `tag-reset` and Tag Programmer treat the refusal as a note and keep polling
  for IDLE.

Hosts that predate the change read only the Ack code. They see OK as they did
before and keep polling for something already done, so the v4.0 tools and the
legacy C++ tools work unchanged with the new firmware. Neither the protobuf
schema nor the monitor version changed: `Ack.payload` already carried a
`Status`.

## Checking a change

On each target, after flashing and `tag-test`:

- **Each command in each state.** OK with the expected Status where the table
  allows it; PERM with the state unchanged everywhere else. Send Calibrate's
  follow-up commands in the same session as the Calibrate.
- **Start, then stop at once, 20 cycles,** with a host that sends one stop and
  takes the reply. Every reply should be FINISHED, confirmed by a later
  status.
- **Erase from FINISHED, then attach at once.** The reply is sRESET; the next
  attach finds sRESET or IDLE. An erase sent then replies sRESET (or PERM if
  the sweep has finished), and the tag reaches IDLE.
- **Self-test, then attach at once.** The tag comes back IDLE with
  TEST_RUNNING, and the next `tag-test` passes.
- **`tag_release_check.py`,** because this is `state_machine.c` and `main.c`
  ([verifying a firmware change](../../../../../docs/bench/verifying-firmware.md)).

These were run on BitTag, PresTag and IMUTag for the branch that introduced the
design; the evidence is in decision 0043.
