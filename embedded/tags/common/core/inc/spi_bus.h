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

/*
 * Device power controls the optional switched power line and safe idle pin
 * states for the device. It does not start or stop the MCU SPI peripheral.
 */
void tagSpiDevicePowerOn(const TagSpiDevice *device);
void tagSpiDevicePowerOff(const TagSpiDevice *device);

/*
 * Bus sessions enable or disable the MCU SPI controller using the device's
 * configuration. Callers normally power the device first, then begin the bus;
 * shutdown happens in the reverse order.
 */
void tagSpiBusBegin(const TagSpiDevice *device);
void tagSpiBusEnd(const TagSpiDevice *device);

/*
 * Apply the device's standby pull policy before entering low-power stop or
 * standby states. This is separate from normal bus-session teardown.
 */
void tagSpiDevicePrepareSleep(const TagSpiDevice *device);

void tagSpiWrite(const TagSpiDevice *device, const uint8_t *buf, uint32_t len);
void tagSpiRead(const TagSpiDevice *device, uint8_t *buf, uint32_t len);
void tagSpiWritePipelined(const TagSpiDevice *device, const uint8_t *buf,
                          uint32_t len);
void tagSpiReadPipelined(const TagSpiDevice *device, uint8_t *buf,
                         uint32_t len);

void tagSpiSelect(const TagSpiDevice *device);
void tagSpiDeselect(const TagSpiDevice *device);

/*
 * Convenience transaction wrappers. These assert chip select, perform one raw
 * byte transfer, then release chip select. Use the lower-level select/write/read
 * calls directly when a device protocol needs multiple phases under one CS.
 */
static inline void tagSpiBusWrite(const TagSpiDevice *device,
                                  const uint8_t *buf, uint32_t len)
{
  tagSpiSelect(device);
  tagSpiWrite(device, buf, len);
  tagSpiDeselect(device);
}

static inline void tagSpiBusRead(const TagSpiDevice *device, uint8_t *buf,
                                 uint32_t len)
{
  tagSpiSelect(device);
  tagSpiRead(device, buf, len);
  tagSpiDeselect(device);
}

#endif
