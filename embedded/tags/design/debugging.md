# Debugging a Tag

A tag in the field has no console. The faults that cost the most time are the
ones where the tag reports a perfectly normal state while doing something else
entirely -- IDLE at run current, a run that ends `EVENT_EXTERNALFULL` because a
write was refused, a start that aborts on an I2C bus nothing has cleared. None
of those is visible to a functional test.

This document lays out the ways to see inside, what each one costs, and what
each one destroys by being used.

## Choosing an approach

| Approach | Cost while enabled | Sees | Survives standby |
| --- | --- | --- | --- |
| Retained SRAM2 scratchpad | ~500 nA | anything the firmware writes down | yes, with retention enabled |
| GDB over SWD | hundreds of uA | full core and peripheral state, live | yes, with `DBG_STANDBY` |
| Joulescope | none (external) | current only, but truthfully | n/a |
| GPIO markers | ~0, or 700 uA if left driven | coarse timing of a few events | no |
| Monitor / qtmonitor | tag never sleeps | protocol-level state | no |

The ordering matters. The scratchpad is nearly free and can be left in a
shipped image; the debugger costs more than the fault you are usually chasing;
the monitor makes sleep measurement meaningless. Start at the top.

## 1. Retained SRAM2 scratchpad

The idea: reserve a page of SRAM2, write diagnostics into it with plain stores,
enable its standby retention before entering standby, then connect under reset
and read the page out. Nothing runs on the tag to produce the output, so the
measurement is undisturbed.

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
| Retention bit | `PWR_CR1_RRSB3`, bit 6 |
| Cost when retained | about 500 nA |

Setting one bit retains exactly this page and nothing else, which is the whole
point: the diagnostic survives standby for a cost far below the faults being
chased.

> **Do not take the page geometry from the CMSIS header.** The `PWR_CR2`
> `SRAM2PDSn` comments in `stm32u375xx.h` describe the *Stop-mode power-down*
> partition, and they number and size the pages differently -- `SRAM2PDS3` is
> commented as 32 KB. The Standby retention pages addressed by `RRSBn` are not
> that partition. Conflating the two is an easy mistake and an expensive one:
> reserve the wrong 8 KB and the scratchpad is powered down at exactly the
> moment it is supposed to survive. RM0503 is the authority for `RRSBn`.

### Reserving it is a linker change and a rebuild

The linker currently hands the C runtime everything. `DATA_RAM`, `BSS_RAM` and
`HEAP_RAM` all alias `ram0`, which spans SRAM1 **and** SRAM2, and the heap runs
to the top of it:

```
ram0 (wx) : org = 0x20000040, len = 256k - 0x40   /* SRAM1+SRAM2 */
```
```
__heap_base__ = 0x200047b8      __heap_end__ = 0x20040000
```

So `.ram2` being an empty section today means nothing -- the heap covers all of
SRAM2 and will allocate into the page unless the region stops short of it.
Because page 3 is the *top* 8 KB, that is a single length change in
`embedded/tags/common/STM32U375xG.ld`:

```
ram0 (wx) : org = 0x20000040, len = 248k - 0x40   /* SRAM1+SRAM2 less page 3 */
```

`ram0` then ends at `0x2003E000` and the scratchpad sits immediately above it.
Nothing else moves, and the heap loses 8 KB of the roughly 238 KB it currently
has -- static use is `.bss` 0x15c4 plus `.ram0` 0x2168, about 18 KB in total.

Declare the scratchpad at its fixed address rather than letting the linker place
it, so the readback address is a constant that cannot drift between builds:

```c
/* SRAM2 page 3: retained across standby when PWR_CR1_RRSB3 is set. */
#define TAG_SCRATCH_BASE 0x2003E000U
#define TAG_SCRATCH_SIZE 0x2000U
```

After the change, confirm the runtime really stopped below it:

```sh
arm-none-eabi-nm build/<Tag>.elf | grep -iE '__heap_end__|__ram0_end__'
# expect 0x2003e000, not 0x20040000
```

### Discipline

- **Single stores only.** A probe that calls a timing function inside the idle
  or power path changes the fault: an instrumented build read 430 uA where the
  pristine one read 1036 uA.
- **Write a magic word** and check it on readback. A page that was powered down
  returns whatever it returns; without a sentinel you cannot tell "nothing was
  recorded" from "the page did not survive".
- **A probe that vanishes is itself evidence.** Standby loses SRAM unless
  retention is on. If the block survives without retention enabled, standby was
  not entered -- which is exactly how the 1 mA idle fault was first bounded.

### Reading it back

```sh
# Must connect under reset. Hotplug does not work on this rig.
STM32_Programmer_CLI -c port=SWD mode=UR -u 0x2003E000 8192 scratch.bin
```

Decode the file afterwards with a small script rather than reading hex by eye;
a struct laid out in the firmware and a matching `struct.unpack` in Python is
enough, and it keeps the field names in one place.

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

This is the part that wastes an afternoon if you get it wrong. `DBG_STANDBY`
keeps the debug power domain alive so an **already-established** session
survives the transition. It does not let a debugger attach to a core that is
already in Standby -- the core is unpowered and there is nothing to enumerate.

A tag reporting IDLE is in Standby. Every attach attempt against it fails with
`init mode failed (unable to connect to the target)`, with or without
`connect_assert_srst`, and that failure looks exactly like a wiring problem.
Put the tag in **RUN** mode first:

```sh
build-host/bin/tag-reset --set-rtc
build-host/bin/tag-start --start-now -c embedded/tools/power-configs/imutag-100.json
```

RUNNING never enters Standby, and `DBG_STOP` covers the Stop periods between
samples, so the session survives the whole run.

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
  figure is close to the 1036 uA of the I2C pin-parking fault -- do not confuse
  a debug build for that regression.
- Use it to answer a *state* question, then remove it and re-measure with a
  clean image to answer the *power* question.

`deviceInit()` already treats an attached debugger as a special case --
`CoreDebug->DHCSR & C_DEBUGEN` is part of the `monitor_reset_recovery` gate --
so a debug build does not take quite the same boot path as a shipped one. Keep
that in mind when a fault appears only under the debugger.

## 3. Power measurement

Covered in detail in `AGENTS.md`. Briefly:

- `embedded/tools/tag_lifecycle_check.py` walks idle -> running -> stopped ->
  idle and measures every resting state, with the clock set. Use this by
  default; measuring one state only tells you about that state.
- `embedded/tools/tag_attach_storm.py` covers clock-cycle and attach/detach
  reliability. It measures no power.
- `embedded/tools/joulescope_measure.py` measures a single state directly.

Two traps worth repeating here: classify sleep on **>=1 ms averages**, never
per-sample, because PFM ripple spans -140 to +7700 uA; and `s/i/range/mode = 0`
disconnects the sense path and cuts power to the tag, which presents as a tag
that has stopped answering SWD.

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

## MCP servers

Two MCP servers are registered for this repository and are the intended way to
drive the first two approaches:

- **`embedded-debugger`** -- probe inspection, core control, memory reads,
  breakpoints, RTT, over either probe-rs or OpenOCD. Probe enumeration works
  out of the box; attaching to this part requires the OpenOCD backend, for the
  reason given above. The `embedded-debugger` skill describes the workflow and
  the CLI fallback.
- **`joulescope-js220`** -- direct instrument access for current measurement.

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

Until then the Python tools are the way to measure, and nothing is lost by
using them.

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

- `embedded/tags/design/open-issues.md` -- known unfixed defects
- `embedded/tags/design/restart-recovery.md` -- boot and recovery paths
- `embedded/tags/design/i2c-bus-recovery.md` -- why attach resets matter
- `AGENTS.md` -- measurement procedure and verification rules
