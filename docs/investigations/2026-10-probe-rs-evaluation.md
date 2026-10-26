---
type: investigation
status: open
summary: Whether probe-rs could replace tagcore's ST-LINK layer, the SWD recovery primitives and STM32CubeProgrammer in qtprogram - what its source says, and what a spike against the tag bases found.
---

# probe-rs evaluation (2026-10)

The question: could [probe-rs](https://probe.rs) replace (1) the bespoke
ST-LINK layer of tagcore-rs, (2) the SWD primitives under
`host/libraries/tagcore/recovery`, and (3) STM32_Programmer_CLI in qtprogram?
The spike is [`host/spikes/probe-rs-spike`](../../host/spikes/probe-rs-spike/README.md)
on the `probe-rs-spike` branch, built against probe-rs 0.32.0 (2026-07-22).

## From the probe-rs source (master `5829d0f`, release 0.32.0)

- **ST-LINK driver.** It accepts `0483:3748`, the PID our bases present, and
  needs all three bulk endpoints (OUT 0x02, IN 0x81, trace IN 0x83). It requires
  ST-LINK/V2 firmware J26 or later, and uses `INIT_AP` from J28; our bases report
  V2 J37. Access-port discovery uses `READ_DAP_REG` (0x45) on each AP's IDR and
  stops at the first IDR of 0 or error. Memory goes through the 32-, 16- and
  8-bit memory commands, each followed by `GETLASTRWSTATUS2`. Core registers go
  through DCRSR/DCRDR in memory, not the ST-LINK register commands.
- **Targets.** STM32L432KB/KC are built in. The STM32U3 family arrived in
  v0.31.0 (2026-01-17, PR #3689), including all eight STM32U375 variants. The
  U3 file is generated from ST's pack, and its second flash bank range looks
  wrong (on the U375RG, BANK_2 is `0x08010000-0x08090000`, overlapping BANK_1);
  check it against RM0487 before flashing.
- **STM32 sequences.** L4 uses `Stm32Armv7`, U3 uses the shared `Stm32Armv8`
  (with H5, L5, U5, WBA). On attach both set DBGMCU_CR DBG_STOP and DBG_STANDBY
  (Armv7 also DBG_SLEEP). On detach Armv7 restores the saved value and Armv8
  clears the bits. Neither calls the default `debug_core_stop`, which would write
  DHCSR.C_DEBUGEN = 0 and DEMCR = 0. A target's sequence can be replaced
  (`Target::debug_sequence` is public; `ArmDebugSequence` is a public trait).
- **Missing.** No option-byte API, no FLASH_SR.PEMPTY / OBL_LAUNCH handling,
  and no public way to read back non-memory-mapped flash through a flash
  algorithm. A `Session` briefly halts the core when it is created; a raw
  `attach_to_unspecified` + `try_into_arm_debug_interface` does not.

## Spike results

### Programmer base for the BitTag (serial 0037001A5556431520303537)

probe-rs listed and opened the base, read its serial, version (2, 37) and
target voltage, drove nRST and entered SWD. Every `READ_DAP_REG` then returned
OK with the value `0x143`: 323, the base's previous target-voltage reply
(3.23 V). The base's firmware predates `773db79a` (2026-07-03), which made the
handler put the value in the reply on success. With every AP reporting an IDR
of 0x143, probe-rs scanned all 256 APs and refused AP0 as "the wrong type".
**This base needs current base firmware before probe-rs can use it.**

### IMUTag breakout base (serial 004D0059543050162030324E, STM32U375 tag)

With current base firmware, probe-rs works:

- AP discovery stops correctly after AP0, and memory reads are right (DBGMCU
  IDCODE `0x10016454`, as tagcore reads it).
- Debug-port reads (DPIDR, CTRL/STAT) return 0. `SWD_ReadDAPReg` in the bases'
  `ll_swd*.c` reads RDBUFF after a DP read, which returns the last AP value
  rather than the DP register, and writes the register address's low bits into
  SELECT.DPBANKSEL. probe-rs's ST-LINK path did not need DP reads here.
- Attach under reset leaves the core halted at its reset vector (PC
  `0x08000638`, vector `0x08000639`) with either sequence.
- During the session the stock `Stm32Armv8` sequence set DBGMCU_CR to 0x6
  (DBG_STOP, DBG_STANDBY); ours (the ARM default) left it at 0. After detach
  both left DBGMCU_CR = 0 and DEMCR = 0.
- **DHCSR.C_DEBUGEN after a detach cannot be observed through our bases:**
  `SWD_Open()` ends by writing `DHCSR = DBGKEY | C_DEBUGEN` on every SWD entry,
  so the read that would check it sets it. The same write means every attach
  through these bases -- CubeProgrammer, tagcore and probe-rs alike -- leaves
  C_DEBUGEN set until a power-on reset. Whether that costs sleep current needs a
  Joulescope measurement.

The tag was IDLE with an empty state log before and after.

### Programming fw-v0.6 on the IMUTag (attempted, not completed)

`program` attaches under reset, then erases, programs and verifies the ELF
in one session. For the fw-v0.6 IMUTagNandBmp581 image it planned the
expected work: the U3 algorithm `stm32u3xx_1m_0800`, segments at their
physical addresses (`.data` at `0x0800EA9C`), and an erase of only the 4 KB
sectors `0x08000000-0x0800F000`. NOLOAD regions (persistent, calibration,
NAND map) have no file data and are not touched. The image stays below the
overlapping bank-2 range, so that target-file bug did not apply.

The flash algorithm's Init ran and returned to its breakpoint, then probe-rs
issued a **1-byte `READMEM_8BIT`**. probe-rs always reads at least 2 bytes
for an 8-bit read (`buffer_len = length.max(2)`, `probe/stlink/mod.rs`),
because a genuine ST-LINK replies with 2 bytes to a 1-byte read; our bases'
handler replies with exactly the length asked, so the USB read timed out.
No erase was issued, and the tag still ran 8f1350da afterwards. The attempt
is logged in `captures/2026-10-26-imutag-probe-rs/flash-log.md` (outside the
repository).

So far, three base-firmware changes stand between our bases and probe-rs:

1. `READ_DAP_REG` must return the value on success (fixed in `773db79a`;
   bases flashed earlier need reflashing).
2. A 1-byte `READMEM_8BIT` must return 2 bytes, as an ST-LINK does.
3. DP register reads in `SWD_ReadDAPReg` should return the DP register, not
   RDBUFF, and leave DPBANKSEL alone. probe-rs has not needed them yet.

### A field-safe driver: probe-rs over tagcore's command subset

Field bases cannot be reflashed, and there is no record of which base
firmware is out there, so the three base changes above cannot be relied on.
probe-rs accepts outside probe drivers (`Probe::from_specific_probe` with the
public `DebugProbe`, `ArmDebugInterface`, `DapAccess` and
`ArmMemoryInterface` traits), so the spike adds one,
[`tagbase.rs`](../../host/spikes/probe-rs-spike/src/tagbase.rs). It sends
only the commands tagcore's `LinkAdapt` already uses on every base -- mode,
DFU exit, nRST, SWD enter/exit, voltage, `READMEM_32BIT`/`WRITEMEM_32BIT` with
the R/W status check in 1 KB chunks, and `READDEBUGREG` for the chip ID -- and
answers the rest itself:

- DAP register reads come from a table of the part's real values (DPIDR, and
  AP0's IDR, BASE, CFG and CSW, read from an STM32U375 through a base whose
  `READ_DAP_REG` works; the L4 values are the standard Cortex-M4 ones); DAP
  writes are ignored, because memory never goes through DRW/TAR.
- 8- and 16-bit accesses are done with 32-bit ones (read-modify-write for
  writes), which suits SRAM and flash-algorithm buffers but not a peripheral
  register with side effects.
- nRST is asserted before SWD is entered, as tagcore does, so a sleeping tag
  answers.

It needed one change to tagcore-rs: `LinkAdapt::open()` and
`LinkAdapt::enter_swd()`, the two halves of `attach_with_reset()`, made
public.

On the IMUTag base, through this driver: `raw` and `session` gave the same
results as the stock driver, and **`program` flashed fw-v0.6 (`56e5e6a0`)
onto the IMUTag, programmed and verified in 2.2 s**, erasing exactly the 15
sectors `0x08000000-0x0800E000`. tagcore's `tag-info` then reported the new
firmware, IDLE, with an empty state log. Logged in
`captures/2026-10-26-imutag-probe-rs/flash-log.md`.

On the BitTag programmer base -- pre-July firmware, the one probe-rs's own
driver could not use -- the tag-base driver works: `raw` identified the
STM32L432 (idcode `0x10016435`) and read memory, and `session` attached
under reset with the core halted at its reset vector (PC `0x08000514`,
vector `0x08000515`), DBGMCU_CR and DEMCR 0 afterwards. The BitTag was IDLE
before and after. The UID reads as zeros on both parts only because the
part is held in reset during `raw`: the same L432 address matched the
firmware's UUID when read with the tag running.

Programming the L432 through the same base also works. `program
--chip-erase` follows the release rule for an L432 image without calibration
pages (a whole-chip erase, which also clears the internal-flash log), and on
an STM32L4 it then reads FLASH_SR and clears PEMPTY only if it is set, as
`flash_release.py` does. fw-v0.6 (`56e5e6a0`) was erased, programmed and
verified in 1.4 s; PEMPTY was clear afterwards (it is sampled at power-on and
option-byte load, not at erase, so one session never exposes it). The BitTag
then booted fw-v0.6, IDLE, and `tag-test` passed every self-test. Logged in
`captures/2026-10-26-bittag-probe-rs/flash-log.md`.

So probe-rs, through the tag-base driver, now attaches to, halts and programs
both STM32L432 and STM32U375 tags through both the oldest base firmware we
have and current base firmware.

### Resting current after each kind of attach (BitTag, 2.5 V)

Measured with the Joulescope on the BitTag in IDLE (L432 Shutdown): after a
tagcore attach, a probe-rs session with our sequence, a probe-rs session with
the stock `Stm32Armv7` sequence, a probe-rs chip-erase-and-program, and a power
cycle with no attach, every pair of 45 s windows read 0.1203-0.1206 uA at
2.496 V. No attach leaves a measurable cost in IDLE; the C_DEBUGEN bit the bases
set and the DBGMCU bits probe-rs's sequence sets during a session are harmless
there. The numbers and conditions are in the BitTag
[power results](../../embedded/tags/BitTag/design/power-results.md).

The same comparison on the IMUTag (STM32U375, terminal sleep Stop 3, 3.693 V)
gives the same answer: after a tagcore attach, a probe-rs session with either
sequence, and a probe-rs program, IDLE rests at 6.41-6.44 uA, the `fw-v0.6`
qualification figure for that board. A power cycle moved the level to
6.55-6.61 uA, where a tagcore attach and a stock-sequence probe-rs session
again agreed; that step is not attach-related and its cause is unknown. The
numbers are in the IMUTag
[power notes](../../embedded/tags/families/IMUTag/design/power.md).

### Base firmware fixed: the stock probe-rs driver works

`be0b151f` on `main` fixes two of the three base problems above: a 1-byte
`READMEM_8BIT` now answers with 2 bytes, and DP register reads return the
register (bank from bits [7:4], no RDBUFF read). `a473918f` adds the bases'
`F6 99` identify command (see `embedded/bases/README.md`). The IMUTag breakout
base was reflashed over DFU with `main` at `a473918f`
(`captures/2026-10-26-base-identify/flash-log.md`). Through it, with
**probe-rs's own ST-LINK driver**:

- `tag-info` printed the base identity,
  `base=tag-breakout-base-l432-u375-1v8;fw=a473918f;dirty=0;...`.
- `raw` read the real DP registers (DPIDR `0x0BE12477`, CTRL/STAT
  `0xF0000040`) and AP0 as before.
- `session` attached under reset, core halted at the reset vector.
- `program` flashed fw-v0.6 onto the IMUTag: verified in 2.2 s, the same 15
  sectors, `56e5e6a0` IDLE afterwards -- the step that timed out on the 1-byte
  read before the fix.

So a base with current firmware works with stock probe-rs, and the tag-base
driver remains the route for field bases, which keep their firmware. A host
can tell which it has from the identify command: a base that answers it has
the fixes.

## Still to do

- Measure RUNNING after a probe-rs attach (BitTag Standby between wakes;
  IMUTag Stop 2 between samples), if the run case matters for the decision.
