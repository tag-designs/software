/**
 * @file bmm350_tag.h
 * @brief Native BMM350 magnetometer driver API for tag firmware.
 * @author tag firmware authors
 * @date 2026-07-17
 *
 * Maintainer notes:
 * - This API is intentionally narrower than Bosch's portable SensorAPI. Tag
 *   firmware owns bus/session policy through TagI2cDevice and only exposes the
 *   identity, continuous-mode, DRDY, compensated-read, and suspend operations
 *   needed by IMUTag-style sampling.
 * - Factory trim/compensation data lives in RAM supplied by the tag descriptor.
 *   User magnetometer calibration is a separate application-layer concern and
 *   should not be mixed into TagBmm350Compensation.
 * - The interrupt polarity and drive mode are descriptor fields because they
 *   are board-wiring choices. Keep board-specific decisions in devices.c rather
 *   than adding target branches to the driver.
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
/** @brief Expected BMM350 chip ID register value. */
#define BMM350_CHIP_ID_VALUE 0x33U

/**
 * @enum bmm350_rate_t
 * @brief Output data-rate selector encoded for the BMM350 PMU command path.
 */
typedef enum {
  BMM350_RATE_400HZ = 0x02,    ///< 400 Hz continuous output rate.
  BMM350_RATE_200HZ = 0x03,    ///< 200 Hz continuous output rate.
  BMM350_RATE_100HZ = 0x04,    ///< 100 Hz continuous output rate.
  BMM350_RATE_50HZ = 0x05,     ///< 50 Hz continuous output rate.
  BMM350_RATE_25HZ = 0x06,     ///< 25 Hz continuous output rate.
  BMM350_RATE_12_5HZ = 0x07,   ///< 12.5 Hz continuous output rate.
  BMM350_RATE_6_25HZ = 0x08,   ///< 6.25 Hz continuous output rate.
  BMM350_RATE_3_125HZ = 0x09,  ///< 3.125 Hz continuous output rate.
  BMM350_RATE_1_5625HZ = 0x0A  ///< 1.5625 Hz continuous output rate.
} bmm350_rate_t;

/**
 * @enum bmm350_performance_t
 * @brief BMM350 averaging/noise-performance selector.
 */
typedef enum {
  BMM350_PERF_LOW_POWER = 0,       ///< Lowest current, highest noise.
  BMM350_PERF_REGULAR = 1,         ///< Balanced current/noise setting.
  BMM350_PERF_LOW_NOISE = 2,       ///< Lower noise with higher current.
  BMM350_PERF_ULTRA_LOW_NOISE = 3  ///< Lowest noise, highest current.
} bmm350_performance_t;

/**
 * @enum bmm350_int_polarity_t
 * @brief Interrupt active-level selector for the BMM350 data-ready pin.
 */
typedef enum {
  BMM350_INT_ACTIVE_LOW = 0,  ///< Data-ready interrupt asserts low.
  BMM350_INT_ACTIVE_HIGH = 1  ///< Data-ready interrupt asserts high.
} bmm350_int_polarity_t;

/**
 * @enum bmm350_int_drive_t
 * @brief Output-drive selector for the BMM350 interrupt pin.
 */
typedef enum {
  BMM350_INT_OPEN_DRAIN = 0, ///< Interrupt pin uses open-drain drive.
  BMM350_INT_PUSH_PULL = 1   ///< Interrupt pin uses push-pull drive.
} bmm350_int_drive_t;
/** @} */

/** @name BMM350 compensation and device descriptors
 * RAM-backed factory compensation state and board binding.
 * @{
 */
/**
 * @struct TagBmm350OffsetCompensation
 * @brief Factory offset compensation terms decoded from BMM350 OTP.
 */
typedef struct {
  float t_offs;   ///< Temperature offset term.
  float offset_x; ///< X-axis magnetic offset term.
  float offset_y; ///< Y-axis magnetic offset term.
  float offset_z; ///< Z-axis magnetic offset term.
} TagBmm350OffsetCompensation;

/**
 * @struct TagBmm350SensitivityCompensation
 * @brief Factory sensitivity compensation terms decoded from BMM350 OTP.
 */
typedef struct {
  float t_sens; ///< Temperature sensitivity term.
  float sens_x; ///< X-axis magnetic sensitivity term.
  float sens_y; ///< Y-axis magnetic sensitivity term.
  float sens_z; ///< Z-axis magnetic sensitivity term.
} TagBmm350SensitivityCompensation;

/**
 * @struct TagBmm350TcoCompensation
 * @brief Temperature coefficient of offset compensation terms.
 */
typedef struct {
  float tco_x; ///< X-axis TCO term.
  float tco_y; ///< Y-axis TCO term.
  float tco_z; ///< Z-axis TCO term.
} TagBmm350TcoCompensation;

/**
 * @struct TagBmm350TcsCompensation
 * @brief Temperature coefficient of sensitivity compensation terms.
 */
typedef struct {
  float tcs_x; ///< X-axis TCS term.
  float tcs_y; ///< Y-axis TCS term.
  float tcs_z; ///< Z-axis TCS term.
} TagBmm350TcsCompensation;

/**
 * @struct TagBmm350CrossAxisCompensation
 * @brief Cross-axis magnetic compensation terms decoded from OTP.
 */
typedef struct {
  float cross_x_y; ///< Y contribution into compensated X.
  float cross_y_x; ///< X contribution into compensated Y.
  float cross_z_x; ///< X contribution into compensated Z.
  float cross_z_y; ///< Y contribution into compensated Z.
} TagBmm350CrossAxisCompensation;

/**
 * @struct TagBmm350Compensation
 * @brief RAM cache of BMM350 factory OTP and decoded compensation terms.
 *
 * @details bmm350ReadCompensationData() fills this structure before
 *          compensated reads are valid. The cache is device-owned RAM so
 *          multiple BMM350 descriptors can coexist without global trim state.
 */
typedef struct {
  bool valid; ///< True after OTP has been read and decoded successfully.
  uint16_t otp_data[32]; ///< Raw OTP words retained for diagnostics.
  uint8_t variant_id; ///< BMM350 variant identifier from OTP.
  TagBmm350OffsetCompensation offset; ///< Offset compensation group.
  TagBmm350SensitivityCompensation sensitivity; ///< Sensitivity group.
  TagBmm350TcoCompensation tco; ///< Temperature coefficient of offset group.
  TagBmm350TcsCompensation tcs; ///< Temperature coefficient of sensitivity group.
  float t0; ///< Reference temperature term used during compensation.
  TagBmm350CrossAxisCompensation cross_axis; ///< Cross-axis compensation group.
} TagBmm350Compensation;

/**
 * @struct TagBmm350Device
 * @brief Board binding and runtime compensation storage for one BMM350.
 */
typedef struct {
  /** Register bus used by the BMM350. The caller owns session bracketing with
   * bmm350DeviceBegin()/bmm350DeviceEnd() for public operations.
   */
  const TagI2cDevice *i2c;
  /** Data-ready/interrupt line. Use TAG_NO_LINE only for diagnostic bring-up;
   * normal collection expects a wired DRDY signal.
   */
  ioline_t drdy;
  /** RAM buffer populated from BMM350 OTP during bmm350InitContinuous(). */
  TagBmm350Compensation *compensation;
  /** Active level configured into INT_CTRL and used by bmm350DataReady(). */
  bmm350_int_polarity_t interrupt_polarity;
  /** Output drive configured into INT_CTRL. */
  bmm350_int_drive_t interrupt_drive;
} TagBmm350Device;

/**
 * @struct TagBmm350RawSample
 * @brief Raw signed BMM350 magnetometer and temperature sample.
 */
typedef struct {
  int32_t x;           ///< Raw X-axis magnetic ADC sample.
  int32_t y;           ///< Raw Y-axis magnetic ADC sample.
  int32_t z;           ///< Raw Z-axis magnetic ADC sample.
  int32_t temperature; ///< Raw temperature ADC sample.
} TagBmm350RawSample;
/** @} */

/** @name BMM350 device lifecycle
 * Bus and optional power helpers.
 * @{
 */
/**
 * @brief Power and begin the I2C session for a BMM350 descriptor.
 *
 * @param[in] dev BMM350 device descriptor.
 */
void bmm350DeviceBegin(const TagBmm350Device *dev);

/**
 * @brief End the I2C session and power down a BMM350 descriptor.
 *
 * @param[in] dev BMM350 device descriptor.
 */
void bmm350DeviceEnd(const TagBmm350Device *dev);
/** @} */

/** @name BMM350 configuration and data API
 * Native driver entry points used by tag/family code.
 * @{
 */
/**
 * @brief Verify that the device responds with the expected BMM350 chip ID.
 *
 * @param[in] dev BMM350 device descriptor.
 * @return true when the chip ID matches BMM350_CHIP_ID_VALUE.
 */
bool bmm350CheckWhoami(const TagBmm350Device *dev);

/**
 * @brief Issue a soft reset and wait for the command to complete.
 *
 * @param[in] dev BMM350 device descriptor.
 * @return MSG_OK on success, or a ChibiOS I2C error.
 */
msg_t bmm350Reset(const TagBmm350Device *dev);

/**
 * @brief Read OTP trim data and populate the descriptor compensation cache.
 *
 * @param[in] dev BMM350 device descriptor with writable compensation storage.
 * @return MSG_OK when OTP was read and decoded, or a ChibiOS I2C error.
 */
msg_t bmm350ReadCompensationData(const TagBmm350Device *dev);

/**
 * @brief Configure continuous magnetic sampling.
 *
 * @param[in] dev BMM350 device descriptor.
 * @param[in] rate Output data rate selector.
 * @param[in] performance Averaging/noise-performance selector.
 * @return MSG_OK when configuration completed, or a ChibiOS I2C error.
 */
msg_t bmm350InitContinuous(const TagBmm350Device *dev,
                           bmm350_rate_t rate,
                           bmm350_performance_t performance);

/**
 * @brief Put the BMM350 into power-down mode.
 *
 * @param[in] dev BMM350 device descriptor.
 * @return MSG_OK when the mode command completed, or a ChibiOS I2C error.
 */
msg_t bmm350InitPowerDown(const TagBmm350Device *dev);

/**
 * @brief Read the descriptor's configured data-ready line.
 *
 * @param[in] dev BMM350 device descriptor.
 * @return true when the DRDY pin is asserted according to descriptor polarity.
 */
bool bmm350DataReady(const TagBmm350Device *dev);

/**
 * @brief Read one raw magnetometer and temperature sample.
 *
 * @param[in] dev BMM350 device descriptor.
 * @param[out] sample Populated with raw ADC values on success.
 * @return MSG_OK when all sample registers were read, or a ChibiOS I2C error.
 */
msg_t bmm350ReadRawSample(const TagBmm350Device *dev,
                          TagBmm350RawSample *sample);

/**
 * @brief Read one compensated magnetic sample in microtesla.
 *
 * @param[in] dev BMM350 device descriptor with valid compensation cache.
 * @param[out] mx Compensated X-axis magnetic field in uT.
 * @param[out] my Compensated Y-axis magnetic field in uT.
 * @param[out] mz Compensated Z-axis magnetic field in uT.
 * @return MSG_OK when a raw sample was read and compensated.
 */
msg_t bmm350ReadMagUT(const TagBmm350Device *dev,
                      float *mx, float *my, float *mz);
/** @} */

#endif
