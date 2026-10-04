---
type: design
status: current
summary: How a tag recovers after a reset -- reset classification, reattach versus failure, the IDLE-means-empty-log invariant, header and page recovery, ECC-checked reads, storage bounds and the external erase sweep.
last-verified: a87fc84a
---

# Restart Recovery

A tag treats `pState` as its recovery journal. `pState` mirrors the RTC backup
registers and survives an MCU reset. The internal-flash state-marker log is the
durable evidence. Every boot classifies the reset. A **reattach** resumes an
active run with `Running(T_CONT, POWERFAIL)`: a reattach is a monitor
connecting under reset, or an external NRST with the retained state intact.
A **brownout** restarts the interrupted state with `T_INIT` where it can.
Any other reset with an active run is a **failure** and aborts it. Boot never
claims IDLE over a non-empty
marker log. If a reset lands mid-write, the partial block or page is abandoned
rather than reconstructed.

Code: `common/core/src/main.c` (`getResetCause()`, `deviceInit()`,
`tagResetRuntimeStateForPowerInit()`), `common/core/src/state_machine.c`
(recovery and `Reset()`), `common/core/src/persistent.c` (the marker log), and
each family's `datalog.c` and `state_run.c`.

## Reset classification

Early boot in `main.c` decides `resetCause` before the reset flags are cleared:

- **No valid retained state.** If `pState->valid` is not
  `BACKUP_STATE_VALID_MAGIC`, the reset is `resetPower` whatever the flags say.
- **STM32L4 Standby wake.** The retained `pState` validity marker combined with
  the Standby flag.
- **STM32L4 Shutdown wake.** The reset flags can look like a power or brownout
  reset while the backup registers are retained, so the L4 terminal-sleep path
  reserves `RTC->BKP31R` as a one-shot marker. Before hardware Shutdown it
  writes `SHUT` (`0x53485554`). Early startup reads and clears it before
  classification, and with `pState` valid and the marker present the reset is
  `resetShutdown`.
- **STM32U375 Stop 3 wake.** Stop 3 sets no Standby flag, so the terminal path
  writes `pState->synthetic_standby_wake` and resets in software. A reset with
  that marker and `RCC_CSR.SFTRSTF` is `resetStandby`. See
  [STM32U375 Low Power](u375-low-power.md#terminal-sleep-stop-3).
- **External reset.** `externalResetAtBoot` records an NRST
  (`RCC_CSR.PINRSTF`) with the retained state valid and none of the failure
  flags set (brownout, watchdog, software, low-power, option-byte). A power-on
  also sets BORRSTF, so it is excluded. This is the plain reset that ends a
  `tag-capture` or `tag-xflash` session. It is recorded for the recovery
  decision and does not change `resetCause`.

## Reattach versus failure

Recovery resumes an active run when the boot is a reattach:

- U375 targets (`TAG_MONITOR_RESET_RECOVERY`): `monitorResetRecoveryActive()`
  is true when the retained state is valid and any of these holds:
  `MONCONNECTED`, `externalResetAtBoot`, or `DHCSR.C_DEBUGEN`. Here, and only
  here, `C_DEBUGEN` counts as evidence of a monitor reset, because the host can
  release `VC_CORERESET` before the state machine decides. `deviceInit()` keeps
  the runtime state for such a reset.
- Other targets: `reattachReset()` is `MONCONNECTED || externalResetAtBoot`.

A run that resumes this way continues with `Running(T_CONT, POWERFAIL)`, and
each family re-arms its own sampling there. On the L4 path a reattach rebuilds
the cursor from the page headers, so new samples start on the next page. That
costs a part-used page per reattach and loses no data.

A brownout that is not a reattach restarts the interrupted state from the
beginning with `State_EVENT_BROWNOUT`: `Configured(T_INIT, ...)`,
`Hibernating(T_INIT, ...)`, or, for RUNNING, `Running(T_INIT, ...)` provided
`clockTrusted` -- with an untrusted clock a RUNNING tag aborts with
`State_EVENT_POWERFAIL` (`state_machine.c`, recovery dispatch).

Any other reset with an active run is a failure, and the run ends ABORTED.

## IDLE means an empty marker log

`Idle()` records no marker. An empty state log is what makes an idle tag
resolve to IDLE, and recovery seeds `TagState_IDLE` before walking `sEpoch`.
Two rules keep that invariant:

- On `TAG_MONITOR_RESET_RECOVERY` targets, where it exists,
  `tagResetRuntimeStateForPowerInit()` runs only for a power init without
  valid retained state, and claims IDLE only when `stateLogEmpty()`. Otherwise it leaves `STATE_UNSPECIFIED`, so recovery must
  resolve the state from the marker log, and leaves the log cursors to
  recovery: `restoreLog()` when detached, the retained values under a monitor.
  `validTagState()` rejects UNSPECIFIED, so the monitor-attach branch cannot
  adopt it.
- On `TAG_MONITOR_RESET_RECOVERY` targets, `deviceInit()` clears
  `pState->valid` only for a genuine power init, not for the forced
  re-initialisation that `Finished()`, `Aborted()`, `Reset()` and
  `SelfTest()` perform. Clearing it there would open a window, spanning all
  of the device power sequencing, in which any reset is classified as a power
  loss.

Breaking the invariant leaves a tag that reads IDLE while its flash ends in
FINISHED. The host cannot erase it, because `tag-reset` erases only from
FINISHED or ABORTED. See
[the investigation](investigations/2026-09-boot-cleanup-claimed-idle.md).

## Header and page recovery

After a reset, a tag starts a new `vddHeader` and abandons the unused part of
the page under the previous header, rather than exposing a partially populated
page. Download timing must therefore expect discontinuities between headers,
and state markers record a restart.

PresTag and CompassTag also start a new page when a sample is more than a
tolerance from where its slot puts it: max(1 s, period / 2) on PresTag, and
period / 2 on CompassTag. This covers a capture halt,
a lost wakeup or a clock set, independent of how the reset was classified.

## Header validation and ECC

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

## IMUTag timing

IMUTag FIFO reads are especially sensitive. A reset can leave the hardware FIFO
phase, the local partial block cache, and the saved block timestamp out of sync.
Recovery therefore reinitializes the IMU FIFO stream, discards a warmup
interval while the IMU clock and FIFO settle (`state_run.c`), and starts the
next header from the first post-resync block timestamp.

`t_DataHeader.millis` uses only ten bits for a 1/1024-second subsecond tick
value. Host log writers convert that value to rounded integer milliseconds when
writing SQLite logs. The IMUTag log format reserves bit `0x0400` as
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

## Storage bounds

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
| `IMUTagNand` | GD5F1GQ5RE SPI-NAND | 128 MiB raw | 1004 logical 128 KiB blocks after bad-block reserve. |
| `IMUTagNandBmp581` | GD5F2GM7RE SPI-NAND | 256 MiB raw | 2008 logical 128 KiB blocks after bad-block reserve (`flash_gd5f2gm7re.mk`). |

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

Linked `vddHeader` limits for `TAG_FLASH_SIZE=256K` builds, read from each
ELF with `arm-none-eabi-nm`, at the commit shown (the BitPresTag images are
from an older build). They move whenever code
size changes, so recompute rather than quote them. The 128 KiB column assumes
the same code layout with `__persistent_end__ = 0x08020000`:

| Target | `vddHeader` | Header size | 256 KiB limit | Approximate 128 KiB limit | Built at |
| --- | ---: | ---: | ---: | ---: | --- |
| `PresTag` | `0x08009a70` | 8 B | 27,826 | 11,442 | `a87fc84a` |
| `BitPresTag` | `0x08009a60` | 8 B | 27,828 | 11,444 | `060a566` |
| `BitPresTagMX25R` | `0x08009a60` | 8 B | 27,828 | 11,444 | `060a566` |
| `CompassTag` | `0x0800b268` | 8 B | 27,059 | 10,675 | `a87fc84a` |
| `CompassTagAT25Breakout` | `0x0800b268` | 8 B | 27,059 | 10,675 | `a87fc84a` |
| `CompassTagAT25` | `0x0800ba68` | 8 B | 26,803 | 10,419 | `a87fc84a` |
| `BitTag` | `0x08008a78` | 16 B | 14,168 | 5,976 | `a87fc84a` |

`BitTag` uses a 16-byte internal data header; the other current active targets
listed above use 8-byte `t_DataHeader` records.

### The IMUTag external erase sweep

On IMUTag, `Reset()` drives the incremental `eraseExternalStart()` /
`eraseExternalNextSector()` / `eraseExternalFinish()` sequence. The blocking
`eraseExternal()` exists only for the common declaration, and nothing on that
target calls it. `eraseExternalStart()` wakes the NAND, then binary-searches
the device for the last sector that still holds data, and sweeps from sector
zero up to it. It reads the device rather than trusting the retained cursor,
because the power loss that leaves data behind also destroys that cursor. It
searches for the last dirty sector rather than the first blank one, because an
interrupted erase leaves a blank region below sectors that still hold data.
The sweep walks logical sectors only, so it never erases a factory bad block
and destroys its marker.

The sweep runs until something needs the main thread, then returns to the
state machine. `Reset()` erases at least one sector per call and then keeps
going while `chEvtGetEventsX()` shows no monitor or work event pending; that
read does not clear the mask, so main()'s `chEvtGetAndClearEvents()` still
sees whatever arrived. Read-only status polls never interrupt it, being
answered from cached state inside the ISR. A reset command received while a
sweep is already running resumes it rather than restarting from sector zero.

The live `Status` message reports:

- `sectors_erased`: sectors completed during the current reset/erase;
- `erase_sectors_total_plus_one`: total sectors expected plus one.

The plus-one encoding avoids protobuf scalar-presence ambiguity. `0` means
unsupported or unknown. `1` means supported and zero sectors need erasing. Any
other value `N + 1` means the actual total is `N`.

Qt monitor/programmer progress should prefer
`Status.erase_sectors_total_plus_one - 1`. They retain a temporary IMUTag
fallback of `Status.external_data_count * 2048` (`kImuDataLogPageBytes` in
`qtmon` and `qtprogram`), rounded up to 4096-byte
sectors, only for older firmware that does not report the new field.
`TagInfo.extflashsz` remains the physical flash capacity and is not the right
denominator for dirty-log erase progress.

## Open items

Open recovery work, including the unimplemented acquisition-phase sentinels and
the unchecked monitor-attach adoption, is in [the tag TODO](../../../TODO.md).
