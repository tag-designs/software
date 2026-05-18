#ifndef TAG_SENSOR_IO_H
#define TAG_SENSOR_IO_H

#include "hal.h"

#include <stdint.h>

/*
 * Shared low-level sensor transport helpers.
 *
 * Device drivers should keep sensor-specific command formats in the driver
 * when they are unusual. This helper covers the common ST-style SPI register
 * convention used by several pressure/IMU parts:
 *
 *   write command: register address
 *   read command:  register address OR read_mask
 *
 * The SPI and USART register helpers own the chip-select line for one complete
 * transaction. The raw transfer helpers only transfer bytes; they are
 * available for drivers that need to build a larger command sequence while
 * managing chip select themselves.
 */
typedef struct {
  I2CDriver *driver;
  uint8_t address;
  uint32_t timeout;
} TagI2cRegisterIO;

/*
 * USART in synchronous mode is used by a few tags as an SPI-like sensor bus.
 * Keep the protocol shape separate from normal ChibiOS serial use: callers
 * pass the hardware USART registers and the ST-style command masks.
 */
typedef struct {
  ioline_t cs;
  uint8_t read_mask;
  uint8_t write_mask;
} TagStSpiRegisterIO;

typedef struct {
  USART_TypeDef *usart;
  ioline_t cs;
  uint8_t read_mask;
  uint8_t write_mask;
  uint8_t dummy;
} TagStUsartRegisterIO;

typedef int (*TagRegisterWrite)(const void *io, uint8_t reg,
                                const uint8_t *buf, uint32_t len);
typedef int (*TagRegisterRead)(const void *io, uint8_t reg, uint8_t *buf,
                               uint32_t len);

typedef struct {
  TagRegisterRead read_register;
  TagRegisterWrite write_register;
  const void *context;
} TagRegisterDevice;

int tagI2cWriteRegister(const void *io, uint8_t reg, const uint8_t *buf,
                        uint32_t len);
int tagI2cReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                       uint32_t len);

void tagSpiWrite(const uint8_t *buf, uint32_t len);
void tagSpiRead(uint8_t *buf, uint32_t len);

int tagStSpiWriteRegister(const void *io, uint8_t reg,
                          const uint8_t *buf, uint32_t len);
int tagStSpiReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                         uint32_t len);

void tagUsartWrite(USART_TypeDef *usart, const uint8_t *buf, uint32_t len);
void tagUsartRead(USART_TypeDef *usart, uint8_t dummy, uint8_t *buf,
                  uint32_t len);

int tagStUsartWriteRegister(const void *io, uint8_t reg,
                            const uint8_t *buf, uint32_t len);
int tagStUsartReadRegister(const void *io, uint8_t reg, uint8_t *buf,
                           uint32_t len);

#endif
