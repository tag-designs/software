#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "ADXL362.h"

#define ADXL_RANGE(r) (((r) >> 6) & 3)
#define ADXL_RATE(r) ((r)&7)
#define ADXL_AA(r) (((r) >> 4) & 1)

#if 0
const Config defaultConfig = {.tag_type = TAG_TYPE,
                              .has_adxl362 = true,
                              .adxl362 = {.range = Adxl362_R4G,
                                          .freq = Adxl362_S50,
                                          .filter = Adxl362_AAquarter,
                                          .act_thresh_g = 0.35,
                                          .inact_thresh_g = 0.35,
                                          .inactive_sec = 0.24},
                              .has_active_interval = true,
                              .active_interval = {
                                  .start_epoch = 0,
                                  .end_epoch = INT32_MAX},
                              .bittag_log = BitTagLogFmt_BITTAG_BITSPERFIVEMIN,
                              .hibernate_count = 2,
                              .hibernate = {{INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX}}};
#endif
t_storedconfig config_tmp;

// = STORED_CONFIG_DEFAULT;

void writeStoredConfig(t_storedconfig *s)
{
  uint32_t *src = (uint32_t *)s;
  uint32_t *dest = (uint32_t *)&sconfig;
  if (s)
  {
    chSysLock();
    FLASH_Unlock();
    FLASH_Program_Array(dest, src, sizeof(*s) / 4);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
  }
}

// Translation between BitTag constants and ProtoBuf constants

static const Adxl362_Rng Adxl362RngToEnum[] = {
    [ADXL362_RANGE_2G] = Adxl362_R2G,
    [ADXL362_RANGE_4G] = Adxl362_R4G,
    [ADXL362_RANGE_8G] = Adxl362_R8G};

static const Adxl362_Odr Adxl362ODRToEnum[] = {
    [ADXL362_ODR_12_5_HZ] = Adxl362_S12_5,
    [ADXL362_ODR_25_HZ] = Adxl362_S25,
    [ADXL362_ODR_50_HZ] = Adxl362_S50,
    [ADXL362_ODR_100_HZ] = Adxl362_S100,
    [ADXL362_ODR_200_HZ] = Adxl362_S200,
    [ADXL362_ODR_400_HZ] = Adxl362_S400};

static const Adxl362_Aa Adxl362FilterToEnum[] = {
    [0] = Adxl362_AAhalf,
    [1] = Adxl362_AAquarter};

// Translation betwee ProtoBuf constants and BitTag constants

static int EnumToAdxl362Filter(Adxl362_Aa a)
{
  switch (a)
  {
  case Adxl362_AAhalf:
    return 0;
  case Adxl362_AAquarter:
    return 1;
  default:
    return -1;
  };
}

static int EnumToAdxl362Rng(Adxl362_Rng rng)
{
  switch (rng)
  {
  case Adxl362_R2G:
    return ADXL362_RANGE_2G;
  case Adxl362_R4G:
    return ADXL362_RANGE_4G;
  case Adxl362_R8G:
    return ADXL362_RANGE_8G;
  default:
    return -1;
  };
}

static int EnumToAdxl362ODR(Adxl362_Odr odr)
{
  switch (odr)
  {
  case Adxl362_S12_5:
    return ADXL362_ODR_12_5_HZ;
  case Adxl362_S25:
    return ADXL362_ODR_25_HZ;
  case Adxl362_S50:
    return ADXL362_ODR_50_HZ;
  case Adxl362_S100:
    return ADXL362_ODR_100_HZ;
  case Adxl362_S200:
    return ADXL362_ODR_200_HZ;
  case Adxl362_S400:
    return ADXL362_ODR_400_HZ;
  default:
    return -1;
  };
}

// See ADXL362 Data sheet
static const float Tdelta[] = {[ADXL362_ODR_12_5_HZ] = 1 / 12.5,
                               [ADXL362_ODR_25_HZ] = 1 / 25.0,
                               [ADXL362_ODR_50_HZ] = 1 / 50.0,
                               [ADXL362_ODR_100_HZ] = 1 / 100.0,
                               [ADXL362_ODR_200_HZ] = 1 / 200.0,
                               [ADXL362_ODR_400_HZ] = 1 / 400.0};

// See ADXL362 Data Sheet

static const float Sens[] = {[ADXL362_RANGE_2G] = 0.001,
                             [ADXL362_RANGE_4G] = 0.002,
                             [ADXL362_RANGE_8G] = 0.004};

void readConfig(Config *config)
{
  if (config == NULL)
    return;
  if ((pState->state == TagState_IDLE) || (pState->state == TagState_TEST))
  {
    //bcopy(&defaultConfig, config, sizeof(Config));
  }
  else
  {
    // Tag type

    config->tag_type = TAG_TYPE;

    // Sensor configuration
    // convert from adxl values to configuration values
    int range = ADXL_RANGE(sconfig.adxl_filter_range_rate);
    int freq = ADXL_RATE(sconfig.adxl_filter_range_rate);
    int filter = ADXL_AA(sconfig.adxl_filter_range_rate);
    int act_thresh = sconfig.adxl_act_thresh_cnt;
    int inact_thresh = sconfig.adxl_inact_thresh_cnt;
    int samples = sconfig.adxl_inactive_samples;   

    config->has_adxl362 = true;
    config->adxl362.range = Adxl362RngToEnum[range];
    config->adxl362.freq = Adxl362ODRToEnum[freq];
    config->adxl362.filter = Adxl362FilterToEnum[filter];
    config->adxl362.act_thresh_g = act_thresh * Sens[range];
    config->adxl362.inact_thresh_g = inact_thresh * Sens[range];
    config->adxl362.inactive_sec = samples * Tdelta[freq];

    // Active interval 

    config->has_active_interval = true;
    config->active_interval.start_epoch = sconfig.start;
    config->active_interval.end_epoch = sconfig.stop;

    // Data Format

    config->bittag_log = sconfig.internal_format;

    // Hibernation  

    config->hibernate_count = 2;  // number of hibernation messages
    
    for (int i = 0; i < 2; i++)
    {
      config->hibernate[i].start_epoch = sconfig.hibernate[i].start_epoch;
      config->hibernate[i].end_epoch = sconfig.hibernate[i].end_epoch;
    }
  }
}

bool writeConfig(Config *config)
{
  if ((config == NULL) || pState->state != TagState_IDLE)
    return false;

  int range = EnumToAdxl362Rng(config->adxl362.range);
  int freq = EnumToAdxl362ODR(config->adxl362.freq);
  int Aa = EnumToAdxl362Filter(config->adxl362.filter);

  if ((range < 0) || (freq < 0) || (Aa < 0))
  {
    return false;
  }

  config_tmp.adxl_filter_range_rate = (range << 6) | (Aa << 4) | freq;
  int thresh = config->adxl362.act_thresh_g / Sens[range];
  thresh = (thresh > 0x7ff) ? 0x7ff : thresh;
  config_tmp.adxl_act_thresh_cnt = thresh;
  
  thresh = config->adxl362.inact_thresh_g / Sens[range];
  thresh = (thresh > 0x7ff) ? 0x7ff : thresh;
  config_tmp.adxl_inact_thresh_cnt = thresh;

  int samples = config->adxl362.inactive_sec / Tdelta[freq];
  samples = samples > 255 ? 255 : samples;
  config_tmp.adxl_inactive_samples = samples;
  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;
  config_tmp.internal_format = config->bittag_log;
  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }

  return true;
}
