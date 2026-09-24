/**
 * @file datalog.h
 * @brief CompassTag external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H
#include "sensors.h"

/** Number of sample blocks under one internal header. */
#define DATALOG_SAMPLES 10

/** Samples per external-log block, and per packed activity word. */
#define SAMPLES_PER_BLOCK 3
/** Bit width of one sample's packed activity field within the block's
 *  activity word (LSB-first): SAMPLES_PER_BLOCK * ACTIVITY_BITS_PER_SAMPLE
 *  must fit within the word's bit width (16). */
#define ACTIVITY_BITS_PER_SAMPLE 5
/** CompassTag sample interval, in seconds. Single source of truth for
 *  state_run.c's ticker and datalog.c's activity-percentage decode; also
 *  reported to the host as CompassTagLog.sample_period_s so host code
 *  doesn't have to duplicate this as its own literal. */
#define COMPASS_SAMPLE_PERIOD_S 30

/**
 * @brief External-flash payload record for CompassTag sensor data.
 *
 * @details Each block's `activity` word packs SAMPLES_PER_BLOCK fields of
 *          ACTIVITY_BITS_PER_SAMPLE bits each, one per sample in
 *          `sensors[]`: field j holds the number of seconds (0..
 *          COMPASS_SAMPLE_PERIOD_S) the accelerometer wake line was active
 *          during that sample's tick.
 */
typedef struct {
  struct {
    RawSensorData sensors[SAMPLES_PER_BLOCK];
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
