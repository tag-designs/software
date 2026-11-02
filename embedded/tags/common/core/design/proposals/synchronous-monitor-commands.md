---
type: proposal
status: proposed
summary: Make the monitor's state-changing commands synchronous - the firmware holds the reply to Start, Stop, Erase and Self-test until the state machine has acted, and answers with the resulting Status or an error - so a host never has to infer from later polls whether a queued command ran; with the firmware design for both STM32 families, the host's detection of old and new firmware, what it replaces, and how to check it on the bench.
---

# Synchronous monitor commands

**Proposal.** Start, Stop, Erase and Self-test are synchronous commands: a
host wants to know that each one happened. The firmware implements them
asynchronously. It acknowledges the request, sets a `MON_WORK_*` event bit,
and lets a later state-machine pass act on it. Nothing ties the reply to the
outcome, so an OK reply means only "queued".

Change the firmware so that:

- the reply to a state-changing command is written **after** the state
  machine has acted on it;
- the reply is an Ack carrying the resulting **`Status`** when the command
  reached its target state, and an error code when it did not;
- a command the state machine has not yet consumed stays in flight, never
  dropped, so the host simply waits longer.

Erase and Self-test keep their own asynchronous states. Their reply means the
tag *has entered* sRESET or TEST, and the work then runs in later passes, which
the host follows by polling that state.

The host tells the new firmware from the old by the reply, with no protocol
version: a `Status` payload means synchronous; a bare OK means the old
firmware, which keeps today's poll-and-resend path. Both families change in the
same shared code, `monitor.c`, `state_machine.c` and the two `handlers*.c`.

This replaces two patches, both from 2026-11-02:

- the re-post of `MON_WORK_*` from the recovery pass in
  [the tags worklist](../../../../TODO.md);
- the host's resend in `Tag::stop_confirmed()` (`tagcore-rs`), which stays only
  for old firmware.

## What was seen

On UIUCTag (STM32L432, breakout base, 2.50 V), `tag-start --start-now`
followed at once by `tag-stop`:

- The tag answered the stop OK, was RUNNING when it was sent, and was still
  RUNNING after 30 s of polls, in 20 of 20 cycles.
- A stop resent in the same session 4 s later worked at the next poll.

The cause was confirmed by a test image. Every attach resets the tag, and the
first `StateMachine()` pass after the boot runs reset recovery, which returns
before the `MON_WORK_*` checks. `main()` had already cleared the events it
passed in, so a stop evaluated in that pass was lost. Re-posting the work from
the recovery branch made 20 of 20 first stops work.

That is one way for a queued command to vanish between the OK and the
transition, but it is not the only one. Any path through `StateMachine()` that
returns before the work checks has the same effect, and the host cannot see
the difference between "queued" and "done". The host workarounds are all
inference from later polls:

- `tag-start` polls until the tag leaves IDLE;
- `tag-stop` and `tag-reset` poll for FINISHED and resend;
- Tag Monitor judges a start from the statuses that follow;
- Tag Programmer resends its stop on a tick.

## How a command flows now

1. The interrupt (`DebugMon_Handler` on L4, the kick IRQ on U3) latches the
   request.
2. At the top of the main loop, `monitorServicePending()` runs `proto_eval()`
   on the main thread. For a command it sets a bit in `work` and encodes
   `errAck(Ack_OK)`.
3. The reply is written at once: DCRDR on L4, `monitor_shared.result` on U3.
   The host is released.
4. `chEvtAddEvents(work)`; `main()` folds the bits into `current_events`.
5. `StateMachine(current_events)` acts on them, if it reaches the work checks.

Steps 2 and 3 run on the main thread, the thread that owns state, on both
families. That is what makes this change cheap: the reply can simply wait for
step 5.

## Firmware design

### A deferred reply

`proto_eval()` already encodes every reply into `ProtoBuf`. For the four
commands it instead:

- records the command in a single in-flight slot;
- sets the `work` bit as now;
- returns a marker meaning "reply deferred".

`monitorServicePending()` then leaves the request in flight:

- on L4, `MON_REQ` stays set and DCRDR is not written;
- on U3, `monitor_shared.status` stays busy;
- the session timeout stays disarmed, as it already is while a request is
  being served.

A new `monitorCompleteCommand(err)` encodes the reply and releases the host.
The reply is `statusAck()`, the same Status a GetStatus returns, with `err`
set to the outcome. It then rearms the timeout, which is the tail of
`monitorServicePending()` today. `StateMachine()` calls it from the work
checks, at the point each command's outcome is known.

| Command | Completed when | Reply on success | Reply on failure |
| --- | --- | --- | --- |
| Start | `Configured(T_INIT, STARTCMD)` returns | `OK` + Status, CONFIGURED or RUNNING | `NXIO` + Status, ABORTED when the stored configuration failed to write (`CONFIGERROR`) |
| Stop | `Finished` or `Aborted(T_INIT, STOPCMD)` returns | `OK` + Status, FINISHED or ABORTED | — |
| Erase | `Reset(T_INIT)` has set sRESET and recorded the marker, **before** the sweep | `OK` + Status, sRESET | — |
| Self-test | `SelfTest(T_INIT)` has set TEST, before the test runs | `OK` + Status, TEST | — |
| any of them | the work check finds the state does not allow it | | `PERM` + Status |

The gate in `monitor_request_allowed()` still refuses a command early when
the state is plainly wrong. The work check is the authority, because it sees
the state after recovery.

### Nothing is dropped

A command stays in flight until a work check completes it:

- **A pass that returns before the checks**, such as the recovery pass,
  leaves the slot set.
- **`main()` re-posts the bit** each pass while the slot is set, so the next
  pass sees it.

This is the recovery re-post made general. It is needed because the bit is
cleared before `StateMachine()` runs; deriving the work from the slot each
pass, instead of from the event bit, is equivalent and may read better.

**Ordering with recovery comes for free.** Today a stop evaluated before the
recovery pass is lost. With the slot it waits for the next pass, which runs
after recovery has restored the clock and the log cursors, so the transition
never acts on pre-recovery state.

### Erase

`Reset(T_INIT)` sweeps until `chEvtGetEventsX()` shows monitor work, then
yields. With the reply held, no new request can arrive, so it would sweep the
whole erase before answering.

Complete the command inside `Reset(T_INIT)`, after `recordState(reason)` and
before `eraseExternalStart()`. The host learns at once that the erase is
under way and polls sRESET to IDLE as it does now. A Reset(T_INIT) that
*resumes* an erase already in progress also completes it, so a retried erase
is answered.

### Self-test

`SelfTest(T_INIT)` currently runs the whole test inside one pass,
`deviceInit(true)` then `test()`, and returns `Idle(T_INIT)`. TEST is visible
only to a fast status answered mid-test, and some tests are long. Split it the
same way as erase:

- **`SelfTest(T_INIT)`** runs `deviceInit(true)`, sets TEST, marks
  `test_status` as running so a stale result cannot be read as this test's,
  completes the command, and returns a sleep mode that keeps the loop running.
  `Reset()` returns `SLEEP` mid-sweep for the same reason, since the host may
  detach after the reply.
- **`SelfTest(T_CONT)`** runs `test()`, then returns `Idle(T_INIT)` with the
  result in `test_status`. `StateMachine()` already dispatches TEST to
  `SelfTest(T_CONT)`, so no new dispatch is needed.

The protocol is then: the test request arrives; `SelfTest(T_INIT)` replies
TEST; the next pass runs the test; the host polls until IDLE and reads
`test_status`. That is what `tag-test` and Tag Monitor do today, except that
they now know the test started.

**Polling during the test needs care on L4.** Debug access fails in bursts
then (`embedded/bases/TODO.md`), and the bus matrix stalls without a DMA clock
([UIUCTag worklist](../../../../UIUCTag/TODO.md)). The host already pauses
its polls after a self-test is accepted (Tag Monitor's `QUIET_AFTER_BUSY`) and
keeps `DMA1EN` set for the session. Keep both.

**A reset during the test** is recovered from the state markers, and
`SelfTest` records none, so the tag comes back IDLE. With `test_status` marked
running at `T_INIT`, the host can tell an interrupted test from a finished one.
Check what recovery leaves in `test_status` when the prototype is built.

### What does not change

- **GetInfo, GetStatus, GetConfig, SetRtc, the logs and Calibrate** keep
  their current replies.
- **The fast status** answered in the interrupt while `pState->safe` is false
  is unchanged.
- **No proto change is needed.** `Ack.payload` already has `status`, and
  `Status` carries `state`, `test_status` and `debug_message`.
- **No monitor version bump is needed.** The reply is self-describing.

## Host design

`Tag::start`, `Tag::stop`, `Tag::erase` and `Tag::test` return the reply's
`Status` when it carries one, as `Ok(Some(status))`. They return `Ok(None)`
for a bare OK.

- **`Some`**: the command is done or definitely under way. `tag-stop` prints
  it and exits; `tag-start` skips its leave-IDLE poll; Tag Monitor reports the
  state at once; `tag-reset` polls only the erase.
- **`None`**: old firmware. Every caller keeps today's path, including
  `stop_confirmed()`'s resend.

**Old hosts keep working.** They read `err` and ignore the payload, so they
see OK as they do now; they simply keep polling for something already done.
That covers the v4.0 tools in the field and the legacy C++ tools.

**Timeouts.** Every synchronous reply is short: a marker write and a device
initialisation at most. Erase and self-test do their long work after the
reply. The `Tag` lock is held no longer than for any other request.

## Rollout

1. **Prototype on UIUCTag.** It is not fabricated, so it is the agreed L4
   testbed and changing its monitor behaviour has no field cost.
2. **Host support next**, since it is harmless against old firmware.
3. **The other tags in the firmware batch.** The change is in shared code, so
   BitTag, PresTag, CompassTag and IMUTag all get it at their next image, and
   are qualified together.

## How to check it

On each target, after flashing and `tag-test`:

- **Start then stop at once, 20 cycles,** with a host that sends one stop and
  takes its reply as the answer. Expect 20 of 20 FINISHED replies, each
  confirmed by a later status. On old firmware this lost 20 of 20 first
  stops.
- **Stop sent to an IDLE tag** returns `PERM` with the IDLE Status.
- **Start with a configuration whose write fails**, if one can be arranged,
  returns `NXIO` with ABORTED.
- **Erase from FINISHED** replies sRESET at once, and the tag reaches IDLE in
  the usual time.
- **Self-test** replies TEST at once; polls then see TEST until the test ends
  and IDLE with this test's `test_status`. A reset mid-test leaves IDLE with
  `test_status` showing the test did not finish.
- **`tag_lifecycle_check.py` and `tag_release_check.py`,** because this
  changes `state_machine.c`. Current must not move.
- **IMUTag only:** an attach storm (`tag_attach_storm.py`), and a self-test
  that runs longer than the U3 heartbeat period, with the host polling
  throughout.

## Open questions

- **Slot or event bit.** Whether the in-flight slot replaces the
  `MON_WORK_*` bits for these four commands, or sits beside them. Replacing
  them is simpler to reason about, while keeping them leaves Calibrate's path
  as it is.
- **A session that ends mid-command.** If the host detaches, for example on a
  `MONITORSTOP` timeout, while a command is in flight, the command should
  still complete and its reply be discarded. Check that
  `monitorCompleteCommand()` handles `monitor_enabled` being false, as the
  current tail does.
- **Calibrate** enters a long-lived state and is stopped by Stop. It could
  follow the erase pattern, replying on entry, in the same change.
