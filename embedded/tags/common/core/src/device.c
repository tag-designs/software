/**
 * @file device.c
 * @brief Weak defaults for optional tag/family device standby hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "device.h"

/** @name Weak standby device hooks
 * Weak defaults for optional tag/family device standby hooks.
 *
 * Tags with non-universal peripherals override these from their devices.c file.
 * Keeping the defaults here lets simple or legacy tags continue to use core
 * pwr.c without providing a device binding file.
 * @{
 */
/**
 * @brief Default no-op device standby preparation hook.
 *
 * @param[in] state Application state that is about to enter standby.
 */
void __attribute__((weak)) tagDevicesPrepareStandby(uint32_t state)
{
  (void)state;
}

/**
 * @brief Default no-op standby GPIO application hook.
 */
void __attribute__((weak)) tagDevicesApplyStandbyPins(void)
{
}

/**
 * @brief Default no-op hook for disabling tag-specific wake sources.
 */
void __attribute__((weak)) tagDevicesDisableWakeupSources(void)
{
}

/**
 * @brief Default wake-source configuration that enables the STM32 wake input latch.
 *
 * @param[in] state Application state that is about to sleep.
 * @param[in] is_active Whether the sleep attempt begins from an active state.
 * @return true because the default has no external input to veto standby.
 */
bool __attribute__((weak)) tagDevicesConfigureWakeupSources(uint32_t state,
                                                           bool is_active)
{
  (void)state;
  (void)is_active;

  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
  return true;
}

/**
 * @brief Default no-op hook for tags without explicit device deinitialization.
 */
void __attribute__((weak)) tagDevicesDeinit(void)
{
}
/** @} */
