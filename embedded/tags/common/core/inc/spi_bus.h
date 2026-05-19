#ifndef TAG_CORE_SPI_BUS_H
#define TAG_CORE_SPI_BUS_H

#include "hal.h"

#include <stdbool.h>
#include <stdint.h>

// Register settings used when a device opens an SPI bus session.
typedef struct {
  uint32_t cr1;
  uint32_t cr2;
} TagSpiConfig;

// SPI controller register setup and bus arbitration.
typedef struct {
  binary_semaphore_t *mutex;
  void (*enable)(const TagSpiConfig *config);
  void (*disable)(void);
} TagSpiController;

// Standby pull policy applied while preparing the MCU for deep sleep.
typedef enum {
  TAG_SPI_SLEEP_FLOAT,
  TAG_SPI_SLEEP_SAFE_IDLE,
  TAG_SPI_SLEEP_CUSTOM
} TagSpiSleepPolicy;

// Board-line description for one SPI device attached to a shared controller.
typedef struct {
  const TagSpiController *controller;
  const TagSpiConfig *config;
  ioline_t cs;
  ioline_t sck;
  ioline_t miso;
  ioline_t mosi;
  ioline_t pwr;
  TagSpiSleepPolicy sleep_policy;
} TagSpiDevice;

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

extern const TagSpiConfig tagSpiDefaultConfig;
extern const TagSpiController tagSpi1DefaultController;

bool isSpi1On(void);
void tagMarkSpi1On(void);
void tagMarkSpi1Off(void);
void tagSpiDisableActiveForStop(void);
void tagSpiEnableActiveAfterStop(void);

void tagSpiDevicePowerOn(const TagSpiDevice *device);
void tagSpiDevicePowerOff(const TagSpiDevice *device);
void tagSpiBusBegin(const TagSpiDevice *device);
void tagSpiBusEnd(const TagSpiDevice *device);
void tagSpiDevicePrepareSleep(const TagSpiDevice *device);

void tagSpiWrite(SPI_TypeDef *spi, const uint8_t *buf, uint32_t len);
void tagSpiRead(SPI_TypeDef *spi, uint8_t *buf, uint32_t len);

void tagSpiSelect(const TagSpiBus *bus);
void tagSpiDeselect(const TagSpiBus *bus);
void tagSpiBusWrite(const TagSpiBus *bus, const uint8_t *buf, uint32_t len);
void tagSpiBusRead(const TagSpiBus *bus, uint8_t *buf, uint32_t len);

#endif
