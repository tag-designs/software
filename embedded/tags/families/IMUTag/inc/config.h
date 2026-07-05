/**
 * @file config.h
 * @brief IMUTagBreakout stored-configuration layout and monitor API.
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
#define IMUTAG_STORED_CONFIG_STM32U3_FLASH 1
#else
#define IMUTAG_STORED_CONFIG_STM32U3_FLASH 0
#endif
#endif

/** @brief Flash-resident IMUTagBreakout acquisition schedule. */
typedef struct
{
  int32_t  start;
  int32_t  stop;
  uint32_t start_delay;
  Lsm6dsv_ODR odr;
  Lsm6dsv_ACCEL accel_range;
  Lsm6dsv_GYRO gyro_range;
  //bool internal;
#if IMUTAG_STORED_CONFIG_STM32U3_FLASH
  uint32_t flash_padding[2];
} t_storedconfig __attribute__ ((aligned (16)));
#else
} t_storedconfig __attribute__ ((aligned (8)));
#endif

extern t_storedconfig sconfig;
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

bool get_lsm_config(lsm6dsv16x_trig_odr_t *odr,lsm6dsv16x_xl_fs_t *xl_fs, lsm6dsv16x_g_fs_t *g_fs);

#endif /* CONFIG_H */
