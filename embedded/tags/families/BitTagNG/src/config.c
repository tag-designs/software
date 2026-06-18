#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "string.h"
#include "ADXL367.h"
#include <pb_decode.h>

#define ADXL367_ACT_THRESH_MIN_MG 1100
#define ADXL367_ACT_THRESH_MAX_MG 1500
#define ADXL367_INACTIVE_SAMPLES_MIN 1
#define ADXL367_INACTIVE_SAMPLES_MAX 5


// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;  

extern const unsigned char tag_default_config[];
extern const unsigned int tag_default_config_len;

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

static uint16_t clampAdxl367ActivityThreshold(float threshold_g)
{
  // Round positive g-to-mg conversion without pulling in libm roundf().
  int threshold_mg = threshold_g * 1000.0f + 0.5f;

  if (threshold_mg < ADXL367_ACT_THRESH_MIN_MG)
    threshold_mg = ADXL367_ACT_THRESH_MIN_MG;
  if (threshold_mg > ADXL367_ACT_THRESH_MAX_MG)
    threshold_mg = ADXL367_ACT_THRESH_MAX_MG;
  return (uint16_t)threshold_mg;
}

static uint16_t clampAdxl367InactivitySamples(float samples_f)
{
  // Round positive sample-count values without pulling in libm roundf().
  int samples = samples_f + 0.5f;

  if (samples < ADXL367_INACTIVE_SAMPLES_MIN)
    samples = ADXL367_INACTIVE_SAMPLES_MIN;
  if (samples > ADXL367_INACTIVE_SAMPLES_MAX)
    samples = ADXL367_INACTIVE_SAMPLES_MAX;
  return (uint16_t)samples;
}

static void readDefaultConfig(Config *config)
{
  bzero(config, sizeof(*config));
  pb_istream_t istream = pb_istream_from_buffer(tag_default_config,
                                                tag_default_config_len);
  pb_decode(&istream, Config_fields, config);
}

static void readStoredConfig(Config *config)
{
  bzero(config, sizeof(*config));
  config->tag_type = TAG_TYPE;

  // Sensor configuration. BitTagNG fixes ADXL367 range and sample rate.
  int act_thresh = sconfig.adxl_act_thresh_cnt;
  int samples = sconfig.adxl_inactive_samples;

  config->has_adxl362 = true;
  config->adxl362.act_thresh_g = act_thresh / 1000.0f;
  /*
   * BitTagNG reuses the existing ADXL config shape. For this ADXL367 wake-up
   * mode, inactive_sec is interpreted as an inactivity sample count at 6 Hz.
   */
  config->adxl362.inactive_sec = samples;
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
 * Read current config
 */

void readConfig(Config *config)
{
  if (config == NULL)
    return;

  if ((pState->state == TagState_IDLE) || (pState->state == TagState_TEST))
  {
    readDefaultConfig(config);
    return;
  }

  readStoredConfig(config);
}

/*
 * Write config to ram
 */

bool writeConfig(Config *config)
{
   if ((config == NULL) || pState->state != TagState_IDLE)
    return false;

  bzero(&config_tmp, sizeof(config_tmp));

  config_tmp.adxl_act_thresh_cnt =
      clampAdxl367ActivityThreshold(config->adxl362.act_thresh_g);
  /*
   * See readStoredConfig(): BitTagNG stores ADXL367 inactivity as samples even
   * though the protobuf field is named inactive_sec for older ADXL362 configs.
   */
  config_tmp.adxl_inactive_samples =
      clampAdxl367InactivitySamples(config->adxl362.inactive_sec);
  debug_log_printf("Configuring ADXL367 wake-up threshold %u mg, inactivity %u samples\r\n",
                   config_tmp.adxl_act_thresh_cnt,
                   config_tmp.adxl_inactive_samples);
  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;

  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }
  return true;
}
