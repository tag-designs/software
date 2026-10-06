---
type: procedure
status: current
summary: Ways to see inside a tag (the retained SRAM2 scratchpad and single-store probes, GDB over SWD, current, GPIO markers, the monitor) and what each costs or disturbs.
---

# Debugging a Tag

A tag in the field has no console. The faults that cost the most time are the
ones where the tag reports a perfectly normal state while doing something else
entirely -- IDLE at run current, a run that ends `EVENT_EXTERNALFULL` because a
write was refused, a start that aborts on an I2C bus nothing has cleared. None
of those is visible to a functional test.

This document lays out the ways to see inside, what each one costs, and what
each one destroys by being used.

## Choosing an approach

| Approach | Cost while enabled | Sees | Survives terminal sleep |
| --- | --- | --- | --- |
| Retained SRAM2 scratchpad | not measured on Stop 3, which sets no retention bit (below) | anything the firmware writes down | expected through Stop 3, not yet verified (below) |
| GDB over SWD | hundreds of uA | full core and peripheral state, live | an established session, with `DBG_STOP`/`DBG_STANDBY` |
| Joulescope | none (external) | current only, but truthfully | n/a |
| GPIO markers | ~0, or 700 uA if left driven | coarse timing of a few events | no |
| Monitor / qtmonitor | tag never sleeps | protocol-level state | no |

The ordering matters. The scratchpad is nearly free and can be left in a
shipped image; the debugger costs more than the fault you are usually chasing;
the monitor makes sleep measurement meaningless. Start at the top.

## 1. Retained SRAM2 scratchpad

`embedded/tags/common/core/inc/scratchpad.h` gives firmware somewhere to write
that a host can read back later over SWD. Nothing runs on the tag to produce
the output, so reading it does not disturb what it recorded.

**It preserves state for debugging and nothing else.** It is not an
observation tool and it does not let you watch a tag: reading it still requires
attaching, attaching still resets, and the run is still over. What it buys is
that whatever the firmware wrote down *survives* that reset. Use it to answer
"what happened before this tag stopped", never "what is this tag doing now".

**It is STM32U375 only, and the restriction is physical.**
`TAG_SCRATCH_BASE` is `0x2003E000`, the last 8 KB of U375 SRAM, held out of
`ram0` by `STM32U375xG.ld` so `crt0` never clears it. On an STM32L432 that
address is outside RAM altogether -- SRAM1 is 48 KB at `0x20000000`, SRAM2
16 KB at `0x10000000` -- and `STM32L432xC.ld` reserves nothing, so every
`tagScratch*()` call writes into the void. `-DTAG_SCRATCHPAD=1` on BitTag,
PresTag, CompassTag or UIUCTag buys nothing at all. Use a plain RAM probe at a
real L432 address instead; SRAM survives reset, so it is read the same way
afterwards.

Enable it per target in `project.mk`:

```
UDEFS += -DTAG_SCRATCHPAD=1
```

With the macro undefined, every entry point compiles to nothing and the image
is byte-identical, so the calls can stay in shipped code.

```c
tagScratchResume();                     /* once, early in boot */
tagScratchPuts("configured");
tagScratchWord("STAT", pState->state);
```

- **`tagScratchResume()` versus `tagScratchInit()`.** `tagScratchResume()`
  keeps whatever the last boot left and advances `seq`; `tagScratchInit()`
  starts empty. Use Resume for a fault that spans resets. An attach storm boots
  the tag several hundred times, so a buffer formatted on every boot keeps only
  the last boot, which is rarely the interesting one. `main.c` calls
  `tagScratchResume()` and writes a `BOOT` record carrying `seq`.
- **`-DTAG_SCRATCHPAD_RING=1`** keeps the newest records instead of the oldest.
  The 8 KB fills long before the end of a storm. Linear mode, the default, then
  discards everything after that point, so it keeps the oldest records while
  the failure is at the newest end. Use linear when the interesting event is
  near the start, such as a boot that never completes.
- **`-DTAG_RETAINED_RUN_DIAGNOSTICS=1`** copies the boot decision into the
  scratchpad.

The identity record notes whether an image was built with the scratchpad, ring
mode or retained run diagnostics. A capture therefore shows which of these it
should expect.

### What the firmware already records

Errors and state transitions are logged there already, so a capture from a tag
that ended a run badly shows how it ended without decoding the flash log:

| Record | Written by | Meaning |
| --- | --- | --- |
| `STAT` | `recordState()`, every transition | `state << 16 \| reason` |
| `ESLF` | `recordState()` | The flash marker log is full and has silently stopped recording transitions |
| `EECC` | IMUTag `datalog.c`, restart scan | Uncorrectable ECC on this NAND page |
| `RSYN`, `RTSC`, `RTMS` | IMUTag `state_run.c`, `restartDataCollectionClock()` | Whether this start is a recovery, and the seconds and milliseconds the new segment was based on |
| `RGST`, `RSTP`, `RRES`, `REXT`, `RSTA` | IMUTag `datalog.c`, `restoreLog()` (with `IMUTAG_NAND_CHECKPOINTS`) | Where the restart scan began and stopped, why it stopped, the resulting `external_blocks`, and the state |
| `BOOT` | `main.c` | One per boot, carrying `seq` |
| `DPHS`, `DVAL`, `DSTA`, `DRST`, `DRC ` | `main.c`, with `TAG_RETAINED_RUN_DIAGNOSTICS` | The backup-state boot decision |

`decode_scratchpad.py` also names `ESKP` (an external page skipped after a
write error) and `EGUP` (gave up after too many consecutive page write
failures). Nothing in the current tree writes them: the page skip itself was
re-landed in `5bb9aed9`, but without those records.

Put new error paths here too. It is the one place that survives a tag which
cannot talk, and the calls cost nothing when the macro is off. What goes in it
is up to the program. Messages are the general case. `tagScratchWord()` suits
a value you want to watch across a transition.

### The memory map

SRAM2 is 64 KB at `0x20030000`. The scratchpad is **page 3**, the last 8 KB.
From RM0503 on `PWR_CR1` bit 6:

> **RRSB3: SRAM2 page 3 retention in Standby mode.** This bit is used to keep
> the SRAM2 page 3 content in Standby mode. The SRAM2 page 3 corresponds to the
> last 8 Kbytes of the SRAM2 (from SRAM2 base address + 0xE000 to SRAM2 base
> address + 0xFFFF).

| | |
| --- | --- |
| Scratchpad | `0x2003E000` - `0x2003FFFF`, 8 KB |
| Standby retention bit | `PWR_CR1_RRSB3`, bit 6, set by `tagScratchRetain()` -- called only from the unused `tagPowerEnterStandby()` |
| Cost of Standby retention | 5.37 uA armed against 5.15 uA unarmed, in the A/B below |

`embedded/tags/common/STM32U375xG.ld` ends `ram0` at `0x2003E000`
(`len = 248k - 0x40`), so `crt0` never clears the page and the heap never
reaches it. What is known about keeping it:
- **A reset.** Reset does not clear SRAM, which is why the contents survive the
  reset that reading them causes.
- **A failed sleep.** A tag that stalls instead of sleeping keeps SRAM
  powered.
- **The shipped Stop 3 terminal sleep: expected, not verified.**
  `tagPowerEnterStop3()` does not set `RRSB3` and does not need to for Stop:
  neither the firmware nor the ChibiOS U3 HAL writes `PWR_CR2`, so every
  `SRAMxPDSn` Stop-mode power-down bit stays at its reset value of 0 (page
  powered), and the wake ends in a software reset, which does not clear SRAM.
  No A/B has been run on this path; it is open in the
  [TODO](../../embedded/tags/TODO.md).
- **A real Standby, with `tagScratchRetain()` armed.** This was checked by A/B
  on the retired Standby path: the armed build came back with `seq` counting
  every boot, and the unarmed control came back as noise
  ([investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md)).
  Shipped images do not take that path.

> **Do not take the page geometry from the CMSIS header.** The `PWR_CR2`
> `SRAM2PDSn` comments in `stm32u375xx.h` describe the *Stop-mode power-down*
> partition, and they number and size the pages differently -- `SRAM2PDS3` is
> commented as 32 KB. The Standby retention pages addressed by `RRSBn` are not
> that partition. Conflating the two is an easy mistake and an expensive one:
> reserve the wrong 8 KB and the scratchpad is powered down at exactly the
> moment it is supposed to survive. RM0503 is the authority for `RRSBn`.

SRAM1 is not retained through Standby, and cannot be. Ordinary `.data` and
`.bss` live there, and after the Stop 3 wake the software reset runs `crt0`,
which reinitialises them, so anything you want to read back after the tag has
slept has to be in the scratchpad.

### Reading it back

```sh
# Must connect under reset. Hotplug does not work on this rig.
STM32_Programmer_CLI -c port=SWD mode=UR -u 0x2003E000 8192 scratch.bin
embedded/tools/decode_scratchpad.py scratch.bin
```

`decode_scratchpad.py` names the states and reasons in `STAT` records. A
`tag-capture --sram` or `tag_capture_state.py` capture includes the page too;
see [Capturing a Tag](capturing-a-tag.md).

### Reading state out of a tag that cannot talk

Sleep faults are hard to instrument, because the usual narration changes the
thing being measured:
- the monitor keeps `isMonitorEnabled()` true, so the tag never sleeps at all;
- `debug_log_printf()` is compiled out of shipped images;
- a probe that calls any timing function inside the idle or power path changes
  the fault outright.

The scratchpad is one instance of a general technique. Reserve a block of RAM
at a known address, log into it with plain stores, and read it back over SWD
with `mode=UR`. Ordinary RAM is enough for **Run and Stop**, which retain SRAM.
To carry data **across Standby**, put the block in SRAM2 and enable its
retention.

- **Single stores only, placed outside the idle and power paths.** A probe
  there changes the fault rather than observing it: an instrumented build read
  430 uA where the pristine one read 1036 uA. Part of that is timing. Part is
  that any added code moves the image, and the STM32U375's low-power entry is
  sensitive to layout; see
  [STM32U375 Low Power](../../embedded/tags/common/core/design/u375-low-power.md).
  Either way, what you measure with the probe in is not the build you ship. Do
  not log from inside the terminal-sleep entry, `tagPowerEnterStop3()`; capture
  at boot instead.
- **Write a magic word** and check it on readback. A page that was powered down
  returns whatever it returns, and without a sentinel you cannot tell "nothing
  was recorded" from "the page did not survive".
- **A probe that vanishes is itself evidence -- when the sleep under test is
  Standby.** Standby loses SRAM unless retention is on. If the block survives
  without retention enabled, Standby was not entered, which is exactly how the
  1 mA idle fault was first bounded. If it is destroyed, Standby was entered.
  Stop 3, the shipped terminal sleep, retains SRAM, so this test says nothing
  about it.

## 2. GDB over SWD

The base board carries an ST-Link. This is the only approach that shows the
full machine -- core registers, peripheral
registers, the stack, and where the program counter actually is -- and the only
one that can stop the tag at a chosen instruction and let you look around.

The obvious use is a breakpoint immediately before the `__WFI()` that enters a
low-power mode, then examining `PWR`, `RTC`, `NVIC`, `SCB` and the peripheral
flags at leisure. That answers "what did the machine look like at the moment it
refused to sleep", which no amount of after-the-fact reasoning does.

### First: use a debugger that knows the part

"Any standard ARM debugger" is not true here, and finding that out costs an
afternoon. The STM32U375 is newer than the tooling most systems have installed:

| Tool | STM32U3 support |
| --- | --- |
| probe-rs, as built into `embedded-debugger-mcp` | **none.** Its target database carries STM32U031/073/083 and STM32U535 through U599, and no U3 whatsoever. There is no generic Armv8-M fallback target name either. |
| OpenOCD 0.12.0 (distribution build) | **none.** `stm32u5x.cfg` exists; there is no `stm32u3x.cfg`. |
| ST's OpenOCD fork | yes. This is the one to build. |

The probe-rs failure is worth recognising by sight, because it looks like a
wiring or reset problem and is not:

```
Failed to attach to target 'STM32U375RGTx'
Error: Unable to load specification for chip
```

That is the target database, not the connection -- it appears identically with
`connect_under_reset` set and the probe enumerating correctly.

With ST's OpenOCD running and exposing its GDB port, drive it through the
`embedded-debugger` MCP with `backend: "openocd"` and `openocd_address`
(default `127.0.0.1:3333`), or attach `arm-none-eabi-gdb` directly. Flashing
does not have to come from the same tool; STM32CubeProgrammer already does that
on this rig, so a debug-only target definition is sufficient.

### What has to change first

Debug is disabled in low-power modes by default, and this tree turned it off on
every entry path. The three `DBGMCU->CR = 0` writes in `pwr-u375.c` now go
through `tagPowerApplyDebugConfig()`, which keeps that behaviour unless
`TAG_DEBUG_LOW_POWER` is set, in which case it holds `DBG_STOP` and
`DBG_STANDBY`. Default builds are byte-identical by `.list` comparison, so the
flag is the only thing that changes behaviour.

Build the debug image by adding to the target's `project.mk`:

```
UDEFS += -DTAG_DEBUG_LOW_POWER=1
```

### Attach while the tag is awake, not while it sleeps

This is the part that wastes an afternoon if you get it wrong. `DBG_STOP` and
`DBG_STANDBY` keep the debug domain alive so an **already-established** session
survives the transition. They do not let a debugger attach to a core that is
already asleep with debug disabled.

A tag reporting IDLE is in its terminal sleep, Stop 3 on the U375. Every hot
attach against a sleeping tag fails with
`init mode failed (unable to connect to the target)` or `Examination failed`,
and that failure looks exactly like a wiring problem. Two ways round it:

**Connect under reset -- the general one.** It works, but the stock ST target
script defeats it: `stm32u3x.cfg` installs an `examine-end` handler that spins
on `PWR_VOSR` until a ready bit sets, and with nRST held the peripheral never
answers, so OpenOCD hangs after "target has 8 breakpoints" with no GDB port.
Override the handler:

```tcl
source [find interface/stlink.cfg]
transport select hla_swd
adapter speed 480
set CHIPNAME stm32u375
source [find target/stm32u3x.cfg]
reset_config srst_only srst_nogate connect_assert_srst
stm32u375.cpu configure -event examine-end {}
stm32u375.cpu configure -event reset-init {}
```

Then `reset halt` parks the core at the reset vector with `DEMCR = 0x01000000`
-- `VC_CORERESET` clear, so the firmware does not mistake the session for a
monitor -- and a hardware breakpoint set there is hit within a second of
`resume`. The stock `examine-end` handler also writes
`DBGMCU_CR |= DBG_STANDBY | DBG_STOP`, so overriding it is what keeps a debug
session from silently turning a shipping image into a debug build.

**RUN mode -- only for the debug build.** With `TAG_DEBUG_LOW_POWER=1`,
`DBG_STOP` covers the Stop periods between samples and a hot attach during a
run succeeds. Without it the examination fails even at 1600 Hz.

```sh
build-host/bin/tag-reset --set-rtc
build-host/bin/tag-start --start-now -c embedded/tools/power-configs/imutag-100.json
```

**Breakpoints near a low-power `WFI` defeat the observation.** With a
hardware breakpoint set on the instruction after the `wfi`, the `WFI` returns
after about a second having consumed exactly 11 `DWT_CYCCNT` cycles, with
`STOPF`/`SBF` clear and no handler run -- on every image. That is a debug
event, not a power fault. Remove every breakpoint before the final `resume`:
with the session attached and no breakpoints, an image on the retired Standby
path entered Standby normally (4.4 uA, measured; see the
[forum post draft](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-forum-post.md))
and OpenOCD reports `communication failure` as it
loses the target, which is what any deep-sleep entry looks like from the
ST-Link and says nothing about which state the part is in.

Dump peripheral state at a breakpoint over telnet (`mdw <base> <words>` per
block) rather than through the MCP one word at a time; a regmap generated from
the CMSIS header's `Address offset:` comments names the words. Two dumps taken
this way on a failing and a working image were bit-identical across ~380
registers -- see
[the Standby layout investigation](../../embedded/tags/common/core/design/investigations/2026-09-u375-standby-layout-dependence.md).

### The working configuration

```tcl
source [find interface/stlink.cfg]
transport select hla_swd
adapter speed 480
set CHIPNAME stm32u375
source [find target/stm32u3x.cfg]
```

```sh
/usr/local/bin/openocd -f u375.cfg
```

Success looks like this:

```
Info : [stm32u375.cpu] Cortex-M33 r0p4 processor detected
Info : [stm32u375.cpu] Examination succeed
Info : [stm32u375.cpu] target has 8 breakpoints, 4 watchpoints
Info : Listening on port 3333 for gdb connections
```

The `Warn : The selected adapter does not support debugging this device in
secure mode` line is benign here: the option bytes read `TZEN 0x0`, TrustZone
globally disabled, and `RDP 0xAA`, level 0.

### Halt before attaching a GDB client

OpenOCD refuses a GDB attach while the target is free-running:

```
Info : accepting 'gdb' connection on tcp/3333
Error: attempted 'gdb' connection rejected
```

Through the MCP that surfaces as `IO error: early eof`, and the session's
socket is dead afterwards -- a later call reports `Broken pipe`, which reads as
a different fault than it is. Halt first, over telnet on 4444 or with the MCP's
own `halt_after_connect`:

```sh
(printf 'halt\nexit\n'; sleep 2) | nc 127.0.0.1 4444
```

Then from the `embedded-debugger` MCP:

```
connect(probe_selector="auto", target_chip="STM32U375",
        backend="openocd", openocd_address="127.0.0.1:3333",
        halt_after_connect=true)
```

`read_memory`, `write_memory`, `halt`, `run`, `step`, `reset`, breakpoints and
`diagnose_fault` all work over this backend. Flash and RTT need probe-rs, which
does not know this part.

A worked check, confirming the debug build is what is actually running:

```
0xE0044004 (DBGMCU_CR) = 0x00000006   = DBG_STOP | DBG_STANDBY
```

### Two hazards

**The work area overlaps the monitor mailbox.** OpenOCD reports `work-area
address is set to 0x20000000`, which is where `monitor_shared_t` lives. Nothing
touches it until OpenOCD runs a flash algorithm, at which point it becomes
scratch and the mailbox is destroyed. Flash with STM32CubeProgrammer, or set a
work area elsewhere before letting OpenOCD program anything.

**The monitor and OpenOCD share one ST-Link.** Only one can hold it. The
`tag-*` tools attach and detach per invocation, so they interleave with an
OpenOCD session, but anything holding a session open -- qtmonitor especially --
locks OpenOCD out, and vice versa.

### What it costs

Hundreds of microamps, continuously, for as long as the bits are set. Measured
on IMUTagNandBmp581: **1035 uA at idle with `TAG_DEBUG_LOW_POWER=1`, against
4.37 uA without**. So:

- **Never in a shipped image.** Same policy, and the same reason, as the debug
  module.
- **Never during a power measurement.** A build with `DBG_STANDBY` set cannot
  be used to measure sleep; the measurement is of the debug unit. Note the
  figure is close to the 1036 uA of the layout-dependent Standby stall and the
  1031 uA of the I2C pin-parking fault
  ([i2c-bus-recovery.md](../../embedded/tags/common/core/design/i2c-bus-recovery.md))
  -- do not confuse a debug build for either regression.
- Use it to answer a *state* question, then remove it and re-measure with a
  clean image to answer the *power* question.

`deviceInit()` already treats an attached debugger as a special case --
`CoreDebug->DHCSR & C_DEBUGEN` is part of the `monitor_reset_recovery` gate --
so a debug build does not take quite the same boot path as a shipped one. Keep
that in mind when a fault appears only under the debugger.

## 3. Power measurement

The procedure, the rig traps and the qualification tools are in
[Power Testing](power-testing.md). Use the life-cycle check by default,
because measuring one state tells you only about that state.

One trap belongs here, because it is a debugging error before it is a
measurement error. **Classify sleep from averages of 1 ms or longer, never per
sample.** PFM ripple spans -140 to +7700 uA, so a single sample says nothing
about the mode the part is in.

## 4. GPIO markers

Cheap and occasionally the right answer for coarse timing, but this rig has
produced four distinct classes of artefact from pin probes, all of which looked
like real signals:

- a pin whose board default is `PIN_ODR_HIGH`, read as a marker that was set;
- a lazy pin initialisation inside the idle hook clearing markers the main
  thread had written;
- standby pull configuration masquerading as marker levels;
- GPI sampling at ~0.12 MS/s missing sub-8 us pulses entirely.

If you use them: raise the pin only on success and clear it first, so there is
no timing race; check for other writers of the same pin across the tree, since
stale ones outlive the build that added them; and remember that a pin left
driven against the board's 4.7k pull-ups costs about 700 uA per line.

## 5. The monitor

`qtmonitor` and the `tag-*` tools speak the protocol and are the right way to
ask what state the tag believes it is in. They cannot be used while measuring
sleep: an open session keeps `isMonitorEnabled()` true, so the tag never sleeps
at all, and the failure is invisible in the output -- it just reads as a high
average.

Attaching also connects **under reset**, which is a real event with real
consequences: a reset landing mid-I2C-byte leaves a slave holding SDA, which is
why `tagI2cBusClearIfStuck()` exists.

The same reset means **no `tag-*` tool can tell you what state a tag was in**.
`tag-info` reports the state the boot *restored* from the backup registers:
a tag whose scheduled start time has passed comes up RUNNING because the
reset started it, not because it had woken by itself, and a tag that had
stalled reports the state it stalled in. To know whether a tag woke, slept
or stalled, measure its current before touching it.

## MCP servers

Two MCP servers are registered for this repository. Use only the first:

- **`embedded-debugger`** -- probe inspection, core control, memory reads,
  breakpoints, RTT, over either probe-rs or OpenOCD. Probe enumeration works
  out of the box; attaching to this part requires the OpenOCD backend, for the
  reason given above. The `embedded-debugger` skill describes the workflow and
  the CLI fallback.
- **`joulescope-js220`** -- **do not use it.** It holds the instrument for the
  life of the session, which blocks every measurement script, and it cannot be
  released without killing the process; see
  [Power Testing](power-testing.md#1-before-measuring). It also cannot drive
  this rig's instrument, as below.

They are registered at local scope. MCP tools bind when a session starts, so a
session that was already running when they were added will not see them until
it is restarted.

### The Joulescope server does not accept a JS320

`list_devices` reports our instrument, but every functional call fails with
"No connected JS220 JouleScope devices found". The filter is a literal
substring match in `joulescope_mcp/service.py`:

```python
if require_js220 and "/js220/" not in device_path:
```

Our device path is `u/js320/Z2GW`. It is a **JS320**, so it never matches, and
only the handful of calls passing `require_js220=False` -- `list_devices` among
them -- work. That reads as a naming assumption rather than a capability one:
`embedded/tools/joulescope_measure.py` drives this same instrument through the
same `pyjoulescope_driver` topics without trouble. Widening that filter in a
fork is the durable fix; patching the `uvx` cache is undone on the next
resolve.

Measure with the Python tools and `joulescope_server.py` in any case, as
[Power Testing](power-testing.md) describes.

## What not to do

Every item here was learned by doing it.

- Do not put timing functions in a probe on the idle or power path.
- Do not classify sleep from per-sample current.
- Do not draw a conclusion from a single measurement of a flaky fault; three or
  four trials per point, or the bisect will lie to you convincingly.
- Do not report results from a build that failed to compile or flash. Gate every
  measurement on both "no `error:`" and "download complete".
- Do not suppress a tool's output and then interpret its silence.
- Do not relax a failing check to make a run pass without first establishing
  that the check is wrong.

## See also

- [`embedded/tags/TODO.md`](../../embedded/tags/TODO.md) -- known unfixed defects
- `embedded/tags/common/core/design/restart-recovery.md` -- boot and recovery paths
- `embedded/tags/common/core/design/i2c-bus-recovery.md` -- why attach resets matter
- [Power Testing](power-testing.md) -- measurement procedure
- [Capturing a Tag](capturing-a-tag.md) -- saving a tag's state before anything resets it
