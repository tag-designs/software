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
  SPI_TypeDef *spi;
  binary_semaphore_t *mutex;
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
  uint8_t dummy;
  TagSpiSleepPolicy sleep_policy;
} TagSpiDevice;

extern const TagSpiConfig tagSpiDefaultConfig;
extern const TagSpiController tagSpi1DefaultController;

bool isSpi1On(void);
void tagMarkSpi1On(void);
void tagMarkSpi1Off(void);
void tagSpiDisableActiveForStop(void);
void tagSpiEnableActiveAfterStop(void);
void tagSpiControllerEnable(const TagSpiController *controller,
                            const TagSpiConfig *config);
void tagSpiControllerDisable(const TagSpiController *controller);

static inline SPI_TypeDef *tagSpiDevicePeripheral(const TagSpiDevice *device)
{
  return device->controller->spi;
}

void tagSpiDevicePowerOn(const TagSpiDevice *device);
void tagSpiDevicePowerOff(const TagSpiDevice *device);
void tagSpiBusBegin(const TagSpiDevice *device);
void tagSpiBusEnd(const TagSpiDevice *device);
void tagSpiDevicePrepareSleep(const TagSpiDevice *device);

void tagSpiWrite(SPI_TypeDef *spi, const uint8_t *buf, uint32_t len);
void tagSpiRead(SPI_TypeDef *spi, uint8_t *buf, uint32_t len);

void tagSpiSelect(const TagSpiDevice *device);
void tagSpiDeselect(const TagSpiDevice *device);
void tagSpiBusWrite(const TagSpiDevice *device, const uint8_t *buf,
                    uint32_t len);
void tagSpiBusRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len);

#endif
