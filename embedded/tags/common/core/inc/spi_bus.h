#ifndef TAG_CORE_SPI_BUS_H
#define TAG_CORE_SPI_BUS_H

#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

// SPI controller register setup and bus arbitration.
typedef struct {
  binary_semaphore_t *mutex;
  void (*enable)(void);
  void (*disable)(void);
} TagSpiController;

/*
 * Generic SPI bus helpers for devices that drive the STM32 SPI registers
 * directly. Higher-level sensor and storage drivers own their command formats;
 * this layer only knows the controller registers and the chip-select line.
 */
typedef struct {
  SPI_TypeDef *spi;
  ioline_t cs;
  uint8_t dummy;
} TagSpiBus;

extern const TagSpiController tagSpi1DefaultController;

bool isSpi1On(void);
void tagMarkSpi1On(void);
void tagMarkSpi1Off(void);
void tagSpiDisableActiveForStop(void);
void tagSpiEnableActiveAfterStop(void);

void tagSpiWrite(SPI_TypeDef *spi, const uint8_t *buf, uint32_t len);
void tagSpiRead(SPI_TypeDef *spi, uint8_t *buf, uint32_t len);

void tagSpiSelect(const TagSpiBus *bus);
void tagSpiDeselect(const TagSpiBus *bus);
void tagSpiBusWrite(const TagSpiBus *bus, const uint8_t *buf, uint32_t len);
void tagSpiBusRead(const TagSpiBus *bus, uint8_t *buf, uint32_t len);

#endif
