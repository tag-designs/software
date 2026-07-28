#include "ch.h"
#include "hal.h"

#include "core_runtime.h"
#include "monitor.h"

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

static inline bool idlePowerUsesStop(enum Sleep mode)
{
  return (mode == STOP0) || (mode == STOP1) || (mode == STOP2);
}

static inline void idlePowerApplyMode(enum Sleep mode)
{
  if (!idlePowerUsesStop(mode))
  {
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

#if defined(LINE_LED1)
  palSetLineMode(LINE_LED1, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLine(LINE_LED1);
#endif
  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, idlePowerLpms(mode));
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
}

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

void idle_loop(void)
{
  if (monitorIsAttached())
  {
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

#if defined(LINE_testpin)
  palSetLine(LINE_testpin);
#endif
  __DSB();
  __WFI();
  __ISB();
}

void idle_leave(void)
{
#if defined(LINE_testpin)
  palClearLine(LINE_testpin);
#endif
#if defined(LINE_LED1)
  palClearLine(LINE_LED1);
  palSetLineMode(LINE_LED1, PAL_MODE_INPUT_ANALOG);
#endif
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
}
