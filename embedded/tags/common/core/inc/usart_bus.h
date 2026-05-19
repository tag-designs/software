#ifndef TAG_CORE_USART_BUS_H
#define TAG_CORE_USART_BUS_H

#include "hal.h"

#include <stdbool.h>
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
  uint32_t brr;
  uint32_t cr1;
  uint32_t cr2;
  uint32_t cr3;
} TagUsartSyncConfig;

typedef struct {
  USART_TypeDef *usart;
  const TagUsartSyncConfig *config;
  ioline_t cs;
  uint8_t dummy;
} TagUsartBus;

extern const TagUsartSyncConfig tagUsart2SyncDefaultConfig;

bool isUsart2On(void);
void tagMarkUsart2On(void);
void tagMarkUsart2Off(void);
void tagUsart2SyncEnable(const TagUsartSyncConfig *config);
void tagUsart2SyncDisable(void);
void tagUsartDisableActiveForStop(void);
void tagUsartEnableActiveAfterStop(void);

void tagUsartWrite(USART_TypeDef *usart, const uint8_t *buf, uint32_t len);
void tagUsartRead(USART_TypeDef *usart, uint8_t dummy, uint8_t *buf,
                  uint32_t len);

void tagUsartSelect(const TagUsartBus *bus);
void tagUsartDeselect(const TagUsartBus *bus);
void tagUsartBusWrite(const TagUsartBus *bus, const uint8_t *buf,
                      uint32_t len);
void tagUsartBusRead(const TagUsartBus *bus, uint8_t *buf, uint32_t len);

#endif
