#include <stdint.h>
#include <limits.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "ADXL362.h"
#include <pb_decode.h>
#include <string.h>

#define ADXL_RANGE(r) (((r) >> 6) & 3)

#define BITTAG_LE_RANGE ADXL362_RANGE_4G
#define BITTAG_LE_RATE ADXL362_ODR_50_HZ
#define BITTAG_LE_AA 1
#define BITTAG_LE_INACTIVE_SAMPLES_MIN 1
#define BITTAG_LE_INACTIVE_SAMPLES_MAX 255

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

extern const unsigned char tag_default_config[];
extern const unsigned int tag_default_config_len;

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

// See ADXL362 Data Sheet

static const float Sens[] = {[ADXL362_RANGE_2G] = 0.001,
                             [ADXL362_RANGE_4G] = 0.002,
                             [ADXL362_RANGE_8G] = 0.004};

static uint16_t clampAdxl362Threshold(float threshold_g, int range)
{
  int threshold = threshold_g / Sens[range];
  if (threshold < 0)
    threshold = 0;
  if (threshold > 0x7ff)
    threshold = 0x7ff;
  return (uint16_t)threshold;
}

static uint16_t clampBitTagLeInactivitySamples(float samples_f)
{
  int samples = samples_f + 0.5f;
  if (samples < BITTAG_LE_INACTIVE_SAMPLES_MIN)
    samples = BITTAG_LE_INACTIVE_SAMPLES_MIN;
  if (samples > BITTAG_LE_INACTIVE_SAMPLES_MAX)
    samples = BITTAG_LE_INACTIVE_SAMPLES_MAX;
  return (uint16_t)samples;
}

static void readDefaultConfig(Config *config)
{
  memset(config, 0, sizeof(*config));
  pb_istream_t istream = pb_istream_from_buffer(tag_default_config,
                                                tag_default_config_len);
  pb_decode(&istream, Config_fields, config);
}

void readConfig(Config *config)
{
  if (config == NULL)
    return;
  if ((pState->state == TagState_IDLE) || (pState->state == TagState_TEST))
  {
    readDefaultConfig(config);
  }
  else
  {
    // Tag type

    config->tag_type = TAG_TYPE;

    // Sensor configuration
    // convert from adxl values to configuration values
    int range = ADXL_RANGE(sconfig.adxl_filter_range_rate);
    if (range > ADXL362_RANGE_8G)
      range = BITTAG_LE_RANGE;
    int act_thresh = sconfig.adxl_act_thresh_cnt;
    int inact_thresh = sconfig.adxl_inact_thresh_cnt;
    int samples = sconfig.adxl_inactive_samples;   

    config->has_adxl362 = true;
    config->adxl362.act_thresh_g = act_thresh * Sens[range];
    config->adxl362.inact_thresh_g = inact_thresh * Sens[range];
    /*
     * BitTag_LE keeps the ADXL362 in wake-up mode. The historical protobuf
     * field carries an inactivity sample count at the wake-mode sample rate,
     * not seconds derived from the configured output data rate.
     */
    config->adxl362.inactive_sec = samples;

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

  memset(&config_tmp, 0, sizeof(config_tmp));

  config_tmp.adxl_filter_range_rate =
      (BITTAG_LE_RANGE << 6) | (BITTAG_LE_AA << 4) | BITTAG_LE_RATE;
  config_tmp.adxl_act_thresh_cnt =
      clampAdxl362Threshold(config->adxl362.act_thresh_g, BITTAG_LE_RANGE);
  config_tmp.adxl_inact_thresh_cnt =
      clampAdxl362Threshold(config->adxl362.inact_thresh_g, BITTAG_LE_RANGE);
  config_tmp.adxl_inactive_samples =
      clampBitTagLeInactivitySamples(config->adxl362.inactive_sec);
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
