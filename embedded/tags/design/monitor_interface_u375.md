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
- Let the target detect a vanished host even if the host never sends
  `MONITORSTOP`.
- Preserve STM32L4 behavior while changing only the STM32U3 path.
- Keep DebugMonitor at a ChibiOS kernel-callable priority because the handler
  calls IRQ-safe ChibiOS APIs.

### Connection Model

The host connects under reset:

1. Host asserts reset and halts the core at or near the reset vector.
2. Host writes `MONITOR_REQUEST_MAGIC` to a fixed RAM address through SWD.
3. Host releases the core.
4. Target early initialization sees the request, fills a shared monitor block,
   and writes `MONITOR_CONNECTED_MAGIC`.
5. Host reads the shared block to learn monitor version, protobuf buffer
   address/size, and SHA-string address.
6. Host uses the existing protobuf RPC payloads. On STM32U3, the wake/kick
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
#define MONITOR_SHARED_ABI_V1     1u
#define MONITOR_STATUS_IDLE       0u
#define MONITOR_STATUS_PENDING    1u
#define MONITOR_STATUS_DONE       2u
#define MONITOR_STATUS_BUSY       3u
#define MONITOR_STATUS_BAD_COMMAND 4u

typedef struct __attribute__((packed, aligned(4))) {
  volatile uint32_t request;          /* host writes request magic */
  volatile uint32_t magic;            /* target writes connected magic last */
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
} monitor_shared_t;
```

`magic` is valid only after the target has filled all other fields and issued a
memory barrier. The host must treat any other value as disconnected or not yet
ready.

The shared section must be `NOLOAD` and outside normal `.bss`/`.data` so C
startup does not erase the request word that the host wrote while the core was
halted.

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
  if (monitor_shared.request == MONITOR_REQUEST_MAGIC) {
    monitor_shared.abi_version   = MONITOR_SHARED_ABI_V1;
    monitor_shared.debug_version = DEBUGVERSION;
    monitor_shared.flags         = 0;
    monitor_shared.buf_addr      = (uint32_t)ProtoBuf;
    monitor_shared.buf_size      = (uint32_t)sizeof(ProtoBuf);
    monitor_shared.sha_addr      = (uint32_t)SHAStr;
    monitor_shared.command       = 0;
    monitor_shared.result        = 0;
    monitor_shared.status        = MONITOR_STATUS_IDLE;
    monitor_shared.host_activity = 1;
    monitor_shared.watchdog_ticks = 0;
    __DMB();
    monitor_shared.magic = MONITOR_CONNECTED_MAGIC;
    monitor_shared.request = 0;
    monitor_requested_at_boot = true;
  } else {
    monitor_shared.magic = 0;
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
  halInit();
  monitorSharedEarlyInit();
  chSysInit();
  monitorSharedSessionStart();
  ...
}
```

If `monitorSharedEarlyInit()` must run before `halInit()` for reset timing, it
must not call any ChibiOS or HAL APIs. The watchdog must still wait until after
`chSysInit()`.

### Awake DebugMonitor Handler

For STM32U3, attach metadata no longer needs `TAG_MONITORINFO`; the host reads
it from the shared block. If the DebugMonitor path remains available for awake
compatibility during the transition, the handler should handle only
active-session control and protobuf kicks. The sleep-capable U3 path is the
stolen external IRQ described below.

Each valid host interaction, whether through DebugMonitor or the stolen IRQ,
sets `monitor_shared.host_activity = 1`. That is the target-side evidence that
the host is still polling.

Sketch:

```c
CH_IRQ_HANDLER(DebugMon_Handler) {
  CH_IRQ_PROLOGUE();

  if (CoreDebug->DEMCR & CoreDebug_DEMCR_MON_REQ_Msk) {
    uint32_t input = CoreDebug->DCRDR;
    uint8_t operation = input & 0xff;
    int operand = input >> 8;

    switch (operation) {
      case MONITORSTOP:
        chSysLockFromISR();
        monitor_shared.magic = 0;
        monitor_shared.host_activity = 0;
        CoreDebug->DCRDR = 1U;
        monitorStopI(false);
        chSysUnlockFromISR();
        break;

      case PROTOBUF:
        chSysLockFromISR();
        if (monitor_enabled && !monitor_pending) {
          monitor_shared.host_activity = 1;
          monitor_operand = operand;
          monitor_pending = true;
          CoreDebug->DCRDR = 0;
          if (tpMain)
            chEvtSignalI(tpMain, EVT_MONITOR_SERVICE);
        } else {
          monitorClearRequest();
        }
        chSysUnlockFromISR();
        break;

      default:
        monitorClearRequest();
        break;
    }
  }

  __DSB();
  CH_IRQ_EPILOGUE();
}
```

`MONITORSTART` can either become unnecessary on U3 or remain as an idempotent
"session is active" call for compatibility. If retained, it should set
`host_activity = 1`, ensure `monitor_enabled = true`, and return success.

### Host-Activity Watchdog

The watchdog is target-side host-disconnect detection:

1. The monitor request handler sets `monitor_shared.host_activity = 1` whenever
   it accepts a valid host request.
2. A periodic virtual timer fires every few seconds.
3. If `host_activity` is nonzero, the timer clears it and rearms itself.
4. If `host_activity` is already zero when the timer fires, the target assumes
   the host disappeared. It clears `monitor_shared.magic`, stops the monitor
   session, and posts `EVT_MONITOR_TIMEOUT` to the main thread.

This is a one-missed-window policy after the first timer clear. With a 3-second
timer and a host that polls at least once every few seconds, a disconnect is
detected after roughly one quiet interval plus any scheduling latency.

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
    chVTSetI(&monitor_timer, chTimeS2I(3), monitor_watchdog_cb, NULL);
  } else {
    monitor_shared.magic = 0;
    monitor_timeout_pending = true;
    if (tpMain)
      chEvtSignalI(tpMain, EVT_MONITOR_TIMEOUT);
  }
  chSysUnlockFromISR();
}
```

`monitorStopI()` should clear `magic`, `host_activity`, `monitor_enabled`, and
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
    monitor_shared (rw) : org = 0x20000000, len = 0x40
    ram0           (wx) : org = 0x20000040, len = 256k - 0x40
    ram1           (wx) : org = 0x20000040, len = 192k - 0x40
    ram2           (wx) : org = 0x20030000, len = 64k
}

SECTIONS
{
    .monitor_shared (NOLOAD) : ALIGN(4)
    {
        __monitor_shared_start__ = .;
        KEEP(*(.monitor_shared))
        KEEP(*(.monitor_shared.*))
        . = ALIGN(4);
        __monitor_shared_end__ = .;
    } > monitor_shared
}
```

Firmware should assert:

- `sizeof(monitor_shared_t) <= MONITOR_SHARED_SIZE`
- `&monitor_shared == MONITOR_SHARED_ADDR`
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

The stolen IRQ handler replaces the U3 DebugMonitor RPC kick path. It reads the
shared `command` word, latches the protobuf request for main-thread processing,
writes `result`/`status`, and clears `command` when the command has been
accepted. The existing `DebugMon_Handler` may remain compiled for awake
compatibility during transition, but the U3 host path should use the shared
memory plus stolen IRQ mechanism.

Target sketch:

```c
#define MONITOR_KICK_IRQN FDCAN1_IT0_IRQn

OSAL_IRQ_HANDLER(STM32_FDCAN1_IT0_HANDLER) {
  OSAL_IRQ_PROLOGUE();

  uint32_t command = monitor_shared.command;
  uint8_t operation = command & 0xff;
  int operand = command >> 8;

  chSysLockFromISR();
  monitor_shared.host_activity = 1;

  switch (operation) {
    case MONITORSTOP:
      monitor_shared.result = 1U;
      monitor_shared.status = MONITOR_STATUS_DONE;
      monitor_shared.command = 0;
      monitorStopI(false);
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

### Host Monitor Library

`tagmonitor.cc` already detects STM32L4 vs STM32U3. STM32L4 should keep the
current attach flow. STM32U3 should use a new attach path:

1. Connect to ST-LINK and enter SWD debug mode.
2. Detect target family.
3. Assert reset and configure reset-vector halt/debug enable.
4. Write `MONITOR_REQUEST_MAGIC` to `MONITOR_SHARED_ADDR`.
5. Release reset/core.
6. Poll `MONITOR_SHARED_ADDR.magic` until it equals
   `MONITOR_CONNECTED_MAGIC` or times out.
7. Read `debug_version`, `buf_addr`, `buf_size`, and `sha_addr`. The kick IRQ
   constants come from the shared `monitor.h` ABI.
8. Validate ABI version, buffer size, SHA pointer, and shared command/status
   fields.
9. Mark the host monitor as attached and use the U3 shared-memory RPC call.

The U3 attach path should not use `TAG_MONITORINFO` to discover the buffer,
because that is the dependency this design removes. The U3 equivalent of
`Call(PROTOBUF, ...)` should write `command`, pend the stolen IRQ through
`ISPR`, then poll `status`/`result`.

`Detach()` should still send `MONITORSTOP` when possible. If that fails, host
cleanup should clear debug request bits and drop the USB/SWD session; target
cleanup is handled by the watchdog.

### `MONCONNECTED` Transition

`MONCONNECTED` currently means "`DEMCR.VC_CORERESET` is set." That should remain
only as a debug-reset/vector-catch hint where needed for reset recovery. Runtime
code that wants "host monitor session is attached" should call
`monitorIsAttached()`, and the STM32U3 implementation of `monitorIsAttached()`
should use `monitor_enabled` plus the shared-block connected magic rather than
`VC_CORERESET`.

Important users to audit:

- `pwr.c` low-power entry guards
- reset recovery in `state_machine.c`
- any SWD pin-release policy
- exception behavior that changes when the monitor is attached

### Work Plan

1. Define the shared monitor ABI in `include/monitor.h`: address, size, magic
   values, ABI version, flags, and `monitor_shared_t`.
2. Add `.monitor_shared` to `embedded/tags/common/STM32U375xG.ld` and move U3
   normal RAM origins/lengths above the reserved block.
3. Add the STM32U3 shared-block object and compile-time layout/address checks
   on the firmware side.
4. Add `monitorSharedEarlyInit()` and `monitorSharedSessionStart()` to the
   common monitor/handler code, keeping all ChibiOS timer work after
   `chSysInit()`.
5. Add a U3 monitor kick IRQ using `FDCAN1_IT0_IRQn`/`VectorDC`, enable it at a
   ChibiOS-safe priority, and keep the CAN driver disabled so the vector has one
   owner.
6. Implement the host kick with `NVIC_ISPR1` and stale-pending cleanup with
   `NVIC_ICPR1`. Use `WriteMem32()` for shared RAM and NVIC memory-mapped
   registers.
7. Replace the current U3 rearmed-per-RPC timeout with the host-activity
   watchdog: request handler sets `host_activity`, timer clears/rearms, second
   quiet tick disconnects.
8. Simplify `handlersU3.c` so U3 attach metadata comes from shared memory;
   keep awake DebugMonitor compatibility only if useful during bring-up.
9. Change U3 `monitorIsAttached()` to use monitor session state and shared
   magic, not `VC_CORERESET`.
10. Audit firmware users of `MONCONNECTED`; convert runtime attachment checks to
   `monitorIsAttached()` while preserving debug-reset recovery checks where
   `VC_CORERESET` is still useful.
11. Split `TagMonitor::Attach()` into L4 and U3 paths. The U3 path writes the
   request magic under reset and reads metadata from the shared block.
12. Replace U3 `Call(PROTOBUF, ...)` with the shared-command/ISPR path while
    keeping the L4 `DCRDR`/DebugMonitor path unchanged.
13. Update U3 detach cleanup to tolerate failed `MONITORSTOP` and rely on the
    target watchdog.
14. Add focused diagnostics for U3 attach and kick timeouts: shared block
    snapshot, pending/active NVIC state, stolen IRQ number/mask,
    `DEMCR`, `DHCSR`, target family/idcode, and reset state.
15. Verify with builds for one L4 target and one U3 target, then hardware-test
    U3 attach, repeated RPC polling, normal detach, host disappearance without
    detach, attach while the tag is already in `RUNNING`, and RPC while the core
    is sleeping in `WFI`.
