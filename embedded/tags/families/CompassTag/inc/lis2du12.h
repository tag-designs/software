#ifndef LIS2DU12_H
#define LIS2DU12_H

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {ACCEL_WAKEUP_MODE, 
            ACCEL_SAMPLE_50HZ_MODE, 
            ACCEL_SAMPLE_100HZ_MODE} lis2du12mode_t;

typedef void (*TagLis2du12Bus)(void);
typedef void (*TagLis2du12WriteByte)(const void *context, uint8_t reg,
                                     uint8_t val);

/*
 * LIS2DU12 instance descriptor.
 *
 * The driver owns the LIS2DU12 register sequence. The tag power layer owns the
 * physical bus wiring, USART/SPI controller setup, and chip-select transaction
 * details. Sensor power lifetime is deliberately not part of this descriptor;
 * callers should power devices at the state-machine or board-policy layer.
 * `write_register_byte` is explicit because the wakeup configuration is a
 * series of byte writes that must be framed as one command+value transaction
 * under chip select.
 */
typedef struct {
  const TagRegisterBus *registers;
  TagLis2du12Bus bus_begin;
  TagLis2du12Bus bus_end;
  TagLis2du12WriteByte write_register_byte;
  const void *write_register_byte_context;
} TagLis2du12Device;

void lis2du12Init(const TagLis2du12Device *device, lis2du12mode_t mode);
void lis2du12Deinit(const TagLis2du12Device *device);
void lis2du12Reset(const TagLis2du12Device *device);
bool lis2du12Test(const TagLis2du12Device *device);
bool lis2du12Sample(const TagLis2du12Device *device, uint8_t *data);
const TagLis2du12Device *tagLis2du12Device(void);

void accelInit(lis2du12mode_t mode);
void accelDeinit(void);
void accelReset(void);
bool accelTest(void);
bool accelSample(uint8_t *);


#endif
