/**
 * @file    edge_log.h
 * @brief   Retained log of ADXL362 AWAKE levels, one record per RUNNING wake.
 *
 * @details For the activity test stand. A running tag cannot be observed --
 *          attaching resets it -- so this records, on every RUNNING wake, the
 *          RTC time (1 ms resolution, the RTC's subsecond counter at
 *          PREDIV_S 1024) and the level of LINE_ACCEL_INT. Every AWAKE edge
 *          wakes the MCU from Standby, so a change of level between
 *          consecutive records is an edge, timestamped at the wake it caused;
 *          the minute-alarm wakes repeat the previous level. The block is read
 *          after the run, over SWD, from SRAM2 at 0x10000000 -- see
 *          Mechanical/ActivityTestStand/edge_log.py in the hardware repo.
 *
 *          SRAM2 is the linker's @c ram1, which no section uses and crt0 never
 *          clears, so the block survives the reset that reading it causes.
 *          It survives Standby only because @c TAG_RETAIN_SRAM2 sets
 *          @c PWR_CR3_RRS in pwr-l432.c, which costs Standby current: this is
 *          a timing build, never a power-measurement build.
 *
 *          Compiled in by @c make @c BITTAG_EDGE_LOG=1 (see project.mk); every
 *          entry point is a no-op otherwise.
 *
 * @note    The timestamp is taken at the top of the main loop after the wake,
 *          so it lags the edge by the Standby exit and boot time, a constant
 *          few milliseconds -- small against the ADXL362's ~160 ms wake-up
 *          mode sample period.
 */
#ifndef BITTAG_EDGE_LOG_H
#define BITTAG_EDGE_LOG_H

#include <stdint.h>
#include <stdbool.h>

#if defined(BITTAG_EDGE_LOG) && BITTAG_EDGE_LOG

/** @brief Base of the block: start of SRAM2 on STM32L432. */
#define EDGE_LOG_BASE   0x10000000U
/** @brief Identifies a formatted block. "EDGL" little-endian. */
#define EDGE_LOG_MAGIC  0x4C474445U
/** @brief Records retained (ring); fills the 16 KB of SRAM2. */
#define EDGE_LOG_SLOTS  2046U

/** @brief Record kinds, in bits 24..31 of @c info. */
enum EdgeLogKind {
  EDGE_LOG_WAKE = 0,   ///< a RUNNING wake (edge or minute alarm)
  EDGE_LOG_START = 1,  ///< Running(T_INIT): the run began
};

/** @brief One record: all words, so a plain 32-bit dump decodes it. */
typedef struct {
  int32_t sec;    ///< RTC Unix seconds
  uint32_t info;  ///< bits 0..9 milliseconds, bit 16 AWAKE level, bits 24..31 kind
} edge_log_rec_t;

/** @brief Block layout. */
typedef struct {
  uint32_t magic;                        ///< EDGE_LOG_MAGIC
  uint32_t count;                        ///< records written since the run began
  uint32_t slots;                        ///< EDGE_LOG_SLOTS, for the decoder
  uint32_t reserved;
  edge_log_rec_t rec[EDGE_LOG_SLOTS];    ///< ring, index count % slots
} edge_log_t;

_Static_assert(sizeof(edge_log_t) <= 16384U, "edge log exceeds SRAM2");

#define edgeLog (*(volatile edge_log_t *)EDGE_LOG_BASE)

/** @brief Append one record at the given time. */
static inline void edgeLogRecord(int32_t sec, uint32_t ms, bool level,
                                 enum EdgeLogKind kind)
{
  uint32_t i = edgeLog.count % EDGE_LOG_SLOTS;
  edgeLog.rec[i].sec = sec;
  edgeLog.rec[i].info = (ms & 0x3FFU) | ((level ? 1U : 0U) << 16) |
                        ((uint32_t)kind << 24);
  edgeLog.count = edgeLog.count + 1U;
}

/** @brief Format the block and record the run's start. */
static inline void edgeLogStart(int32_t sec, uint32_t ms, bool level)
{
  edgeLog.magic = EDGE_LOG_MAGIC;
  edgeLog.count = 0U;
  edgeLog.slots = EDGE_LOG_SLOTS;
  edgeLog.reserved = 0U;
  edgeLogRecord(sec, ms, level, EDGE_LOG_START);
}

/** @brief Record a RUNNING wake; ignored if the block was never formatted. */
static inline void edgeLogWake(int32_t sec, uint32_t ms, bool level)
{
  if (edgeLog.magic == EDGE_LOG_MAGIC)
    edgeLogRecord(sec, ms, level, EDGE_LOG_WAKE);
}

#else

static inline void edgeLogStart(int32_t sec, uint32_t ms, bool level)
{
  (void)sec; (void)ms; (void)level;
}

static inline void edgeLogWake(int32_t sec, uint32_t ms, bool level)
{
  (void)sec; (void)ms; (void)level;
}

#endif

#endif /* BITTAG_EDGE_LOG_H */
