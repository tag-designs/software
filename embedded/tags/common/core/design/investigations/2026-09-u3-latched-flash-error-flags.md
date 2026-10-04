---
type: investigation
status: closed
summary: The 995 uA STM32U3 idle fault attributed to a latched flash ECC flag on 2026-09-02, how the flag gets latched, and why the clear was later found not to be on the cause.
---

# Latched Flash Error Flags and STM32U3 Low-Power Entry

2026-09-02 to 2026-09-04. Cut verbatim from [Restart Recovery](../restart-recovery.md).
The conclusion here was later revised: a failing build was captured with the
flash error flags clean, and the 1036 uA measured when the clear was added to
the live path was the layout-dependent Standby stall. See the first entry of
[the Standby layout investigation](2026-09-u375-standby-layout-dependence.md). The callout marked
"Stale as of 2026-09-04" is itself stale: since 0638a76 (2026-09-07) the
terminal sleep is `tagPowerEnterStop3()`, which calls
`tagPowerClearFlashErrorFlags()`.

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
> [`open-issues.md`](../../../../design/open-issues.md). Flags are cleared by writing 1, so no flash
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
