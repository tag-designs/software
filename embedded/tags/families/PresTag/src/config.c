/**
 * @file config.c
 * @brief PresTag configuration persistence and protobuf conversion.
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

// ram based config (used by monitor to communicate to tag)

t_storedconfig config_tmp;  

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
  config->period = sconfig.lps_period;

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
 * @param[in] config Host-provided configuration message.
 * @return true when the configuration can be staged in the current state.
 */
bool writeConfig(Config *config)
{
  if ((config == NULL) || pState->state != TagState_IDLE)
    return false;

  config_tmp.start = config->active_interval.start_epoch;
  config_tmp.stop = config->active_interval.end_epoch;
  if (config->period) {
    config_tmp.lps_period = config->period;
  } else {
    return false;
  }

  for (int i = 0; i < config->hibernate_count; i++)
  {
    config_tmp.hibernate[i].start_epoch = config->hibernate[i].start_epoch;
    config_tmp.hibernate[i].end_epoch = config->hibernate[i].end_epoch;
  }
  return true;
}
