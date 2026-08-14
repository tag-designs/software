/**
 * @file    main.c
 * @brief   IMUTagNand STOP-mode bench test.
 *
 * This is intentionally a single-board STM32U375 test program. It does not try
 * to be portable across STM32 families, and it does not use the tag state
 * machine, sensor drivers, external flash driver, or RV3028 driver.
 *
 * Probe pins:
 * - PA2: proof of life, high once this firmware owns the pins.
 * - PA5: SPI1 SCK, visible only during the SPI receive probe.
 * - PA1: STOP envelope, high before WFI and low after wake.
 * - PA4: flash/error latch, high if flash/STOP diagnostics fail.
 * - PA6: SPI1 MISO for the receive probe.
 * - PA7: SPI1 MOSI for the receive probe.
 */

#include "ch.h"
#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

#define PIN_STOP 1U
#define PIN_PROOF 2U
#define PIN_ERROR 4U

#define MASK_STOP (1U << PIN_STOP)
#define MASK_PROOF (1U << PIN_PROOF)
#define MASK_ERROR (1U << PIN_ERROR)
#define MASK_PROBES (MASK_STOP | MASK_PROOF | MASK_ERROR)
#define MASK_RV3028_I2C ((1U << GPIOB_SCL) | (1U << GPIOB_SDA))

#define FLASH_KEY1 0x45670123U
#define FLASH_KEY2 0xCDEF89ABU
#define INTERNAL_FLASH_BASE 0x08000000U
#define FLASH_PAGE_BYTES 4096U
#define FLASH_ROW_WORDS 4U
#define FLASH_ROW_ALIGN 16U
#define FLASH_VISIBLE_ROWS 64U
#define FLASH_MAGIC 0x53543154U

#define SPI_RECEIVE_BYTES 256U
#define FIRST_PATTERN 0x11223344U
#define SECOND_PATTERN 0x55667788U

#define STOP1_LPMS PWR_CR1_LPMS_0
#define STOP3_LPMS (PWR_CR1_LPMS_0 | PWR_CR1_LPMS_1)
#define RTC_WAKEUP_1HZ ((4U << 16) | 0U)
#define WAIT_LIMIT 1000000U

#if !defined(FLASH_SR_MISERR)
#define FLASH_SR_MISERR 0U
#endif
#if !defined(FLASH_SR_FASTERR)
#define FLASH_SR_FASTERR 0U
#endif
#if !defined(FLASH_SR_RDERR)
#define FLASH_SR_RDERR 0U
#endif
#if !defined(FLASH_SR_OPTVERR)
#define FLASH_SR_OPTVERR 0U
#endif

#define FLASH_ERROR_FLAGS                                                     \
  (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |   \
   FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |  \
   FLASH_SR_RDERR | FLASH_SR_OPTVERR | FLASH_SR_OPTWERR)
#define FLASH_CLEAR_FLAGS (FLASH_SR_EOP | FLASH_ERROR_FLAGS)
#define FLASH_SLEEP_BITS                                                      \
  (FLASH_ACR_LPM | FLASH_ACR_PDREQ1 | FLASH_ACR_PDREQ2 | FLASH_ACR_SLEEP_PD)
#define RTC_INTERRUPT_BITS                                                    \
  (RTC_CR_WUTIE | RTC_CR_ALRAIE | RTC_CR_ALRBIE | RTC_CR_TSIE)
#define RTC_CLEAR_FLAGS                                                       \
  (RTC_SCR_CWUTF | RTC_SCR_CALRAF | RTC_SCR_CALRBF | RTC_SCR_CTSF |          \
   RTC_SCR_CTSOVF)
#define PWR_CLEAR_WAKE_FLAGS                                                  \
  (PWR_WUSCR_CWUF1 | PWR_WUSCR_CWUF2 | PWR_WUSCR_CWUF3 | PWR_WUSCR_CWUF4 |   \
   PWR_WUSCR_CWUF5 | PWR_WUSCR_CWUF6 | PWR_WUSCR_CWUF7 | PWR_WUSCR_CWUF8 |   \
   PWR_WUSCR_CWUF9 | PWR_WUSCR_CWUF10)

typedef struct {
  uint32_t magic;
  uint32_t sequence;
  uint32_t pattern;
  uint32_t stop_cr1;
} FlashRecord;

typedef struct {
#if (OSAL_ST_MODE == OSAL_ST_MODE_FREERUNNING) && (STM32_ST_USE_TIM2 == TRUE)
  uint32_t st_dier;
#endif
#if OSAL_ST_MODE == OSAL_ST_MODE_PERIODIC
  uint32_t systick_ctrl;
#endif
} WakeMask;

static const uint32_t probe_flash_page[FLASH_PAGE_BYTES / sizeof(uint32_t)]
    __attribute__((section(".nand_map"), used, aligned(FLASH_PAGE_BYTES)));

static uint8_t spi_buffer[SPI_RECEIVE_BYTES];
static volatile uint32_t last_stop_cr1;

static const SPIConfig spi_config = {
#if SPI_SUPPORTS_CIRCULAR == TRUE
    .circular = false,
#endif
#if SPI_SUPPORTS_SLAVE_MODE == TRUE
    .slave = false,
#endif
    .data_cb = NULL,
    .error_cb = NULL,
    .cfg1 = SPI_CFG1_DSIZE_8BITS | SPI_CFG1_MBR_DIV16,
    .cfg2 = SPI_CFG2_SSM,
    .dtr1rx = 0U,
    .dtr1tx = 0U,
    .dtr2rx = 0U,
    .dtr2tx = 0U,
};

static void probes_write(uint32_t high, uint32_t low) {
  GPIOA->BSRR.W = high | (low << 16U);
}

static void probes_init(void) {
  RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOAEN;
  (void)RCC->AHB2ENR1;

  probes_write(MASK_PROOF, MASK_STOP | MASK_ERROR);

  GPIOA->OTYPER &= ~MASK_PROBES;
  GPIOA->PUPDR &= ~((3U << (PIN_STOP * 2U)) | (3U << (PIN_PROOF * 2U)) |
                    (3U << (PIN_ERROR * 2U)));
  GPIOA->MODER = (GPIOA->MODER &
                  ~((3U << (PIN_STOP * 2U)) | (3U << (PIN_PROOF * 2U)) |
                    (3U << (PIN_ERROR * 2U)))) |
                 (1U << (PIN_STOP * 2U)) | (1U << (PIN_PROOF * 2U)) |
                 (1U << (PIN_ERROR * 2U));
}

static void raise_error(void) {
  probes_write(MASK_ERROR, 0U);
}

static void halt_with_error(void) {
  probes_init();
  probes_write(MASK_PROOF | MASK_ERROR, MASK_STOP);

  while (true) {
    __WFI();
  }
}

void __attribute__((noinline, used))
stop1test_system_halt_hook(const char *reason) {
  (void)reason;
  halt_with_error();
}

void __attribute__((noinline, used)) HardFault_Handler(void) {
  halt_with_error();
}

void __attribute__((noinline, used)) MemManage_Handler(void) {
  halt_with_error();
}

void __attribute__((noinline, used)) BusFault_Handler(void) {
  halt_with_error();
}

void __attribute__((noinline, used)) UsageFault_Handler(void) {
  halt_with_error();
}

static void pins_to_analog(stm32_gpio_t *port, uint32_t pins) {
  uint32_t mode_mask = 0U;

  for (uint32_t pin = 0U; pin < 16U; pin++) {
    if ((pins & (1U << pin)) != 0U) {
      mode_mask |= 3U << (pin * 2U);
    }
  }

  port->PUPDR &= ~mode_mask;
  port->OSPEEDR &= ~mode_mask;
  port->MODER = (port->MODER & ~mode_mask) | mode_mask;
}

static void quiesce_gpio_for_stop(void) {
  probes_init();
  pins_to_analog(GPIOA, (~MASK_PROBES) & 0xFFFFU);
  pins_to_analog(GPIOB, (~MASK_RV3028_I2C) & 0xFFFFU);
  pins_to_analog(GPIOC, 0xFFFFU);

  palSetLineMode(LINE_SCL, PAL_MODE_INPUT_PULLUP);
  palSetLineMode(LINE_SDA, PAL_MODE_INPUT_PULLUP);
}

static void flash_unlock(void) {
  if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
  }
}

static void flash_finish_operation(uint32_t operation_bit) {
  while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
  }
  if ((FLASH->SR & FLASH_SR_EOP) != 0U) {
    CLEAR_BIT(FLASH->SR, FLASH_SR_EOP);
  }
  CLEAR_BIT(FLASH->CR, operation_bit);
  __enable_irq();
  SET_BIT(FLASH->CR, FLASH_CR_LOCK);

  if ((FLASH->SR & FLASH_ERROR_FLAGS) != 0U) {
    raise_error();
  }
}

static void flash_erase_probe_page(void) {
  const uint32_t address = (uint32_t)(uintptr_t)probe_flash_page;
  const uint32_t absolute_page =
      (address - INTERNAL_FLASH_BASE) / FLASH_PAGE_BYTES;
  const uint32_t pages_per_bank =
      (FLASH_CR_PNB >> POSITION_VAL(FLASH_CR_PNB)) + 1U;
  const uint32_t bank = absolute_page / pages_per_bank;
  const uint32_t page_in_bank = absolute_page % pages_per_bank;
  uint32_t cr_set = page_in_bank << POSITION_VAL(FLASH_CR_PNB);

  if (bank > 1U) {
    raise_error();
    return;
  }
  if (bank == 1U) {
    cr_set |= FLASH_CR_BKER;
  }

  SET_BIT(FLASH->SR, FLASH_CLEAR_FLAGS);
  flash_unlock();
  __disable_irq();
  MODIFY_REG(FLASH->CR, FLASH_CR_PNB | FLASH_CR_BKER, cr_set);
  SET_BIT(FLASH->CR, FLASH_CR_PER);
  SET_BIT(FLASH->CR, FLASH_CR_STRT);
  flash_finish_operation(FLASH_CR_PER);
}

static void flash_program_row(uint32_t *address, const uint32_t *data) {
  if ((((uint32_t)address) & (FLASH_ROW_ALIGN - 1U)) != 0U) {
    raise_error();
    return;
  }

  SET_BIT(FLASH->SR, FLASH_CLEAR_FLAGS);
  flash_unlock();
  __disable_irq();
  SET_BIT(FLASH->CR, FLASH_CR_PG);
  for (uint32_t i = 0U; i < FLASH_ROW_WORDS; i++) {
    ((__IO uint32_t *)address)[i] = data[i];
  }
  __DSB();
  flash_finish_operation(FLASH_CR_PG);
}

static void flash_write_record_block(uint32_t block, uint32_t pattern) {
  for (uint32_t row = 0U; row < FLASH_VISIBLE_ROWS; row++) {
    const uint32_t sequence = (block * FLASH_VISIBLE_ROWS) + row;
    const FlashRecord record = {
        .magic = FLASH_MAGIC,
        .sequence = sequence,
        .pattern = pattern ^ row,
        .stop_cr1 = last_stop_cr1,
    };
    uint32_t *address =
        (uint32_t *)((uintptr_t)probe_flash_page + (sequence * sizeof(record)));

    flash_program_row(address, (const uint32_t *)&record);

    const volatile FlashRecord *readback = (const volatile FlashRecord *)address;
    if ((readback->magic != record.magic) ||
        (readback->sequence != record.sequence) ||
        (readback->pattern != record.pattern) ||
        (readback->stop_cr1 != record.stop_cr1)) {
      raise_error();
    }
  }
}

static bool wait_for_bit(volatile uint32_t *reg, uint32_t mask) {
  for (volatile uint32_t i = 0U; i < WAIT_LIMIT; i++) {
    if ((*reg & mask) == mask) {
      return true;
    }
  }

  return false;
}

static void clear_rtc_flags(void) {
  PWR->DBPR |= PWR_DBPR_DBP;
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  RTC->SCR = RTC_CLEAR_FLAGS;
  RTC->WPR = 0xFFU;

  STM32_RTC_CLEAR_ALL_EXTI();
  nvicClearPending(STM32_RTC_GLOBAL_NUMBER);
  nvicClearPending(STM32_RTC_TAMP_NUMBER);
}

static void rtc_init_for_test(void) {
  rtcObjectInit(&RTCD1);
  RTCD1.rtc = RTC;
  RTCD1.tamp = TAMP;
  RTCD1.callback = NULL;

  RCC->APB1ENR1 |= RCC_APB1ENR1_RTCAPBEN;
  (void)RCC->APB1ENR1;

  PWR->DBPR |= PWR_DBPR_DBP;
  RTC->WPR = 0xCAU;
  RTC->WPR = 0x53U;
  RTC->CR &= ~RTC_INTERRUPT_BITS;
  RTC->CR &= ~RTC_CR_WUTE;
  RTC->SCR = RTC_CLEAR_FLAGS;

  if ((RTC->ICSR & RTC_ICSR_INITS) == 0U) {
    RTC->ICSR |= RTC_ICSR_INIT;
    if (!wait_for_bit(&RTC->ICSR, RTC_ICSR_INITF)) {
      raise_error();
      while (true) {
        __WFI();
      }
    }

    RTC->CR = (STM32_RTC_CR_INIT & STM32_RTC_CR_MASK) | RTC_CR_BYPSHAD;
    RTC->PRER = STM32_RTC_PRER_BITS & 0x7FFFU;
    RTC->PRER = STM32_RTC_PRER_BITS;
    RTC->ICSR &= ~RTC_ICSR_INIT;
  }

  TAMP->CR1 |= (STM32_TAMP_CR1_INIT & STM32_TAMP_CR1_MASK);
  TAMP->CR2 |= (STM32_TAMP_CR2_INIT & STM32_TAMP_CR2_MASK);
  TAMP->FLTCR |= (STM32_TAMP_FLTCR_INIT & STM32_TAMP_FLTCR_MASK);
  TAMP->IER |= (STM32_TAMP_IER_INIT & STM32_TAMP_IER_MASK);

  RTC->WPR = 0xFFU;
  STM32_RTC_ENABLE_ALL_EXTI();
  STM32_RTC_IRQ_ENABLE();
}

static void hal_init_for_test(void) {
  osalInit();
  hal_lld_init();
  palInit();
  spiInit();
  rtc_init_for_test();
  boardInit();
  stInit();
}

static void spi_receive_probe(void) {
  palSetLineMode(LINE_LSM_FLASH_SCK,
                 PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);
  palSetLineMode(LINE_LSM_FLASH_MISO,
                 PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST |
                     PAL_STM32_PUPDR_PULLDOWN);
  palSetLineMode(LINE_LSM_FLASH_MOSI,
                 PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);

  spiStart(&SPID1, &spi_config);
  (void)spiReceive(&SPID1, sizeof(spi_buffer), spi_buffer);
  spiStop(&SPID1);
}

static WakeMask mask_os_tick_wakeups(void) {
  WakeMask mask = {0};

#if (OSAL_ST_MODE == OSAL_ST_MODE_FREERUNNING) && (STM32_ST_USE_TIM2 == TRUE)
  mask.st_dier = STM32_ST_TIM->DIER;
  STM32_ST_TIM->DIER = mask.st_dier & ~STM32_TIM_DIER_IRQ_MASK;
  STM32_ST_TIM->SR = ~STM32_TIM_DIER_IRQ_MASK;
  nvicClearPending(STM32_TIM2_NUMBER);
  nvicDisableVector(STM32_TIM2_NUMBER);
#endif

#if OSAL_ST_MODE == OSAL_ST_MODE_PERIODIC
  mask.systick_ctrl = SysTick->CTRL;
  SysTick->CTRL = mask.systick_ctrl & ~SysTick_CTRL_TICKINT_Msk;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
#endif

  return mask;
}

static void restore_os_tick_wakeups(const WakeMask *mask) {
#if (OSAL_ST_MODE == OSAL_ST_MODE_FREERUNNING) && (STM32_ST_USE_TIM2 == TRUE)
  STM32_ST_TIM->SR = ~STM32_TIM_DIER_IRQ_MASK;
  STM32_ST_TIM->DIER = mask->st_dier;
  nvicClearPending(STM32_TIM2_NUMBER);
  nvicEnableVector(STM32_TIM2_NUMBER, STM32_IRQ_TIM2_PRIORITY);
#endif

#if OSAL_ST_MODE == OSAL_ST_MODE_PERIODIC
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  SysTick->CTRL = mask->systick_ctrl;
#endif
}

static void clear_all_pending_irqs(void) {
  SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;

  for (uint32_t i = 0U; i < (sizeof(NVIC->ICPR) / sizeof(NVIC->ICPR[0]));
       i++) {
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }

  __DSB();
  __ISB();
}

static void apply_stop_pulls(void) {
  PWR->PUCRA = STM32_PWR_PUCRA;
  PWR->PDCRA = STM32_PWR_PDCRA;
  PWR->PUCRB = STM32_PWR_PUCRB | MASK_RV3028_I2C;
  PWR->PDCRB = STM32_PWR_PDCRB & ~MASK_RV3028_I2C;
  PWR->PUCRC = STM32_PWR_PUCRC;
  PWR->PDCRC = STM32_PWR_PDCRC;
  PWR->PUCRD = STM32_PWR_PUCRD;
  PWR->PDCRD = STM32_PWR_PDCRD;
  PWR->PUCRE = STM32_PWR_PUCRE;
  PWR->PDCRE = STM32_PWR_PDCRE;
  PWR->PUCRG = STM32_PWR_PUCRG;
  PWR->PDCRG = STM32_PWR_PDCRG;
  PWR->PUCRH = STM32_PWR_PUCRH;
  PWR->PDCRH = STM32_PWR_PDCRH;
  PWR->APCR |= PWR_APCR_APC;
}

static void restore_run_after_stop(void) {
  const uint32_t flash_acr =
      (STM32_FLASH_ACR & ~FLASH_ACR_LATENCY_Msk) | STM32_FLASHBITS;
  uint32_t vos_ready = 0U;

  stm32_clock_init();
  WRITE_REG(PWR->VOSR, STM32_PWR_VOSR);

  if ((STM32_PWR_VOSR & PWR_VOSR_R1EN) != 0U) {
    vos_ready |= PWR_VOSR_R1RDY;
  }
  if ((STM32_PWR_VOSR & PWR_VOSR_R2EN) != 0U) {
    vos_ready |= PWR_VOSR_R2RDY;
  }
  if ((vos_ready != 0U) && !wait_for_bit(&PWR->VOSR, vos_ready)) {
    raise_error();
  }

  CLEAR_BIT(FLASH->ACR, FLASH_SLEEP_BITS);
  WRITE_REG(FLASH->ACR, flash_acr & ~FLASH_SLEEP_BITS);
  for (uint32_t timeout = WAIT_LIMIT;
       (timeout > 0U) && ((FLASH->SR & (FLASH_SR_PD1 | FLASH_SR_PD2)) != 0U);
       timeout--) {
    __NOP();
  }
  if ((FLASH->SR & (FLASH_SR_PD1 | FLASH_SR_PD2)) != 0U) {
    raise_error();
  }
  SET_BIT(FLASH->SR, FLASH_CLEAR_FLAGS);
}

static void arm_rtc_wakeup_1hz(void) {
  const RTCWakeup wakeup = {.wutr = RTC_WAKEUP_1HZ};

  clear_rtc_flags();
  rtcSTM32SetPeriodicWakeup(&RTCD1, &wakeup);
}

static void enter_stop_and_wake(uint32_t lpms) {
  WakeMask wake_mask;

  rccResetSPI1();
  RCC->AHB1ENR2 |= RCC_AHB1ENR2_PWREN;
  (void)RCC->AHB1ENR2;

  MODIFY_REG(PWR->WUCR3, PWR_WUCR3_WUSEL7,
             PWR_WUCR3_WUSEL7_0 | PWR_WUCR3_WUSEL7_1);
  CLEAR_BIT(PWR->WUCR2, PWR_WUCR2_WUPP7);
  SET_BIT(PWR->WUCR1, PWR_WUCR1_WUPEN7);

  clear_rtc_flags();
  PWR->WUSCR = PWR_CLEAR_WAKE_FLAGS;
  PWR->SR = PWR_SR_CSSF;

  wake_mask = mask_os_tick_wakeups();
  clear_all_pending_irqs();
  clear_rtc_flags();

  apply_stop_pulls();
  probes_write(MASK_STOP, 0U);
  DBGMCU->CR = 0U;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, lpms);
  last_stop_cr1 = PWR->CR1;


  SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
  __DSB();
  __ISB();
  __WFI();
  __DSB();
  __ISB();
  CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  probes_write(0U, MASK_STOP);
  restore_run_after_stop();
  PWR->SR = PWR_SR_CSSF;
  PWR->APCR &= ~PWR_APCR_APC;
  restore_os_tick_wakeups(&wake_mask);
}

static void enter_terminal_stop3(void) {
  rtcSTM32SetPeriodicWakeup(&RTCD1, NULL);
  clear_rtc_flags();

  RCC->AHB1ENR2 |= RCC_AHB1ENR2_PWREN;
  (void)RCC->AHB1ENR2;

  PWR->WUSCR = PWR_CLEAR_WAKE_FLAGS;
  PWR->SR = PWR_SR_CSSF;
  (void)mask_os_tick_wakeups();
  clear_all_pending_irqs();

  apply_stop_pulls();
  probes_write(MASK_STOP, 0U);
  DBGMCU->CR = 0U;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, STOP3_LPMS);
  last_stop_cr1 = PWR->CR1;

  SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
  __DSB();
  __ISB();
  __WFI();

  raise_error();
  while (true) {
    __WFI();
  }
}

int main(void) {
  probes_init();
  hal_init_for_test();
  probes_init();
  clear_rtc_flags();
  chSysInit();

  flash_erase_probe_page();
  spi_receive_probe();
  quiesce_gpio_for_stop();

  arm_rtc_wakeup_1hz();
  enter_stop_and_wake(STOP1_LPMS);
  flash_write_record_block(0U, FIRST_PATTERN);

  SPI1->CR1 &= ~SPI_CR1_SPE;
  (void)SPI1->CR1;

  enter_stop_and_wake(STOP1_LPMS);
  flash_write_record_block(1U, SECOND_PATTERN);

  enter_terminal_stop3();
}
