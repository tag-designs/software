/**
 * @file power_modes.c
 * @brief IMUTagU3bmm350 ChibiOS idle-thread STOP-mode hooks.
 *
 * @details The idle hook enters the managed sleep mode selected by
 *          idlePowerMode when the monitor is detached. When a monitor is
 *          attached, the hook avoids deep sleep so shared-memory monitor
 *          requests can be serviced promptly.
 */

#include "ch.h"
#include "hal.h"

#include "core_runtime.h"
#include "monitor.h"

/**
 * @brief Convert a core sleep selector into the STM32U3 PWR LPMS field.
 *
 * @param[in] mode Core sleep selector requested by the runtime.
 * @return PWR_CR1_LPMS field value for supported STOP modes.
 */
static inline uint32_t idlePowerLpms(enum Sleep mode)
{
  switch (mode)
  {
  case STOP1:
#if defined(PWR_CR1_LPMS_STOP1)
    return PWR_CR1_LPMS_STOP1;
#elif defined(PWR_CR1_LPMS_0)
    return PWR_CR1_LPMS_0;
#else
    return 0U;
#endif
  case STOP2:
#if defined(PWR_CR1_LPMS_STOP2)
    return PWR_CR1_LPMS_STOP2;
#elif defined(PWR_CR1_LPMS_1)
    return PWR_CR1_LPMS_1;
#else
    return 0U;
#endif
  case STOP0:
  default:
    return 0U;
  }
}

/**
 * @brief Report whether a core sleep selector requires STOP-mode entry.
 *
 * @param[in] mode Core sleep selector requested by the runtime.
 * @return true for STOP0/STOP1/STOP2, false for shallow sleep modes.
 */
static inline bool idlePowerUsesStop(enum Sleep mode)
{
  return (mode == STOP0) || (mode == STOP1) || (mode == STOP2);
}

/**
 * @brief Program SCB/PWR state for the requested idle sleep depth.
 *
 * @param[in] mode Core sleep selector requested by the runtime.
 */
static inline void idlePowerApplyMode(enum Sleep mode)
{
  if (!idlePowerUsesStop(mode))
  {
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

  palSetLineMode(LINE_LED1, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLine(LINE_LED1);
  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, idlePowerLpms(mode));
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
}

/* Public idle-hook contract documented in power_modes.h. */
void idle_enter(void)
{

  /* --- 2. Enable Clocks in Sleep Mode --- */
  /* these appear on by default
  RCC->AHB1SLPENR1 |= RCC_AHB1SLPENR1_GPDMA1SLPEN | RCC_AHB1SLPENR1_SRAM1SLPEN;
  RCC->APB2SLPENR  |= RCC_APB2SLPENR_SPI1SLPEN;
  */ 
  /* Dummy read to ensure RCC write finishes before moving on */
  //(void)RCC->APB2ENR;

  if (!monitorIsAttached()){
    idlePowerApplyMode(idlePowerMode);
  }
}

/* Public idle-hook contract documented in power_modes.h. */
void idle_loop(void)
{
  if (monitorIsAttached())
  {
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

  palSetLine(LINE_testpin);
  __DSB();
  __WFI();
  __ISB();
}

/* Public idle-hook contract documented in power_modes.h. */
void idle_leave(void)
{
  palClearLine(LINE_testpin);
  palClearLine(LINE_LED1);
  palSetLineMode(LINE_LED1, PAL_MODE_INPUT_ANALOG);
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
}
