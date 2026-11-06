---
type: proposal
status: proposed
summary: A tag-base vendor command (F6 9A plus a key) that answers, then restarts the base into its MCU's ROM DFU bootloader, so base-flash can update a base without its button - analysed for the STM32F042 bases in use and an STM32L432 base, with a reset-then-jump design that works the same on both.
---

# Entering DFU from the running base

Today a base is put in DFU mode by plugging it in with its button held, and
then `base-flash program` writes it. A base in a field kit, or one wired into
a bench rig, is awkward to reach, and every reflash during debugging needs a
person. This proposes a second tag-base vendor command beside
`STLINK_TAGBASE_IDENTIFY`: the base answers it, then restarts into its MCU's
ROM bootloader, and `base-flash` carries on as if the button had been held.

## The command

| Bytes | Meaning |
| --- | --- |
| `F6` | `STLINK_TAGBASE_COMMAND`, the tag bases' vendor opcode |
| `9A` | `STLINK_TAGBASE_ENTER_DFU`, new, after `STLINK_TAGBASE_IDENTIFY` (`99`) |
| `44 46 55 21` | The key, ASCII `DFU!`, so that no stray or corrupted command restarts a base |

The reply is 2 bytes: `STLINK_DEBUG_ERR_OK` when the base will restart, or
`STLINK_DEBUG_ERR_FAULT` when it will not (wrong key, or a build that does not
support it). A base built before the command already answers any `F6`
subcommand other than `99` with `STLINK_DEBUG_ERR_FAULT`, so a host can just
try it; no capability flag is needed in the identify string.

`STLINK_DFU_COMMAND` (`F3`) is not used for this, although a real ST-LINK
uses it for its own firmware update. Host tools send `F3 07` (DFU exit) on
every attach, as an ST-LINK expects, and the base must keep treating `F3` as a
no-op.

## Two ways to reach the bootloader

**A. Jump from the running firmware.** Undo everything the firmware set up,
then load the bootloader's stack pointer and reset vector from system memory
and branch. This is the usual application note recipe. On a base it means:
- stop USB and drop the D+ pull-up long enough for the host to see a detach,
  or the host never re-enumerates the device as a DFU bootloader;
- stop ChibiOS's tick, DMA and every enabled interrupt, and clear pending ones;
- return the clock tree to its reset state. That differs by family: the F042
  runs USB from HSI48 trimmed by CRS, and the L432 starts on MSI. The
  snippet that prompted this proposal is L4-specific (MSI, `PLLCFGR`, `CIER`);
- return the tag-side pins to inputs, or SWDIO, SWCLK and NRST stay driven
  and the tag may be held in reset;
- on the F042, which has no `VTOR`, map system memory at address 0 before
  the jump, and leave `SYSCFG` as the bootloader expects.

Each item is a chance to leave the bootloader a state it was not written for,
and the list differs per MCU and per base.

**B. Reset, then jump before anything is set up (proposed).** The command
handler writes a flag to RAM that startup does not clear, then calls
`NVIC_SystemReset()`. Very early in the next boot, before clocks, `.data` or
`.bss`, the firmware sees the flag, clears it and jumps. The reset has already
put every peripheral, clock, GPIO and `SYSCFG` register back to its reset
value, and USB with them. Releasing D+ for the reset is what makes the host
see a detach. The bootloader starts in the same state as after a cold boot
with the button held, which is the case it is tested for. The only state that
has to survive is the flag, and SRAM keeps its contents across a system reset
on both MCUs.

B is the same code on both MCUs except for three constants, and it needs no
knowledge of what the firmware had running.

## Design (B)

**Firmware, in `embedded/bases/common`:**

1. `stlink_eval()` handles `F6 9A`: check the key; if it does not match, or
   the build has no `TAGBASE_DFU_SYSTEM_MEMORY` (below), answer `FAULT` and
   carry on. Otherwise answer `OK`, wait for the IN transfer to complete, and
   wait about 50 ms more so the host's read returns before the device goes.
2. Write the flag to a variable placed in section `.ram0`
   (`__attribute__((section(".ram0")))`), which ChibiOS's `rules_memory.ld`
   puts after `__ram0_noinit__`, in RAM that is neither loaded nor cleared: two words, `0xDF11B007` and its
   complement, so that the noise SRAM holds after power-up cannot match.
3. `NVIC_SystemReset()`.
4. Override ChibiOS's weak `__cpu_init()`, which `crt0` calls right after
   setting the stack and before `__early_init()`, the clock setup and the
   `.data` and `.bss` initialisation. On a Cortex-M0 or M4 the default does
   nothing. If both flag words match: clear them, enable the `SYSCFG` clock,
   map system memory at address 0, set MSP from the first word of system
   memory, and branch to the second. Otherwise return.

Clearing the flag before the jump means that leaving DFU, or a reset or power
cycle while in it, boots the firmware again. The button and the BOOT0 pin are
untouched, so a failed or abandoned DFU entry never leaves a base that will
not start.

**Per MCU:**

| | STM32F042 (every base today) | STM32L432 |
| --- | --- | --- |
| System memory (bootloader) | `0x1FFFC400` | `0x1FFF0000` |
| Map it at 0 | `SYSCFG_CFGR1.MEM_MODE` = `01`, after `RCC_APB2ENR.SYSCFGCOMPEN` | `SYSCFG_MEMRMP.MEM_MODE` = `001`, after `RCC_APB2ENR.SYSCFGEN` |
| Vector table | No `VTOR` on the Cortex-M0: the remap is what makes the bootloader's vectors live | `VTOR` resets to 0, so the remap serves here too |
| Flag survives the reset | SRAM is kept across a system reset; the flag sits in section `.ram0` | Put it in SRAM1. SRAM2 is erased on reset when the `SRAM2_RST` option bit is clear |
| USB pins | The bases are TSSOP20 F042F6 parts, with USB on pins remapped by `SYSCFG_CFGR1.PA11_PA12_RMP` (`main.c` sets it). The button-entered DFU already works on them, so the ROM handles the remap itself | PA11/PA12, no remap |
| Bootloader's USB clock | HSI48 with CRS, crystal-less; proven by button entry | Check the L43x/L44x entry of AN2606 for the DFU clock source before building an L432 base; a crystal-less board relies on HSI48 with CRS |
| Watchdog | The bases do not start the IWDG | If an L432 base ever selects the hardware watchdog (`IWDG_SW` = 0), it runs from reset and the bootloader does not refresh it; keep `IWDG_SW` = 1 |
| References | RM0091 (STM32F0), AN2606 | RM0394 (STM32L4), AN2606 |

`TAGBASE_DFU_SYSTEM_MEMORY` and the remap are selected by the MCU family the
base is built for (`STM32F0XX`, `STM32L4XX`). A family without them, such as
the STM32C071 of `tag-base-c071`, answers `FAULT` until it is checked: AN2606
lists a USB DFU bootloader for the C071, at its own system-memory address.

**Host, in `base-flash`:**

- `base-flash program <elf> --base <ST-LINK serial>` (or with one base
  attached, no option): if no device is in DFU mode, open the base, read its
  identity, record its USB bus and port path, send `F6 9A DFU!`, and wait up
  to a few seconds for a DFU device to appear **at the same port path**. The
  path is the only way to pair them: the F042 bootloader reports the same
  serial (`FFFFFFFEFFFF`) on every base. Then program it, and confirm as now
  that the base comes back with the expected identity and the same ST-LINK
  serial.
- A base that answers `FAULT`, or no DFU device appearing, falls back to the
  current instructions: plug it in with the button held.
- Restarting a base drops the tag's SWD lines and, on a base that powers the
  tag, its power, as a button reflash does. `base-flash` already says so; it
  should refuse unless `--yes` is given while the base reads a target
  voltage, because a running tag would lose its run. It cannot ask the tag,
  because attaching to a running tag ends the run.
- Tag Programmer's base-update path, when it has one, does the same through
  `tagprobe::dfu`.

The monitor and recovery tools never send `F6 9A`.

## Risks

- **The F042 bootloader may check how it was entered.** Some STM32 ROM
  bootloaders read the boot configuration on entry. B gives the bootloader
  the reset state, so the risk is low, but it must be shown on hardware
  before anything depends on it.
- **A flag false positive** would send a base to DFU on an ordinary reset. The
  two-word pattern makes power-up noise matching it negligible, and the flag
  is cleared on every match.
- **Running bases cannot get the command until they are reflashed once** by
  the button. Field bases need one manual update to gain it.

## Checks before it is relied on

On `bittag-base-v7` and `tag-breakout-base-l432-v1`:

1. `F6 9A DFU!` gives `OK`, and a DFU device appears at the same port within
   2 s.
2. `base-flash program` then writes and confirms the base's identity.
3. A wrong key, and `F6 9A` sent to a base without the command, give `FAULT`
   and the base keeps running.
4. Leaving DFU, unplugging while in DFU, and a reset button press while in
   DFU each bring the firmware back.
5. Twenty cold power-ups never land in DFU.
6. A tag attached afterwards attaches normally.

An L432 base would repeat 1-6.
