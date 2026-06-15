/**
 * @file datalog.h
 * @brief PresTag external-log record layout and log IO API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef DATALOG_H
#define DATALOG_H

/** Number of pressure samples grouped under one internal header. */
#define DATALOG_SAMPLES 60
/**
 * @brief External-flash payload record for one PresTag log page.
 *
 * One written t_DataHeader owns one t_DataLog in external flash.
 */
typedef struct {
  struct {
    int16_t pressure;
    int16_t temperature;
  } data[DATALOG_SAMPLES];
} t_DataLog;

/** @brief Internal-flash header that anchors one external log page. */
typedef struct {
  int32_t epoch;
  uint16_t vdd100[2];
} t_DataHeader;

/** Internal flash header array placed by the linker script. */
extern t_DataHeader vddHeader[];

/** @brief Append sample words to the external data log. */
extern enum LOGERR writeDataLog(uint16_t *data, int num);
/** @brief Write the next internal-flash log header. */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/** @brief Recover log cursors from internal flash after reset. */
extern int restoreLog(void);

#endif
