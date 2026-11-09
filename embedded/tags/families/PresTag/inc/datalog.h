/**
 * @file datalog.h
 * @brief PresTag external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H

#include "prestag_log_format.h"

#ifndef PRESTAG_HR_LOG
/** @brief 1 for PresTagHR: 48 five-byte full-resolution samples per page. */
#define PRESTAG_HR_LOG 0
#endif

#if PRESTAG_HR_LOG
/** Number of pressure samples grouped under one internal header. */
#define DATALOG_SAMPLES PRESTAG_HR_LOG_SAMPLES
/** Bytes per stored sample: 24-bit pressure, 16-bit temperature. */
#define DATALOG_SAMPLE_BYTES PRESTAG_HR_SAMPLE_BYTES
/**
 * @brief External-flash payload record for one PresTagHR log page.
 *
 * One written t_DataHeader owns one t_DataLog in external flash.
 */
typedef t_PresTagHRDataLog t_DataLog;
#else
/** Number of pressure samples grouped under one internal header. */
#define DATALOG_SAMPLES PRESTAG_LOG_SAMPLES
/** Bytes per stored sample: 16-bit pressure, 16-bit temperature. */
#define DATALOG_SAMPLE_BYTES 4
/**
 * @brief External-flash payload record for one PresTag log page.
 *
 * One written t_DataHeader owns one t_DataLog in external flash.
 */
typedef t_PresTagDataLog t_DataLog;
#endif

/** @brief Internal-flash header that anchors one external log page. */
typedef struct {
  int32_t epoch;
  uint16_t vdd100[2];
} t_DataHeader;

/** Internal flash header array placed by the linker script. */
extern t_DataHeader vddHeader[];

/** @brief Append sample words to the external data log. */
extern enum LOGERR writeDataLog(uint16_t *data, int num);
#if PRESTAG_HR_LOG
/** @brief Append one DATALOG_SAMPLE_BYTES sample to the external data log. */
extern enum LOGERR writeDataSample(const uint8_t *sample);
#endif
/** @brief Write the next internal-flash log header. */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/** @brief Recover log cursors from internal flash after reset. */
extern int restoreLog(void);

#endif
