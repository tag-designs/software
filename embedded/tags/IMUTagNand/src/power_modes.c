/**
 * @file power_modes.c
 * @brief IMUTagNand ChibiOS idle-thread STOP-mode hooks.
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

#define IDLE_STOP_WAIT_LIMIT 1000000U

#ifndef TAG_IDLE_STOP_DIAGNOSTICS
/**
 * @brief Enable IMUTagNand idle-hook GPIO probes around STOP/WFI entry.
 */
#define TAG_IDLE_STOP_DIAGNOSTICS 0
#endif

static volatile bool idleStopNeedsRecovery;

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
 * @brief Report whether STOP wake needs explicit run-state recovery.
 *
 * @param[in] mode Core sleep selector requested by the runtime.
 * @return true for STOP1/STOP2, false for STOP0 and shallow sleep modes.
 */
static inline bool idlePowerNeedsRecovery(enum Sleep mode)
{
  return (mode == STOP1) || (mode == STOP2);
}

/**
 * @brief Restore regulator state after STOP1/STOP2.
 *
 * @details STOP wake returns with the CPU executing, but the VCORE range is
 *          not guaranteed to match the normal IMUTagNand run configuration.
 *          The tested recovery path only restores the VCORE range/booster
 *          state; do not re-run clock init, re-arm LPTIM1, or modify flash
 *          state from the idle hook.
 */
static void idlePowerRecoverAfterStop(void)
{
  uint32_t vos_ready = 0U;

#if STM32_BOOSTER_ENABLED == TRUE
  MODIFY_REG(RCC->CFGR4, RCC_CFGR4_BOOSTSEL | RCC_CFGR4_BOOSTDIV,
             STM32_BOOSTSEL | STM32_BOOSTDIV);
  WRITE_REG(PWR->VOSR, STM32_PWR_VOSR | PWR_VOSR_BOOSTEN);
#else
  WRITE_REG(PWR->VOSR, STM32_PWR_VOSR);
#endif

  if ((STM32_PWR_VOSR & PWR_VOSR_R1EN) != 0U)
  {
    vos_ready |= PWR_VOSR_R1RDY;
  }
  if ((STM32_PWR_VOSR & PWR_VOSR_R2EN) != 0U)
  {
    vos_ready |= PWR_VOSR_R2RDY;
  }
#if STM32_BOOSTER_ENABLED == TRUE
  vos_ready |= PWR_VOSR_BOOSTRDY;
#endif

  for (uint32_t timeout = IDLE_STOP_WAIT_LIMIT;
       (timeout > 0U) && ((PWR->VOSR & vos_ready) != vos_ready);
       timeout--)
  {
    __NOP();
  }

#if TAG_IDLE_STOP_DIAGNOSTICS
  if ((vos_ready != 0U) && ((PWR->VOSR & vos_ready) != vos_ready))
  {
    palSetLine(LINE_LED1);
  }
#endif
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
    idleStopNeedsRecovery = false;
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
    return;
  }

#if TAG_IDLE_STOP_DIAGNOSTICS
  palSetLine(LINE_LED1);
#endif
  DBGMCU->CR = 0;
  MODIFY_REG(PWR->CR1, PWR_CR1_LPMS, idlePowerLpms(mode));
  idleStopNeedsRecovery = idlePowerNeedsRecovery(mode);
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

  __DSB();
  __WFI();
  __DSB();
  __ISB();
}

/* Public idle-hook contract documented in power_modes.h. */
void idle_leave(void)
{

#if TAG_IDLE_STOP_DIAGNOSTICS
  palClearLine(LINE_LED1);
#endif
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  if (idleStopNeedsRecovery)
  {
    idleStopNeedsRecovery = false;
    idlePowerRecoverAfterStop();
  }
}
