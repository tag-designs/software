/**
 * @file config.h
 * @brief CompassTag stored-configuration layout and monitor translation API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CONFIG_H
#define CONFIG_H

/** Tag type reported in monitor configuration messages. */
#define TAG_TYPE COMPASSTAG

/** @brief Hibernation window stored in flash configuration. */
typedef struct {
  int32_t start_epoch;
  int32_t end_epoch;
} hibernate_t;

/** @brief Flash-resident CompassTag acquisition schedule. */
typedef struct
{
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

/**
 * @brief Store magnetometer calibration constants from a host message.
 *
 * @param[in] constants Host calibration message.
 * @return Encoded ACK result length or error response length.
 */
extern int write_calibration(CalibrationConstants *);
/**
 * @brief Read magnetometer calibration constants into a host ACK.
 *
 * @param[in] index Calibration slot to read, or negative for latest.
 * @param[out] ack ACK message to populate.
 * @return Encoded ACK result length or error response length.
 */
extern int read_calibration(int32_t, Ack *);

#endif /* CONFIG_H */
