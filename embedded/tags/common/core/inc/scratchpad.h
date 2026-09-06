/**
 * @file    scratchpad.h
 * @brief   Retained debug scratchpad in SRAM2 page 3, readable after the fact.
 *
 * @details A tag in the field has no console. The monitor cannot be attached
 *          while measuring sleep, `debug_log_printf()` is compiled out of
 *          shipped images, and a probe that runs code in the power path
 *          changes the behaviour being observed. This gives firmware somewhere
 *          to write that survives both a reset and Standby, and that a host
 *          can read out afterwards over SWD.
 *
 *          The region is the last 8 KB of SRAM2, held out of `ram0` by the
 *          linker script so `crt0` never clears it. Reading it back resets the
 *          tag, which does not matter: a reset does not clear SRAM, so the
 *          contents survive the readout.
 *
 *          What it is for: a tag that failed to sleep, crashed, or is wedged.
 *          Those never lose SRAM, so the log is intact exactly when it is
 *          wanted. Verified: a build that stalled instead of entering Standby
 *          read back its complete log afterwards.
 *
 * @note    The region also survives a *successful* Standby, which powers SRAM2
 *          down unless `PWR_CR1_RRSB3` retains it. tagScratchRetain() sets
 *          that bit. Verified by A/B at one commit, three Standby cycles each,
 *          with idle current confirming the part really slept: armed, the page
 *          came back with its magic intact and `seq` counting every boot;
 *          not armed, it came back as noise. An earlier note here called this
 *          unreliable -- that predates the Standby entry fix, and a build that
 *          failed to sleep retained the page trivially, which reads as success.
 *
 *          What goes in it is up to the program. Messages are the general
 *          case; `tagScratchWord()` suits a value you want to watch across a
 *          transition; a register scan is one specific use, not the purpose.
 *
 * Enable with `-DTAG_SCRATCHPAD=1` in the target's project.mk. When disabled
 * every entry point compiles to nothing.
 *
 * @warning Do not log from inside `tagPowerEnterStandby()`. Not because that
 *          window is special -- the layout sensitivity that made it look so is
 *          fixed by the `noinline` on that function -- but because writing
 *          there changes the image, so whatever you measure is not the build
 *          you ship. Capture at boot instead. See
 *          embedded/tags/design/open-issues.md.
 *
 * Read it back with:
 * @code
 *   STM32_Programmer_CLI -c port=SWD mode=UR -u 0x2003E000 8192 scratch.bin
 *   embedded/tools/decode_scratchpad.py scratch.bin
 * @endcode
 *
 * @see embedded/tools/decode_scratchpad.py, embedded/tags/design/debugging.md
 */
#ifndef TAG_SCRATCHPAD_H
#define TAG_SCRATCHPAD_H

#include <stdint.h>

#if defined(TAG_SCRATCHPAD) && TAG_SCRATCHPAD

/** @brief Base of the reserved region: SRAM2 page 3, the last 8 KB. */
#define TAG_SCRATCH_BASE  0x2003E000U
/** @brief Size of the reserved region. */
#define TAG_SCRATCH_SIZE  0x2000U
/** @brief Identifies a formatted scratchpad. "SCR3" little-endian. */
#define TAG_SCRATCH_MAGIC 0x33524353U
/** @brief Bytes available for content after the header. */
#define TAG_SCRATCH_BYTES (TAG_SCRATCH_SIZE - 32U)

/**
 * @brief Header at the base of the region.
 *
 * @details `used` is a byte offset into `data`, so a decoder needs no
 *          knowledge of what the program wrote. `seq` counts formats, which
 *          distinguishes a fresh boot from a reused buffer.
 */
typedef struct {
  uint32_t magic;    /**< TAG_SCRATCH_MAGIC once formatted. */
  uint32_t seq;      /**< Incremented each time the buffer is formatted. */
  uint32_t used;     /**< Bytes of @c data written so far. */
  uint32_t overflow; /**< Bytes discarded because the buffer was full. */
  uint32_t wrapped;  /**< Non-zero once a ring-mode buffer has wrapped. */
  uint32_t resv[3];  /**< Pad the header to 32 bytes. */
  uint8_t  data[TAG_SCRATCH_BYTES];
} tag_scratch_t;

#define tagScratch (*(volatile tag_scratch_t *)TAG_SCRATCH_BASE)

/** @brief Record kinds, written as the first byte of each record. */
enum TagScratchKind {
  TAG_SCRATCH_TEXT = 1, /**< NUL-terminated message. */
  TAG_SCRATCH_WORD = 2  /**< 4-byte label plus a uint32_t. */
};

/**
 * @brief Format the scratchpad if it does not already hold a valid header.
 *
 * @details Call once early in boot. The region is never cleared by the C
 *          runtime, so on a cold start it contains arbitrary bytes; without
 *          this the length fields would be garbage and the first write would
 *          scribble anywhere in the page.
 *
 * @post @c tagScratch is valid and empty, and @c seq has advanced.
 */
static inline void tagScratchInit(void)
{
  if (tagScratch.magic != TAG_SCRATCH_MAGIC) {
    tagScratch.seq = 0U;
  }
  tagScratch.seq = tagScratch.seq + 1U;
  tagScratch.used = 0U;
  tagScratch.overflow = 0U;
  tagScratch.wrapped = 0U;
  tagScratch.magic = TAG_SCRATCH_MAGIC;
}

/**
 * @brief Number of boots that have formatted or resumed this region.
 *
 * @return @c seq, which advances once per tagScratchInit() or
 *         tagScratchResume(). Zero before either has run.
 */
static inline uint32_t tagScratchSeq(void)
{
  return tagScratch.seq;
}

/**
 * @brief Keep any existing log and continue appending to it.
 *
 * @details The alternative to tagScratchInit() for faults that span resets.
 *          A tag that is being reset repeatedly -- by an attach storm, a
 *          watchdog, or restart recovery -- formats its scratchpad on every
 *          boot if it calls tagScratchInit(), so only the final boot survives
 *          to be read back, which is rarely the interesting one. This keeps
 *          the accumulated log, advances @c seq so boots remain countable, and
 *          lets @c overflow report anything lost once the buffer fills.
 *
 *          Falls back to formatting when the region does not hold a valid
 *          header, or when @c used is out of range -- a cold start leaves
 *          arbitrary bytes there, and a stale @c used would otherwise make
 *          every later append land in the overflow counter.
 *
 * @post @c tagScratch is valid and @c seq has advanced. Existing content and
 *       @c used are preserved when the header was intact.
 */
static inline void tagScratchResume(void)
{
  if ((tagScratch.magic != TAG_SCRATCH_MAGIC) ||
      (tagScratch.used > TAG_SCRATCH_BYTES)) {
    tagScratchInit();
    return;
  }
  tagScratch.seq = tagScratch.seq + 1U;
}

#ifndef TAG_SCRATCHPAD_RING
/**
 * @def TAG_SCRATCHPAD_RING
 * @brief Keep the newest records instead of the oldest when the buffer fills.
 *
 * @details Linear (the default) is right when the interesting event is near
 *          the start, such as a boot that never completes. Ring is right when
 *          it is near the end, such as a fault after thousands of resets: a
 *          storm boots the tag hundreds of times, and a linear buffer fills
 *          long before the failure and then discards exactly the records that
 *          matter.
 */
#define TAG_SCRATCHPAD_RING 0
#endif

/**
 * @brief Append one complete record, wrapping or discarding when full.
 *
 * @details Records are written whole. A record that would straddle the end of
 *          the buffer starts again at offset zero and the unused tail is
 *          marked with a zero kind byte, so a decoder reading from an
 *          arbitrary point can tell a gap from a record.
 *
 * @param[in] kind Record kind, a non-zero TagScratchKind value.
 * @param[in] p    Payload bytes.
 * @param[in] n    Payload length.
 *
 * @post In linear mode @c overflow counts anything dropped. In ring mode
 *       @c wrapped is set once the buffer has turned over, and @c used is the
 *       write cursor, which is also the oldest surviving record.
 */
static inline void tagScratchRecord(uint8_t kind, const uint8_t *p, uint32_t n)
{
  uint32_t used;
  uint32_t total = n + 1U;
  uint32_t i;

  if (tagScratch.magic != TAG_SCRATCH_MAGIC) {
    tagScratchInit();
  }
  used = tagScratch.used;
  if (total > TAG_SCRATCH_BYTES) {
    tagScratch.overflow = tagScratch.overflow + total;
    return;
  }
  if ((used + total) > TAG_SCRATCH_BYTES) {
#if TAG_SCRATCHPAD_RING
    if (used < TAG_SCRATCH_BYTES) {
      tagScratch.data[used] = 0U;    /* gap marker for the decoder */
    }
    tagScratch.wrapped = 1U;
    used = 0U;
#else
    tagScratch.overflow = tagScratch.overflow + total;
    return;
#endif
  }
  tagScratch.data[used] = kind;
  for (i = 0U; i < n; i++) {
    tagScratch.data[used + 1U + i] = p[i];
  }
  tagScratch.used = used + total;
}

/**
 * @brief Append a message.
 *
 * @param[in] s NUL-terminated string; the terminator is stored.
 *
 * @warning Plain stores only, and no formatting. Anything that waits, locks or
 *          allocates does not belong on a path worth instrumenting.
 */
static inline void tagScratchPuts(const char *s)
{
  uint8_t kind = (uint8_t)TAG_SCRATCH_TEXT;
  uint32_t n = 0U;

  while (s[n] != '\0') {
    n++;
  }
  tagScratchRecord(kind, (const uint8_t *)s, n + 1U);
}

/**
 * @brief Append a labelled 32-bit value.
 *
 * @param[in] label Four characters identifying the value, e.g. "LPMS".
 * @param[in] value Value to record.
 */
static inline void tagScratchWord(const char label[4], uint32_t value)
{
  uint8_t payload[8];
  uint32_t i;

  for (i = 0U; i < 4U; i++) {
    payload[i] = (uint8_t)label[i];
  }
  for (i = 0U; i < 4U; i++) {
    payload[4U + i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
  }
  tagScratchRecord((uint8_t)TAG_SCRATCH_WORD, payload, 8U);
}

/**
 * @brief Ask the power controller to retain the region through Standby.
 *
 * @details Sets `PWR_CR1_RRSB3`, which keeps SRAM2 page 3 powered through
 *          Standby. Without it the page comes back as noise; with it the
 *          contents survive intact. See the note on the file for the A/B that
 *          established this.
 *
 * @pre Must be called before the Standby arming sequence begins. It is called
 *      from the top of tagPowerEnterStandby(), which is as little work as the
 *      power path can do and still arm retention.
 */
static inline void tagScratchRetain(void)
{
  SET_BIT(PWR->CR1, PWR_CR1_RRSB3);
}

#else /* !TAG_SCRATCHPAD */

/*
 * The arguments are discarded with sizeof, which the language guarantees is
 * unevaluated, rather than with a cast to void. A cast evaluates, and these
 * macros are used on volatile operands -- pState is `volatile BackupState *`
 * -- where evaluation is a side effect the compiler must keep. Logging
 * `pState->state` through a (void) cast emitted a real load and shifted the
 * surrounding code, which defeats the point of a facility that is supposed to
 * leave the shipped image alone. sizeof still type-checks the expression.
 */
#define tagScratchInit()            do { } while (0)
#define tagScratchResume()          do { } while (0)
#define tagScratchSeq()             0U
#define tagScratchPuts(s)           do { (void)sizeof(s); } while (0)
#define tagScratchWord(label, v)    do { (void)sizeof(label); \
                                         (void)sizeof(v); } while (0)
#define tagScratchRetain()          do { } while (0)

#endif
#endif /* TAG_SCRATCHPAD_H */
