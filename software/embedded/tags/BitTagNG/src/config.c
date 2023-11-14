#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "string.h"
#include "ADXL367.h"

#define ADXL_RANGE(r) (((r) >> 6) & 3)
#define ADXL_RATE(r) ((r)&7)


// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;  

/*
 * Write config in ram to flash
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

// Translation between BitTag constants and ProtoBuf constants

static const Adxl362_Rng Adxl367RngToEnum[] = {
    [ADXL367_RANGE_2G] = Adxl362_R2G,
    [ADXL367_RANGE_4G] = Adxl362_R4G,
    [ADXL367_RANGE_8G] = Adxl362_R8G};

static const Adxl362_Odr Adxl367ODRToEnum[] = {
    [ADXL367_ODR_12P5HZ] = Adxl362_S12_5,
    [ADXL367_ODR_25HZ] = Adxl362_S25,
    [ADXL367_ODR_50HZ] = Adxl362_S50,
    [ADXL367_ODR_100HZ] = Adxl362_S100,
    [ADXL367_ODR_200HZ] = Adxl362_S200,
    [ADXL367_ODR_400HZ] = Adxl362_S400};

static int EnumToAdxl367Rng(Adxl362_Rng rng)
{
  switch (rng)
  {
  case Adxl362_R2G:
    return ADXL367_RANGE_2G;
  case Adxl362_R4G:
    return ADXL367_RANGE_4G;
  case Adxl362_R8G:
    return ADXL367_RANGE_8G;
  default:
    return -1;
  };
}

static int EnumToAdxl367ODR(Adxl362_Odr odr)
{
  switch (odr)
  {
  case Adxl362_S12_5:
    return ADXL367_ODR_12P5HZ;
  case Adxl362_S25:
    return ADXL367_ODR_25HZ;
  case Adxl362_S50:
    return ADXL367_ODR_50HZ;
  case Adxl362_S100:
    return ADXL367_ODR_100HZ;
  case Adxl362_S200:
    return ADXL367_ODR_200HZ;
  case Adxl362_S400:
    return ADXL367_ODR_400HZ;
  default:
    return -1;
  };
}

// See ADXL367 Data sheet
static const float Tdelta[] = {[ADXL367_ODR_12P5HZ] = 1 / 12.5,
                               [ADXL367_ODR_25HZ] = 1 / 25.0,
                               [ADXL367_ODR_50HZ] = 1 / 50.0,
                               [ADXL367_ODR_100HZ] = 1 / 100.0,
                               [ADXL367_ODR_200HZ] = 1 / 200.0,
                               [ADXL367_ODR_400HZ] = 1 / 400.0};

// See ADXL367 Data Sheet

static const float Sens[] = {[ADXL367_RANGE_2G] = 0.00025,
                             [ADXL367_RANGE_4G] = 0.0005,
                             [ADXL367_RANGE_8G] = 0.001};
/*
 * Read current config
 */

void readConfig(Config *config)
{
  if (config == NULL)
    return;

  bzero(config, sizeof(*config));
  config->tag_type = TAG_TYPE;
  //config->period = sconfig.lps_period;
// Sensor configuration
    // convert from adxl values to configuration values
  int range = ADXL_RANGE(sconfig.adxl_filter_range_rate);
  int freq = ADXL_RATE(sconfig.adxl_filter_range_rate);
  int act_thresh = sconfig.adxl_act_thresh_cnt;
  int inact_thresh = sconfig.adxl_inact_thresh_cnt;
  int samples = sconfig.adxl_inactive_samples;   

  config->has_adxl362 = true;
  config->adxl362.range = Adxl367RngToEnum[range];
  config->adxl362.freq = Adxl367ODRToEnum[freq];
  config->adxl362.act_thresh_g = act_thresh * Sens[range];
  config->adxl362.inact_thresh_g = inact_thresh * Sens[range];
  config->adxl362.inactive_sec = samples * Tdelta[freq];
  config->adxl362.accel_type = Adxl362_AdxlType_367;
  config->has_active_interval = true;
  config->active_interval.start_epoch = sconfig.start;
  config->active_interval.end_epoch = sconfig.stop;

  config->hibernate_count = 2; // number of hibernation messages

  for (int i = 0; i < 2; i++)
  {
    config->hibernate[i].start_epoch = sconfig.hibernate[i].start_epoch;
    config->hibernate[i].end_epoch = sconfig.hibernate[i].end_epoch;
  }
}

/*
 * Write config to ram
 */

bool writeConfig(Config *config)
{
   if ((config == NULL) || pState->state != TagState_IDLE)
    return false;

  int range = EnumToAdxl367Rng(config->adxl362.range);
  int freq = EnumToAdxl367ODR(config->adxl362.freq);


  if ((range < 0) || (freq < 0))
  {
    return false;
  }

  config_tmp.adxl_filter_range_rate = (range << 6) | (1 << 5) | freq;
  int thresh = config->adxl362.act_thresh_g / Sens[range];
  thresh = (thresh > 0x1fff) ? 0x1fff : thresh;
  config_tmp.adxl_act_thresh_cnt = thresh;
  
  thresh = config->adxl362.inact_thresh_g / Sens[range];
  thresh = (thresh > 0x1fff) ? 0x1fff : thresh;
  config_tmp.adxl_inact_thresh_cnt = thresh;

  int samples = config->adxl362.inactive_sec / Tdelta[freq];
  samples = samples > 255 ? 255 : samples;
  config_tmp.adxl_inactive_samples = samples;
  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;

  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }
  return true;
}
