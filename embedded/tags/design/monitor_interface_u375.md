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
6. Host uses the existing DebugMonitor RPC mechanism for protobuf calls.

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

typedef struct __attribute__((packed, aligned(4))) {
  volatile uint32_t request;          /* host writes request magic */
  volatile uint32_t magic;            /* target writes connected magic last */
  volatile uint32_t abi_version;
  volatile uint32_t debug_version;
  volatile uint32_t flags;
  volatile uint32_t buf_addr;
  volatile uint32_t buf_size;
  volatile uint32_t sha_addr;
  volatile uint32_t host_activity;    /* DebugMonitor sets; timer clears */
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

### DebugMonitor Handler

For STM32U3, attach metadata no longer needs `TAG_MONITORINFO`; the host reads
it from the shared block. The handler should handle only active-session control
and protobuf kicks.

Each valid host interaction sets `monitor_shared.host_activity = 1`. That is
the target-side evidence that the host is still polling.

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

1. `DebugMon_Handler` sets `monitor_shared.host_activity = 1` whenever it
   accepts a valid host request.
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
7. Read `debug_version`, `buf_addr`, `buf_size`, and `sha_addr`.
8. Validate ABI version, buffer size, and SHA pointer.
9. Mark the host monitor as attached and use normal `PROTOBUF` RPC calls.

The U3 attach path should not use `TAG_MONITORINFO` to discover the buffer,
because that is the dependency this design removes. `Call(PROTOBUF, ...)` can
remain largely unchanged after attach.

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
5. Replace the current U3 rearmed-per-RPC timeout with the host-activity
   watchdog: handler sets `host_activity`, timer clears/rearms, second quiet
   tick disconnects.
6. Simplify `handlersU3.c` so U3 attach metadata comes from shared memory;
   keep `PROTOBUF`, `MONITORSTOP`, and optionally idempotent `MONITORSTART`.
7. Change U3 `monitorIsAttached()` to use monitor session state and shared
   magic, not `VC_CORERESET`.
8. Audit firmware users of `MONCONNECTED`; convert runtime attachment checks to
   `monitorIsAttached()` while preserving debug-reset recovery checks where
   `VC_CORERESET` is still useful.
9. Split `TagMonitor::Attach()` into L4 and U3 paths. The U3 path writes the
   request magic under reset and reads metadata from the shared block.
10. Keep protobuf RPC transport unchanged after attach, but update U3 detach
    cleanup to tolerate failed `MONITORSTOP` and rely on the target watchdog.
11. Add focused diagnostics for U3 attach timeouts: shared block snapshot,
    `DEMCR`, `DHCSR`, target family/idcode, and reset state.
12. Verify with builds for one L4 target and one U3 target, then hardware-test
    U3 attach, repeated RPC polling, normal detach, host disappearance without
    detach, and attach while the tag is already in `RUNNING`.
