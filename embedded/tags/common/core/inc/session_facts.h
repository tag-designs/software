/**
 * @file    session_facts.h
 * @brief   Facts about a logging session, stored with the stored
 *          configuration so they share its lifetime.
 *
 * @details A family that opts in (TAG_STORED_CONFIG_HAS_SESSION, defined in its
 *          config.h) embeds a t_sessionFacts member named `session` in its
 *          t_storedconfig. The common state machine fills it just before the
 *          start command writes the configuration (state_machine.c). The
 *          stored configuration is erased when the data are cleared, so these
 *          facts are rewritten for every session and never describe an earlier
 *          one.
 *
 *          The first fact is the RV3028's factory clock offset (EEOffset). The
 *          tag applies it to its own RTC, so epochs are already corrected, but
 *          IMUTag samples run on the raw clock and the host scales their
 *          elapsed time by it. Until this release the offset left the tag only
 *          live, read over I2C into the tag-info reply at download time; a
 *          capture of a tag that could not talk had no way to recover it.
 *
 *          An erased record reads as all 0xFF, so `version` is
 *          0xFFFFFFFF until a start writes it.
 *
 * @see     design/offline-log-reconstruction.md, item 5 of "Decisions and plan"
 * @see     families/IMUTag/design/jitter-free-sampling-timing-reconstruction.md
 */

#ifndef SESSION_FACTS_H
#define SESSION_FACTS_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Layout version of t_sessionFacts; 0xFFFFFFFF means erased. */
#define TAG_SESSION_FACTS_VERSION 1U

/** @brief t_sessionFacts::flags bit: the RV3028 EEOffset was read successfully. */
#define TAG_SESSION_FLAG_RTC_OFFSET_VALID (1U << 0)

/**
 * @struct  t_sessionFacts
 * @brief   Session facts, embedded in a family's t_storedconfig.
 *
 * @details 16 bytes, a whole number of STM32L4 flash double-words, so adding
 *          it keeps a stored configuration that was a multiple of 8 bytes a
 *          multiple of 8. That matters: on STM32L4, FLASH_Program_Array()
 *          programs double-words and, given an odd word count, reads and
 *          programs one word past the end of the struct. persistent.c asserts
 *          the size for every family that opts in.
 */
typedef struct {
  uint32_t version;              ///< TAG_SESSION_FACTS_VERSION once written.
  int16_t rtc_offset_steps;      ///< RV3028 EEOffset, signed 9-bit steps.
  uint16_t flags;                ///< TAG_SESSION_FLAG_* bits.
  float rtc_offset_ppm;          ///< EEOffset in ppm, as tag-info reports it.
  uint32_t reserved;             ///< Written as 0; keeps the size 16 bytes.
} t_sessionFacts;

/**
 * @brief   Record the current session facts.
 *
 * @details Uses the RV3028 correction cached at RTC initialisation, so it
 *          performs no bus traffic. Called by the state machine immediately
 *          before the start command writes the stored configuration.
 *
 * @param[out] facts  Facts to fill in.
 */
void tagSessionFactsCapture(t_sessionFacts *facts);

/**
 * @brief   Report whether stored session facts hold a valid clock offset.
 *
 * @param[in] facts  Stored facts, normally `&sconfig.session`.
 * @return  true when the record was written by this layout version and its
 *          clock offset was read successfully.
 */
bool tagSessionFactsRtcOffsetValid(const t_sessionFacts *facts);

#endif /* SESSION_FACTS_H */
