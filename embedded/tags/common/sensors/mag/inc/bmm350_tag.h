/**
 * @file bmm350_tag.h
 * @brief Native BMM350 magnetometer driver API for tag firmware.
 * @author tag firmware authors
 * @date 2026-07-17
 */

#ifndef TAG_BMM350_H
#define TAG_BMM350_H

#include "hal.h"
#include "i2c_bus.h"

#include <stdbool.h>
#include <stdint.h>

/** @name BMM350 constants
 * Public BMM350 register values and selectors used by tag/family bindings.
 * @{
 */
#define BMM350_CHIP_ID_VALUE 0x33U

typedef enum {
  BMM350_RATE_400HZ = 0x02,
  BMM350_RATE_200HZ = 0x03,
  BMM350_RATE_100HZ = 0x04,
  BMM350_RATE_50HZ = 0x05,
  BMM350_RATE_25HZ = 0x06,
  BMM350_RATE_12_5HZ = 0x07,
  BMM350_RATE_6_25HZ = 0x08,
  BMM350_RATE_3_125HZ = 0x09,
  BMM350_RATE_1_5625HZ = 0x0A
} bmm350_rate_t;

typedef enum {
  BMM350_PERF_LOW_POWER = 0,
  BMM350_PERF_REGULAR = 1,
  BMM350_PERF_LOW_NOISE = 2,
  BMM350_PERF_ULTRA_LOW_NOISE = 3
} bmm350_performance_t;

typedef enum {
  BMM350_INT_ACTIVE_LOW = 0,
  BMM350_INT_ACTIVE_HIGH = 1
} bmm350_int_polarity_t;

typedef enum {
  BMM350_INT_OPEN_DRAIN = 0,
  BMM350_INT_PUSH_PULL = 1
} bmm350_int_drive_t;
/** @} */

/** @name BMM350 compensation and device descriptors
 * RAM-backed factory compensation state and board binding.
 * @{
 */
typedef struct {
  float t_offs;
  float offset_x;
  float offset_y;
  float offset_z;
} TagBmm350OffsetCompensation;

typedef struct {
  float t_sens;
  float sens_x;
  float sens_y;
  float sens_z;
} TagBmm350SensitivityCompensation;

typedef struct {
  float tco_x;
  float tco_y;
  float tco_z;
} TagBmm350TcoCompensation;

typedef struct {
  float tcs_x;
  float tcs_y;
  float tcs_z;
} TagBmm350TcsCompensation;

typedef struct {
  float cross_x_y;
  float cross_y_x;
  float cross_z_x;
  float cross_z_y;
} TagBmm350CrossAxisCompensation;

typedef struct {
  bool valid;
  uint16_t otp_data[32];
  uint8_t variant_id;
  TagBmm350OffsetCompensation offset;
  TagBmm350SensitivityCompensation sensitivity;
  TagBmm350TcoCompensation tco;
  TagBmm350TcsCompensation tcs;
  float t0;
  TagBmm350CrossAxisCompensation cross_axis;
} TagBmm350Compensation;

typedef struct {
  const TagI2cDevice *i2c;
  ioline_t drdy;
  TagBmm350Compensation *compensation;
  bmm350_int_polarity_t interrupt_polarity;
  bmm350_int_drive_t interrupt_drive;
} TagBmm350Device;

typedef struct {
  int32_t x;
  int32_t y;
  int32_t z;
  int32_t temperature;
} TagBmm350RawSample;
/** @} */

/** @name BMM350 device lifecycle
 * Bus and optional power helpers.
 * @{
 */
void bmm350DeviceBegin(const TagBmm350Device *dev);
void bmm350DeviceEnd(const TagBmm350Device *dev);
/** @} */

/** @name BMM350 configuration and data API
 * Native driver entry points used by tag/family code.
 * @{
 */
bool bmm350CheckWhoami(const TagBmm350Device *dev);
msg_t bmm350Reset(const TagBmm350Device *dev);
msg_t bmm350ReadCompensationData(const TagBmm350Device *dev);
msg_t bmm350InitContinuous(const TagBmm350Device *dev,
                           bmm350_rate_t rate,
                           bmm350_performance_t performance);
msg_t bmm350InitPowerDown(const TagBmm350Device *dev);
bool bmm350DataReady(const TagBmm350Device *dev);
msg_t bmm350ReadRawSample(const TagBmm350Device *dev,
                          TagBmm350RawSample *sample);
msg_t bmm350ReadMagUT(const TagBmm350Device *dev,
                      float *mx, float *my, float *mz);
/** @} */

#endif
