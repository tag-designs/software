## STM32U375 Monitor Interface Design

The current monitor interface has had reliability problems on STM32U3 tags,
mostly around interpretation of `DEMCR` state bits. Low-power sleep/stop work
has also exposed a broader weakness: the firmware cannot reliably determine
whether the host monitor is still attached if that decision depends only on
`DEMCR`.

This design keeps the existing DebugMonitor RPC mechanism, but changes STM32U3
attachment and session ownership so `DEMCR` is no longer the source of truth.
STM32L4 tags continue to use the current monitor path.

### Goals

- Connect STM32U3 tags under reset.
- Let the host discover monitor metadata before relying on monitor RPC calls.
- Keep protobuf RPCs as the normal command/data transport after attach.
- Let the target detect a vanished host with a heartbeat watchdog, while normal
  detach remains an explicit `MONITORSTOP` command.
- Preserve STM32L4 behavior while changing only the STM32U3 path.
- Keep DebugMonitor at a ChibiOS kernel-callable priority because the handler
  calls IRQ-safe ChibiOS APIs.

### Connection Model

The host connects through a reset-vector halt:

1. Host enters SWD debug mode, enables core debug, and configures reset vector
   catch.
2. Host asserts reset, releases reset, and waits for the core to halt at or
   near the reset vector before C startup.
3. Host writes `MONITOR_REQUEST_MAGIC` to a fixed RAM address through SWD.
4. Host releases the core.
5. Target early initialization sees the request, fills a shared monitor block,
   and replaces the request word with `MONITOR_CONNECTED_MAGIC`.
6. Host reads the shared block to learn monitor version, protobuf buffer
   address/size, and SHA-string address.
7. Host uses the existing protobuf RPC payloads. On STM32U3, the wake/kick
   transport for those RPCs moves from DebugMonitor/`DCRDR` to shared memory
   plus the reserved external IRQ.

This removes the current circular dependency where the host must successfully
call `TAG_MONITORINFO` before it knows the target-side RPC buffer.

### Shared State

The STM32U3 monitor shared block lives at a fixed RAM address known to both host
and firmware. The host only needs to know the address and layout from
`include/monitor.h`.

Example ABI:

```c
#define MONITOR_SHARED_ADDR       0x20000000u
#define MONITOR_SHARED_SIZE       0x40u
#define MONITOR_REQUEST_MAGIC     0xC0FFEE00u
#define MONITOR_CONNECTED_MAGIC   0xDEADBEEFu
#define MONITOR_PATH_U3_MAGIC     0x55330003u
#define MONITOR_SHARED_ABI_VERSION 1u
#define MONITOR_HEARTBEAT_PERIOD_S 5u
#define MONITOR_STATUS_IDLE       0u
#define MONITOR_STATUS_PENDING    1u
#define MONITOR_STATUS_DONE       2u
#define MONITOR_STATUS_BUSY       3u
#define MONITOR_STATUS_BAD_COMMAND 4u

typedef struct __attribute__((packed, aligned(4))) {
  volatile uint32_t request;          /* 0, request magic, or connected magic */
  volatile uint32_t abi_version;
  volatile uint32_t debug_version;
  volatile uint32_t flags;
  volatile uint32_t buf_addr;
  volatile uint32_t buf_size;
  volatile uint32_t sha_addr;
  volatile uint32_t command;          /* host writes op | operand<<8 */
  volatile uint32_t result;           /* target writes return value */
  volatile uint32_t status;           /* target writes command status */
  volatile uint32_t host_activity;    /* request handler sets; timer clears */
  volatile uint32_t watchdog_ticks;   /* optional diagnostics */
  volatile uint32_t path_magic;       /* target writes compiled monitor path */
} monitor_shared_t;
```

`request` is the state word for the attach session:

- `0`: detached, idle, or no host request.
- `MONITOR_REQUEST_MAGIC`: the host requested attach before releasing the
  reset-vector halt.
- `MONITOR_CONNECTED_MAGIC`: the target accepted the request and has published
  fixed metadata.

The target writes `MONITOR_CONNECTED_MAGIC` only after it has filled the other
metadata fields and issued a memory barrier. The host must treat any other
request value as disconnected or not yet ready. `MONITOR_SHARED_FLAG_SESSION_READY`
is set later, after ChibiOS starts and the interrupt/watchdog path is armed.
Runtime firmware should use the request word itself as the attached-monitor
indicator: `MONITOR_CONNECTED_MAGIC` means attached, and `0` means detached.

The shared block must be outside normal `.bss`/`.data` so C startup does not
erase the request word that the host wrote while the core was halted. The
implementation reserves a hole at the bottom of U3 SRAM by moving the normal RAM
origin above `MONITOR_SHARED_SIZE`; firmware accesses the block through the fixed
address in `monitor.h`, and no C object owns that RAM.

Hardware bring-up showed that writes attempted while physical reset is still
asserted may not persist or may not be visible through ST-LINK. The more reliable
sequence is to hold the core with reset vector catch, write the shared request
while the core is halted at the reset vector, then resume before startup reaches
the monitor early-init hook.

`DHCSR` and `DEMCR` are host-side debug control registers used to create that
reset-vector halt and for diagnostics. They are not U3 monitor protocol state;
the firmware protocol decision is the shared `request` word.

### Target Initialization

Early target initialization has two phases:

1. A raw, pre-session phase publishes fixed metadata if the host requested a
   connection.
2. A ChibiOS-aware phase enables the runtime monitor session and starts the
   watchdog after `chSysInit()`.

Sketch:

```c
static bool monitor_requested_at_boot;

void monitorSharedEarlyInit(void) {
  bool requested = monitor_shared.request == MONITOR_REQUEST_MAGIC;

  monitor_shared.abi_version   = MONITOR_SHARED_ABI_VERSION;
  monitor_shared.debug_version = DEBUGVERSION;
  monitor_shared.flags         = 0;
  monitor_shared.buf_addr      = (uint32_t)ProtoBuf;
  monitor_shared.buf_size      = (uint32_t)sizeof(ProtoBuf);
  monitor_shared.sha_addr      = (uint32_t)SHAStr;
  monitor_shared.command       = 0;
  monitor_shared.result        = 0;
  monitor_shared.status        = MONITOR_STATUS_IDLE;
  monitor_shared.host_activity = 0;
  monitor_shared.watchdog_ticks = 0;
  monitor_shared.path_magic    = MONITOR_PATH_U3_MAGIC;

  if (requested) {
    monitor_shared.host_activity = 1;
    __DMB();
    monitor_shared.request = MONITOR_CONNECTED_MAGIC;
    monitor_requested_at_boot = true;
  } else {
    monitor_shared.request = 0;
    monitor_requested_at_boot = false;
  }
}

void monitorSharedSessionStart(void) {
  if (!monitor_requested_at_boot)
    return;

  chSysLock();
  monitor_enabled = true;
  monitor_pending = false;
  monitor_timeout_pending = false;
  monitorArmWatchdogI();
  chSysUnlock();
}
```

The ordering with `main()` should be:

```c
int main(void) {
  uint32_t rstFlags = RCC->CSR;
  monitorSharedEarlyInit();
  halInit();
  chSysInit();
  monitorSharedSessionStart();
  ...
}
```

`monitorSharedEarlyInit()` runs before `halInit()` for reset timing and must not
call any ChibiOS or HAL APIs. It may only touch fixed RAM, core debug registers,
and link-time addresses such as `ProtoBuf` and `SHAStr`. The watchdog must still
wait until after `chSysInit()`.

### No U3 DebugMonitor RPC

For STM32U3, attach metadata no longer uses `TAG_MONITORINFO`; the host reads
it from the shared block. U3 also does not keep an awake DebugMonitor/DCRDR
compatibility path. All runtime U3 monitor traffic uses shared memory plus the
stolen external IRQ described below. Each valid IRQ-driven host interaction
sets `monitor_shared.host_activity = 1`, which is the target-side evidence that
the host is still polling.

### Host-Activity Watchdog

The watchdog is target-side host-disconnect detection:

1. The monitor request handler sets `monitor_shared.host_activity = 1` whenever
   it accepts a valid host request.
2. A periodic virtual timer fires every `MONITOR_HEARTBEAT_PERIOD_S` seconds.
3. If `host_activity` is nonzero, the timer clears it and rearms itself.
4. If `host_activity` is already zero when the timer fires, the target assumes
   the host disappeared. It clears `monitor_shared.request`, stops the monitor
   session, and posts `EVT_MONITOR_TIMEOUT` to the main thread.
5. If the host has cleared `request`, the same disconnect path runs without
   waiting for a second quiet heartbeat window.

This is a one-missed-window policy after the first timer clear. With the
5-second heartbeat period, a disconnect is detected roughly 5 to 10 seconds
after the last host poll, depending on timer phase and scheduling latency.

Sketch:

```c
static void monitor_watchdog_cb(virtual_timer_t *vtp, void *arg) {
  (void)vtp;
  (void)arg;

  chSysLockFromISR();
  if (!monitor_enabled) {
    chSysUnlockFromISR();
    return;
  }

  monitor_shared.watchdog_ticks++;
  if (monitor_shared.host_activity != 0) {
    monitor_shared.host_activity = 0;
    chVTSetI(&monitor_timer, chTimeS2I(MONITOR_HEARTBEAT_PERIOD_S),
             monitor_watchdog_cb, NULL);
  } else {
    monitor_shared.request = 0;
    monitor_timeout_pending = true;
    if (tpMain)
      chEvtSignalI(tpMain, EVT_MONITOR_TIMEOUT);
  }
  chSysUnlockFromISR();
}
```

`monitorStopI()` should clear `request`, `host_activity`, `monitor_enabled`, and
pending state. It should also disarm the watchdog. The main thread should remain
the place where protobuf evaluation happens.

### Shared State in Linker Script

For STM32U375, reserve a small fixed block at the start of non-secure SRAM and
move the normal RAM regions above it. The exact length should be defined in
`include/monitor.h` and checked against the C struct size.

Example:

```ld
MEMORY
{
    ram0 (wx) : org = 0x20000040, len = 256k - 0x40
    ram1 (wx) : org = 0x20000040, len = 192k - 0x40
    ram2 (wx) : org = 0x20030000, len = 64k
}
```

Firmware should assert:

- `sizeof(monitor_shared_t) <= MONITOR_SHARED_SIZE`
- `monitor_shared` accesses are through a fixed pointer to `MONITOR_SHARED_ADDR`
- fields are 32-bit aligned for ST-LINK 32-bit memory access

### Waking the Core for Monitor RPC

The STM32U3 failures point to a separate problem from initial attach:
`DEMCR.MON_REQ`/`MON_PEND` can request DebugMonitor while the core is awake, but
they should not be treated as a reliable wake source when the core is sleeping
in `WFI`.

`DebugMonitor_IRQn` is a system exception, not an external NVIC interrupt. The
host therefore cannot wake the core by writing DebugMonitor's exception number
to `STIR`, and it cannot use an `ISPR` bit for DebugMonitor either. The wake
source has to be a normal external IRQ.

For STM32U3, reserve an otherwise-unused external interrupt as the monitor kick
IRQ. `FDCAN1_IT0_IRQn` is a good first candidate:

- the STM32U375 header defines `FDCAN1_IT0_IRQn = 39`;
- ChibiOS names it `STM32_FDCAN1_IT0_HANDLER` / `VectorDC`;
- both U3 tag configurations currently have `STM32_CAN_USE_FDCAN1 FALSE`;
- it is low enough in the IRQ table to be easy to pend with `NVIC_ISPR1`.

USB1 is another plausible candidate because `STM32_USB_USE_USB1` is also false,
but USB carries more future feature pressure. Prefer FDCAN1_IT0 unless a board
or future product variant needs CAN.

The stolen IRQ handler is the only U3 runtime monitor kick path. It reads the
shared `command` word, handles explicit `MONITORSTOP` detach requests, latches
protobuf requests for main-thread processing, writes `result`/`status`, and
clears `command` when the command has been accepted. A cleared `request` remains
a defensive detached-state check for stale or duplicate kicks.

Target sketch:

```c
#define MONITOR_KICK_IRQN FDCAN1_IT0_IRQn

OSAL_IRQ_HANDLER(STM32_FDCAN1_IT0_HANDLER) {
  OSAL_IRQ_PROLOGUE();

  uint32_t command = monitor_shared.command;
  uint8_t operation = command & 0xff;
  int operand = command >> 8;

  chSysLockFromISR();

  if (monitor_shared.request == 0) {
    monitorAcknowledgeDetachI();
    chSysUnlockFromISR();
    OSAL_IRQ_EPILOGUE();
    return;
  }

  monitor_shared.host_activity = 1;

  switch (operation) {
    case MONITORSTOP:
      monitorAcknowledgeDetachI();
      break;

    case PROTOBUF:
      if (monitor_enabled && !monitor_pending) {
        monitor_operand = operand;
        monitor_pending = true;
        monitor_shared.result = 0;
        monitor_shared.status = MONITOR_STATUS_PENDING;
        monitor_shared.command = 0;
        if (tpMain)
          chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
      } else {
        monitor_shared.status = MONITOR_STATUS_BUSY;
      }
      break;

    default:
      monitor_shared.status = MONITOR_STATUS_BAD_COMMAND;
      monitor_shared.command = 0;
      break;
  }

  chSysUnlockFromISR();
  OSAL_IRQ_EPILOGUE();
}
```

When the main thread finishes `proto_eval()`, it writes the encoded response
length to `monitor_shared.result` and changes `status` to
`MONITOR_STATUS_DONE`.

#### ISPR vs STIR

Both `STIR` and `ISPR` ultimately create a pending external interrupt. For this
monitor path, `ISPR` is preferred.

`STIR` is simple: the host writes the external IRQ number to `0xE000EF00`.
However, it is write-only, has no convenient readback, and is easy to confuse
with system exception numbers. That confusion matters here because
DebugMonitor cannot be triggered through `STIR`.

`ISPR` is more explicit. It is the same mechanism CMSIS uses for
`NVIC_SetPendingIRQ()`: choose the IRQ bank, write the bit, and optionally read
or clear pending state with `ISPR`/`ICPR` during diagnostics. For
`FDCAN1_IT0_IRQn = 39`:

- `NVIC_ISPR_BASE = 0xE000E200`
- bank is `39 / 32 = 1`
- bit is `39 % 32 = 7`
- host writes `1u << 7` to `0xE000E204`
- stale pending state can be cleared by writing the same bit to
  `NVIC_ICPR1` at `0xE000E284`

The host should use normal memory access (`WriteMem32()` in this repository),
not the ST-LINK debug-register command, for both `ISPR` and shared RAM.

Host sketch:

```c
#define NVIC_ISPR_BASE          0xE000E200u
#define NVIC_ICPR_BASE          0xE000E280u
#define MONITOR_KICK_IRQN       39u
#define MONITOR_KICK_ISPR_ADDR  (NVIC_ISPR_BASE + 4u * (MONITOR_KICK_IRQN / 32u))
#define MONITOR_KICK_ICPR_ADDR  (NVIC_ICPR_BASE + 4u * (MONITOR_KICK_IRQN / 32u))
#define MONITOR_KICK_MASK       (1u << (MONITOR_KICK_IRQN % 32u))

bool monitor_kick(uint32_t command) {
  WriteMem32(MONITOR_SHARED_ADDR + offsetof(monitor_shared_t, status),
             MONITOR_STATUS_IDLE);
  WriteMem32(MONITOR_SHARED_ADDR + offsetof(monitor_shared_t, command),
             command);
  WriteMem32(MONITOR_KICK_ICPR_ADDR, MONITOR_KICK_MASK);
  WriteMem32(MONITOR_KICK_ISPR_ADDR, MONITOR_KICK_MASK);
  return poll_monitor_status();
}
```

This wake path should work for normal Sleep entered with `WFI`. It should not
be assumed to wake Stop or Standby. While the host monitor is attached, U3
runtime policy should therefore remain in run/sleep modes that the monitor kick
can wake; detached tags can use Stop/Standby normally.

The U3 idle-hook `WFI` path must also be gated until core startup is complete.
During connect-under-reset there is a short interval where SWD is active and the
monitor request has been written, but the monitor session is not yet fully
published. Entering `WFI` in that window can make host polling of the shared
block unreliable. Targets that install a `CH_CFG_IDLE_LOOP_HOOK()` sleep helper
should leave WFI disabled until common startup calls the target hook that marks
startup complete, and should continue to suppress WFI while
`monitorIsAttached()` is true. Startup sequencing, not the steady-state idle
hook, protects the pre-session reset-vector window.

### Host Monitor Library

`tagmonitor.cc` already detects STM32L4 vs STM32U3. STM32L4 should keep the
current attach flow. STM32U3 should use a new attach path:

1. Connect to ST-LINK and enter SWD debug mode.
2. Detect target family.
3. Configure reset-vector halt/debug enable.
4. Release reset into the reset-vector halt.
5. Write `MONITOR_REQUEST_MAGIC` to `MONITOR_SHARED_ADDR` while the core is
   halted before C startup.
6. Resume the core.
7. Poll `MONITOR_SHARED_ADDR.request` until it equals
   `MONITOR_CONNECTED_MAGIC` or times out.
8. Read `debug_version`, `buf_addr`, `buf_size`, and `sha_addr`. The kick IRQ
   constants come from the shared `monitor.h` ABI.
9. Validate ABI version, buffer size, SHA pointer, and shared command/status
   fields.
10. Mark the host monitor as attached and use the U3 shared-memory RPC call.
    Do not issue a redundant `MONITORSTART` after `SESSION_READY`; that state is
    already the target's acknowledgement that the U3 monitor session is active.

The U3 attach path should not use `TAG_MONITORINFO` to discover the buffer,
because that is the dependency this design removes. The U3 equivalent of
`Call(PROTOBUF, ...)` should write `command`, pend the stolen IRQ through
`ISPR`, then poll `status`/`result`.

For STM32U3, `Detach()` sends `MONITORSTOP` through the shared-command/ISPR
path. The target's main monitor interrupt handler stops the timer/session,
cleans up monitor state, and acknowledges by clearing `request` to 0. The host
polls for that cleared request instead of relying on `command`/`status` after
the session has been torn down. If the explicit detach is missed because the
host disappears, target cleanup is still handled by the heartbeat watchdog.

### `MONCONNECTED` Transition

`MONCONNECTED` currently means "`DEMCR.VC_CORERESET` is set." That should remain
only as a debug-reset/vector-catch hint where needed for reset recovery. Runtime
code that wants "host monitor session is attached" should call
`monitorIsAttached()`, and the STM32U3 implementation of `monitorIsAttached()`
should use the shared-block connected request value rather than `VC_CORERESET`.

Important users to audit:

- `pwr.c` low-power entry guards
- reset recovery in `state_machine.c`
- any SWD pin-release policy
- exception behavior that changes when the monitor is attached

### Work Plan

1. Define the shared monitor ABI in `include/monitor.h`: address, size, state
   values, ABI version, flags, and `monitor_shared_t`.
2. Reserve the U3 monitor shared block in `embedded/tags/common/STM32U375xG.ld`
   by moving normal RAM origins/lengths above the fixed block, leaving no C
   startup-owned section at `MONITOR_SHARED_ADDR`.
3. Add the STM32U3 shared-block object and compile-time layout/address checks
   on the firmware side.
4. Add `monitorSharedEarlyInit()` and `monitorSharedSessionStart()` to the
   common monitor/handler code, calling early init before `halInit()` and
   keeping all ChibiOS timer work after `chSysInit()`.
5. Add a U3 monitor kick IRQ using `FDCAN1_IT0_IRQn`/`VectorDC`, enable it at a
   ChibiOS-safe priority, and keep the CAN driver disabled so the vector has one
   owner.
6. Implement the host kick with `NVIC_ISPR1` and stale-pending cleanup with
   `NVIC_ICPR1`. Use `WriteMem32()` for shared RAM and NVIC memory-mapped
   registers.
7. Replace the current U3 rearmed-per-RPC timeout with the host-activity
   watchdog: request handler sets `host_activity`, timer clears/rearms, second
   quiet tick disconnects.
8. Simplify `handlersU3.c` so U3 attach metadata comes from shared memory and
   the U3 runtime path has no DebugMonitor/DCRDR compatibility handler.
9. Change U3 `monitorIsAttached()` to use monitor session state and the shared
   request word, not `VC_CORERESET`.
10. Audit firmware users of `MONCONNECTED`; convert runtime attachment checks to
   `monitorIsAttached()` while preserving debug-reset recovery checks where
   `VC_CORERESET` is still useful.
11. Split `TagMonitor::Attach()` into L4 and U3 paths. The U3 path writes the
   request magic under reset and reads metadata from the shared block.
12. Replace U3 `Call(PROTOBUF, ...)` with the shared-command/ISPR path while
    keeping the L4 `DCRDR`/DebugMonitor path unchanged.
13. Update U3 detach cleanup to send `MONITORSTOP` through the stolen IRQ so the
    target interrupt handler performs timer stop and cleanup, then clears the
    shared request word as the host-visible detached acknowledgement.
14. Add focused diagnostics for U3 attach and kick timeouts: shared block
    snapshot, pending/active NVIC state, stolen IRQ number/mask,
    `DEMCR`, `DHCSR`, target family/idcode, and reset state.
15. Verify with builds for one L4 target and one U3 target, then hardware-test
    U3 attach, repeated RPC polling, normal detach, host disappearance without
    detach, attach while the tag is already in `RUNNING`, and RPC while the core
    is sleeping in `WFI`.
