#include "usart_bus.h"

#include "core_sync.h"
#include "gpio_utils.h"
#include "power.h"

/*
 * Polling synchronous-USART byte transfers shared by simple sensor drivers.
 *
 * This file owns USART controller setup, device bus-session pin state,
 * active-state tracking, and raw byte-transfer mechanics.
 */

static bool usart2_on = false;
static bool usart2_suspended_for_stop = false;

const TagUsartSyncConfig tagUsart2SyncDefaultConfig = {
    .brr = 0x10,
    .cr1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE,
    .cr2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL,
    .cr3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT,
};

const TagUsartController tagUsart2SyncController = {
    .usart = USART2,
    .mutex = &USARTmutex,
};

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

void tagUsart2SyncEnable(const TagUsartSyncConfig *config)
{
  tagUsartControllerEnable(&tagUsart2SyncController, config);
}

void tagUsart2SyncDisable(void)
{
  tagUsartControllerDisable(&tagUsart2SyncController);
}

void tagUsartControllerEnable(const TagUsartController *controller,
                              const TagUsartSyncConfig *config)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  if (!config)
  {
    config = &tagUsart2SyncDefaultConfig;
  }

  if (controller->usart == USART2)
  {
    rccEnableUSART2(true);
    rccResetUSART2();
  }

  // Synchronous USART mode, MSB first. Several tags use USART2 as a
  // three-wire SPI-like sensor bus when the device is not routed to SPI.
  controller->usart->BRR = config->brr;
  controller->usart->CR2 = config->cr2;
  controller->usart->CR3 = config->cr3;
  controller->usart->CR1 = config->cr1;

  if (controller->usart == USART2)
  {
    tagMarkUsart2On();
  }
#else
  (void)controller;
  (void)config;
#endif
}

void tagUsartControllerDisable(const TagUsartController *controller)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  controller->usart->CR1 = 0;
  controller->usart->CR2 = 0;
  controller->usart->CR3 = 0;

  if (controller->usart == USART2)
  {
    tagMarkUsart2Off();
  }
#else
  (void)controller;
#endif
}

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
  const TagUsartController *controller = device->controller;

  if (controller && controller->mutex)
  {
    chBSemWait(controller->mutex);
  }

  palSetLine(device->cs);
  toOutput(device->cs);

  toAlternate(device->sck);
  toAlternate(device->tx);
  toAlternate(device->rx);

  if (controller)
  {
    tagUsartControllerEnable(controller, device->config);
  }
}

void tagUsartBusEnd(const TagUsartDevice *device)
{
  const TagUsartController *controller = device->controller;

  palSetLine(device->cs);

  if (controller)
  {
    tagUsartControllerDisable(controller);
  }

  toAnalog(device->sck);
  toAnalog(device->tx);
  toAnalog(device->rx);

  if (controller && controller->mutex)
  {
    chBSemSignal(controller->mutex);
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

void tagUsartWrite(USART_TypeDef *usart, const uint8_t *buf, uint32_t len)
{
  volatile uint8_t *tdr = (volatile uint8_t *)&usart->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&usart->RDR;

  while (len--) {
    *tdr = *buf++;
    while ((usart->ISR & USART_ISR_RXNE) == 0)
      ;
    (void)*rdr;
  }
}

void tagUsartRead(USART_TypeDef *usart, uint8_t dummy, uint8_t *buf,
                  uint32_t len)
{
  volatile uint8_t *tdr = (volatile uint8_t *)&usart->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&usart->RDR;

  while (len--) {
    *tdr = dummy;
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

void tagUsartBusWrite(const TagUsartDevice *device, const uint8_t *buf,
                      uint32_t len)
{
  tagUsartSelect(device);
  tagUsartWrite(tagUsartDevicePeripheral(device), buf, len);
  tagUsartDeselect(device);
}

void tagUsartBusRead(const TagUsartDevice *device, uint8_t *buf, uint32_t len)
{
  tagUsartSelect(device);
  tagUsartRead(tagUsartDevicePeripheral(device), device->dummy, buf, len);
  tagUsartDeselect(device);
}
