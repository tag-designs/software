/**
 * @file config.c
 * @brief UIUCTag configuration persistence and protobuf conversion.
 * @author tag firmware authors
 * @date 2026-09-26
 */

#include <stdint.h>
#include "hal.h"
#include "app.h"
#include "tag.pb.h"
#include "config.h"
#include "persistent.h"
#include "strings.h"

/*
 * UIUCTag has no local config.h -- it shares the t_storedconfig layout and
 * Config/adxl362 protobuf shape with the rest of the BitPresTag family (see
 * ../families/BitPresTag/inc/config.h). Only the accelerometer field
 * conversions below differ, because UIUCTag is the one member of this
 * family wired to an ADXL367 (see sensors.c) rather than the family's
 * ADXL362. Reusing BitPresTag's own config.c unmodified was the bug this
 * file exists to fix: it converted g thresholds using the ADXL362's native
 * sensitivity table (0.002 g/LSB at its fixed 4G range), which is not the
 * scale sensors.c's inline register writes expect. sensors.c's inline
 * writes are a verbatim port of BitTagNG's ADXL367 driver, which stores the
 * activity threshold as raw milligrams and shifts it into the register
 * field assuming the ADXL367's own native 2G-range sensitivity (4 LSB/mg).
 * Reading a value produced by the wrong table applied a threshold exactly
 * half of what was requested (measured: requesting 0.498 g resulted in
 * 0.249 g actually being written to THRESH_ACT). This file restores
 * BitTagNG's conversion (see families/BitTagNG/src/config.c) so the
 * configured threshold matches what is actually applied.
 */
#define UIUCTAG_ADXL_ACT_THRESH_MIN_MG 200
#define UIUCTAG_ADXL_ACT_THRESH_MAX_MG 700
#define UIUCTAG_ADXL_INACTIVE_SAMPLES_MIN 3
#define UIUCTAG_ADXL_INACTIVE_SAMPLES_MAX 12

/*
 * The inactivity threshold is not a configuration input: sensors.c hardcodes
 * it to 1100 mg (UIUCTAG_ADXL_INACT_THRESH_MG there), matching BitTagNG's
 * own field-validated configuration verbatim -- see the "referenced
 * activity, absolute inactivity" note in sensors.c and
 * feedback-port-reference-verbatim. t_storedconfig.adxl_inact_thresh_cnt
 * exists in the shared struct but is deliberately left unset by
 * writeConfig() below, the same way BitTagNG's own writeConfig() leaves it
 * unset. readConfig() reports the true, hardcoded value here rather than
 * deriving a number from that unused field, so a host reading back the
 * configuration sees what is actually applied to the chip instead of a
 * value nothing enforces.
 */
#define UIUCTAG_ADXL_INACT_THRESH_MG 1100U

// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;
static const char *config_error;

/**
 * @brief Return the most recent UIUCTag configuration validation failure.
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

/**
 * @brief Convert and clamp a g threshold to the raw milligrams sensors.c
 *        expects.
 *
 * @param[in] threshold_g Threshold in g.
 * @return Threshold in whole milligrams, clamped to BitTagNG's field-
 *         validated range.
 */
static uint16_t clampAdxl367ActivityThreshold(float threshold_g)
{
  int threshold_mg = (int)(threshold_g * 1000.0f + 0.5f);

  if (threshold_mg < UIUCTAG_ADXL_ACT_THRESH_MIN_MG)
    threshold_mg = UIUCTAG_ADXL_ACT_THRESH_MIN_MG;
  if (threshold_mg > UIUCTAG_ADXL_ACT_THRESH_MAX_MG)
    threshold_mg = UIUCTAG_ADXL_ACT_THRESH_MAX_MG;
  return (uint16_t)threshold_mg;
}

/**
 * @brief Round and clamp the wake-mode inactivity sample count.
 *
 * @param[in] samples_f Inactivity sample count encoded in the historical
 *                      inactive_sec protobuf field (samples at the ADXL367
 *                      wake-mode rate, not seconds -- see sensors.c).
 * @return Inactivity sample count, clamped to BitTagNG's field-validated
 *         range.
 */
static uint16_t clampAdxl367InactivitySamples(float samples_f)
{
  int samples = (int)(samples_f + 0.5f);

  if (samples < UIUCTAG_ADXL_INACTIVE_SAMPLES_MIN)
    samples = UIUCTAG_ADXL_INACTIVE_SAMPLES_MIN;
  if (samples > UIUCTAG_ADXL_INACTIVE_SAMPLES_MAX)
    samples = UIUCTAG_ADXL_INACTIVE_SAMPLES_MAX;
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

  config->has_adxl362 = true;
  config->adxl362.act_thresh_g = sconfig.adxl_act_thresh_cnt / 1000.0f;
  /*
   * Not a configuration input -- see the file-level note above. Reports
   * the value sensors.c actually hardcodes, not one derived from the
   * unused adxl_inact_thresh_cnt field.
   */
  config->adxl362.inact_thresh_g = UIUCTAG_ADXL_INACT_THRESH_MG / 1000.0f;
  /*
   * Historical protobuf field name; sensors.c uses this as a sample count
   * at the ADXL367 wake-mode rate, not seconds.
   */
  config->adxl362.inactive_sec = sconfig.adxl_inactive_samples;

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
    config_error = "UIUCTag config request was empty";
    return false;
  }

  if (pState->state != TagState_IDLE)
  {
    config_error = "UIUCTag can only start from IDLE";
    return false;
  }

  if (!config->has_adxl362)
  {
    config_error = "UIUCTag config missing ADXL367 settings";
    return false;
  }

  if (!config->has_active_interval)
  {
    config_error = "UIUCTag config missing active interval";
    return false;
  }

  config_tmp.adxl_act_thresh_cnt =
      clampAdxl367ActivityThreshold(config->adxl362.act_thresh_g);
  config_tmp.adxl_inactive_samples =
      clampAdxl367InactivitySamples(config->adxl362.inactive_sec);

  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;

  if (config->hibernate_count >
      (pb_size_t)(sizeof(config_tmp.hibernate) / sizeof(config_tmp.hibernate[0])))
  {
    config_error = "UIUCTag config has too many hibernate intervals";
    return false;
  }

  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }
  return true;
}
