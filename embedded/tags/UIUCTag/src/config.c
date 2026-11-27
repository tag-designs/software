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
 * Stored configuration (region stored_config), as readConfig() reads it.
 * The host rebuilds a download from an SWD capture by reading these at
 * these offsets (host/libraries/tagcore/recovery/capturesource.cc). Moving a
 * member is a layout change: bump the region's layout_version in the tag
 * identity record and teach that decoder the new layout. The host refuses a
 * layout version it does not know, but cannot see a change made without one.
 */
_Static_assert(offsetof(t_storedconfig, adxl_act_thresh_cnt) == 0, "UIUCTag stored_config layout: see capturesource.cc");
_Static_assert(offsetof(t_storedconfig, adxl_inactive_samples) == 4, "UIUCTag stored_config layout: see capturesource.cc");
_Static_assert(offsetof(t_storedconfig, start) == 8, "UIUCTag stored_config layout: see capturesource.cc");
_Static_assert(offsetof(t_storedconfig, stop) == 12, "UIUCTag stored_config layout: see capturesource.cc");
_Static_assert(offsetof(t_storedconfig, hibernate) == 16, "UIUCTag stored_config layout: see capturesource.cc");


/*
 * UIUCTag has no local config.h -- it shares the t_storedconfig layout and
 * Config/adxl362 protobuf shape with the rest of the BitPresTag family (see
 * ../families/BitPresTag/inc/config.h). Only the accelerometer field
 * conversions below differ. They define one contract that both fitted parts
 * implement -- the ADXL362 on UIUCTag (accel_adxl362.c) and the ADXL367 on
 * UIUCadxl367 (accel_adxl367.c) -- so the host sees the same configuration
 * whichever part is fitted:
 * - adxl_act_thresh_cnt holds the activity threshold in whole mg, clamped to
 *   BitTagNG's field-validated 200..700 mg;
 * - adxl_inactive_samples holds the inactivity time as a sample count at the
 *   ~6 Hz wake-up rate, clamped to 3..12;
 * - the inactivity threshold is fixed (below).
 *
 * The BitPresTag family's own config.c converts g thresholds with the
 * ADXL362's 4G-range sensitivity table, which is not the scale the accel_*.c
 * files expect. Reusing it unmodified was the bug this file exists to fix
 * (requesting 0.498 g wrote 0.249 g to the ADXL367's THRESH_ACT); this file
 * follows BitTagNG's conversion (see families/BitTagNG/src/config.c) instead.
 */
#define UIUCTAG_ADXL_ACT_THRESH_MIN_MG 200
#define UIUCTAG_ADXL_ACT_THRESH_MAX_MG 700
#define UIUCTAG_ADXL_INACTIVE_SAMPLES_MIN 3
#define UIUCTAG_ADXL_INACTIVE_SAMPLES_MAX 12

/*
 * The inactivity threshold is not a configuration input: accel_adxl362.c and
 * accel_adxl367.c hardcode it to 1100 mg (UIUCTAG_ADXL_INACT_THRESH_MG
 * there), matching BitTagNG's own field-validated configuration verbatim --
 * see the "referenced activity, absolute inactivity" note in those files. t_storedconfig.adxl_inact_thresh_cnt
 * exists in the shared struct but is deliberately left unset by
 * writeConfig() below, the same way BitTagNG's own writeConfig() leaves it
 * unset. readConfig() reports the true, hardcoded value here rather than
 * deriving a number from that unused field, so a host reading back the
 * configuration sees what is actually applied to the chip instead of a
 * value nothing enforces.
 */
#define UIUCTAG_ADXL_INACT_THRESH_MG 1100U

#if defined(TAG_SENSOR_ACCEL_ADXL367)
/** Accelerometer readConfig() reports: UIUCadxl367 carries an ADXL367. */
#define UIUCTAG_ACCEL_TYPE Adxl362_AdxlType_367
#else
/** Accelerometer readConfig() reports: UIUCTag carries an ADXL362. */
#define UIUCTAG_ACCEL_TYPE Adxl362_AdxlType_362
#endif
/* The host capture decoder reports this value too (capturesource.cc). */
_Static_assert(UIUCTAG_ADXL_INACT_THRESH_MG == 1100U, "UIUCTag reported inactivity threshold: see capturesource.cc");

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
bool writeStoredConfig(t_storedconfig *s)
{
  uint32_t *src = (uint32_t *)s;
  uint32_t *dest = (uint32_t *)&sconfig;
  ssize_t size = sizeof(*s)/4;

  if (s == NULL)
    return false;

  chSysLock();
  FLASH_Unlock();
  FLASH_Program_Array(dest, src, size);
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();

  /*
   * Verify, because the program can be refused and say nothing. sconfig
   * shares its page with sEpoch and the checkpoint headers here, so it cannot
   * be erased first, and flash programming only clears bits: a second write
   * over a populated region yields the bitwise AND of old and new. Returning
   * false aborts the start, which is the loud failure that replaces a tag
   * silently running a configuration nobody sent.
   */
  return storedConfigVerify(dest, src, (size_t)size);
}

/**
 * @brief Convert and clamp a g threshold to the raw milligrams the
 *        accel_*.c files expect.
 *
 * @param[in] threshold_g Threshold in g.
 * @return Threshold in whole milligrams, clamped to BitTagNG's field-
 *         validated range.
 */
static uint16_t clampActivityThresholdMg(float threshold_g)
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
 *                      inactive_sec protobuf field (samples at the ~6 Hz
 *                      wake-up rate, not seconds -- see accel_*.c).
 * @return Inactivity sample count, clamped to BitTagNG's field-validated
 *         range.
 */
static uint16_t clampInactivitySamples(float samples_f)
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
   * the value accel_*.c actually hardcode, not one derived from the
   * unused adxl_inact_thresh_cnt field.
   */
  config->adxl362.inact_thresh_g = UIUCTAG_ADXL_INACT_THRESH_MG / 1000.0f;
  /*
   * Historical protobuf field name; accel_*.c use this as a sample count
   * at the ~6 Hz wake-up rate, not seconds.
   */
  config->adxl362.inactive_sec = sconfig.adxl_inactive_samples;
  /*
   * Report the fitted part. Set explicitly even for the ADXL362, whose value
   * is the proto3 default, so this stays correct if either target changes.
   */
  config->adxl362.accel_type = UIUCTAG_ACCEL_TYPE;

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
    config_error = "UIUCTag config missing accelerometer settings";
    return false;
  }

  if (!config->has_active_interval)
  {
    config_error = "UIUCTag config missing active interval";
    return false;
  }

  config_tmp.adxl_act_thresh_cnt =
      clampActivityThresholdMg(config->adxl362.act_thresh_g);
  config_tmp.adxl_inactive_samples =
      clampInactivitySamples(config->adxl362.inactive_sec);

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
