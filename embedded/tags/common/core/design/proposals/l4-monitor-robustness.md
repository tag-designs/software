---
type: proposal
status: proposed
summary: Make the STM32L4 tags' monitor as robust as the STM32U3 path - first by adding the U3's fast status answer and BUSY reply to the L4 DebugMonitor handler, then optionally by moving L4 tags onto the U3 shared-memory mailbox, chosen per image by a marker in the vector table - with what each step needs, what it would and would not fix, and how to check it on the bench.
---

# A more robust monitor for the STM32L4 tags

**Proposal: in two steps.**

1. **Fix the L4 DebugMonitor handler in place.** Answer `get_status` from
   cached state while the main thread is busy, and reply BUSY to a request
   that arrives while one is pending, instead of clearing it. This is a
   contained firmware change with no host change and no protocol version.
2. **Then, if bursts remain, move L4 tags onto the U3 shared-memory
   mailbox.** Each image says in its vector table which protocol it speaks,
   so the host can choose before the firmware runs. Older images, including
   every fw-v0.6 field tag, keep the DebugMonitor path.

Step 1 removes the two weaknesses found on 2026-10-27. Step 2 also stops the
host polling a debug register, and is the experiment that tells whether the
remaining 0x81 errors come from the protocol or from the base.

## What was seen

On a UIUCTag breakout (STM32L432, `tag-breakout-base-l432-v1`), Synchronize
Clock and Self-Test each set off bursts of base 0x81
(`STLINK_DEBUG_ERR_FAULT`) errors. The details are in the Defects item of
[the tags worklist](../../../../TODO.md) and the 0x81 item of
[the bases worklist](../../../../../bases/TODO.md). In short:

- **The handler drops overlapping requests.** `DebugMon_Handler`
  (`common/core/src/handlersL4.c`) latches a PROTOBUF request and leaves
  `MON_REQ` set until the main thread serves it at a cooperative point. A
  second request that arrives meanwhile is dropped with
  `monitorClearRequest()`, which also clears the first request's `MON_REQ`.
  The host then reads a reply nobody wrote, and the first request is never
  served.
- **The main thread is busy for seconds.** Set RTC runs inside `proto_eval()`:
  internal RTC, re-initialisation, clock correction, then the RV3028 over
  I2C. A self-test runs straight after it is accepted. Status polls wait for
  all of it.
- **Most failures are DEMCR reads.** The host polls DEMCR, a debug register,
  for `MON_REQ` to clear, and those reads fail at the SWD level for much of
  that time: 334 in one `tag-test` run.

The host was changed the same day so that it no longer overwrites a pending
request: `TagMonitor::Rpc()` and tagcore-rs `rpc()` wait for `MON_REQ` and
`MON_PEND` to clear. qtmonitor also pauses its polls for 1.5 s after
Synchronize Clock and Self-Test. Neither fixes the tag.

**The same tests on an STM32U375 tag** with `tag-breakout-base-l432-u375-1v8`
show no bursts. That setup differs on both sides of the link:

| | L432 tag, L432-v1 base | U375 tag, U375 1v8 base |
| --- | --- | --- |
| Monitor protocol | DebugMonitor exception: DCRDR, DEMCR `MON_REQ`/`MON_PEND`; the host polls DEMCR | Shared-memory mailbox in SRAM, a kick interrupt, a heartbeat; the host polls SRAM |
| Status during a long operation | waits for the main thread | answered from cached state (`monitor_fast_status_eval()`) |
| A second request while one is pending | dropped, and the first one's `MON_REQ` cleared | status BUSY |
| Base SWD code | bit-banged `ll_swd.c` | SPI shift-outs, `ll_swd_spi.c` |
| Base WAIT retries | `MAX_SWD_RETRY` 25, `REGRETRIES` 20 | the same |

The U375 result points at the protocol, but does not prove it, since the base
code differs too.

## How the U3 mailbox works

From [`include/monitor.h`](../../../../../../include/monitor.h) and
[`handlersU3.c`](../../src/handlersU3.c):

- **The block.** A 64-byte `monitor_shared_t` at `MONITOR_SHARED_ADDR`
  (0x20000000, the start of SRAM), held out of RAM by the linker script
  (`STM32U375xG.ld` starts `ram0` at 0x20000040). It holds:
  - the session word: 0, `MONITOR_REQUEST_MAGIC` written by the host under
    reset, then `MONITOR_CONNECTED_MAGIC` written by the firmware once it
    has published its metadata;
  - the ABI and monitor versions;
  - the protobuf buffer address and size, and the SHA string's address;
  - `command`, `result` and `status` (IDLE, PENDING, DONE, BUSY,
    BAD_COMMAND, NOT_ATTACHED);
  - `host_activity` (the heartbeat), `watchdog_ticks` and `path_magic`.
- **The kick.** The host pends interrupt 39 (`MONITOR_SHARED_KICK_IRQN`) by
  writing NVIC ISPR over SWD; the interrupt routine latches the command.
  ISPR/ICPR are architectural, so the same addresses work on Cortex-M4.
- **Requests stay cooperative,** as on L4: the main thread evaluates them.
  The one exception is a plain `get_status` while `pState->safe` is false,
  which the interrupt answers from cached state.
- **The heartbeat.** The host sets `host_activity` at least every
  `MONITOR_HEARTBEAT_PERIOD_S` (5 s); the tag ends the session after one to
  two quiet periods.

## Step 1: harden the L4 handler

In `handlersL4.c` and `monitor.c`, without changing what the host sends:

- **Reply BUSY instead of clearing.** When a PROTOBUF arrives while one is
  pending, do not call `monitorClearRequest()`. Leave the pending request's
  `MON_REQ` alone, so the host's own check (it now waits for `MON_REQ` and
  `MON_PEND` to clear) sees that the tag is still busy. The clear stays for a
  request outside a session, so the host does not hang.
- **Answer status from the interrupt when the main thread is busy.** Port
  `monitor_fast_status_eval()`'s rule. If the pending request is an exact
  `get_status` and `pState->safe` is false, encode the cached status in the
  DebugMonitor handler, write its length to DCRDR and clear `MON_REQ` there.
  The handler already runs at DebugMonitor priority; the encode must use only
  cached state and no locks the main thread could hold.
- **Leave Set RTC where it is,** inside `proto_eval()`. The host aligns the
  call to a whole second and relies on it taking effect then. With the fast
  status path, polls no longer queue behind it.

**What it fixes.** Status polls answer during a self-test and an RTC set,
and overlapping requests are no longer lost or answered with an empty reply.
If the DEMCR failures come from waiting on a busy main thread, this removes
most of them too.

**What it does not.** If debug-register accesses themselves fail while the
tag works on its RTC or sensors, the bursts remain. Step 2 tests that.

**Cost.** One handler file and a small piece of `monitor.c`, shared by
every L4 tag; the host does not change. It touches the monitor path, so it is
verified per [Verifying a firmware change](../../../../../../docs/bench/verifying-firmware.md):
- `.list` comparison;
- `tag-test` on each target;
- `qtmon_session_check` on a UIUCTag;
- a resting-current check that the idle current did not move.

## Step 2: the mailbox on L4

### What the firmware needs

- **Reserve the block.** In `STM32L432xC.ld`, start `ram0` at 0x20000040, as
  the U375 script does. ChibiOS puts the main stack at the bottom of `ram0`,
  so it moves up with it.
- **A kick interrupt.** Interrupt 39 is free on the L432: the vector table
  has USART2 at 38 and EXTI15_10 at 40, and the L432 has no USART3. Check
  each L4 part in use against its reference manual. NVIC pending works for
  any interrupt the NVIC implements, named or not.
- **The handler.** Most of `handlersU3.c` is generic: the mailbox, the
  heartbeat, the fast status and BUSY. Its U3-specific parts are the attach
  and reset details and the persistent-state hooks. The proposal is one
  mailbox handler built for both families, selected at build time, with
  `handlersL4.c` kept for images that must stay compatible.
- **A marker in flash** (see below).

### How the host tells the protocols apart

Attaching is done under reset, so the host must choose the protocol before
the firmware runs; trying one and falling back would cost a second reset.
The marker therefore has to be in flash, readable while the core is held at
the reset vector.

- **Proposed: a reserved vector-table word.** Entries 8 to 10 of the
  Cortex-M vector table (offsets 0x20 to 0x28) are reserved by the
  architecture. ChibiOS fills them with the weak symbols `Vector20`,
  `Vector24` and `Vector28` (`os/common/startup/ARMCMx/compilers/GCC/vectors.S`;
  entry 7 is `SecureFault_Handler`). A tag image can define one of them as
  an absolute value in its linker script, such as
  `Vector28 = 0x4D4F4E31;` ("MON1"), so the word holds a magic and protocol
  version instead of a handler address. Nothing in `ChibiOS/` changes. Still
  to confirm: that a linker-script assignment overrides the weak definition
  in every target's link, and that no code calls these handlers.
- **Fallback, if that does not work: a descriptor at a fixed flash offset**
  after the vector table, holding the magic, the version and the block's
  address. It costs a linker-script section per target.

The host reads the word after halting at the reset vector, and uses the
mailbox path when the magic is present and the DebugMonitor path otherwise.
Once the session is up, `path_magic` in the block confirms it.

### What the host needs

- **tagcore and tagcore-rs** already have `AttachU3()` and `CallU3()`
  (`attach_u3`, `call_u3`). They become the mailbox path, chosen by the
  marker rather than by `DBGMCU_IDCODE`. Chip-specific registers are already
  separated by family detection: the IDCODE address, `RCC_CSR`, the flash
  empty check.
- **The heartbeat.** Every tool must keep the session alive. Tag Programmer
  and qtmonitor poll every 400 ms, and the command-line tools that wait poll
  every 1 to 2 s. A tool that sits idle for 10 s or more would need to call
  `KeepAlive()`, as the U3 tools already must.
- **The old path stays,** for every image without the marker.

### What it would show

The mailbox still uses SWD, for SRAM reads and NVIC writes. So:
- **If the UIUCTag on the mailbox is free of bursts on the same L432-v1
  base,** the cause was the L4 protocol.
- **If not,** it is the base or the bus. Building the L432-v1 base with its
  SPI SWD code (`ll_swd_spi.c`, not built today; see the bases worklist) is
  then the next comparison.

### Cost

- the linker scripts for every L4 target;
- the shared handler;
- the marker;
- host routing in tagcore and tagcore-rs;
- `docs/shared/monitor-interface.md`.

Every L4 target is then requalified per
[the release procedure](../../../../../../docs/release/release-procedure.md):
the boot path and the idle behaviour change, so the power figures need
measuring again.

## Questions for the owner

- **Step 1 first, as proposed, or straight to the mailbox?** Step 1 is
  smaller and keeps one protocol; the mailbox is the end state if bursts
  remain.
- **Should new L4 images drop the DebugMonitor path entirely** once they
  carry the marker, or keep both in firmware for a release?
- **Is a reserved vector-table word acceptable as the marker,** or would you
  rather have an explicit descriptor section?
