/**
 * @file config.h
 * @brief PresTag stored-configuration layout and protobuf translation API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "session_facts.h"

/** @brief This family's t_storedconfig carries t_sessionFacts `session`. */
#define TAG_STORED_CONFIG_HAS_SESSION 1

/** Tag type reported in monitor configuration messages. */
#define TAG_TYPE PRESTAG

/**
 * @brief 1 when this build honours Config.lps27: PresTag does, PresTagRaw,
 *        whose download format is fixed, does not.
 */
#ifndef PRESTAG_CONFIGURABLE
#if defined(PRESTAG_RAW_LOG) && PRESTAG_RAW_LOG
#define PRESTAG_CONFIGURABLE 0
#else
#define PRESTAG_CONFIGURABLE 1
#endif
#endif

/** @brief Hibernation window stored in flash configuration. */
typedef struct {
  int32_t start_epoch;
  int32_t end_epoch;
} hibernate_t;

/** @brief Flash-resident PresTag acquisition schedule. */
typedef struct
{
  int32_t  start;
  int32_t  stop;
  hibernate_t hibernate[2];
  uint32_t lps_period;
  bool internal;
  /**
   * 1: convert in LPS27 low-noise mode (Config.lps27.low_noise). Only the
   * value 1 counts, so an erased configuration (0xFF) reads as off. With
   * lps_high_res, occupies what was padding before session, so no other
   * member moves. Stored-config layout 3.
   */
  uint8_t lps_low_noise;
  /**
   * 1: log full-resolution pages, 48 five-byte samples
   * (Config.lps27.high_resolution); otherwise 60 four-byte samples. Fixed for
   * a run: the page geometry, the log cursor and the download all follow it.
   */
  uint8_t lps_high_res;
  t_sessionFacts session; ///< Session facts; filled at start (session_facts.h).
} t_storedconfig __attribute__ ((aligned (8)));

extern t_storedconfig sconfig;
extern t_storedconfig config_tmp;

/** @brief Persist a RAM configuration image into the flash configuration slot. */
extern bool writeStoredConfig(t_storedconfig *s);
/** @brief Translate a host protobuf configuration into the RAM staging image. */
extern bool writeConfig(Config *config);
/** @brief Translate the stored flash configuration into a host protobuf message. */
extern void readConfig(Config *config);

#endif /* CONFIG_H */
