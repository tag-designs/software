/**
 * @file config.c
 * @brief BitPresTag configuration persistence and protobuf conversion.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "strings.h"
#include "ADXL362.h"


#define ADXL_RANGE(r) (((r) >> 6) & 3)

#define BITPRESTAG_WAKE_RANGE ADXL362_RANGE_4G
#define BITPRESTAG_WAKE_RATE ADXL362_ODR_50_HZ
#define BITPRESTAG_WAKE_AA 1
#define BITPRESTAG_INACTIVE_SAMPLES_MIN 1
#define BITPRESTAG_INACTIVE_SAMPLES_MAX 255

// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;  
static const char *config_error;

/**
 * @brief Return the most recent BitPresTag configuration validation failure.
 *
 * @return Null when the last configuration staged successfully, otherwise a
 *         static diagnostic string suitable for monitor acknowledgements.
 */
const char *writeConfigErrorMessage(void)
{
  return config_error;
}

/**
 * @brief Write the staged configuration to internal flash.
 *
 * This is called when a start command should make the monitor-supplied
 * configuration durable across low-power transitions and resets.
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

// See ADXL362 Data Sheet

static const float Sens[] = {[ADXL362_RANGE_2G] = 0.001,
                           [ADXL362_RANGE_4G] = 0.002,
                           [ADXL362_RANGE_8G] = 0.004};

/**
 * @brief Convert and clamp a g threshold for the fixed BitPresTag wake range.
 *
 * @param[in] threshold_g Threshold in g.
 * @return ADXL362 threshold register count.
 */
static uint16_t clampAdxl362Threshold(float threshold_g)
{
  int threshold = threshold_g / Sens[BITPRESTAG_WAKE_RANGE];
  if (threshold < 0)
    threshold = 0;
  if (threshold > 0x7ff)
    threshold = 0x7ff;
  return (uint16_t)threshold;
}

/**
 * @brief Round and clamp the wake-mode inactivity sample count.
 *
 * @param[in] samples_f Inactivity sample count encoded in the historical
 *                      inactive_sec protobuf field.
 * @return ADXL362 inactivity time count.
 */
static uint16_t clampBitPresTagInactivitySamples(float samples_f)
{
  int samples = samples_f + 0.5f;
  if (samples < BITPRESTAG_INACTIVE_SAMPLES_MIN)
    samples = BITPRESTAG_INACTIVE_SAMPLES_MIN;
  if (samples > BITPRESTAG_INACTIVE_SAMPLES_MAX)
    samples = BITPRESTAG_INACTIVE_SAMPLES_MAX;
  return (uint16_t)samples;
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

  // Sensor configuration
  // convert from adxl values to configuration values
  int range = ADXL_RANGE(sconfig.adxl_filter_range_rate);
  if (range > ADXL362_RANGE_8G)
    range = BITPRESTAG_WAKE_RANGE;
  int act_thresh = sconfig.adxl_act_thresh_cnt;
  int inact_thresh = sconfig.adxl_inact_thresh_cnt;
  int samples = sconfig.adxl_inactive_samples;   

  config->has_adxl362 = true;
  config->adxl362.act_thresh_g = act_thresh * Sens[range];
  config->adxl362.inact_thresh_g = inact_thresh * Sens[range];
  /*
   * BitPresTag keeps the ADXL362 in wake-up mode. The historical protobuf
   * field carries an inactivity sample count at the wake-mode sample rate,
   * not seconds derived from the configured output data rate.
   */
  config->adxl362.inactive_sec = samples;

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

/**
 * @brief Validate and stage a host-provided configuration.
 *
 * The staged image is written to flash later by writeStoredConfig() when the
 * monitor starts acquisition.
 *
 * @param[in] config Host-provided configuration message.
 * @return true when the configuration can be staged in the current state.
 */
bool writeConfig(Config *config)
{
  config_error = NULL;

  if (config == NULL)
  {
    config_error = "BitPresTag config request was empty";
    return false;
  }

  if (pState->state != TagState_IDLE)
  {
    config_error = "BitPresTag can only start from IDLE";
    return false;
  }

  if (!config->has_adxl362)
  {
    config_error = "BitPresTag config missing ADXL362 settings";
    return false;
  }

  if (!config->has_active_interval)
  {
    config_error = "BitPresTag config missing active interval";
    return false;
  }

  config_tmp.adxl_filter_range_rate =
      (BITPRESTAG_WAKE_RANGE << 6) | (BITPRESTAG_WAKE_AA << 4) |
      BITPRESTAG_WAKE_RATE;
  config_tmp.adxl_act_thresh_cnt =
      clampAdxl362Threshold(config->adxl362.act_thresh_g);
  config_tmp.adxl_inact_thresh_cnt =
      clampAdxl362Threshold(config->adxl362.inact_thresh_g);
  config_tmp.adxl_inactive_samples =
      clampBitPresTagInactivitySamples(config->adxl362.inactive_sec);

  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;
 

  if (config->hibernate_count >
      (pb_size_t)(sizeof(config_tmp.hibernate) / sizeof(config_tmp.hibernate[0])))
  {
    config_error = "BitPresTag config has too many hibernate intervals";
    return false;
  }

  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }
  return true;
}
