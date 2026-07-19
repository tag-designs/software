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

typedef t_ImuTagDataLog t_DataLog;
typedef t_ImuTagDataHeader t_DataHeader;

#if !defined(IMUTAG_STM32U3_FLASH)
#if defined(STM32U3XX) || defined(STM32U375xx) || defined(STM32U385xx) || defined(BOARD_IMUTagU375)
#define IMUTAG_STM32U3_FLASH 1
#else
#define IMUTAG_STM32U3_FLASH 0
#endif
#endif

#if IMUTAG_STM32U3_FLASH
typedef struct {
  t_DataHeader header;
  uint32_t flash_padding[2];
} t_InternalDataHeader __attribute__((aligned(16)));
#else
typedef t_DataHeader t_InternalDataHeader;
#endif

static_assert(sizeof(t_DataLog) == IMUTAG_PAGE_SIZE,
              "IMUTag data page must be exactly 2048 bytes!");

/** Number of external log pages under one internal header. */
#define DATALOG_SAMPLES 1

/** Internal flash header array placed by the linker script. */
extern t_InternalDataHeader vddHeader[];

/** @brief Append a complete page to datalog. */
extern enum LOGERR writeDataLog(t_DataLog *data);
/** @brief Write the current external page header before collecting samples. */
extern enum LOGERR writeDataLogPageHeader(t_DataHeader *head);
/** @brief Write the current external page header and first superframe. */
extern enum LOGERR writeDataLogPageStart(t_DataHeader *head,
                                         const t_ImuTagSuperFrame *frame);
/** @brief Write one superframe into the current external page. */
extern enum LOGERR writeDataLogSuperFrame(uint16_t frame_index,
                                          const t_ImuTagSuperFrame *frame);
/** @brief Commit any staged external page cache to storage. */
extern enum LOGERR commitDataLogPage(void);
/** @brief Write the next internal-flash log header. */
extern enum LOGERR writeDataHeader(t_DataHeader *head);
/** @brief Recover log cursors from internal flash after reset. */
extern int restoreLog(void);

#endif
