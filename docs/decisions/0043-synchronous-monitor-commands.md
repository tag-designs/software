---
type: decision
status: accepted
summary: The monitor's state-changing commands run their transition inside proto_eval() and reply with the resulting Status, PERM or NXIO, instead of acknowledging the request and leaving the work to a later state-machine pass; the main loop runs the state machine before serving the monitor, and the host reads the Status, polling only for older firmware.
---

# 0043. Monitor commands are synchronous

Date: 2026-11-03

## Context

Start, Stop, Erase and Self-test are synchronous as far as a host is
concerned: it wants to know that each one happened. The firmware implemented
them asynchronously. `proto_eval()` set a `MON_WORK_*` event bit, replied OK,
and a later `StateMachine()` pass acted on the bit, if it reached the checks.
OK meant only "queued", so every host inferred the outcome from later status
polls: `tag-start` polled until the tag left IDLE; `tag-stop` and `tag-reset`
polled for FINISHED and resent the stop; Tag Monitor judged a start from the
statuses that followed; Tag Programmer resent its stop on a timer.

On 2026-11-02 that gap lost commands. On UIUCTag (STM32L432, breakout base,
2.50 V), `tag-start --start-now` followed at once by `tag-stop` was answered
OK and left the tag RUNNING after 30 s of polls, in 20 of 20 cycles. Every
attach resets the tag, and the first pass after the boot is reset recovery,
which returned before the `MON_WORK_*` checks; `main()` had already cleared
the bits, so the stop vanished. Any other path through `StateMachine()` that
returned early would do the same, and the host could not tell "queued" from
"done".

## Decision

- **`proto_eval()` carries out each state-changing command before it
  replies.** `commandAck()` calls `StateMachineCommand()`, which runs the
  transition (`Configured(T_INIT)`, `Finished(T_INIT)`, `resetEnter()`,
  `SelfTest(T_INIT)`, `Calibrating(T_INIT)`) on the main thread, and the reply
  is the resulting Status: OK when it happened, PERM with the state unchanged
  when the state does not allow it, NXIO when a start or calibrate did not
  reach its state. Calibrate is included.
- **Erase and Self-test reply on entering sRESET or TEST.** The sweep and the
  test run on later passes, and the host polls for IDLE. An erase sent while
  the tag is in sRESET continues the sweep and replies sRESET, so a host can
  resend an erase, or pick up one under way, without restarting it. A self-test cut
  short by a reset is abandoned, leaving `test_status` TEST_RUNNING, not
  rerun.
- **The `MON_WORK_*` bits are removed.** Commands are a `MonitorCommand` enum
  for the length of one call; the handlers post no work events.
- **The main loop runs the state machine, then serves the monitor if a
  session is open, or calls `godown()` if not.** A served request goes
  straight back to the state machine. The boot's reset recovery therefore
  always runs before any request is served, and the tag never sleeps on a
  mode chosen before a command changed the state.
- **The host reads the Status.** `Tag::start`, `stop`, `erase`, `test` and
  `calibrate` return `Ok(Some(status))` for a synchronous reply and
  `Ok(None)` for a bare OK, which keeps the old polling and resend path.

The design is described in
[Monitor Commands](../../embedded/tags/common/core/design/monitor-commands.md).

## Alternatives considered

- **Re-post the work from the recovery pass.** `chEvtAddEvents(events &
  MON_WORK_ALL)` in the recovery branch fixed the observed loss (20 of 20
  first stops on a test image). Rejected: it closes one early return, not the
  class of them, and the reply still means only "queued".
- **Hold the reply until the state machine has acted.** The first proposal
  kept `MON_WORK_*`, recorded the command in an in-flight slot, left the
  monitor request pending (L4 `MON_REQ`, U3 status busy), re-posted the bit
  each pass, and released the host from the state machine's work checks with
  a new `monitorCompleteCommand()`. Rejected as too complicated: it adds a
  second reply path, an in-flight state that a detach or timeout can strand,
  and a held monitor session, to reach the same result as running the
  transition where the request is decoded. `proto_eval()` already runs on the
  main thread, the thread that owns the state, so the transition can simply
  run there.
- **Serve the monitor before the state machine.** Rejected: the sleep mode
  `godown()` uses would then be chosen before the command changed the state,
  and the first request after a boot would be served before reset recovery.
- **Host-side resends only.** Already in place, and kept for older firmware,
  but a host that sends one stop and waits, including the legacy C++ tools,
  still lost it.

## Evidence

All on branch `sync-monitor-commands`:

- **UIUCTag** (L4, 2.50 V), 2026-11-02 prototype: 20 of 20 single stops sent
  at once after a start replied FINISHED, where `main` lost 20 of 20.
- **BitTag** (L4, 2.5 V, bittag-base-v7): stops, test, erase and PERM replies
  correct in 10 of 10 cycles; release check passed (idle 0.122, run 0.45,
  stopped 0.12 uA).
- **PresTag** (L4): release check and a matrix of every command in every
  state passed.
- **IMUTag** (U3, 3.69 V), commit `eb0ba0d3`, 2026-11-03: release check passed
  (idle 6.2, run 578 uA at 400 Hz); every command in every state as specified;
  erase with an attach during the sweep; a delayed start; calibrate and its
  refusals in one session; an attach during a run cost 900 of 119,100
  samples. The IMUTag matrix also found the one fault in the first version: a
  reset during TEST reran the test. `test_pending` fixed it (`3182f7f4`), and
  three interrupted tests then came back IDLE with TEST_RUNNING.
- **CompassTag** (L4, 2.50 V), commit `eb165461`, 2026-11-03: every command in
  every state as specified, calibrate in one session, and 20 of 20 stops sent
  in a fresh attach straight after a start replied FINISHED; rest 0.22 uA. The
  host tools' bare-OK path passed against the fw-v0.7 image first.

## Consequences

- A host can take a command's reply as its result. `stop_confirmed()`,
  `tag-start`, `tag-test` and Tag Monitor do; older firmware still gets the
  polling path.
- Hosts that read only the Ack code, including the v4.0 tools and the legacy
  C++ tools, keep working: they see OK and poll for something already done.
- No protobuf or monitor version change: `Ack.payload` already carried a
  Status.
- A request that runs a transition holds the main thread for that
  transition; Start includes the configuration write and device setup.
- Calibration does not survive an attach, so a calibration session must stay
  in one monitor session.
- Hosts send an erase to a tag found in sRESET. Older firmware refuses it
  with PERM, so the tools that do so treat that refusal as harmless.
