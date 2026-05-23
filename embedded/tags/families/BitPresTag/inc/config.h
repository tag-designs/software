/**
 * @file config.h
 * @brief BitPresTag stored-configuration layout and protobuf translation API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CONFIG_H
#define CONFIG_H

/** Tag type reported in monitor configuration messages. */
#define TAG_TYPE BITPRESTAG

/** @brief Hibernation window stored in flash configuration. */
typedef struct {
  int32_t start_epoch;
  int32_t end_epoch;
} hibernate_t;

/**
 * @brief Flash-resident BitPresTag configuration.
 *
 * The structure is aligned to match the internal flash write granularity used
 * by writeStoredConfig().
 */
typedef struct
{
  uint16_t adxl_act_thresh_cnt;
  uint16_t adxl_inact_thresh_cnt;
  uint16_t adxl_inactive_samples;
  uint8_t  adxl_filter_range_rate;
  uint8_t  fill1;
  int32_t  start;
  int32_t  stop;
  hibernate_t hibernate[2];
  //bool internal;
} t_storedconfig __attribute__ ((aligned (8)));

extern t_storedconfig sconfig;
extern t_storedconfig config_tmp;

/**
 * @brief Persist a RAM configuration image into the flash configuration slot.
 *
 * @param[in] s Source configuration to write.
 */
extern void writeStoredConfig(t_storedconfig *s);
/**
 * @brief Translate a host protobuf configuration into the RAM staging image.
 *
 * @param[in] config Host-provided configuration message.
 * @return true when the configuration is valid for this tag state.
 */
extern bool writeConfig(Config *config);
/**
 * @brief Translate the stored flash configuration into a host protobuf message.
 *
 * @param[out] config Destination protobuf configuration message.
 */
extern void readConfig(Config *config);

#endif /* CONFIG_H */
