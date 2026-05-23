/**
 * @file datalog.h
 * @brief CompassTag external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H
#include "sensors.h"

/** Number of one-minute sample groups under one internal header. */
#define DATALOG_SAMPLES 10

/** @brief External-flash payload record for CompassTag sensor data. */
typedef struct {
  struct { 
   RawSensorData sensors[4];
    uint16_t activity;
  } data[DATALOG_SAMPLES];
} t_DataLog;

/** @brief Internal-flash header that anchors one external log page. */
typedef struct {
  int32_t epoch;
  uint16_t vdd100;
  uint16_t temp10;
} t_DataHeader;

/** Internal flash header array placed by the linker script. */
extern t_DataHeader vddHeader[];

/**
 * @brief Append sample words to the external data log.
 *
 * @param[in] data Words to write.
 * @param[in] num Number of 16-bit words to write.
 * @return Log write status for capacity, battery, or flash failures.
 */
extern enum LOGERR writeDataLog(uint16_t *data, int num);
/**
 * @brief Write the next internal-flash log header.
 *
 * @param[in] head Header to persist.
 * @return Log write status for capacity, battery, or flash failures.
 */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/**
 * @brief Recover log cursors from internal flash after reset.
 *
 * @return Best recovered log timestamp, or 0 for targets without one.
 */
extern int restoreLog(void);

#endif
