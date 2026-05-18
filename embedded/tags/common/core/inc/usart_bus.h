#ifndef TAG_CORE_USART_BUS_H
#define TAG_CORE_USART_BUS_H

#include "hal.h"

#include <stdint.h>

/*
 * Synchronous-USART bus helpers.
 *
 * Some tags use an STM32 USART in clocked synchronous mode as a small
 * SPI-like sensor bus. Core owns the raw byte-transfer mechanics because they
 * are MCU/peripheral plumbing. Sensor code should build device register
 * protocols on top of this layer through sensor_io.
 */
typedef struct {
  USART_TypeDef *usart;
  ioline_t cs;
  uint8_t dummy;
} TagUsartBus;

void tagUsartWrite(USART_TypeDef *usart, const uint8_t *buf, uint32_t len);
void tagUsartRead(USART_TypeDef *usart, uint8_t dummy, uint8_t *buf,
                  uint32_t len);

void tagUsartSelect(const TagUsartBus *bus);
void tagUsartDeselect(const TagUsartBus *bus);
void tagUsartBusWrite(const TagUsartBus *bus, const uint8_t *buf,
                      uint32_t len);
void tagUsartBusRead(const TagUsartBus *bus, uint8_t *buf, uint32_t len);

#endif
