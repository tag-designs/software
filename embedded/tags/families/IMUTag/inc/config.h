/**
 * @file config.h
 * @brief IMUTag family stored-configuration layout and monitor API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "tagdata.pb.h"
#include "lsm6dsv16x.h"

/** Tag type reported in monitor configuration messages. */
#define TAG_TYPE IMUTAG

#if !defined(IMUTAG_STORED_CONFIG_STM32U3_FLASH)
#if defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx) || defined(BOARD_IMUTagU375)
/** @brief Store IMUTag configuration in STM32U3 16-byte flash rows. */
#define IMUTAG_STORED_CONFIG_STM32U3_FLASH 1
#else
/** @brief Store IMUTag configuration in legacy 8-byte flash rows. */
#define IMUTAG_STORED_CONFIG_STM32U3_FLASH 0
#endif
#endif

/**
 * @brief Flash-resident IMUTag family acquisition schedule.
 *
 * @details The struct is written directly to the internal configuration flash
 *          slot. Keep alignment in sync with the flash programming width for
 *          the target MCU family.
 */
typedef struct
{
  int32_t  start;       ///< Collection start epoch in seconds.
  int32_t  stop;        ///< Collection stop epoch in seconds.
  uint32_t start_delay; ///< Delay from start command to collection start, in seconds.
  Lsm6dsv_ODR odr;      ///< Host-facing LSM6DSV16X output data-rate selector.
  Lsm6dsv_ACCEL accel_range; ///< Host-facing accelerometer full-scale selector.
  Lsm6dsv_GYRO gyro_range;   ///< Host-facing gyroscope full-scale selector.
  //bool internal;
#if IMUTAG_STORED_CONFIG_STM32U3_FLASH
  uint32_t flash_padding[2]; ///< Padding required for STM32U3 row writes.
} t_storedconfig __attribute__ ((aligned (16)));
#else
} t_storedconfig __attribute__ ((aligned (8)));
#endif

/** Active stored configuration loaded from internal flash. */
extern t_storedconfig sconfig;
/** Temporary configuration image staged before validation/persistence. */
extern t_storedconfig config_tmp;

/** @brief Persist a RAM configuration image into the flash configuration slot. */
extern void writeStoredConfig(t_storedconfig *s);
/** @brief Translate a host protobuf configuration into the RAM staging image. */
extern bool writeConfig(Config *config);
/** @brief Translate the stored flash configuration into a host protobuf message. */
extern void readConfig(Config *config);

/** @brief Store calibration constants from a host message. */
extern int write_calibration(CalibrationConstants *);
/** @brief Read calibration constants into a host ACK. */
extern int read_calibration(int32_t, Ack *);

/**
 * @brief Convert stored host LSM6DSV16X settings into driver selectors.
 *
 * @param[out] odr Triggered FIFO output data-rate selector.
 * @param[out] xl_fs Accelerometer full-scale selector.
 * @param[out] g_fs Gyroscope full-scale selector.
 * @return true when all stored fields map to supported driver settings.
 */
bool get_lsm_config(lsm6dsv16x_trig_odr_t *odr,lsm6dsv16x_xl_fs_t *xl_fs, lsm6dsv16x_g_fs_t *g_fs);

#endif /* CONFIG_H */
