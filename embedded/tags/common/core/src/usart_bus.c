#include "usart_bus.h"

/*
 * Polling synchronous-USART byte transfers shared by simple sensor drivers.
 *
 * The tag power layer owns USART clocking, GPIO alternate functions, and
 * controller register setup. These helpers assume the peripheral has already
 * been configured for the current bus session.
 */
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
