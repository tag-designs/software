/**
 * @file datalog.h
 * @brief IMUTagBreakout external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H
#include "sensors.h"
#include "assert.h"
#include "imutag_log_format.h"

/** @brief basic data log block -- data is written in 128 byte units */

typedef struct t_DataLog {
   int16_t pressure;    // pressure sensor
   int16_t mx, my, mz;  // magnetometer
   RawSensorData raw_data[10];
} t_DataLog;

static_assert(sizeof(t_DataLog) == 128, "imudata size must be exactly 128 bytes!");

/** Number of datalog blocks under one internal header. */
#define DATALOG_SAMPLES 16

/** @brief Internal-flash header that anchors one external log page. */
typedef struct {
  int32_t epoch;
  uint16_t millis;
  int16_t rawtemp;
} t_DataHeader;

/** Internal flash header array placed by the linker script. */
extern t_DataHeader vddHeader[];

/** @brief Append block to datalog */
extern enum LOGERR writeDataLog(t_DataLog *data);
/** @brief Write the next internal-flash log header. */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/** @brief Recover log cursors from internal flash after reset. */
extern int restoreLog(void);

#endif
