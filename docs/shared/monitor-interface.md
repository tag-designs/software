---
type: design
status: current
summary: Reference for the monitor transport as implemented -- attach, calls, detach and sleep interaction for the L4 DebugMonitor, the U3 shared-memory mailbox and the L4 mailbox (UIUCTag).
---

# Tag Monitor Interface

This document describes the monitor interface implemented by the tag firmware
and host tools. It covers the three supported target paths:

- STM32L4 / L432: legacy DebugMonitor and `DCRDR` transport.
- STM32U3 / U375: shared-memory attach and shared-memory RPC transport.
- STM32L4 / L432 on the mailbox (UIUCTag): the L4 attach, then the U3
  shared-memory RPC transport.

This is a reference for the current implementation. Open work on the transport
is in the [shared contracts worklist](TODO.md).

## Common Model

The monitor is the SWD-accessible control path used by host tools to inspect a
tag, read/write protobuf RPC packets, run tests, download logs, and manage
state.

Both target families share these pieces:

- `include/monitor.h`: monitor opcodes, U3 shared-memory ABI, and firmware
  monitor entry point declarations.
- `embedded/tags/common/core/src/handlers.c`: common monitor state, protobuf
  buffer storage, timeout helpers, and target-path selection.
- `embedded/tags/common/core/src/handlersL4.c`: STM32L4 transport.
- `embedded/tags/common/core/src/handlersL4Mailbox.c`: STM32L4 on the
  shared-memory mailbox, for images built with `TAG_L4_MONITOR_MAILBOX`.
- `embedded/tags/common/core/src/handlersU3.c`: STM32U3 transport.
- `embedded/tags/common/core/src/monitor.c`: protobuf request handling.
- `host/libraries/tagcore/tagmonitor.cc`: host-side attach, call, and detach
  logic.

The common firmware handler keeps monitor protobuf evaluation out of interrupt
context. Interrupts latch the request and signal the main thread; the main
thread calls `monitorServicePending()`, runs `proto_eval()`, and publishes the
response length.

`DebugMonitor_IRQn` must run at a ChibiOS kernel-callable priority. The shared
startup hook sets it to `TAG_DEBUG_MONITOR_PRIORITY`, defaulting to 8.

## Opcodes

The monitor opcodes are shared:

- `TAG_MONITORINFO`: L4 metadata query.
- `MONITORSTART`: start an L4 monitor session.
- `MONITORSTOP`: detach.
- `PROTOBUF`: process a protobuf request already written into the shared
  protobuf buffer.

`TAG_MONITORINFO` operands are:

- `MONITORVERSION`
- `MONITORBUF`
- `MONITORBUFSIZE`
- `TAGSHASTR`

On U3, attach metadata is read from the shared monitor block instead of using
`TAG_MONITORINFO`.

## Attachment State

Firmware exposes two related predicates:

```c
bool monitorIsAttached(void);
bool isMonitorEnabled(void);
```

`monitorIsAttached()` means the target-specific monitor session is active.

`isMonitorEnabled()` is broader, and **differs by transport**:

```c
/* STM32L4: vector catch is the attach mechanism. */
return MONCONNECTED || monitorIsAttached() || monitorAttachGraceActive();

/* STM32U3 (TAG_MONITOR_MAILBOX): the mailbox is the only session truth. */
return monitorIsAttached() || monitorAttachGraceActive();
```

`MONCONNECTED` is the `DEMCR.VC_CORERESET` attach hint set by the host. This is
important for the STM32L4 path because the host sets vector catch before it has
completed `MONITORSTART`. Runtime sleep code uses `isMonitorEnabled()` so a tag
does not enter standby while a host is still in the early attach/info phase.
The L4 transport also treats recent `TAG_MONITORINFO` traffic as a short attach
grace so metadata reads can bridge reliably into `MONITORSTART`.

The U3 path must not consult `MONCONNECTED`: it is an attach hint, not session
truth, and the host does not always clear it. A host that resumes the core
after a timed-out monitor call leaves `VC_CORERESET` set, so a U3 tag that
trusted it would refuse every sleep for the rest of the boot. `MONCONNECTED` is
still read by `monitorResetRecoveryActive()`, where the question really is
whether a debugger connected under reset. See
[decision 0006](../decisions/0006-monitor-u3-attachment-is-the-shared-session.md)
for the fault this caused and its measurement.

## STM32L4 Path

The L4 path is the legacy DebugMonitor transport. It uses:

- `DEMCR.MON_EN` to enable DebugMonitor.
- `DEMCR.MON_REQ` and `DEMCR.MON_PEND` as request/pending bits.
- `DEMCR.VC_CORERESET` as the firmware-visible monitor attach hint.
- `DCRDR` as both request and response mailbox.

### L4 Attach

The host `AttachL4()` sequence is:

1. Read `DEMCR`.
2. Enable debug and write `DEMCR` with `MON_EN | VC_CORERESET`, clearing
   `MON_REQ` and `MON_PEND`.
3. Release reset.
4. Clear halt by writing `DHCSR`.
5. Call `TAG_MONITORINFO` to read:
   - monitor version;
   - protobuf buffer address;
   - SHA string address;
   - protobuf buffer size.
6. Call `MONITORSTART`.

During this metadata phase, `monitorIsAttached()` is still false because the
firmware session has not started yet. `MONCONNECTED` is therefore the only
host-provided signal that standby must be suppressed. Each `TAG_MONITORINFO`
request also arms a short target-side attach grace, covering hosts that briefly
drop the vector-catch hint between metadata reads and `MONITORSTART`.

### L4 Calls

For a normal L4 monitor call, the host:

1. Checks that `MON_REQ` and `MON_PEND` are clear; if either is still set,
   `Call()` logs an error and fails the call rather than waiting.
2. Writes `(operand << 8) | operation` to `DCRDR`.
3. Sets `MON_EN | MON_PEND | MON_REQ | VC_CORERESET` in `DEMCR`.
4. Polls `MON_REQ` until firmware clears it.
5. Reads the result from `DCRDR`.

The target `DebugMon_Handler` reads `DCRDR`, decodes the operation, and handles
commands as follows:

- `TAG_MONITORINFO`: starts or refreshes the short attach grace, immediately
  writes metadata to `DCRDR`, and clears `MON_REQ`.
- `MONITORSTART`: starts the monitor session, writes success to `DCRDR`, clears
  `MON_REQ`, and signals the main thread.
- `MONITORSTOP`: writes success, stops the session, and clears DebugMonitor
  control/request state. The host treats posting this request as sufficient for
  detach because the target may enter terminal low power immediately afterward.
- `PROTOBUF`: latches the protobuf request length, arms the timeout, signals
  the main thread, and returns later through `monitorServicePending()`.

`monitorServicePending()` runs `proto_eval()` in thread context, writes the
encoded response length to `DCRDR`, clears `MON_REQ`, and rearms the 60-second L4
watchdog (`L4_MONITOR_TIMEOUT_S`) while attached.

A protobuf call also writes the request into the protobuf buffer before step 1.
The buffer belongs to the pending request until the firmware clears `MON_REQ`,
and the main thread can take a second or more to reach it (Set RTC runs inside
`proto_eval()`; a self-test runs on the pass after it is accepted). So
`TagMonitor::Rpc()` first waits, up to 2.5 s, for `MON_REQ` and `MON_PEND` to
clear (`WaitL4MonitorIdle()`), and fails without writing if they do not. A
request that an earlier, failed call left pending on the tag is therefore
never overwritten.

**Known firmware defect.** A `PROTOBUF` that reaches `DebugMon_Handler` while
another is still pending is dropped with `monitorClearRequest()`, which clears
the pending request's `MON_REQ` too. The host then reads a reply that was never
written, and the pending request is never served. With the wait above, the
host does not send that second request; the firmware fix is recorded in
`embedded/tags/TODO.md`.

### L4 Detach And Timeout

`MONITORSTOP` clears `VC_CORERESET`, `MON_EN`, `MON_PEND`, and `MON_REQ`, then
stops the monitor session.

The host does not require a post-stop `DCRDR` read on L4 detach. Once the stop
request is posted, firmware owns cleanup and may enter terminal low power before
another SWD transaction can complete.

The L4 attach grace is 10 seconds. Once `MONITORSTART` completes, the L4
watchdog is a 60-second request/session watchdog. If it fires while
`monitor_enabled` is true, it posts `EVT_MONITOR_TIMEOUT`; the main-thread
service path stops the session.

## STM32U3 Path

The U3 path avoids using `DCRDR` and DebugMonitor as the runtime transport.
Instead it uses:

- a fixed shared RAM block at `MONITOR_SHARED_ADDR`;
- a reset-vector-halt attach handshake;
- an external IRQ used as a software kick;
- a target-side heartbeat watchdog.

The shared block ABI is defined in `include/monitor.h`:

```c
#define MONITOR_SHARED_ADDR 0x20000000U
#define MONITOR_SHARED_SIZE 0x40U
#define MONITOR_REQUEST_MAGIC 0xC0FFEE00U
#define MONITOR_CONNECTED_MAGIC 0xDEADBEEFU
#define MONITOR_SHARED_ABI_VERSION 1U
```

The fixed block contains:

- `request`: detached/requested/connected state;
- ABI and debug version;
- protobuf buffer address and size;
- SHA string address;
- command, result, and status words;
- `host_activity` heartbeat word;
- `watchdog_ticks` diagnostics;
- `path_magic` identifying the U3 path.

Normal firmware RAM is moved above `MONITOR_SHARED_SIZE` so C startup does not
erase the host-written request word.

### U3 Attach

The host `AttachU3()` sequence is:

1. Enable debug and reset vector catch without enabling the L4 DebugMonitor
   request path.
2. Release reset and wait for the core to halt at the reset vector.
3. Clear the shared block.
4. Write `MONITOR_REQUEST_MAGIC` to `monitor_shared.request`.
5. Clear any stale monitor kick IRQ pending bit.
6. Resume the core.
7. Poll the shared block until:
   - `request == MONITOR_CONNECTED_MAGIC`; and
   - `MONITOR_SHARED_FLAG_SESSION_READY` is set.
8. Validate ABI version, metadata addresses, buffer size, and SHA address.
9. Read the SHA string directly from target memory.

Target startup has two phases:

- `monitorSharedEarlyInit()` runs before ChibiOS startup. It detects
  `MONITOR_REQUEST_MAGIC`, publishes fixed metadata, sets `host_activity`, and
  writes `MONITOR_CONNECTED_MAGIC`.
- `monitorSharedSessionStart()` runs after ChibiOS startup. It enables the
  reserved kick IRQ, starts the monitor watchdog, sets
  `MONITOR_SHARED_FLAG_SESSION_READY`, and marks the session idle.

### U3 Calls

For a normal U3 monitor call, the host:

1. Reads the shared block and verifies the target is connected.
2. Verifies the command/status words are idle.
3. Writes result/status/command words in the shared block.
4. Clears and then pends the reserved monitor kick IRQ through NVIC
   `ICPR`/`ISPR`.
5. Polls shared `status` until it becomes `MONITOR_STATUS_DONE`, then reads
   `result`.

The U3 kick IRQ handler:

- sets `host_activity` whenever the shared `request` word is non-zero;
- treats `request == 0` as a stale kick/detach case;
- handles `MONITORSTOP` by stopping the session and clearing the shared state;
- handles an exact `get_status` protobuf request directly when `pState->safe`
  is false, using cached runtime state and the last main-loop timestamp;
- handles `PROTOBUF` by latching the request length, setting status pending,
  clearing command, and signaling the main thread;
- reports busy/not-attached/bad-command through the shared status word.

The U3 status fast path exists only to keep host status polling responsive while
the main thread is inside a guarded long operation. It intentionally omits
thread-context work done by the normal status path, including RTC reads, ADC
measurement, debug-log draining, diagnostics, and storage-derived totals. The
reported `millis` field is the timestamp last captured by the main loop, so host
clock-error displays can show stale-time drift during the busy interval.

`monitorServicePending()` runs `proto_eval()` in main-thread context, writes
the result length to `monitor_shared.result`, sets status done, clears command,
and rearms the heartbeat watchdog.

### U3 Detach And Heartbeat

`MONITORSTOP` is explicit detach. The target stops the session and clears the
shared request word to 0. The host treats `request == 0` as detach completion.

U3 also has target-side disappearance detection. The heartbeat is any monitor
call: the kick IRQ sets `host_activity = 1` whenever `request` is non-zero. The
host writes `host_activity = 1` directly only while polling for attach. The
target watchdog:

1. increments `watchdog_ticks`;
2. disconnects immediately if `request` is no longer connected;
3. clears `host_activity` and rearms if host activity was seen;
4. disconnects on the next quiet heartbeat if `host_activity` remains clear.

The heartbeat period is `MONITOR_HEARTBEAT_PERIOD_S`, currently 5 seconds. The
effective disconnect latency is one to two heartbeat periods after the last
host monitor call, depending on timer phase, so a host that makes no call for
5 to 10 seconds is treated as gone.

## STM32L4 Mailbox Path

An STM32L4 image built with `TAG_L4_MONITOR_MAILBOX` (today UIUCTag only)
carries protobuf requests over the U3 shared block, kick and heartbeat, and
keeps the DebugMonitor only for `TAG_MONITORINFO`. Its `project.mk` sets the
define and links `embedded/tags/common/STM32L432xC-mailbox.ld`, which starts
`ram0` at `0x20000040` so the block at `MONITOR_SHARED_ADDR` belongs to no
section or stack. The kick is IRQ 39 (`VectorDC`), which the L432 does not
implement as a peripheral interrupt; NVIC pending still works for it.

### How the host chooses the protocol

The host attaches as on any L4 tag (steps 1-5 of [L4 Attach](#l4-attach)) and
reads `MONITORVERSION`. A version of 0x21 or later means the image answers
the `MONITORPROTOCOL` operand of `TAG_MONITORINFO`:

- `MONITOR_PROTOCOL_DEBUGMON` (1): continue with `MONITORSTART`, as above.
- `MONITOR_PROTOCOL_MAILBOX` (2): start a mailbox session instead.

A host asks only of 0x21 or later, because the DebugMonitor handler
(`handlersL4.c`) has no default case: an unknown operand comes back with the
host's own command word still in `DCRDR`. DebugMonitor images report 0x20 and
are never asked.

### Monitor versions

There are two version constants in `include/monitor.h`, one per transport:

| Constant | Value | Reported by |
| --- | --- | --- |
| `DEBUGVERSION` | 0x20 | STM32L4 DebugMonitor images, as `MONITORVERSION`. Also every STM32U3 image up to fw-v0.7, in the shared block. |
| `DEBUGVERSION_MAILBOX` | 0x21 | Every mailbox image: STM32U3 from the release after fw-v0.7, and STM32L4 with `TAG_L4_MONITOR_MAILBOX`. It is the shared block's `debug_version`, and on an L4 mailbox image also its `MONITORVERSION`. |

The mailbox attach accepts a block version from `DEBUGVERSION` to
`DEBUGVERSION_MAILBOX`, so it takes both old and new STM32U3 images. Hosts up
to v4.0.1 required exactly 0x20, so they refuse 0x21 STM32U3 firmware: ship
the host before the firmware.

### L4 Mailbox Attach

After the protocol call the host (`attach_l4_mailbox()` in tagcore-rs):

1. sets `DBGMCU_CR.DBG_SLEEP` and, if clear, `RCC_AHB1ENR.DMA1EN`, so debug
   accesses to SRAM are served while the tag idles in Sleep;
2. writes `MONITOR_REQUEST_MAGIC` to `request` and pends the kick;
3. polls, re-kicking, until `request == MONITOR_CONNECTED_MAGIC` and
   `MONITOR_SHARED_FLAG_SESSION_READY` are set, then validates the block as on
   U3 (`path_magic` is `MONITOR_PATH_L4_MAGIC`).

Unlike U3, the session request arrives after boot. The firmware:

- rewrites the whole block in `monitorSharedEarlyInit()` on every boot, so
  nothing left there (a flash algorithm or loader runs from
  `0x20000004`-`0x20000020` while the core is halted) can look like a request;
- opens the 10-second attach grace at boot when `MONCONNECTED` shows a host
  attaching under reset, and on each `TAG_MONITORINFO`, keeping the tag out of
  terminal sleep until the request comes;
- enables the kick IRQ unconditionally, and starts the session in the kick
  handler when it finds `MONITOR_REQUEST_MAGIC`, turning DMA1's clock on if it
  was off.

`MONITORSTART` and `PROTOBUF` over the DebugMonitor are refused with a zero
result, so a host that does not know the mailbox (v4.0 and earlier) fails its
attach at once with `Monitor Start failed`. The tag then stays awake, at about
355 uA on UIUCTag, until the attach grace ends 10 s later.

### L4 Mailbox Calls, Detach and Heartbeat

Calls, the fast status answer, `BUSY`, detach and the 5-second heartbeat are
those of the U3 path ([U3 Calls](#u3-calls),
[U3 Detach And Heartbeat](#u3-detach-and-heartbeat)). `MONITORSTOP` is also
still accepted over the DebugMonitor. On disconnect the firmware clears vector
catch and the DebugMonitor bits, and turns DMA1's clock off if its own connect
turned it on; the host's detach clears the clock if the host set it.

**Known gap.** Because the host sets `DMA1EN` before posting the request, the
firmware's connect finds it already on and does not take ownership. A session
that ends by heartbeat timeout rather than a host detach therefore leaves the
clock on. It costs nothing on UIUCTag, whose Standby resets RCC, but would on
an image that idles in Stop. Recorded in `embedded/tags/TODO.md`.

## Main-Thread Service

Both target paths funnel protobuf evaluation into the same model:

1. Interrupt context records a pending request and signals `tpMain`.
2. The main loop calls `monitorServicePending()`.
3. `monitorServicePending()` runs `proto_eval()`.
4. The target-specific response mailbox is completed:
   - L4 writes response length to `DCRDR` and clears `MON_REQ`.
   - U3 and the L4 mailbox write response length/status into the shared
     block.

This keeps protobuf parsing, storage access, and tag command execution out of
the interrupt handler. A state-changing command (Start, Stop, Erase, Self-test,
Calibrate) runs its transition inside `proto_eval()`, so its reply carries the
resulting `Status`; see
[Monitor Commands](../../embedded/tags/common/core/design/monitor-commands.md).
The main loop serves requests only after a `StateMachine()` pass, and goes
straight back to the state machine after serving one.

## Sleep And Reset Interaction

Monitor attachment is part of the low-power contract:

- Terminal sleep uses `isMonitorEnabled()` and must not be entered during early
  L4 attach or during an active monitor session.
- On targets built with `TAG_MONITOR_RESET_RECOVERY` (IMUTagNand and
  IMUTagNandBmp581), reset recovery distinguishes a monitor attach reset from
  field power loss using `MONCONNECTED`, an external reset at boot, and
  `DHCSR.C_DEBUGEN`; it does not read the shared request word.
- Runtime code that only needs to know whether an RPC session is active should
  use `monitorIsAttached()`.
- Runtime code that must avoid sleeping while a host is trying to attach should
  use `isMonitorEnabled()`.

`MONCONNECTED` remains a narrow attach hint: it is `DEMCR.VC_CORERESET`. It is
not the U3 session truth. U3 session truth is the shared request word.

**A host must keep talking to an attached tag.** On L4 the session ends when
the 60 s watchdog above runs out without a request, and an IDLE tag then
enters terminal sleep, after which every SWD access fails (the base reports
status `0x81`, or the USB transfer times out) until a reset.

**UIUCTag images up to and including fw-v0.6 end the session after 5 s, not
60 s.** Where `RANGE_MULTIPLIER` was defined (UIUCTag and the BitPresTag
variants) the session raised MSI to 24 MHz and wrote TIM2's prescaler to
compensate; TIM2 is the free-running ChibiOS system timer and a prescaler
write only takes effect at an update event it never reaches, so the tick ran
12x fast for the whole session. Measured on a UIUCTag breakout (fw-v0.6,
2.496 V): 2.9 mA attached, then 0.16 uA about 4.6 s after the last request;
400 ms polls kept it attached for 30 s. Later images do not change the clock
for the session (only `data_logAck()` raises it, around one page of a
download); the same breakout then stayed attached through 15 s of quiet and
drew 433 uA attached. Hosts should keep the gap between requests well under
5 s for the tags already in the field: Tag Programmer polls every 400 ms, and
attaches again at the start of a session if the tag does not answer.

## Current Target Split

STM32L4 targets other than UIUCTag, including PresTag:

- use `handlersL4.c`;
- use `TAG_MONITORINFO` for attach metadata;
- use `DCRDR` and DebugMonitor request bits for RPC calls;
- need `MONCONNECTED` and the L4 attach grace to suppress standby during early
  attach.

UIUCTag (STM32L432, `TAG_L4_MONITOR_MAILBOX`):

- uses `handlersL4Mailbox.c` and `STM32L432xC-mailbox.ld`;
- uses `TAG_MONITORINFO` for the version and protocol, then the shared block;
- uses IRQ 39 as the kick, and the shared request word and heartbeat as on U3;
- keeps `MONCONNECTED` and the attach grace to suppress standby until the
  session request arrives.

STM32U3 targets, IMUTagNand and IMUTagNandBmp581:

- use `handlersU3.c`;
- use shared memory for attach metadata;
- use the reserved external IRQ as the runtime kick;
- use the shared request word as the attached-state truth;
- use heartbeat activity to detect host disappearance.

## Validation Notes

Useful regression checks:

- L4/PresTag attach can complete all early `TAG_MONITORINFO` calls.
- L4 `MONITORSTART`, protobuf calls, and `MONITORSTOP` clear request bits; L4
  detach does not require a post-stop response read.
- U3 attach reaches `MONITOR_CONNECTED_MAGIC` and `SESSION_READY`.
- U3 protobuf calls transition status idle -> pending -> done.
- U3 `MONITORSTOP` clears `request` to 0.
- U3 timeout clears the shared session after missing heartbeat activity.
- On L4, standby entry is suppressed while `MONCONNECTED`,
  `monitorIsAttached()`, or the L4 attach grace is active. On U3,
  `isMonitorEnabled()` ignores `MONCONNECTED` and there is no attach grace, so
  only `monitorIsAttached()` suppresses sleep.
- L4 mailbox: `MONITORVERSION` reads 0x21 and `MONITORPROTOCOL` reads 2; the
  session reaches `MONITOR_CONNECTED_MAGIC` with `path_magic` 0x554C0004; an
  idle session drops after 5-10 s; a v4.0 host fails its attach and the tag
  returns to its floor 10 s later. Checked on a UIUCTag breakout 2026-11-04.
