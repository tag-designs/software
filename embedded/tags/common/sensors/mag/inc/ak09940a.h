/**
 * @file ak09940a.h
 * @brief AK09940A magnetometer register definitions and descriptor-backed API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef __AK09940A_H__
#define __AK09940A_H__

#include "sensor_io.h"

#include <stdbool.h>
#include <stdint.h>

/** @name AK09940A registers and constants
 * Register addresses, bit masks, and sensitivity constants used by the driver.
 * @{
 */
enum AK09940A_Reg
{

AK09940A_WIA1 = (0x00),
AK09940A_WIA2 = (0x01),

// Status for polling

AK09940A_ST   = (0x0f),

// Status

AK09940A_ST1  = (0x10),

// Magnetometer data 

AK09940A_HXL  = (0x11),
AK09940A_HXM  = (0x12),
AK09940A_HXH  = (0x13),
AK09940A_HYL  = (0x14),
AK09940A_HYM  = (0x15),
AK09940A_HYH  = (0x16),
AK09940A_HZL  = (0x17),
AK09940A_HZM  = (0x18),
AK09940A_HZH  = (0x19),

// Temperature data

AK09940A_TMPS = (0x1A),

// Status for release of data
//    must be read after data

AK09940A_ST2  = (0x1B),

// Self test data

AK09940A_SXL = (0x20),
AK09940A_SXH = (0x21),
AK09940A_SYL = (0x22),
AK09940A_SYH = (0x23),
AK09940A_SZL = (0x24),
AK09940A_SZH = (0x25),

// Control Registers

AK09940A_CNTL1 = (0x30),
AK09940A_CNTL2 = (0x31),
AK09940A_CNTL3 = (0x32),
AK09940A_CNTL4 = (0x33),

// Register to release I2C

AK09940A_I2CDIS =  (0x36),

};

// Chip ID constants

#define AK09940A_COMPANY_ID (0x48)
#define AK09940A_PRODUCT_ID (0xA3)

#define AK09940A_ST_DOR_MSK  (0x2)
#define AK09940A_ST_DRDY_MSK (0x1)
#define AK09940A_ST1_FIFO_CNT_MSK (0x0f)
#define AK09940A_ST2_INV_MSK (0x02)
#define AK09940A_ST2_DOR_MSK (0x01)

#define AK09940A_CNTL1_WM_MSK (0x07)
#define AK09940A_CNTL1_MT2_MSK (0x80)
#define AK09940A_CNTL1_DTSET_MSK (0x20)

#define AK09940A_CNTL2_TEM_MSK (0x40)

#define AK09940A_CNTL3_PWRDOWN  (0x00)
#define AK09940A_CNTL3_SINGLE_MEASURE (0x01)
#define AK09940A_CNTL3_10HZ (0x02)
#define AK09940A_CNTL3_20HZ (0x04)
#define AK09940A_CNTL3_50HZ (0x06)
#define AK09940A_CNTL3_100HZ (0x08)
#define AK09940A_CNTL3_200HZ (0x0A)
#define AK09940A_CNTL3_400HZ (0x0C)
#define AK09940A_CNTL3_EXTERNAL_TRIGGER (0x18)

#define AK09940A_CNTL3_SELF_TEST_MODE (0x20)

#define AK09940A_CNTL3_FIFO_EN (0x1<<7)

#define AK09940A_CNTL3_LP1 (0x0 << 5)
#define AK09940A_CNTL3_LP2 (0x1 << 5)
#define AK09940A_CNTL3_LN1 (0x2 << 5)
#define AK09940A_CNTL3_LN2 (0x3 << 5)

#define AK09940A_CNTL4_SRST (1)
#define AK09940A_I2CDIS_DISABLE (0x1B)

#define AK09940A_SENSITIVITY_NT_PER_LSB 10
#define AK09940A_SENSITIVITY_UT_PER_LSB 0.01f
/** @} */

/** @name AK09940A configuration types
 * Mode, sample-rate, temperature, and drive selectors that map to AK09940A
 * control-register fields.
 * @{
 */
typedef enum { MAG_SAMPLE_SINGLE_MODE = AK09940A_CNTL3_SINGLE_MEASURE , 
               MAG_SAMPLE_10HZ_MODE   = AK09940A_CNTL3_10HZ,
               MAG_SAMPLE_20HZ_MODE   = AK09940A_CNTL3_20HZ,
               MAG_SAMPLE_50HZ_MODE   = AK09940A_CNTL3_50HZ,
               MAG_SAMPLE_100HZ_MODE  = AK09940A_CNTL3_100HZ 
               } ak09940_mode_t;
/** @} */

/** @name AK09940A device descriptor
 * Tag-provided callbacks and register descriptor needed to run AK09940A on the
 * current board.
 * @{
 */
typedef void (*TagMagSleep)(int ms);
typedef void (*TagMagTriggerMode)(bool output);
typedef void (*TagMagTrigger)(void);
typedef bool (*TagMagDataReadyLine)(void);

typedef struct {
  const TagRegisterDevice *registers;
  TagMagSleep sleep_ms;
  TagMagTriggerMode set_trigger_output;
  TagMagTrigger trigger;
  TagMagDataReadyLine data_ready_line;
} TagMagDevice;
/** @} */

/** @name AK09940A extended configuration types
 * Newer descriptor-backed API selectors for rate, temperature, and drive mode.
 * @{
 */

typedef enum {
  AK09940_RATE_10HZ = 0,
  AK09940_RATE_20HZ,
  AK09940_RATE_50HZ,
  AK09940_RATE_100HZ,
  AK09940_RATE_200HZ,
  AK09940_RATE_400HZ
} ak09940_rate_t;

typedef enum {
  AK09940_TEMP_DISABLED = 0,
  AK09940_TEMP_ENABLED = 1
} ak09940_temp_mode_t;

typedef enum {
  AK09940_DRIVE_LOW_POWER_1 = 0,
  AK09940_DRIVE_LOW_POWER_2,
  AK09940_DRIVE_LOW_NOISE_1,
  AK09940_DRIVE_LOW_NOISE_2,
  AK09940_DRIVE_ULTRA_LOW_POWER
} ak09940_drive_t;
/** @} */

/** @name Legacy magnetometer API
 * Historical global functions kept for tag code that has not moved to explicit
 * TagMagDevice descriptors.
 * @{
 */
/**
 * @brief Test the default AK09940A device.
 *
 * @return true when the expected identity registers are present.
 */
bool magTest(void);
/**
 * @brief Read one raw 3-axis sample from the default AK09940A.
 *
 * Sample elements are 3 bytes, so the destination must hold 9 bytes.
 *
 * @param[in] single true to start a single conversion first.
 * @param[out] xyz Destination for X/Y/Z raw sample bytes.
 * @return true when sample data was read.
 */
bool  magSample(bool single, uint8_t *xyz);
/**
 * @brief Initialize the default AK09940A in a legacy operating mode.
 *
 * @param[in] mode Legacy sample mode.
 */
void  magInit(ak09940_mode_t mode);
/** @} */

/** @name Descriptor-backed AK09940A API
 * Explicit device API used by new tag/family code.
 * @{
 */
/**
 * @brief Test an AK09940A descriptor.
 *
 * @param[in] device Magnetometer descriptor.
 * @return true when the expected identity registers are present.
 */
bool ak09940aTest(const TagMagDevice *device);
/**
 * @brief Read one raw 3-axis sample from an AK09940A descriptor.
 *
 * @param[in] device Magnetometer descriptor.
 * @param[in] single true to start a single conversion first.
 * @param[out] xyz Destination for X/Y/Z raw sample bytes.
 * @return true when sample data was read.
 */
bool ak09940aSample(const TagMagDevice *device, bool single, uint8_t *xyz);
/**
 * @brief Initialize an AK09940A descriptor in a legacy operating mode.
 *
 * @param[in] device Magnetometer descriptor.
 * @param[in] mode Legacy sample mode.
 */
void ak09940aInit(const TagMagDevice *device, ak09940_mode_t mode);
/**
 * @brief Power and acquire the AK09940A register bus.
 *
 * @param[in] device Magnetometer descriptor.
 */
void ak09940aDeviceBegin(const TagMagDevice *device);
/**
 * @brief Release and power down the AK09940A register bus.
 *
 * @param[in] device Magnetometer descriptor.
 */
void ak09940aDeviceEnd(const TagMagDevice *device);
/**
 * @brief Return the default AK09940A descriptor.
 *
 * @return Default magnetometer descriptor for this tag.
 */
const TagMagDevice *tagAk09940aDevice(void);

/**
 * @brief Check the AK09940A identity registers.
 *
 * @param[in] device Magnetometer descriptor.
 * @return true when the company and product IDs match.
 */
bool ak09940aCheckWhoami(const TagMagDevice *device);
/**
 * @brief Put an AK09940A into power-down mode.
 *
 * @param[in] device Magnetometer descriptor.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940aInitPowerDown(const TagMagDevice *device);
/**
 * @brief Configure an AK09940A for continuous sampling.
 *
 * @param[in] device Magnetometer descriptor.
 * @param[in] rate Output sample rate.
 * @param[in] drive Noise/power drive selection.
 * @param[in] temp_mode Temperature channel enable state.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940aInitContinuous(const TagMagDevice *device,
                             ak09940_rate_t rate,
                             ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode);
/**
 * @brief Configure an AK09940A for external-trigger sampling.
 *
 * @param[in] device Magnetometer descriptor.
 * @param[in] drive Noise/power drive selection.
 * @param[in] temp_mode Temperature channel enable state.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940aInitTriggered(const TagMagDevice *device,
                            ak09940_drive_t drive,
                            ak09940_temp_mode_t temp_mode);
/**
 * @brief Trigger one external-trigger conversion.
 *
 * @param[in] device Magnetometer descriptor.
 * @return MSG_OK when a trigger callback is available and used.
 */
msg_t ak09940aTrigger(const TagMagDevice *device);
/**
 * @brief Check whether AK09940A sample data is ready.
 *
 * @param[in] device Magnetometer descriptor.
 * @param[in] is_continuous true when using continuous-mode status semantics.
 * @return true when sample data is ready.
 */
bool ak09940aDataReady(const TagMagDevice *device, bool is_continuous);
/**
 * @brief Read one raw AK09940A sample.
 *
 * @param[in] device Magnetometer descriptor.
 * @param[out] mx_raw X-axis magnetic sample.
 * @param[out] my_raw Y-axis magnetic sample.
 * @param[out] mz_raw Z-axis magnetic sample.
 * @param[out] temp_raw Optional raw temperature sample.
 * @return true when a valid sample was read.
 */
bool ak09940aReadSample(const TagMagDevice *device, int32_t *mx_raw,
                        int32_t *my_raw, int32_t *mz_raw, int16_t *temp_raw);
/**
 * @brief Run the AK09940A self-test flow.
 *
 * @param[in] device Magnetometer descriptor.
 * @return true when self-test readings are in range.
 */
bool ak09940aSelfTest(const TagMagDevice *device);
/** @} */

/** @name Default-device AK09940A API
 * Wrappers that operate on tagAk09940aDevice() for legacy callers.
 * @{
 */
/**
 * @brief Check the default AK09940A identity registers.
 *
 * @return true when the company and product IDs match.
 */
bool ak09940_check_whoami(void);
/**
 * @brief Put the default AK09940A into power-down mode.
 *
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940_init_power_down(void);
/**
 * @brief Configure the default AK09940A for continuous sampling.
 *
 * @param[in] rate Output sample rate.
 * @param[in] drive Noise/power drive selection.
 * @param[in] temp_mode Temperature channel enable state.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940_init_continuous(ak09940_rate_t rate, ak09940_drive_t drive,
                              ak09940_temp_mode_t temp_mode);
/**
 * @brief Configure the default AK09940A for external-trigger sampling.
 *
 * @param[in] drive Noise/power drive selection.
 * @param[in] temp_mode Temperature channel enable state.
 * @return MSG_OK on success or a register-transport error.
 */
msg_t ak09940_init_triggered(ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode);
/**
 * @brief Trigger one conversion on the default AK09940A.
 *
 * @return MSG_OK when a trigger callback is available and used.
 */
msg_t ak09940_trigger(void);
/**
 * @brief Check whether default AK09940A sample data is ready.
 *
 * @param[in] is_continuous true when using continuous-mode status semantics.
 * @return true when sample data is ready.
 */
bool ak09940_data_ready(bool is_continuous);
/**
 * @brief Read one raw sample from the default AK09940A.
 *
 * @param[out] mx_raw X-axis magnetic sample.
 * @param[out] my_raw Y-axis magnetic sample.
 * @param[out] mz_raw Z-axis magnetic sample.
 * @param[out] temp_raw Optional raw temperature sample.
 * @return true when a valid sample was read.
 */
bool ak09940_read_sample(int32_t *mx_raw, int32_t *my_raw, int32_t *mz_raw,
                         int16_t *temp_raw);
/**
 * @brief Run self-test on the default AK09940A.
 *
 * @return true when self-test readings are in range.
 */
bool ak09940_self_test(void);
/**
 * @brief Convert raw magnetic samples to microtesla.
 *
 * @param[in] mx_raw X-axis raw sample.
 * @param[in] my_raw Y-axis raw sample.
 * @param[in] mz_raw Z-axis raw sample.
 * @param[out] mx_uT X-axis converted sample.
 * @param[out] my_uT Y-axis converted sample.
 * @param[out] mz_uT Z-axis converted sample.
 */
void ak09940_convert_to_uT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           float *mx_uT, float *my_uT, float *mz_uT);
/**
 * @brief Convert raw magnetic samples to nanotesla.
 *
 * @param[in] mx_raw X-axis raw sample.
 * @param[in] my_raw Y-axis raw sample.
 * @param[in] mz_raw Z-axis raw sample.
 * @param[out] mx_nT X-axis converted sample.
 * @param[out] my_nT Y-axis converted sample.
 * @param[out] mz_nT Z-axis converted sample.
 */
void ak09940_convert_to_nT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           int32_t *mx_nT, int32_t *my_nT, int32_t *mz_nT);
/** @} */

// https://github.com/SlimeVR/SlimeVR-Tracker-nRF/blob/master/src/sensor/mag/AK09940.c


#endif
