/**
 * @file    phase_probe.h
 * @brief   Retained per-sample phase timestamps for off-line power attribution.
 *
 * @details A current trace shows how much charge a sample costs and when, but
 *          not which code was running; a tag with no spare pins cannot toggle a
 *          marker either. This records the OS time (10 kHz system tick) at
 *          named points in the sample path into a fixed block of SRAM2, which
 *          no linker region uses and crt0 never clears, so it survives the
 *          reset that reading it back over SWD causes. Read it with
 *          @c STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0x10000000 N
 *          while the tag is awake between samples.
 *
 *          Every entry point compiles to nothing unless @c TAG_PHASE_PROBE is
 *          defined non-zero, so the calls can stay in place. Each mark is a
 *          single word store outside any power path.
 *
 * @note    SRAM2 is not retained through Shutdown on STM32L4, so collect at a
 *          sample period below 10 s, where the RUNNING state never Shuts down.
 */
#ifndef TAG_PHASE_PROBE_H
#define TAG_PHASE_PROBE_H

#include <stdint.h>

#if defined(TAG_PHASE_PROBE) && TAG_PHASE_PROBE
#include "ch.h"

/** @brief Base of the probe block: start of SRAM2 on STM32L432. */
#ifndef TAG_PHASE_PROBE_BASE
#define TAG_PHASE_PROBE_BASE 0x10000000U
#endif
/** @brief Identifies a formatted block. "PHPB" little-endian. */
#define TAG_PHASE_PROBE_MAGIC 0x42504850U
/** @brief Samples retained (ring). */
#define TAG_PHASE_PROBE_SLOTS 8U
/** @brief Marks per sample. */
#define TAG_PHASE_PROBE_MARKS 64U
/** @brief Auxiliary words per sample (counts, status bytes). */
#define TAG_PHASE_PROBE_AUX   8U

/** @brief Probe block layout; all words so a plain -r32 dump decodes it. */
typedef struct {
  uint32_t magic;                                          ///< TAG_PHASE_PROBE_MAGIC
  uint32_t seq;                                            ///< samples recorded
  uint32_t open;                                           ///< 1 while a record is open
  uint32_t loop_top;                                       ///< last main-loop entry time
  uint32_t adc_done;                                       ///< last adcVDD completion time
  uint32_t reserved;
  uint32_t marks[TAG_PHASE_PROBE_SLOTS][TAG_PHASE_PROBE_MARKS]; ///< system ticks, 0 = not reached
  uint32_t aux[TAG_PHASE_PROBE_SLOTS][TAG_PHASE_PROBE_AUX];     ///< per-sample counters
} tag_phase_probe_t;

#define tagPhaseProbe (*(volatile tag_phase_probe_t *)TAG_PHASE_PROBE_BASE)

/** @brief Start a new sample record; formats the block on first use. */
static inline void tagPhaseProbeBegin(void)
{
  if (tagPhaseProbe.magic != TAG_PHASE_PROBE_MAGIC) {
    tagPhaseProbe.magic = TAG_PHASE_PROBE_MAGIC;
    tagPhaseProbe.seq = 0U;
  }
  uint32_t s = tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS;
  for (uint32_t i = 0; i < TAG_PHASE_PROBE_MARKS; i++) tagPhaseProbe.marks[s][i] = 0U;
  for (uint32_t i = 0; i < TAG_PHASE_PROBE_AUX; i++) tagPhaseProbe.aux[s][i] = 0U;
  tagPhaseProbe.open = 1U;
  /* Carry the pre-sample loop timings into the record now being opened. */
  tagPhaseProbe.marks[s][12] = tagPhaseProbe.loop_top;
  tagPhaseProbe.marks[s][13] = tagPhaseProbe.adc_done;
}

/** @brief Note the main-loop entry time; copied into the next record. */
static inline void tagPhaseProbeLoopTop(void)
{
  uint32_t now = (uint32_t)chVTGetSystemTimeX();
  /* A record stays open across the five loop passes that follow the sample;
   * marks 24..28 are their loop-top times, so slow deferred work shows up. */
  if (tagPhaseProbe.magic == TAG_PHASE_PROBE_MAGIC && tagPhaseProbe.open) {
    uint32_t s = tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS;
    uint32_t k = tagPhaseProbe.open;          /* 1 on the sample pass */
    if (k <= 40U) tagPhaseProbe.marks[s][23U + k] = now;   /* marks 24..63 */
    if (k >= 40U) { tagPhaseProbe.open = 0U; tagPhaseProbe.seq++; }
    else tagPhaseProbe.open = k + 1U;
  }
  tagPhaseProbe.loop_top = now;
}

/** @brief Note the adcVDD completion time; copied into the next record. */
static inline void tagPhaseProbeAdcDone(void)
{
  tagPhaseProbe.adc_done = (uint32_t)chVTGetSystemTimeX();
}

/** @brief Record a mark only while a sample record is open. */
static inline void tagPhaseProbeMarkIfOpen(uint32_t idx);

/** @brief Record the current system time under mark @p idx. */
static inline void tagPhaseProbeMark(uint32_t idx)
{
  uint32_t s = tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS;
  if (idx < TAG_PHASE_PROBE_MARKS)
    tagPhaseProbe.marks[s][idx] = (uint32_t)chVTGetSystemTimeX();
}

/** @brief Record an auxiliary value (poll count, status) under @p idx. */
static inline void tagPhaseProbeAux(uint32_t idx, uint32_t v)
{
  uint32_t s = tagPhaseProbe.seq % TAG_PHASE_PROBE_SLOTS;
  if (idx < TAG_PHASE_PROBE_AUX)
    tagPhaseProbe.aux[s][idx] = v;
}

static inline void tagPhaseProbeMarkIfOpen(uint32_t idx)
{
  if (tagPhaseProbe.magic == TAG_PHASE_PROBE_MAGIC && tagPhaseProbe.open == 1U)
    tagPhaseProbeMark(idx);   /* sample pass only; later passes must not overwrite */
}

/** @brief Close the current sample record. */
static inline void tagPhaseProbeEnd(void)
{
  if (tagPhaseProbe.open) { tagPhaseProbe.open = 0U; tagPhaseProbe.seq++; }
}
#else
#define tagPhaseProbeBegin()          ((void)0)
#define tagPhaseProbeMark(idx)        ((void)0)
#define tagPhaseProbeMarkIfOpen(idx)  ((void)0)
#define tagPhaseProbeAux(idx, v)      ((void)0)
#define tagPhaseProbeEnd()            ((void)0)
#define tagPhaseProbeLoopTop()        ((void)0)
#define tagPhaseProbeAdcDone()        ((void)0)
#endif

#endif /* TAG_PHASE_PROBE_H */
