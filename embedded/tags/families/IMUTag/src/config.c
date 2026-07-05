/**
 * @file config.c
 * @brief IMUTagBreakout configuration persistence and protobuf conversion.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "sensors.h"
#include "strings.h"

// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;  
static const char *config_error_message;

const char *writeConfigErrorMessage(void)
{
  return config_error_message;
}


// config to lsm6dsv16x conversion routines

static bool lsm6totrig_odr(Lsm6dsv_ODR odr_in, lsm6dsv16x_trig_odr_t *odr){
  bool rval = true;
  switch(odr_in) {
    case Lsm6dsv_ODR_S50:
      *odr = LSM6DSV16X_TRIG_ODR_50HZ;
      break;
    case Lsm6dsv_ODR_S100:
      *odr = LSM6DSV16X_TRIG_ODR_100HZ;
      break;
    case Lsm6dsv_ODR_S200:
      *odr = LSM6DSV16X_TRIG_ODR_200HZ;
      break;
    case Lsm6dsv_ODR_S400:
      *odr = LSM6DSV16X_TRIG_ODR_400HZ;
      break;
    case Lsm6dsv_ODR_S800:
      *odr = LSM6DSV16X_TRIG_ODR_800HZ;
      break;
    case Lsm6dsv_ODR_S1600:
      *odr = LSM6DSV16X_TRIG_ODR_1600HZ;
      break;
    default:
      rval = false;
  }
  return rval;
}

static bool lsm6toxl_fs(Lsm6dsv_ACCEL accel_in,lsm6dsv16x_xl_fs_t *xl_fs)
{
  bool rval = true;
  switch(accel_in) {
    case Lsm6dsv_ACCEL_R2G:
      *xl_fs =  LSM6DSV16X_XL_FS_2G;
      break;
    case Lsm6dsv_ACCEL_R4G:
      *xl_fs =  LSM6DSV16X_XL_FS_4G;
      break;
    case Lsm6dsv_ACCEL_R8G:
      *xl_fs =  LSM6DSV16X_XL_FS_8G;
      break;
    case Lsm6dsv_ACCEL_R16G:
      *xl_fs =  LSM6DSV16X_XL_FS_16G;
      break;
    default:
      rval = false;
  }
  return rval;
}

static bool lsm6tog_fs(Lsm6dsv_GYRO gyro_in,lsm6dsv16x_g_fs_t *g_fs){
  bool rval = true;

  switch(gyro_in) {
    case Lsm6dsv_GYRO_R125dps:
      *g_fs = LSM6DSV16X_G_FS_125DPS;
      break;
    case Lsm6dsv_GYRO_R250dps:
      *g_fs = LSM6DSV16X_G_FS_250DPS;
      break;
    case Lsm6dsv_GYRO_R500dps:
      *g_fs = LSM6DSV16X_G_FS_500DPS;
      break;
    case Lsm6dsv_GYRO_R1000dps:
      *g_fs = LSM6DSV16X_G_FS_1000DPS;
      break;
    case Lsm6dsv_GYRO_R2000dps:
      *g_fs = LSM6DSV16X_G_FS_2000DPS;
      break;
    case Lsm6dsv_GYRO_R4000dps:
      *g_fs = LSM6DSV16X_G_FS_4000DPS;
      break;
    default:
      rval = false;
  }
  return rval;
}

bool get_lsm_config(lsm6dsv16x_trig_odr_t *odr,lsm6dsv16x_xl_fs_t *xl_fs, lsm6dsv16x_g_fs_t *g_fs){
    return lsm6totrig_odr(sconfig.odr, odr)
           && lsm6toxl_fs(sconfig.accel_range, xl_fs)
           && lsm6tog_fs(sconfig.gyro_range, g_fs);
}


/**
 * @brief Write the staged configuration to internal flash.
 *
 * @param[in] s Configuration image to persist.
 */
void writeStoredConfig(t_storedconfig *s)
{
  uint32_t *src = (uint32_t *)s;
  uint32_t *dest = (uint32_t *)&sconfig;
  ssize_t size = sizeof(*s)/4;
  if (s)
  {
    chSysLock();
    FLASH_Unlock();
    FLASH_Program_Array(dest, src, size);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
  }
}


/**
 * @brief Export the current stored configuration as a protobuf message.
 *
 * @param[out] config Destination configuration message.
 */
void readConfig(Config *config)
{
  if (config == NULL)
    return;

  bzero(config, sizeof(*config));
  config->tag_type = TAG_TYPE;

  config->has_active_interval = false;
  //config->active_interval.start_epoch = sconfig.start;
  //config->active_interval.end_epoch = sconfig.stop;
  config->start_delay = sconfig.start_delay;
  config->has_lsm6 = true;
  config->lsm6.odr = sconfig.odr;
  config->lsm6.accel_rng = sconfig.accel_range;
  config->lsm6.gyro_rng = sconfig.gyro_range;
}

/**
 * @brief Validate and stage a host-provided configuration.
 *
 * @param[in] config Host-provided configuration message.
 * @return true when the configuration can be staged in the current state.
 */
bool writeConfig(Config *config)
{
  config_error_message = NULL;
  // minimum requirement

  if ((config == NULL) || pState->state != TagState_IDLE)
    return false;

  if (!sensorsHaveCalibration())
  {
    config_error_message = "Device must be calibrated";
    return false;
  }

  // check for sensor configuration

  if (!config->has_lsm6 
       || (config->lsm6.odr == 0)
       || (config->lsm6.accel_rng == 0)
       || (config->lsm6.gyro_rng == 0))
    return false;

  // copy into temporary config
  if (config->start_delay == 0) // force to be > 0
    config->start_delay = 1;
  config_tmp.start = (config->start_delay-1)*60 + timestamp; // computed start time
  config_tmp.start_delay = config->start_delay;       // start delay in seconds (rounded to minute)
  config_tmp.stop = INT32_MAX;
  config_tmp.odr = config->lsm6.odr;
  config_tmp.accel_range = config->lsm6.accel_rng;
  config_tmp.gyro_range = config->lsm6.gyro_rng;
 
  return true;
}
