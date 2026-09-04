# Restart Recovery Notes

These notes capture deferred design thinking for tag recovery after monitor
connect-under-reset or another reset during active acquisition. They are not an
implemented behavior contract yet.

## Current Direction

- Treat `pState` as a retained recovery journal because it lives in RTC backup
  registers and survives MCU reset.
- Reinitialize software ownership on every boot: bus semaphores, GPIO muxes,
  EXTI routing, trigger timers, and peripheral register drivers.
- Add retained acquisition-phase sentinels around critical sections such as
  sensor read, header write, external data write, and cursor commit.
- If reset occurs during sensor read or data logging, prefer abandoning the
  current block/page over trying to reconstruct partially read or partially
  written data.

## Low-Power Wake Classification

STM32L4 Standby wake can be distinguished from a cold reset by combining the
retained `pState` validity marker with the platform standby flag. Hardware
Shutdown is more ambiguous because the reset flags can look like a power or
brownout reset while RTC backup registers are still retained.

The common L4 terminal-sleep path reserves `RTC->BKP31R` as a one-shot
Shutdown-entry marker. Before entering hardware Shutdown it writes `SHUT`
(`0x53485554`) to that register; early startup reads and clears the marker
before reset-cause classification. If `pState` is valid and the marker is
present, the reset is classified as `resetShutdown` even if the ordinary standby
flag is absent.

## Header/Page Recovery

One simple recovery policy is to start a new `vddHeader` after reset and
abandon unused external pages under the previous header. This avoids exposing a
partially populated page, but log-download timing code must understand that
state markers can indicate a restart and a discontinuity between headers.

The download path should eventually combine:

- `vddHeader` timestamps for ordinary page anchors;
- state markers for restart/discontinuity events;
- retained cursor/sentinel state to decide whether the final pre-reset page is
  complete, abandoned, or should be hidden.

## Header Validation and ECC

Internal flash headers and state markers should be read through the checked
helpers in `stm32flash.c`, not by direct struct access. Those helpers install a
narrow `NMI_Handler` path that converts flash ECC NMIs into read errors only
while an explicit flash probe is active. ECC outside that probe remains an
unexpected exception.

Recovery scanners should treat checked-read failure the same way they treat an
erased or invalid header boundary: stop before that record and abandon the
possibly incomplete page. Download code can additionally defer exposing a page
until either the following header exists or a terminal state marker proves that
the final page was completed.

A useful hardware test is to run a tag, interrupt/reset it repeatedly during
internal header writes, then verify that recovery and monitor download stop at
the last checked-readable header instead of entering the generic exception path.
If deliberately producing an ECC-faulted double-word is practical on a bench
unit, the expected result is that a guarded read returns an ECC error and an
unguarded read still follows the ordinary exception path.

## Storage Bounds

The monitor download path should treat the internal flash space from
`vddHeader` to flash end as the first practical limit. The persistent section
intentionally lives at the trailing end of flash, and existing recovery/download
code treats `vddHeader` as the start of a flash-backed trailing header table,
not as a fixed-size C array boundary. The linker expands `.persistent` to the
end of `flash0` and exports `__persistent_end__`, so firmware can use that
symbol instead of reading the MCU flash-size register at runtime. The effective
header limit is therefore
approximately:

```c
((uint32_t)&__persistent_end__ - (uint32_t)&vddHeader[0]) /
    sizeof(t_DataHeader)
```

That limit depends on the final linked image size and is best checked from the
map file by comparing `&vddHeader[0]` to `&__persistent_end__`.

Approximate active-target external flash capacities:

| Target | External flash | Capacity | Notes |
| --- | --- | ---: | --- |
| `BitTag` | none | n/a | Internal flash log only. |
| `PresTag` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `BitPresTag` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `BitPresTagMX25R` | MX25R | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `CompassTag` | MX25R | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `CompassTagAT25` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `CompassTagAT25Breakout` | AT25XE | 4 MiB | 4096-byte sectors, 1024 sectors. |
| `IMUTagNand` | GD5F SPI-NAND | 128 MiB raw | 1004 logical 128 KiB blocks after bad-block reserve. |

External-flash capacity is usually not the limiting factor for current log
download. For example, 4 MiB flash can hold many thousands of current PresTag,
BitPresTag, or CompassTag log pages. The internal trailing header space normally
limits complete anchored downloads before external flash fills.

The current number of complete internal-header pages needed to fill external
flash is:

| Target | Bytes written per internal header | External flash | Complete headers to fill | Unused tail bytes | First header that cannot fit |
| --- | ---: | ---: | ---: | ---: | ---: |
| `PresTag` | 240 B | 4 MiB | 17,476 | 64 | 17,477 |
| `BitPresTag` | 96 B | 4 MiB | 43,690 | 64 | 43,691 |
| `BitPresTagMX25R` | 96 B | 4 MiB | 43,690 | 64 | 43,691 |
| `CompassTag` | 500 B | 4 MiB | 8,388 | 304 | 8,389 |
| `CompassTagAT25Breakout` | 500 B | 4 MiB | 8,388 | 304 | 8,389 |
| `CompassTagAT25` | 500 B | 4 MiB | 8,388 | 304 | 8,389 |

`BitTag` has no external flash. For 256 KiB internal flash builds, external
flash fills first for `PresTag` and the CompassTag variants. The internal
`vddHeader` region fills first for the BitPresTag variants. U375 IMUTag targets
use a different checkpoint and NAND/SPI-NOR accounting model and are documented
with the IMUTag family.

Current linked `vddHeader` limits from
`/Users/geobrown/Build/tag-designs/software-embedded-clean`, with
`TAG_FLASH_SIZE=256K`, are:

| Target | `vddHeader` | Header size | `__persistent_end__` | Header limit |
| --- | ---: | ---: | ---: | ---: |
| `PresTag` | `0x08007a60` | 8 B | `0x08040000` | 28,852 |
| `BitPresTag` | `0x08008a60` | 8 B | `0x08040000` | 28,340 |
| `BitPresTagMX25R` | `0x08008a60` | 8 B | `0x08040000` | 28,340 |
| `CompassTag` | `0x08009258` | 8 B | `0x08040000` | 28,085 |
| `CompassTagAT25Breakout` | `0x08009258` | 8 B | `0x08040000` | 28,085 |
| `CompassTagAT25` | `0x08009a58` | 8 B | `0x08040000` | 27,829 |
| `BitTag` | `0x08007268` | 16 B | `0x08040000` | 14,553 |

For a 128 KiB build, assuming similar code layout and
`__persistent_end__ = 0x08020000`, the approximate limits would be:

| Target | Approximate 128 KiB header limit |
| --- | ---: |
| `PresTag` | 12,468 |
| `BitPresTag` | 11,956 |
| `BitPresTagMX25R` | 11,956 |
| `CompassTag` | 11,701 |
| `CompassTagAT25Breakout` | 11,701 |
| `CompassTagAT25` | 11,445 |
| `BitTag` | 6,361 |

`BitTag` uses a 16-byte internal data header; the other current active targets
listed above use 8-byte `t_DataHeader` records.

`eraseExternal()` depends on reset calling `restoreLog()` first. It erases
`pState->pages * 2048`, rounded up to external flash sectors. It does not probe
or erase sectors beyond the internal header count, because headers are the
authoritative record of complete IMU log pages that may be downloaded.

The reset state processes external erase work in small batches before yielding
back to monitor/status handling. `TAG_EXTERNAL_ERASE_SECTORS_PER_PASS` defaults
to 16, avoiding a one-sector-per-host-poll erase rate while still allowing the
monitor to report progress during long external flash erases.

The live `Status` message reports:

- `sectors_erased`: sectors completed during the current reset/erase;
- `erase_sectors_total_plus_one`: total sectors expected plus one.

The plus-one encoding avoids protobuf scalar-presence ambiguity. `0` means
unsupported or unknown. `1` means supported and zero sectors need erasing. Any
other value `N + 1` means the actual total is `N`.

Qt monitor/programmer progress should prefer
`Status.erase_sectors_total_plus_one - 1`. They retain a temporary IMUTag
fallback of `Status.internal_data_count * 2048`, rounded up to 4096-byte
sectors, only for older firmware that does not report the new field.
`TagInfo.extflashsz` remains the physical flash capacity and is not the right
denominator for dirty-log erase progress.

TODO: once deployed firmware reliably reports `erase_sectors_total_plus_one`,
remove the host-side IMUTag page-size fallback so Qt monitor/programmer code no
longer needs to know `DATALOG_SAMPLES * sizeof(t_DataLog)`.

## IMUTag Timing Caveat

IMUTag FIFO reads are especially sensitive. A reset can leave the hardware FIFO
phase, the local partial block cache, and the saved block timestamp out of sync.
The safest recovery is likely to reset/reinitialize the IMU FIFO stream, discard
a fresh lock/warmup interval, and start the next header from the first
post-resync block timestamp.

`t_DataHeader.millis` uses only ten bits for a 1/1024-second subsecond tick
value. Host log writers convert that value to rounded integer milliseconds when
writing SQLite logs. The IMUTag log format now reserves bit `0x0400` as
`IMUTAG_HEADER_RESYNC`, which marks the first header after the FIFO stream has
been reinitialized or the log stream has otherwise lost continuity. Bit
`0x0800` is
`IMUTAG_HEADER_RESYNC_STORAGE_SKIP`, which refines `RESYNC` to say that the
previous segment ended because an external flash block was skipped after a
storage write failure. The host decoder should treat any `RESYNC` header as the
start of a new smooth timing segment: anchor the segment to the header epoch and
rounded millisecond, then place samples by IMU sample count until the next resync
marker. Ordinary headers should not re-anchor the high-rate data because the
rounded millisecond field can introduce page-to-page jitter. If the rounded
resync anchor would place the new segment before samples already emitted for the
previous segment, the decoder rounds the new segment start up to the next
expected block boundary so elapsed microsecond timestamps remain monotonic.

SQLite logs retain the decoded header flags in `ImuHeader.Flags` and write a
`RESYNC` or `RESYNC_STORAGE_SKIP` row to `ImuEvent` at the corresponding
elapsed microsecond time. SensorViz can draw those event rows as vertical
discontinuity markers without turning them into y-axis streams.

## Boot Cleanup Must Not Claim IDLE (fixed 2026-09-02)

`IDLE => empty state log` is an invariant of this firmware. `Idle()` records no
marker precisely so that an empty log is what makes an idle tag resolve to idle,
and reset recovery relies on it: it seeds `TagState_IDLE` before walking
`sEpoch`, so a tag with no markers needs no marker to be found.

`tagResetRuntimeStateForPowerInit()` in `core/src/main.c` broke that invariant.
It set `pState->state = TagState_IDLE` directly, bypassing `Idle()`, and also
zeroed `pages` and `external_blocks`. Reached with markers still in the log, it
produced a tag whose live state read IDLE while internal flash still ended in
`FINISHED` with a nonzero external page count.

That is unrecoverable from the host, because the erase path only runs from
FINISHED or ABORTED. `tag-reset` saw IDLE, skipped the erase, and the next run
started on a dirty NAND, collected nothing, and aborted. The abort *was*
erasable, so the run after it succeeded — the observed symptom was every second
collection failing, with downloads refused whenever data was present.

Two defects combined:

- `deviceInit()` cleared `pState->valid` even when called with `force`, which
  the terminal transitions all do (`Finished()`, `Aborted()`, `Reset()`,
  `SelfTest()`). That opened a window spanning all of the device power
  sequencing in which any reset — a host tool detaching and the next one
  attaching is enough — was classified by `getResetCause()` as `resetPower` with
  no valid retained state, which is the condition that runs the cleanup. The
  clear bought nothing: the same block restores the magic unconditionally at the
  end. It is now done only for a genuine power init.
- The cleanup asserted IDLE regardless of the log. It now claims IDLE only when
  `stateLogEmpty()`, and otherwise leaves `STATE_UNSPECIFIED` so recovery must
  resolve the state from the marker log. Leaving the state unspecified also
  keeps the monitor-attach branch from adopting it — `validTagState()` rejects
  UNSPECIFIED — which is what forces the scan. The log cursors are left
  untouched in that case, because recovery owns them: `restoreLog()` when
  detached, the retained values under a monitor.

The marker scan itself was never at fault. Instrumentation caught it repairing
the damage on a detached boot: entry state IDLE, resolved FINISHED from three
markers ending in `EVENT_STOPCMD`. The failure only persisted when a monitor was
attached, because that branch trusts retained state and never reads the log.

Verified on hardware: a counter in `tagResetRuntimeStateForPowerInit()`
incremented exactly once per failing cycle before the fix and never after it,
across four reset/start/detach/stop/download cycles, with two consecutive
successful runs — which had not happened once in the preceding runs.

### Still open

- The monitor-attach recovery branch adopts retained state without
  cross-checking the marker log. Nothing depends on that now, but a future wipe
  or corruption of `pState->state` would again outrank durable flash evidence.
- `tag-start --set-rtc` intermittently fails with "RTC sync failed while writing
  tag clock" on the IMUTagNandBmp581 breakout, and boots frequently report
  `rtcInitializedAtBoot` false and `clockTrusted` false. Independent of the
  above; it prevented two of four verification cycles from starting at all.

## Latched Flash Error Flags Block STM32U3 Low-Power Entry (fixed 2026-09-02)

On STM32U3 an uncleared flag in `FLASH_SR` — `OPERR`, `WRPERR`, `PGAERR`,
`PGSERR` and the rest — makes the power controller either abort the low-power
transition or wake immediately out of `__WFI()`. The ECC flags in `FLASH_ECCR`
behave the same way and are far easier to latch: any read of internal flash can
set `ECCC`, and the flag outlives the read. `FLASH_Read_Checked()` clears what
it detects, but nothing clears a flag raised by an ordinary load through a
pointer into flash.

The U3 terminal sleep path cleared neither. It cleared `FLASH_SR` only
*after* waking, in `tagPowerRestoreFlashAfterStop3()`, which is too late to
help entry. `tagPowerClearFlashErrorFlags()` clears both registers as the last
step before arming sleep, immediately ahead of `DBGMCU->CR = 0` and the
`LPMS`/`SLEEPDEEP`/`WFI` sequence.

> **Stale as of 2026-09-04.** It does so inside `tagPowerEnterStop3()`, which
> is no longer the live terminal path: `tagPowerEnterTerminalSleep()` calls
> `tagPowerEnterStandby()`, and Stop3 carries `__attribute__((unused))`. The
> clear runs on no path the tag takes today. Two attempts to add it to the live
> idle and standby paths both measured about 1036 uA at idle against 4.94 uA
> without it, which is not yet explained -- see
> [`open-issues.md`](open-issues.md). Flags are cleared by writing 1, so no flash
unlock is needed and the call is safe with the flash locked.

### How the flag gets latched in the first place

Reading erased flash is enough. On modern STM32 parts an uninitialized or
never-written region does not necessarily read as all ones; reading one can
trick the ECC logic into flagging a double-bit error, `ECCD`.

The marker log is scanned that way by design. `recordState()` walks `sEpoch`
looking for the first slot whose `epoch` reads `-1`, which means it reads an
erased record on every single call. Reset recovery does the same, terminating
its scan on the first erased entry, and so do `stateLogAck()` and
`stateLogEmpty()`. Every one of those paths reads uninitialized flash as its
normal, non-error case.

`FLASH_Read_Checked()` clears what it detects, which is why this stayed hidden
for so long -- most reads self-clean. What it cannot cover is a flag raised by
a read it did not perform, or one raised and left behind between the last
checked read and the `WFI`. Clearing unconditionally at the point of sleep does
not depend on knowing which read was responsible.

### Why this was mistaken for something else

The symptom is that a tag reports IDLE, sits in `__WFI()`, and draws run
current — 995 uA against 6.6 uA on an IMUTagNandBmp581 at 3.29 V — instead of
entering Stop3. Crucially it is triggered by *whatever last touched internal
flash*, not by the code that appears to be at fault, so it presents as
"adding this unrelated change broke standby".

It was blamed on retained boot-recovery instrumentation, which was defaulted
off because enabling it reproduced the 995 uA exactly. That was wrong: with the
flag clear in place the same instrumentation measures 6.705 uA against 6.716 uA
with it disabled. What the instrumentation actually did was add a field read of
a flash-resident record, which latched a flag that nothing then cleared.

Bisecting found it only because the failing difference narrowed to a single
line — one added read of a marker field in `stateLogAck()` — which is far too
small to explain a 150x current change by any mechanism other than a state flag.

### What it does not explain

The `debug_log` module still prevents standby, retested after this fix and
unchanged at 1.71 mA. That is a separate fault in the module itself and remains
open; see the warning in `embedded/tags/IMUTagNandBmp581/project.mk`. Note the
two have different signatures — 1.71 mA against 995 uA — which is now a useful
way to tell them apart.

### Consequence for diagnostics

Retained diagnostics are not inherently expensive on this part, which was the
conclusion drawn from the earlier measurement and is now known to be false.
Instrumentation that reads internal flash still needs an idle measurement
afterwards, but it no longer needs to be presumed unaffordable.
