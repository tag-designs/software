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

static bool usart2_on = false;
static bool usart2_suspended_for_stop = false;

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
 * Generic bus-device adapter.
 *
 * TagRegisterDevice stores a TagBusDevice so register drivers can open a bus
 * without knowing whether the concrete transport is SPI, USART, or I2C.
 */
static void tagUsartBusOpsPowerOn(const void *device)
{
  tagUsartDevicePowerOn((const TagUsartDevice *)device);
}

static void tagUsartBusOpsPowerOff(const void *device)
{
  tagUsartDevicePowerOff((const TagUsartDevice *)device);
}

static void tagUsartBusOpsBegin(const void *device)
{
  tagUsartBusBegin((const TagUsartDevice *)device);
}

static void tagUsartBusOpsEnd(const void *device)
{
  tagUsartBusEnd((const TagUsartDevice *)device);
}

static void tagUsartBusOpsPrepareSleep(const void *device)
{
  tagUsartDevicePrepareSleep((const TagUsartDevice *)device);
}

const TagBusOps tagUsartBusOps = {
    .power_on = tagUsartBusOpsPowerOn,
    .power_off = tagUsartBusOpsPowerOff,
    .bus_begin = tagUsartBusOpsBegin,
    .bus_end = tagUsartBusOpsEnd,
    .prepare_sleep = tagUsartBusOpsPrepareSleep,
};

/*
 * Active-peripheral tracking for Stop2.
 *
 * Short sleeps suspend active peripherals without changing device power,
 * chip-select ownership, or pin alternate-function setup.
 */
bool isUsart2On(void)
{
  return usart2_on;
}

void tagMarkUsart2On(void)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  usart2_on = true;
#endif
}

void tagMarkUsart2Off(void)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  usart2_on = false;
#endif
}

static void tagUsartDeviceEnable(const TagUsartDevice *device)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  const TagUsartSyncConfig *config = device->config;
  if (!config)
  {
    config = &tagUsart2SyncDefaultConfig;
  }

  if (device->usart == USART2)
  {
    rccEnableUSART2(true);
    rccResetUSART2();
  }

  // Synchronous USART mode, MSB first. Several tags use USART2 as a
  // three-wire SPI-like sensor bus when the device is not routed to SPI.
  device->usart->BRR = config->brr;
  device->usart->CR2 = config->cr2;
  device->usart->CR3 = config->cr3;
  device->usart->CR1 = config->cr1;

  if (device->usart == USART2)
  {
    tagMarkUsart2On();
  }
#else
  (void)device;
#endif
}

static void tagUsartDeviceDisable(const TagUsartDevice *device)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  device->usart->CR1 = 0;
  device->usart->CR2 = 0;
  device->usart->CR3 = 0;

  if (device->usart == USART2)
  {
    tagMarkUsart2Off();
  }
#else
  (void)device;
#endif
}

/*
 * Stop2 suspend/resume hooks.
 *
 * These only toggle the USART enable bit for a peripheral that was already
 * open. Normal bus close/open still goes through tagUsartBusEnd/Begin.
 */
void tagUsartDisableActiveForStop(void)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  if (usart2_on)
  {
    USART2->CR1 &= ~USART_CR1_UE;
    usart2_suspended_for_stop = true;
  }
#endif
}

void tagUsartEnableActiveAfterStop(void)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  if (usart2_suspended_for_stop)
  {
    USART2->CR1 |= USART_CR1_UE;
    usart2_suspended_for_stop = false;
  }
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
