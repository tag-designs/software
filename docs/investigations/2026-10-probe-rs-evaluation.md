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

## Still to do

- Program an image with probe-rs (erase, program, start in one session, then
  clear PEMPTY on L4), after checking the U3 bank map.
- Measure resting current after a probe-rs session against one of ours, and
  after a fresh power-on, to settle the C_DEBUGEN and DBGMCU questions.
- Reflash the BitTag programmer base with current firmware and repeat.
