#include "usart_bus.h"

/*
 * Polling synchronous-USART byte transfers shared by simple sensor drivers.
 *
 * The tag power layer owns GPIO alternate functions and chip-select policy.
 * This file owns the shared USART controller setup, active-state tracking, and
 * raw byte-transfer mechanics.
 */

static bool usart2_on = false;
static bool usart2_suspended_for_stop = false;

const TagUsartSyncConfig tagUsart2SyncDefaultConfig = {
    .brr = 0x10,
    .cr1 = USART_CR1_OVER8 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE,
    .cr2 = USART_CR2_MSBFIRST | USART_CR2_CLKEN | USART_CR2_LBCL,
    .cr3 = USART_CR3_OVRDIS | USART_CR3_ONEBIT,
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
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  if (!config)
  {
    config = &tagUsart2SyncDefaultConfig;
  }

  rccEnableUSART2(true);
  rccResetUSART2();

  // Synchronous USART mode, MSB first. Several tags use USART2 as a
  // three-wire SPI-like sensor bus when the device is not routed to SPI.
  USART2->BRR = config->brr;
  USART2->CR2 = config->cr2;
  USART2->CR3 = config->cr3;
  USART2->CR1 = config->cr1;
  tagMarkUsart2On();
#else
  (void)config;
#endif
}

void tagUsart2SyncDisable(void)
{
#if defined(STM32_HAS_USART2) && STM32_HAS_USART2
  USART2->CR1 = 0;
  USART2->CR2 = 0;
  USART2->CR3 = 0;
  tagMarkUsart2Off();
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

void tagUsartSelect(const TagUsartBus *bus)
{
  palClearLine(bus->cs);
}

void tagUsartDeselect(const TagUsartBus *bus)
{
  palSetLine(bus->cs);
}

void tagUsartBusWrite(const TagUsartBus *bus, const uint8_t *buf,
                      uint32_t len)
{
  tagUsartSelect(bus);
  tagUsartWrite(bus->usart, buf, len);
  tagUsartDeselect(bus);
}

void tagUsartBusRead(const TagUsartBus *bus, uint8_t *buf, uint32_t len)
{
  tagUsartSelect(bus);
  tagUsartRead(bus->usart, bus->dummy, buf, len);
  tagUsartDeselect(bus);
}
