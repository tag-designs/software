#include "usart_bus.h"

#include "core_sync.h"
#include "gpio_utils.h"
#include "power.h"

/*
 * Polling synchronous-USART byte transfers shared by simple sensor drivers.
 *
 * This file owns USART peripheral setup, device bus-session pin state,
 * active-state tracking, and raw byte-transfer mechanics.
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

/*
 * Default synchronous-USART2 binding.
 *
 * This is the common "SPI-lite" configuration used by tag-local descriptors
 * unless they provide a device-specific TagUsartSyncConfig.
 */
const TagUsartSyncConfig tagUsart2SyncDefaultConfig = {
    .brr = 0x10,
    .cr1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE,
    .cr2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL,
    .cr3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT,
};

/*
 * Active-peripheral tracking for Stop2.
 *
 * Short sleeps suspend active peripherals without changing device power,
 * chip-select ownership, or pin alternate-function setup.
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

bool tagUsartIsOn(USART_TypeDef *usart)
{
  bool *on = tagUsartOnFlag(usart);

  return on && *on;
}

void tagMarkUsartOn(USART_TypeDef *usart)
{
  bool *on = tagUsartOnFlag(usart);

  if (on)
  {
    *on = true;
  }
}

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

static void tagUsartDeviceDisable(const TagUsartDevice *device)
{
  device->usart->CR1 = 0;
  device->usart->CR2 = 0;
  device->usart->CR3 = 0;

  tagMarkUsartOff(device->usart);
}

/*
 * Stop2 suspend/resume hooks.
 *
 * These only toggle the USART enable bit for a peripheral that was already
 * open. Normal bus close/open still goes through tagUsartBusEnd/Begin.
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

/*
 * Device power and bus sessions.
 *
 * Power on/off handles optional switched power and safe idle pins. Bus
 * begin/end owns the shared mutex, alternate functions, and peripheral enable
 * state for one transaction/session.
 */
void tagUsartDevicePowerOn(const TagUsartDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    toOutput(device->pwr);
    palSetLine(device->pwr);
  }

  palSetLine(device->cs);
  toOutput(device->cs);
}

void tagUsartDevicePowerOff(const TagUsartDevice *device)
{
  if (tagLineIsValid(device->pwr))
  {
    palClearLine(device->pwr);
  }

  toAnalog(device->sck);
  toAnalog(device->tx);
  toAnalog(device->rx);
  toAnalog(device->cs);
}

void tagUsartBusBegin(const TagUsartDevice *device)
{
  if (device->mutex)
  {
    chBSemWait(device->mutex);
  }

  palSetLine(device->cs);
  toOutput(device->cs);

  toAlternate(device->sck);
  toAlternate(device->tx);
  toAlternate(device->rx);

  tagUsartDeviceEnable(device);
}

void tagUsartBusEnd(const TagUsartDevice *device)
{
  palSetLine(device->cs);

  tagUsartDeviceDisable(device);

  toAnalog(device->sck);
  toAnalog(device->tx);
  toAnalog(device->rx);

  if (device->mutex)
  {
    chBSemSignal(device->mutex);
  }
}

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

/*
 * Raw byte transfers.
 *
 * Synchronous USART behaves like an SPI-style full-duplex shifter for these
 * devices: every transmitted byte produces one received byte.
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

void tagUsartSelect(const TagUsartDevice *device)
{
  palClearLine(device->cs);
}

void tagUsartDeselect(const TagUsartDevice *device)
{
  palSetLine(device->cs);
}
