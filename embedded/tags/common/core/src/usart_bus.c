/**
 * @file usart_bus.c
 * @brief Synchronous-USART lifecycle, active tracking, and raw transfers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "usart_bus.h"

#include "core_sync.h"
#include "gpio_utils.h"
#include "power.h"

/** @name Synchronous-USART implementation overview
 * Polling synchronous-USART byte transfers shared by simple sensor drivers.
 *
 * This file owns USART peripheral setup, device bus-session pin state,
 * active-state tracking, and raw byte-transfer mechanics.
 * @{
 */

#if (defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1) ||        \
    (defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2) ||        \
    (defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3)
#define TAG_USART_TRACKING_ENABLED 1
#else
#define TAG_USART_TRACKING_ENABLED 0
#endif

#if defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1
static bool usart1_on = false;
static bool usart1_suspended_for_stop = false;
#endif
#if defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2
static bool usart2_on = false;
static bool usart2_suspended_for_stop = false;
#endif
#if defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3
static bool usart3_on = false;
static bool usart3_suspended_for_stop = false;
#endif
/** @} */

/** @name Default synchronous-USART configuration
 * Default synchronous-USART2 binding.
 *
 * This is the common "SPI-lite" configuration used by tag-local descriptors
 * unless they provide a device-specific TagUsartSyncConfig.
 * @{
 */
const TagUsartSyncConfig tagUsart2SyncDefaultConfig = {
    .brr = 0x10,
    .cr1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE,
    .cr2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL,
    .cr3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT,
};
/** @} */

/** @name USART active-state tracking
 * Active-peripheral tracking for Stop2.
 *
 * Short sleeps suspend active peripherals without changing device power,
 * chip-select ownership, or pin alternate-function setup.
 * @{
 */
/**
 * @brief Return the active-state flag for a configured USART peripheral.
 *
 * @param[in] usart STM32 USART peripheral to look up.
 * @return Pointer to the active flag, or NULL when the peripheral is not tracked.
 */
static bool *tagUsartOnFlag(USART_TypeDef *usart)
{
  (void)usart;

#if defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1
  if (usart == USART1)
    return &usart1_on;
#endif
#if defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2
  if (usart == USART2)
    return &usart2_on;
#endif
#if defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3
  if (usart == USART3)
    return &usart3_on;
#endif
  return 0;
}

/**
 * @brief Return the Stop2-suspended flag for a configured USART peripheral.
 *
 * @param[in] usart STM32 USART peripheral to look up.
 * @return Pointer to the suspended flag, or NULL when the peripheral is not tracked.
 */
static bool *tagUsartSuspendedForStopFlag(USART_TypeDef *usart)
{
  (void)usart;

#if defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1
  if (usart == USART1)
    return &usart1_suspended_for_stop;
#endif
#if defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2
  if (usart == USART2)
    return &usart2_suspended_for_stop;
#endif
#if defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3
  if (usart == USART3)
    return &usart3_suspended_for_stop;
#endif
  return 0;
}

/**
 * @brief Enable and reset the RCC clock for a configured USART peripheral.
 *
 * @param[in] usart STM32 USART peripheral whose clock should be prepared.
 */
static void tagUsartPeripheralEnableClock(USART_TypeDef *usart)
{
#if defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1
  if (usart == USART1)
  {
    rccEnableUSART1(true);
    rccResetUSART1();
    return;
  }
#endif
#if defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2
  if (usart == USART2)
  {
    rccEnableUSART2(true);
    rccResetUSART2();
    return;
  }
#endif
#if defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3
  if (usart == USART3)
  {
    rccEnableUSART3(true);
    rccResetUSART3();
    return;
  }
#endif
  (void)usart;
}

#if TAG_USART_TRACKING_ENABLED
/**
 * @brief Disable a tracked active USART peripheral for Stop2.
 *
 * @param[in] usart STM32 USART peripheral to suspend if active.
 */
static void tagUsartDisableForStop(USART_TypeDef *usart)
{
  bool *on = tagUsartOnFlag(usart);
  bool *suspended = tagUsartSuspendedForStopFlag(usart);

  if (on && suspended && *on)
  {
    usart->CR1 &= ~USART_CR1_UE;
    *suspended = true;
  }
}

/**
 * @brief Re-enable a tracked USART peripheral that was suspended for Stop2.
 *
 * @param[in] usart STM32 USART peripheral to resume if previously suspended.
 */
static void tagUsartEnableAfterStop(USART_TypeDef *usart)
{
  bool *suspended = tagUsartSuspendedForStopFlag(usart);

  if (suspended && *suspended)
  {
    usart->CR1 |= USART_CR1_UE;
    *suspended = false;
  }
}
#endif

/**
 * @brief Report whether a USART peripheral is currently enabled by a bus session.
 *
 * @param[in] usart STM32 USART peripheral to inspect.
 * @return true when the peripheral is tracked as active.
 */
bool tagUsartIsOn(USART_TypeDef *usart)
{
  bool *on = tagUsartOnFlag(usart);

  return on && *on;
}

/**
 * @brief Mark a USART peripheral active for Stop2 suspend tracking.
 *
 * @param[in] usart STM32 USART peripheral to mark active.
 */
void tagMarkUsartOn(USART_TypeDef *usart)
{
  bool *on = tagUsartOnFlag(usart);

  if (on)
  {
    *on = true;
  }
}

/**
 * @brief Mark a USART peripheral inactive and clear any suspended state.
 *
 * @param[in] usart STM32 USART peripheral to mark inactive.
 */
void tagMarkUsartOff(USART_TypeDef *usart)
{
  bool *on = tagUsartOnFlag(usart);
  bool *suspended = tagUsartSuspendedForStopFlag(usart);

  if (on)
  {
    *on = false;
  }
  if (suspended)
  {
    *suspended = false;
  }
}
/** @} */

/** @name USART configuration and peripheral enable
 * Helpers that translate a compile-time device descriptor into the STM32 USART
 * synchronous-mode register state needed for one bus session.
 * @{
 */
/**
 * @brief Configure and enable the USART peripheral for one device session.
 *
 * @param[in] device USART device descriptor supplying peripheral and config.
 */
static void tagUsartDeviceEnable(const TagUsartDevice *device)
{
  const TagUsartSyncConfig *config = device->config;
  if (!config)
  {
    config = &tagUsart2SyncDefaultConfig;
  }

  tagUsartPeripheralEnableClock(device->usart);

  // Synchronous USART mode, MSB first. Several tags use USART as a three-wire
  // SPI-like sensor bus when the device is not routed to SPI.
  device->usart->BRR = config->brr;
  device->usart->CR2 = config->cr2;
  device->usart->CR3 = config->cr3;
  device->usart->CR1 = config->cr1;

  tagMarkUsartOn(device->usart);
}

/**
 * @brief Disable the USART peripheral at the end of one device session.
 *
 * @param[in] device USART device descriptor supplying the peripheral.
 */
static void tagUsartDeviceDisable(const TagUsartDevice *device)
{
  device->usart->CR1 = 0;
  device->usart->CR2 = 0;
  device->usart->CR3 = 0;

  tagMarkUsartOff(device->usart);
}
/** @} */

/** @name USART Stop2 suspend/resume hooks
 * Stop2 suspend/resume hooks.
 *
 * These only toggle the USART enable bit for a peripheral that was already
 * open. Normal bus close/open still goes through tagUsartBusEnd/Begin.
 * @{
 */
/**
 * @brief Disable currently active USART peripherals before Stop2 entry.
 */
void tagUsartDisableActiveForStop(void)
{
#if TAG_USART_TRACKING_ENABLED
#if defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1
  tagUsartDisableForStop(USART1);
#endif
#if defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2
  tagUsartDisableForStop(USART2);
#endif
#if defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3
  tagUsartDisableForStop(USART3);
#endif
#endif
}

/**
 * @brief Re-enable USART peripherals suspended by Stop2 entry.
 */
void tagUsartEnableActiveAfterStop(void)
{
#if TAG_USART_TRACKING_ENABLED
#if defined(STM32_SERIAL_USE_USART1) && STM32_SERIAL_USE_USART1
  tagUsartEnableAfterStop(USART1);
#endif
#if defined(STM32_SERIAL_USE_USART2) && STM32_SERIAL_USE_USART2
  tagUsartEnableAfterStop(USART2);
#endif
#if defined(STM32_SERIAL_USE_USART3) && STM32_SERIAL_USE_USART3
  tagUsartEnableAfterStop(USART3);
#endif
#endif
}
/** @} */

/** @name USART device power and bus sessions
 * Device power and bus sessions.
 *
 * Power on/off handles optional switched power and safe idle pins. Bus
 * begin/end owns the shared mutex, alternate functions, and peripheral enable
 * state for one transaction/session.
 * @{
 */
/**
 * @brief Assert device power and put chip select into a safe idle state.
 *
 * @param[in] device USART device descriptor to power.
 */
void tagUsartDevicePowerOn(const TagUsartDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palSetLine(device->pwr);
    palSetLineMode(device->pwr, PAL_MODE_OUTPUT_PUSHPULL);
  }

  palSetLine(device->cs);
  palSetLineMode(device->cs, PAL_MODE_OUTPUT_PUSHPULL);

  palClearLine(device->sck);
  palClearLine(device->tx);

  palSetLineMode(device->sck, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->tx, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->rx, PAL_MODE_INPUT_ANALOG);
}

/**
 * @brief Remove device power and return USART pins to analog low-leakage mode.
 *
 * @param[in] device USART device descriptor to power down.
 */
void tagUsartDevicePowerOff(const TagUsartDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    
    palSetLineMode(device->sck, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(device->tx, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(device->rx, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(device->cs, PAL_MODE_INPUT_ANALOG);
    palClearLine(device->pwr);
  } else {

    palSetLine(device->cs);
    palClearLine(device->sck);
    palClearLine(device->tx);

    palSetLineMode(device->sck, PAL_MODE_OUTPUT_PUSHPULL);
    palSetLineMode(device->tx, PAL_MODE_OUTPUT_PUSHPULL);
    palSetLineMode(device->rx, PAL_MODE_INPUT_ANALOG);
  }

}

/**
 * @brief Claim the shared bus and enable the USART peripheral for this device.
 *
 * @param[in] device USART device descriptor whose session should begin.
 */
void tagUsartBusBegin(const TagUsartDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  palSetLine(device->cs);
  palSetLineMode(device->cs,  PAL_MODE_OUTPUT_PUSHPULL);
  
  palSetLineMode(device->sck, PAL_MODE_ALTERNATE(device->alternate_function) | PAL_STM32_OSPEED_MID2);
  palSetLineMode(device->tx, PAL_MODE_ALTERNATE(device->alternate_function) | PAL_STM32_OSPEED_MID2);
  palSetLineMode(device->rx, PAL_MODE_ALTERNATE(device->alternate_function) | PAL_STM32_OSPEED_MID2);

  tagUsartDeviceEnable(device);
}

/**
 * @brief Disable the USART peripheral and release the shared bus.
 *
 * @param[in] device USART device descriptor whose session should end.
 */
void tagUsartBusEnd(const TagUsartDevice *device)
{
  palSetLine(device->cs);

  tagUsartDeviceDisable(device);

  palClearLine(device->sck);
  palClearLine(device->tx);

  palSetLineMode(device->sck, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->tx, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(device->rx, PAL_MODE_INPUT_ANALOG);

  if (device->mutex)
  {
    chBSemSignal(device->mutex);
  }
}

/**
 * @brief Apply standby pull policy for a USART-backed device.
 *
 * @param[in] device USART device descriptor whose sleep policy should be applied.
 */
void tagUsartDevicePrepareSleep(const TagUsartDevice *device)
{
  switch (device->sleep_policy)
  {
  case TAG_USART_SLEEP_SAFE_IDLE:
    tagEnableStandbyPullup(device->cs);
    tagEnableStandbyPulldown(device->sck);
    tagEnableStandbyPulldown(device->tx);
    break;

  case TAG_USART_SLEEP_FLOAT:
    tagEnableStandbyPulldown(device->pwr);
    break;

  case TAG_USART_SLEEP_CUSTOM:
    break;
  }
}
/** @} */

/** @name USART raw byte transfers
 * Raw byte transfers.
 *
 * Synchronous USART behaves like an SPI-style full-duplex shifter for these
 * devices: every transmitted byte produces one received byte.
 * @{
 */
/**
 * @brief Write bytes and discard the full-duplex response stream.
 *
 * @param[in] device USART device descriptor.
 * @param[in] buf Bytes to transmit.
 * @param[in] len Number of bytes to transmit.
 */
void tagUsartWrite(const TagUsartDevice *device, const uint8_t *buf,
                   uint32_t len)
{
  USART_TypeDef *usart = tagUsartDevicePeripheral(device);
  volatile uint8_t *tdr = (volatile uint8_t *)&usart->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&usart->RDR;

  while (len--) {
    *tdr = *buf++;
    while ((usart->ISR & USART_ISR_RXNE) == 0)
      ;
    (void)*rdr;
  }
}

/**
 * @brief Read bytes by clocking the descriptor's dummy byte.
 *
 * @param[in] device USART device descriptor.
 * @param[out] buf Buffer that receives bytes from the device.
 * @param[in] len Number of bytes to read.
 */
void tagUsartRead(const TagUsartDevice *device, uint8_t *buf, uint32_t len)
{
  USART_TypeDef *usart = tagUsartDevicePeripheral(device);
  volatile uint8_t *tdr = (volatile uint8_t *)&usart->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&usart->RDR;

  while (len--) {
    *tdr = device->dummy;
    while ((usart->ISR & USART_ISR_RXNE) == 0)
      ;
    *buf++ = *rdr;
  }
}

/**
 * @brief Assert the USART device chip-select line.
 *
 * @param[in] device USART device descriptor to select.
 */
void tagUsartSelect(const TagUsartDevice *device)
{
  palClearLine(device->cs);
}

/**
 * @brief Deassert the USART device chip-select line.
 *
 * @param[in] device USART device descriptor to deselect.
 */
void tagUsartDeselect(const TagUsartDevice *device)
{
  palSetLine(device->cs);
}
/** @} */
