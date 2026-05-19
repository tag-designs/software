#ifndef TAG_CORE_POWER_H
#define TAG_CORE_POWER_H

#include "hal.h"
#include "spi_bus.h"
#include "usart_bus.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_NO_LINE ((ioline_t)0)

// Standby pull policy applied while preparing the MCU for deep sleep.
typedef enum {
  TAG_SPI_SLEEP_FLOAT,
  TAG_SPI_SLEEP_SAFE_IDLE,
  TAG_SPI_SLEEP_CUSTOM
} TagSpiSleepPolicy;

// Board-line description for one SPI device attached to a shared controller.
// Power helpers own device idle pin state; bus helpers own controller setup.
typedef struct {
  const TagSpiController *controller;
  binary_semaphore_t *mutex;
  ioline_t cs;
  ioline_t sck;
  ioline_t miso;
  ioline_t mosi;
  ioline_t pwr;
  TagSpiSleepPolicy sleep_policy;
} TagSpiDevice;

// Board-line and fallback-I2C configuration for one I2C device.
typedef struct {
  I2CDriver *driver;
  binary_semaphore_t *mutex;
  I2CConfig config;
  ioline_t sda;
  ioline_t scl;
  ioline_t pwr;
} TagI2cDevice;

bool tagLineIsValid(ioline_t line);
void tagDisableActiveBusesForStop(void);
void tagEnableActiveBusesAfterStop(void);
void tagEnableStandbyPullup(ioline_t line);
void tagEnableStandbyPulldown(ioline_t line);
void tagSpiDevicePowerOn(const TagSpiDevice *device);
void tagSpiDevicePowerOff(const TagSpiDevice *device);
void tagSpiBusBegin(const TagSpiDevice *device);
void tagSpiBusEnd(const TagSpiDevice *device);
void tagSpiDevicePrepareSleep(const TagSpiDevice *device);
void tagI2cDeviceOn(const TagI2cDevice *device);
void tagI2cDeviceOff(const TagI2cDevice *device);
void tagI2cDevicePrepareSleep(const TagI2cDevice *device);
void tagPrepareDevicesForStandby(void);

void rtcOn(void);
void rtcOff(void);

void spi1On(void);
void spi1Off(void);

void accelSpiOn(void);
void accelSpiOff(void);
int accelConfigureFIFO(int entries);
extern uint8_t adxl_status;

void accelOn(void);
void accelOff(void);

void FlashSpiOn(void);
void FlashSpiOff(void);

#endif
