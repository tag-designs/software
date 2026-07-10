/**
 * @file device.c
 * @brief Weak defaults for optional tag/family device standby hooks.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"

#include "core_sync.h"
#include "device.h"

/** @name Weak device lifecycle hooks
 * Weak defaults for optional tag/family device lifecycle hooks.
 *
 * Tags with non-universal peripherals override these from their devices.c file.
 * Keeping the defaults here lets simple or legacy tags continue to use core
 * pwr.c without providing a device binding file.
 * @{
 */
binary_semaphore_t SPI1mutex __attribute__((weak));
binary_semaphore_t USART2mutex __attribute__((weak));

/**
 * @brief Default initialization for legacy tags without device-owned bus state.
 */
void __attribute__((weak)) tagDevicesInit(void)
{
  chBSemObjectInit(&SPI1mutex, false);
  chBSemObjectInit(&USART2mutex, false);
}

/**
 * @brief Default bridge from common lifecycle phases to legacy device hooks.
 *
 * Tags can override this directly to keep their shutdown policy in one place.
 * Older tags that only override tagDevicesPrepareStandby() or
 * tagDevicesDeinit() keep their previous behavior through this bridge.
 *
 * @param[in] reason Common lifecycle phase that is quiescing the devices.
 * @param[in] state Current state-machine state.
 */
void __attribute__((weak))
tagDevicesApplyPowerState(TagDevicePowerReason reason, uint32_t state)
{
  switch (reason) {
  case TAG_DEVICE_POWER_STANDBY_ENTRY:
    tagDevicesPrepareStandby(state);
    break;
  case TAG_DEVICE_POWER_BOOT_CLEANUP:
  case TAG_DEVICE_POWER_RUNTIME_DEINIT:
  case TAG_DEVICE_POWER_TERMINAL_ENTRY:
  default:
    tagDevicesDeinit();
    break;
  }
}

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

#if defined(PWR_CR3_EIWF_Msk)
  /* Enable STM32L4 internal wakeup source when the part exposes it. */
  SET_BIT(PWR->CR3, PWR_CR3_EIWF_Msk);
#endif
  return true;
}

/**
 * @brief Default no-op hook for tags without explicit device deinitialization.
 */
void __attribute__((weak)) tagDevicesDeinit(void)
{
}
/** @} */
