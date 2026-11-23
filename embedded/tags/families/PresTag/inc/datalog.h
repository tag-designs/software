/**
 * @file datalog.h
 * @brief PresTag external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H

#include "prestag_log_format.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 1 when this build honours Config.lps27 (also defined in config.h):
 *        PresTag does, PresTagRaw, whose download format is fixed, does not.
 */
#ifndef PRESTAG_CONFIGURABLE
#if defined(PRESTAG_RAW_LOG) && PRESTAG_RAW_LOG
#define PRESTAG_CONFIGURABLE 0
#else
#define PRESTAG_CONFIGURABLE 1
#endif
#endif

/** Samples per page in PresTag's default 4-byte format. */
#define DATALOG_SAMPLES PRESTAG_LOG_SAMPLES
/**
 * @brief External-flash payload record for one PresTag log page.
 *
 * One written t_DataHeader owns one t_DataLog in external flash. The page is
 * 240 bytes in both formats: 60 four-byte samples (t_PresTagRawSample) or, in
 * high resolution, 48 five-byte samples (t_PresTagHRSample). databuf is
 * this type; a high-resolution page is copied out of it as
 * t_PresTagHRDataLog.
 */
typedef t_PresTagDataLog t_DataLog;

_Static_assert(sizeof(t_PresTagHRDataLog) == sizeof(t_PresTagDataLog),
               "both PresTag formats must fill the same 240-byte page");

/** @brief Internal-flash header that anchors one external log page. */
typedef struct {
  int32_t epoch;
  uint16_t vdd100[2];
} t_DataHeader;

/** Internal flash header array placed by the linker script. */
extern t_DataHeader vddHeader[];

/** @brief Append sample words to the external data log. */
extern enum LOGERR writeDataLog(uint16_t *data, int num);

#if PRESTAG_CONFIGURABLE
/**
 * @brief True when the stored configuration selects full-resolution pages.
 *
 * @return sconfig.lps_high_res == 1; an erased configuration reads false.
 */
extern bool prestagHighResolution(void);
/**
 * @brief True when the stored configuration selects low-noise conversion.
 *
 * @return sconfig.lps_low_noise == 1; an erased configuration reads false.
 */
extern bool prestagLowNoise(void);
/**
 * @brief Append one full-resolution sample at the current log cursor.
 *
 * @param[in] sample PRESTAG_HR_SAMPLE_BYTES bytes in t_PresTagHRSample layout.
 * @return LOGWRITE_FULL past the end of external flash, else LOGWRITE_OK.
 */
extern enum LOGERR writeDataSample(const uint8_t *sample);
#else
#define prestagHighResolution() false
#define prestagLowNoise() false
#endif

/**
 * @brief Samples per page for this run: 48 in high resolution, else 60.
 *
 * @details The log cursor pState->external_blocks counts samples, and header
 *          i owns page i, so every page-boundary rule (restoreLog(), the
 *          round-up after a reset, hibernation, the drift check) uses this.
 *          It is constant for a run because the stored configuration is.
 */
#define prestagSamplesPerPage() \
  (prestagHighResolution() ? PRESTAG_HR_LOG_SAMPLES : DATALOG_SAMPLES)
/** @brief Write the next internal-flash log header. */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/** @brief Recover log cursors from internal flash after reset. */
extern int restoreLog(void);

#endif
