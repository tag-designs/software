/**
 * @file ak09940.h
 * @brief AK09940A magnetometer driver interface for ChibiOS HAL.
 *
 * This driver assumes:
 *   - ChibiOS SPI HAL is available
 *   - ChibiOS PAL is available for DRDY/TRG pin control
 *   - The board routes the sensor's DRDY/TRG pin to one MCU GPIO line
 *
 * The AK09940A supports:
 *   - Power-down mode
 *   - Continuous sampling mode with DRDY output
 *   - Triggered sampling mode with external trigger input
 *
 * Important protocol details:
 *   - Magnetometer data is 18-bit per axis.
 *   - The data registers must be read in one SPI burst transaction.
 *   - ST2 must be the final register read to release the sensor's data latch.
 */

#ifndef AK09940_H
#define AK09940_H

#include "ch.h"
#include "hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AK09940_SENSITIVITY_NT_PER_LSB 10
#define AK09940_SENSITIVITY_UT_PER_LSB 10000

/**
 * @brief Output sample rate selections for continuous mode.
 *
 * The enum names are intentionally driver-facing rather than register-facing.
 * The implementation maps these to the AK09940A continuous mode codes.
 */
typedef enum {
    AK09940_RATE_10HZ = 0,
    AK09940_RATE_20HZ,
    AK09940_RATE_50HZ,
    AK09940_RATE_100HZ,
    AK09940_RATE_200HZ,
    AK09940_RATE_400HZ
} ak09940_rate_t;

/**
 * @brief Temperature measurement enable control.
 *
 * The device can be configured to include or omit temperature readings.
 */
typedef enum {
    AK09940_TEMP_DISABLED = 0,
    AK09940_TEMP_ENABLED  = 1
} ak09940_temp_mode_t;

/**
 * @brief Sensor drive selection.
 *
 * This maps to the drive-select bitfield configured in CNTL3 MT[1:0],
 * with CNTL1.MT2 used for the ultra-low-power option.
 *
 * The names are descriptive abstractions for firmware use. The implementation
 * maps them to the exact AK09940A control bits.
 */
typedef enum {
    AK09940_DRIVE_LOW_POWER_1 = 0,
    AK09940_DRIVE_LOW_POWER_2,
    AK09940_DRIVE_LOW_NOISE_1,
    AK09940_DRIVE_LOW_NOISE_2,
    AK09940_DRIVE_ULTRA_LOW_POWER
} ak09940_drive_t;


/**
 * @brief Read and verify the WHO_AM_I / WIA2 value.
 *
 * @param dev Driver instance.
 * @return true if the sensor identity matches the expected value.
 */
bool ak09940_check_whoami(void);

/**
 * @brief Put the sensor into power-down mode.
 *
 * @param dev Driver instance.
 * @return MSG_OK on success.
 */
msg_t ak09940_init_power_down(void);

/**
 * @brief Initialize continuous sampling mode.
 *
 * In continuous mode the DRDY/TRG pin is configured as an input on the MCU
 * side and can be used as a data-ready interrupt source.
 *
 * @param rate Continuous output data rate.
 * @param drive Sensor drive selection.
 * @param temp_mode Enable or disable temperature sampling.
 * @return MSG_OK on success.
 */
msg_t ak09940_init_continuous(ak09940_rate_t rate,
                              ak09940_drive_t drive,
                              ak09940_temp_mode_t temp_mode);

/**
 * @brief Initialize triggered sampling mode.
 *
 * In triggered mode the DRDY/TRG pin is configured as an output on the MCU
 * side so firmware can pulse it to start a measurement.
 *
 * @param drive Sensor drive selection.
 * @param temp_mode Enable or disable temperature sampling.
 * @return MSG_OK on success.
 */
msg_t ak09940_init_triggered(ak09940_drive_t drive,
                             ak09940_temp_mode_t temp_mode);

/**
 * @brief Trigger Sample.
 *
 * In triggered mode the DRDY/TRG pin is configured as an output on the MCU
 * side so firmware can pulse it to start a measurement.
 *
 * @return MSG_OK on success.
 */

 msg_t ak09940_tringger(void);


/**
 * @brief Check whether a new sample is available.
 *
 * This uses the latched interrupt flag when enabled, otherwise it polls ST1.
 *
 * @param isContinous If operating in continous mode.
 * @return true if a sample is ready to read.
 */

bool ak09940_data_ready(bool isContinuous);

/**
 * @brief Read one coherent sensor sample.
 *
 * This performs a single SPI burst read starting at ST1 and continuing through
 * HXL..HZH, TMPS, and ST2. Reading ST2 last is required to release the
 * internal latch and make the next sample available.
 *
 * @param mx_raw Output raw X magnetic field reading (18-bit signed).
 * @param my_raw Output raw Y magnetic field reading (18-bit signed).
 * @param mz_raw Output raw Z magnetic field reading (18-bit signed).
 * @param temp_raw Output temperature reading.
 * @return true if the sample is valid and was read successfully.
 */
bool ak09940_read_sample(int32_t *mx_raw,
                         int32_t *my_raw,
                         int32_t *mz_raw,
                         int16_t *temp_raw);

/**
 * @brief Run the device self test.
 *
 * The implementation configures the sensor in a self-test-oriented mode,
 * waits for a conversion, and checks the returned values against placeholder
 * bounds. Replace those bounds with the exact datasheet thresholds if you
 * want a strict production acceptance test.
 *
 * @param dev Driver instance.
 * @return true if the test passes.
 */
bool ak09940_self_test(void);

/**
 * @brief Convert raw sample values to microtesla.
 *
 * @param mx_raw Raw X reading.
 * @param my_raw Raw Y reading.
 * @param mz_raw Raw Z reading.
 * @param mx_uT Output X in microtesla, or NULL.
 * @param my_uT Output Y in microtesla, or NULL.
 * @param mz_uT Output Z in microtesla, or NULL.
 */
void ak09940_convert_to_uT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           float *mx_uT, float *my_uT, float *mz_uT);

/**
 * @brief Convert raw sample values to integer nanotesla.
 *
 * This is often the easiest format for DSP pipelines because it avoids
 * floating-point operations while preserving intuitive engineering units.
 */
void ak09940_convert_to_nT(int32_t mx_raw, int32_t my_raw, int32_t mz_raw,
                           int32_t *mx_nT, int32_t *my_nT, int32_t *mz_nT);

#ifdef __cplusplus
}
#endif

#endif /* AK09940_H */
