#ifndef TAG_CORE_USART_BUS_H
#define TAG_CORE_USART_BUS_H

#include "bus_device.h"

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

// Synchronous-USART controller register setup and bus arbitration.
typedef struct {
  USART_TypeDef *usart;
  binary_semaphore_t *mutex;
} TagUsartController;

// Standby pull policy applied while preparing a USART-backed device for sleep.
typedef enum {
  TAG_USART_SLEEP_FLOAT,
  TAG_USART_SLEEP_SAFE_IDLE,
  TAG_USART_SLEEP_CUSTOM
} TagUsartSleepPolicy;

// Board-line description for one synchronous-USART device.
typedef struct {
  const TagUsartController *controller;
  const TagUsartSyncConfig *config;
  ioline_t cs;
  ioline_t sck;
  ioline_t tx;
  ioline_t rx;
  ioline_t pwr;
  uint8_t dummy;
  TagUsartSleepPolicy sleep_policy;
} TagUsartDevice;

extern const TagUsartSyncConfig tagUsart2SyncDefaultConfig;
extern const TagUsartController tagUsart2SyncController;
extern const TagBusOps tagUsartBusOps;

bool isUsart2On(void);
void tagMarkUsart2On(void);
void tagMarkUsart2Off(void);
void tagUsart2SyncEnable(const TagUsartSyncConfig *config);
void tagUsart2SyncDisable(void);
void tagUsartDisableActiveForStop(void);
void tagUsartEnableActiveAfterStop(void);
void tagUsartControllerEnable(const TagUsartController *controller,
                              const TagUsartSyncConfig *config);
void tagUsartControllerDisable(const TagUsartController *controller);

static inline USART_TypeDef *tagUsartDevicePeripheral(
    const TagUsartDevice *device)
{
  return device->controller->usart;
}

/*
 * Device power controls the optional switched power line and safe idle pin
 * states for the device. It does not start or stop the MCU USART peripheral.
 */
void tagUsartDevicePowerOn(const TagUsartDevice *device);
void tagUsartDevicePowerOff(const TagUsartDevice *device);

/*
 * Bus sessions enable or disable the MCU USART controller using the device's
 * synchronous configuration. Callers normally power the device first, then
 * begin the bus; shutdown happens in the reverse order.
 */
void tagUsartBusBegin(const TagUsartDevice *device);
void tagUsartBusEnd(const TagUsartDevice *device);

/*
 * Apply the device's standby pull policy before entering low-power stop or
 * standby states. This is separate from normal bus-session teardown.
 */
void tagUsartDevicePrepareSleep(const TagUsartDevice *device);

void tagUsartWrite(const TagUsartDevice *device, const uint8_t *buf,
                   uint32_t len);
void tagUsartRead(const TagUsartDevice *device, uint8_t *buf, uint32_t len);

void tagUsartSelect(const TagUsartDevice *device);
void tagUsartDeselect(const TagUsartDevice *device);

/*
 * Convenience transaction wrappers. These assert chip select, perform one raw
 * byte transfer, then release chip select. Use the lower-level select/write/read
 * calls directly when a device protocol needs multiple phases under one CS.
 */
static inline void tagUsartBusWrite(const TagUsartDevice *device,
                                    const uint8_t *buf, uint32_t len)
{
  tagUsartSelect(device);
  tagUsartWrite(device, buf, len);
  tagUsartDeselect(device);
}

static inline void tagUsartBusRead(const TagUsartDevice *device, uint8_t *buf,
                                   uint32_t len)
{
  tagUsartSelect(device);
  tagUsartRead(device, buf, len);
  tagUsartDeselect(device);
}

#endif
