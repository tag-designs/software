STM32U375: Standby entry silently degrades to Sleep depending on code placement — identical register state at the WFI

Part: STM32U375CE (Cortex-M33 r0p4), ChibiOS, system clock MSIS 4 MHz, FLASH_ACR = 0x100 (0 WS, prefetch on), ICACHE enabled, LDO. Firmware enters Standby from a small function: PWR_CR1.LPMS = 100b, SCB_SCR.SLEEPDEEP = 1, DSB, ISB, WFI. Standby exit is by reset, as documented.

Symptom. With identical source, whether the part enters Standby depends on where code lands in the image. Inserting n nops into a function that is not even on the idle path flips idle current between 4.4 uA and 1040 uA (0/2 nops sleep; 4/8/16/32 stall; three trials each). Two builds with a byte-identical 77-instruction arming function, same WFI offset within its flash line, same call list, differing only in two literal-pool data words, behave differently. Whole-tree -O0 always sleeps. Disabling ICACHE, or running the arming sequence from SRAM (.ramtext), makes every build stall.

What the stalled state is. Plain stores into retained SRAM2 (PWR_CR1.RRSB3) read back over SWD after connect-under-reset show the firmware reaches the WFI and never returns (a record written immediately before the WFI is present; one written immediately after is absent). Current is flat at 1040 uA in every 0.2 s block for minutes -- not a duty cycle. Gating TIM2's APB clock just before the WFI lowers the stalled current by 42 uA, so bus clocks are running: the core is in ordinary Sleep, the deep-sleep request having been declined. (Datasheet Stop 0 is ~170 uA; this is not a Stop.) Every measurement was taken with the debugger disconnected and, in the cases below, with no host session at all -- the tag booted from a plain programmer reset.

Register state one instruction before the WFI, captured live in a stalling boot (no debugger):

PWR_CR1   = 0x00000044   LPMS=100 (Standby), RRSB3
SCB_SCR   = 0x00000004   SLEEPDEEP
PWR_SR    = 0x00000000   no STOPF/SBF
PWR_WUSR  = 0x00000000   no WUFx pending
PWR_VOSR  = 0x01010101   R1EN, BOOSTEN, R1RDY, BOOSTRDY
PWR_SVMSR = 0x05000000
RCC_CR    = 0x0000001F   MSIS/MSIK on and ready, HSI/HSE off
FLASH_SR  = 0x00000000   no programming in progress (none in the whole boot)
NVIC_ISPR0/1 = 0, SCB_ICSR = 0   nothing pending
EXTI_RPR1/FPR1 = 0, RTC_SR = 0, I2C1_ISR = 0x1 (TXE only)
DHCSR = 0x01000000 (C_DEBUGEN = 0), DBGMCU_CR = 0

Every precondition in RM0487 Table 93 is met.

Comparison at the WFI, failing vs working image (OpenOCD connect-under-reset, hardware breakpoint on the wfi): ~380 registers -- PWR (all, incl. PUCR/PDCR), RCC (CR/ICSCR/CFGR/CCIPR/BDCR/CSR and every ENR/SLPENR/STPENR), SCB, NVIC ISER/ISPR/IABR, SysTick, EXTI, RTC, TAMP, FLASH, ICACHE, SYSCFG, GPIOA-H, SPI1, I2C1, LPTIM1, ADC1, CRS, DBGMCU, DWT/FPU -- are bit-identical. Only the core GPRs, the RTC time, one backup-register counter and FPCAR differ. ICACHE hit/miss monitors across the WFI: hits only, in both.

Debugger caveat. A hardware breakpoint anywhere near the WFI defeats the observation: with the FPB armed the WFI returns after ~1 s having consumed exactly 11 DWT_CYCCNT cycles, with STOPF/SBF clear and no handler run, on both images -- a debug event, not the fault. With the debug session attached and no breakpoints set, the working image enters Standby normally (4.4 uA) and the failing image stalls at 1036 uA exactly as it does with no debugger -- the debug connection neither causes nor cures it. In both cases OpenOCD then reports "communication failure", so losing the DAP is what any deep-sleep entry looks like from the ST-Link and does not identify the state. (Also: ST's stm32u3x.cfg examine-end handler spins on PWR_VOSR forever under connect_assert_srst; override it with an empty handler.)

Tested on a reliably failing layout, >=3 trials each, no effect: WFI forced to the start of a 16-byte line with nop padding after it (the STM32U5 errata workaround shape); a PWR register read-back before the WFI (APB posted-write theory); TIM2 stopped and unclocked; every LPTIM reset through RCC before the WFI (ES0626 2.11.1); FLASH_ACR.PRFTEN = 0; waiting for C_DEBUGEN to clear; __attribute__((aligned(16))); -O0 on the bus drivers with LTO off; a dedicated linker section for the power code. ES0626 Rev 3 has no item on Standby entry.

What helps. __attribute__((noinline)) on the arming function removed the failure at nine deliberately varied layouts, then failed at a tenth. There is no build setting that makes entry deterministic; we now measure idle current on every image we ship.

Stop 3 instead. Requesting Stop 3 (LPMS = 011) from the same function, with the same device preparation and wake configuration (RTC alarm routed through WKUP7, WUSEL7 = 11) followed by a software reset on wake, enters reliably at every layout that stalls Standby: five layouts x IDLE, three layouts x a full idle/run/finished/idle walk, all rest states 7.9-8.1 uA, and a scheduled start woke on the minute alarm exactly as programmed. Cost against Standby about 3.6 uA. So the entry sequence and the wake path are sound; it is specifically the Standby (1xx) request that is declined.

Has anyone seen a Standby (LPMS=1xx) request on the U3 degrade to Sleep with the state above? A reproducer is trivial: any U375 project, nop padding in an unrelated function, idle current.
