# Tag Monitor Interface

This document describes the monitor interface implemented by the tag firmware
and host tools. It covers both supported target paths:

- STM32L4 / L432: legacy DebugMonitor and `DCRDR` transport.
- STM32U3 / U375: shared-memory attach and shared-memory RPC transport.

This is a reference document for the current implementation, not a design
proposal.

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

`isMonitorEnabled()` is intentionally broader:

```c
return MONCONNECTED || monitorIsAttached();
```

`MONCONNECTED` is the `DEMCR.VC_CORERESET` attach hint set by the host. This is
important for the STM32L4 path because the host sets vector catch before it has
completed `MONITORSTART`. Runtime sleep code uses `isMonitorEnabled()` so a tag
does not enter standby while a host is still in the early attach/info phase.

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
target-side signal that standby must be suppressed.

### L4 Calls

For a normal L4 monitor call, the host:

1. Waits until `MON_REQ` and `MON_PEND` are clear.
2. Writes `(operand << 8) | operation` to `DCRDR`.
3. Sets `MON_EN | MON_PEND | MON_REQ | VC_CORERESET` in `DEMCR`.
4. Polls `MON_REQ` until firmware clears it.
5. Reads the result from `DCRDR`.

The target `DebugMon_Handler` reads `DCRDR`, decodes the operation, and handles
commands as follows:

- `TAG_MONITORINFO`: immediately writes metadata to `DCRDR` and clears
  `MON_REQ`.
- `MONITORSTART`: starts the monitor session, writes success to `DCRDR`, clears
  `MON_REQ`, and signals the main thread.
- `MONITORSTOP`: writes success, stops the session, clears vector catch and
  request state.
- `PROTOBUF`: latches the protobuf request length, arms the timeout, signals
  the main thread, and returns later through `monitorServicePending()`.

`monitorServicePending()` runs `proto_eval()` in thread context, writes the
encoded response length to `DCRDR`, clears `MON_REQ`, and rearms the short L4
watchdog while attached.

### L4 Detach And Timeout

`MONITORSTOP` clears `VC_CORERESET`, clears `MON_REQ`, and stops the monitor
session.

The L4 watchdog is a 3-second request/session watchdog. If it fires while
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

- sets `host_activity` when the shared request word is still connected;
- treats `request == 0` as a stale kick/detach case;
- handles `MONITORSTOP` by stopping the session and clearing the shared state;
- handles `PROTOBUF` by latching the request length, setting status pending,
  clearing command, and signaling the main thread;
- reports busy/not-attached/bad-command through the shared status word.

`monitorServicePending()` runs `proto_eval()` in main-thread context, writes
the result length to `monitor_shared.result`, sets status done, clears command,
and rearms the heartbeat watchdog.

### U3 Detach And Heartbeat

`MONITORSTOP` is explicit detach. The target stops the session and clears the
shared request word to 0. The host treats `request == 0` as detach completion.

U3 also has target-side disappearance detection. The host periodically writes
`host_activity = 1`. The target watchdog:

1. increments `watchdog_ticks`;
2. disconnects immediately if `request` is no longer connected;
3. clears `host_activity` and rearms if host activity was seen;
4. disconnects on the next quiet heartbeat if `host_activity` remains clear.

The heartbeat period is `MONITOR_HEARTBEAT_PERIOD_S`, currently 5 seconds. The
effective disconnect latency is one to two heartbeat periods after the last
host poll, depending on timer phase.

## Main-Thread Service

Both target paths funnel protobuf evaluation into the same model:

1. Interrupt context records a pending request and signals `tpMain`.
2. The main loop calls `monitorServicePending()`.
3. `monitorServicePending()` runs `proto_eval()`.
4. Any returned monitor work bits are posted as normal ChibiOS events.
5. The target-specific response mailbox is completed:
   - L4 writes response length to `DCRDR` and clears `MON_REQ`.
   - U3 writes response length/status into the shared block.

This keeps protobuf parsing, storage access, and tag command execution out of
the interrupt handler.

## Sleep And Reset Interaction

Monitor attachment is part of the low-power contract:

- Terminal standby uses `isMonitorEnabled()` and must not run during early L4
  attach or during an active monitor session.
- U3 reset recovery can use shared monitor request state and debug control
  state to distinguish monitor attach resets from field power loss.
- Runtime code that only needs to know whether an RPC session is active should
  use `monitorIsAttached()`.
- Runtime code that must avoid sleeping while a host is trying to attach should
  use `isMonitorEnabled()`.

`MONCONNECTED` remains a narrow attach hint: it is `DEMCR.VC_CORERESET`. It is
not the U3 session truth. U3 session truth is the shared request word.

## Current Target Split

STM32L4 targets, including PresTag:

- use `handlersL4.c`;
- use `TAG_MONITORINFO` for attach metadata;
- use `DCRDR` and DebugMonitor request bits for RPC calls;
- need `MONCONNECTED` to suppress standby during early attach.

STM32U3 targets, including U375/U3bmm350:

- use `handlersU3.c`;
- use shared memory for attach metadata;
- use the reserved external IRQ as the runtime kick;
- use the shared request word as the attached-state truth;
- use heartbeat activity to detect host disappearance.

## Validation Notes

Useful regression checks:

- L4/PresTag attach can complete all early `TAG_MONITORINFO` calls.
- L4 `MONITORSTART`, protobuf calls, and `MONITORSTOP` clear request bits.
- U3 attach reaches `MONITOR_CONNECTED_MAGIC` and `SESSION_READY`.
- U3 protobuf calls transition status idle -> pending -> done.
- U3 `MONITORSTOP` clears `request` to 0.
- U3 timeout clears the shared session after missing heartbeat activity.
- Standby entry is suppressed while `MONCONNECTED || monitorIsAttached()`.
