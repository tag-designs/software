/**
 * @file monitor.c
 * @brief Protobuf monitor request evaluation and acknowledgement generation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "monitor.h"
#include <tag.pb.h>

#include "config.h"
#include "core_events.h"
#include "core_sync.h"
#include "core_types.h"
#include "custom.h"
#include "debug_log.h"
#include "flash_internal.h"
#include "persistent.h"
#include "rtc_api.h"
#include "sensor_calibration.h"
#include "test_support.h"
#include "timekeeping.h"
#include "version.h"

#include <pb_decode.h>
#include <pb_encode.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#if !defined(CONFIG_HAS_HIBERNATE)
#define CONFIG_HAS_HIBERNATE 1
#endif

#include "adc.h"

#define MAJOR_VERSION "1"
#define MINOR_VERSION "0"

/**
 * @brief Generate one page of data-log acknowledgements.
 *
 * @param[in] index First data-log entry to include.
 * @param[out] ack Acknowledgement object to populate.
 * @return Encoded byte count or tag-specific status.
 */
extern int data_logAck(int index, Ack *ack);

/**
 * @brief Generate a calibration-log acknowledgement when calibration is enabled.
 *
 * @param[out] ack Acknowledgement object to populate.
 * @return Encoded byte count or tag-specific status.
 */
extern int calibration_logAck(Ack *ack);

/** Default protobuf configuration supplied by the generated/tag build. */
extern const Config defaultConfig;

/** Size of the shared protobuf monitor buffer. */
extern const uint32_t protobuf_size;

/** Shared protobuf monitor buffer owned by handlers.c. */
extern uint8_t ProtoBuf[];
static Req req NOINIT;
static Ack ack NOINIT;
TestReq test_to_run NOINIT;
static uint16_t status_vdd100;
static int16_t status_temp10;

static bool monitor_acquisition_active(void);

__attribute__((weak)) const char *writeConfigErrorMessage(void)
{
  return NULL;
}

/**
 * @brief Capture voltage and temperature for status acknowledgements.
 *
 * @param[out] vdd100 Supply voltage in 0.01 V units.
 * @param[out] temp10 Temperature in 0.1 C units.
 */
static void monitorStatusMeasure(uint16_t *vdd100, int16_t *temp10)
{
  adcVDD(vdd100, temp10);
#if defined(TAG_STATUS_FIXED_VDD100)
  *vdd100 = TAG_STATUS_FIXED_VDD100;
#endif
}

#if !defined(TAG_RECOVERY_TRACE)
/** @brief Families without retained recovery-trace fields report nothing. */
#define TAG_RECOVERY_TRACE 0
#endif

#if (defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS) || \
    TAG_RECOVERY_TRACE
/** @brief Build the bounded string appenders shared by the diagnostic writers. */
#define TAG_STATUS_DIAG_HELPERS 1
#else
/** @brief No status diagnostic writer needs the string appenders. */
#define TAG_STATUS_DIAG_HELPERS 0
#endif

#if TAG_STATUS_DIAG_HELPERS
/**
 * @brief Append one character to a bounded diagnostic string.
 *
 * @param[in,out] dst Current write pointer.
 * @param[in] end One-past-last writable byte.
 * @param[in] c Character to append if space remains.
 * @return Updated write pointer.
 */
static char *statusDiagAppendChar(char *dst, char *end, char c)
{
  if (dst < end)
    *dst++ = c;
  return dst;
}

/**
 * @brief Append a null-terminated string to a bounded diagnostic buffer.
 *
 * @param[in,out] dst Current write pointer.
 * @param[in] end One-past-last writable byte.
 * @param[in] src Null-terminated text to append.
 * @return Updated write pointer.
 */
static char *statusDiagAppendString(char *dst, char *end, const char *src)
{
  while (*src)
    dst = statusDiagAppendChar(dst, end, *src++);
  return dst;
}

/**
 * @brief Append an unsigned integer to a bounded diagnostic buffer.
 *
 * @param[in,out] dst Current write pointer.
 * @param[in] end One-past-last writable byte.
 * @param[in] value Value to append in decimal.
 * @return Updated write pointer.
 */
static char *statusDiagAppendU32(char *dst, char *end, uint32_t value)
{
  char digits[10];
  int count = 0;

  do
  {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value != 0U);

  while (count > 0)
    dst = statusDiagAppendChar(dst, end, digits[--count]);
  return dst;
}

#endif /* TAG_STATUS_DIAG_HELPERS */

#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
/**
 * @brief Append an unsigned integer to a bounded diagnostic buffer in hex.
 *
 * @param[in,out] dst Current write pointer.
 * @param[in] end One-past-last writable byte.
 * @param[in] value Value to append in lowercase hexadecimal.
 * @return Updated write pointer.
 */
static char *statusDiagAppendHex(char *dst, char *end, uint32_t value)
{
  static const char digits[] = "0123456789abcdef";
  bool started = false;

  dst = statusDiagAppendString(dst, end, "0x");
  for (int shift = 28; shift >= 0; shift -= 4)
  {
    uint32_t nibble = (value >> (uint32_t)shift) & 0xfU;
    if ((nibble != 0U) || started || (shift == 0))
    {
      dst = statusDiagAppendChar(dst, end, digits[nibble]);
      started = true;
    }
  }
  return dst;
}

/**
 * @brief Recover the newest valid state/reason pair from the state marker log.
 *
 * @param[out] state Last valid TagState value, or STATE_UNSPECIFIED.
 * @param[out] reason Last valid State_Event value, or State_EVENT_UNSPECIFIED.
 */
static void statusDiagLastStateMarker(uint32_t *state, uint32_t *reason)
{
  *state = STATE_UNSPECIFIED;
  *reason = State_EVENT_UNSPECIFIED;

  for (size_t i = 0; i < sEPOCH_SIZE; i++)
  {
    t_StateMarker marker;
    if (FLASH_Read_Checked(&sEpoch[i], &marker, sizeof(marker)))
      break;
    if (marker.epoch == -1)
      break;
    if ((marker.state <= STATE_UNSPECIFIED) ||
        (marker.state > _TagState_MAX) ||
        (marker.reason > _State_Event_MAX))
      break;
    *state = marker.state;
    *reason = marker.reason;
  }
}

/**
 * @brief Write retained run diagnostics into the status debug-message field.
 */
static void statusDiagWrite(void)
{
  char *dst = ack.payload.status.debug_message;
  char *end = dst + sizeof(ack.payload.status.debug_message) - 1U;
  uint32_t last_state;
  uint32_t last_reason;

  statusDiagLastStateMarker(&last_state, &last_reason);

  dst = statusDiagAppendString(dst, end, "run_diag hb=");
  dst = statusDiagAppendU32(dst, end, pState->run_heartbeat);
  dst = statusDiagAppendString(dst, end, " valid=");
  dst = statusDiagAppendHex(dst, end, pState->valid);
  dst = statusDiagAppendString(dst, end, " safe=");
  dst = statusDiagAppendU32(dst, end, pState->safe);
  dst = statusDiagAppendString(dst, end, " state=");
  dst = statusDiagAppendU32(dst, end, pState->state);
  dst = statusDiagAppendString(dst, end, " rc=");
  dst = statusDiagAppendU32(dst, end, pState->resetCause);
#if defined(TAMP_BKP0R) && !defined(RTC_BKP0R)
  dst = statusDiagAppendString(dst, end, " tamp=");
  dst = statusDiagAppendHex(dst, end, TAMP->MISR);
#endif
  dst = statusDiagAppendString(dst, end, " cycle=");
  dst = statusDiagAppendU32(dst, end, pState->cycle_count);
  dst = statusDiagAppendString(dst, end, " pages=");
  dst = statusDiagAppendU32(dst, end, pState->pages);
  dst = statusDiagAppendString(dst, end, " ext=");
  dst = statusDiagAppendU32(dst, end, pState->external_blocks);
  dst = statusDiagAppendString(dst, end, " last=");
  dst = statusDiagAppendU32(dst, end, last_state);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, last_reason);
  dst = statusDiagAppendString(dst, end, " term=");
  dst = statusDiagAppendU32(dst, end, pState->terminal_state);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, pState->terminal_reason);
  dst = statusDiagAppendString(dst, end, " hs=");
  dst = statusDiagAppendU32(dst, end, pState->header_status);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, pState->header_flasherr);
  dst = statusDiagAppendString(dst, end, " hr=");
  dst = statusDiagAppendU32(dst, end, pState->header_retries);
  dst = statusDiagAppendString(dst, end, " hp=");
  dst = statusDiagAppendU32(dst, end, pState->header_page);
  dst = statusDiagAppendString(dst, end, " se=");
  dst = statusDiagAppendU32(dst, end, pState->sample_error_count);
  dst = statusDiagAppendString(dst, end, " sf=");
  dst = statusDiagAppendU32(dst, end, pState->sample_fifo_overruns);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, pState->sample_fifo_watermark_shorts);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, pState->sample_fifo_empty_reads);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, pState->sample_fifo_short_blocks);
  *dst = 0;
}

/**
 * @brief Write non-blocking retained-state diagnostics for fast status.
 */
static void statusDiagWriteFast(void)
{
  char *dst = ack.payload.status.debug_message;
  char *end = dst + sizeof(ack.payload.status.debug_message) - 1U;

#if defined(TAMP_BKP0R) && !defined(RTC_BKP0R)
  extern volatile uint32_t tag_backup_diag_phase_mask;
  extern volatile uint32_t tag_backup_diag_latest_phase;
  extern volatile uint32_t tag_backup_diag_valid;
  extern volatile uint32_t tag_backup_diag_safe;
  extern volatile uint32_t tag_backup_diag_reset_cause;
  extern volatile uint32_t tag_backup_diag_state;

  dst = statusDiagAppendString(dst, end, "fast ph=");
  dst = statusDiagAppendHex(dst, end, tag_backup_diag_phase_mask);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendHex(dst, end, tag_backup_diag_latest_phase);
  dst = statusDiagAppendString(dst, end, " lv=");
  dst = statusDiagAppendHex(dst, end, tag_backup_diag_valid);
  dst = statusDiagAppendString(dst, end, " ls=");
  dst = statusDiagAppendU32(dst, end, tag_backup_diag_safe);
  dst = statusDiagAppendString(dst, end, " lr=");
  dst = statusDiagAppendU32(dst, end, tag_backup_diag_reset_cause);
  dst = statusDiagAppendString(dst, end, " lst=");
  dst = statusDiagAppendU32(dst, end, tag_backup_diag_state);
  dst = statusDiagAppendString(dst, end, " c=");
  dst = statusDiagAppendHex(dst, end, TAMP->BKP0R);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendHex(dst, end, TAMP->BKP1R);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendHex(dst, end, TAMP->BKP2R);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendHex(dst, end, TAMP->BKP3R);
  dst = statusDiagAppendString(dst, end, " db=");
  dst = statusDiagAppendHex(dst, end, PWR->DBPR);
  dst = statusDiagAppendString(dst, end, " ap=");
  dst = statusDiagAppendHex(dst, end, RCC->APB1ENR1);
#else
  dst = statusDiagAppendString(dst, end, "fast_diag valid=");
  dst = statusDiagAppendHex(dst, end, pState->valid);
  dst = statusDiagAppendString(dst, end, " safe=");
  dst = statusDiagAppendU32(dst, end, pState->safe);
  dst = statusDiagAppendString(dst, end, " state=");
  dst = statusDiagAppendU32(dst, end, pState->state);
  dst = statusDiagAppendString(dst, end, " rc=");
  dst = statusDiagAppendU32(dst, end, pState->resetCause);
#endif
  *dst = 0;
}
#endif

#if TAG_RECOVERY_TRACE
/** @brief Letters reported for recovery_flags, in bit order. */
static const char statusTraceFlagLetters[] = "EMVUCARTfbiF";

/** @brief Bits matching statusTraceFlagLetters, same order. */
static const uint32_t statusTraceFlagBits[] = {
    RECOVERY_TRACE_ENTERED,          RECOVERY_TRACE_MONITOR,
    RECOVERY_TRACE_RETAINED_VALID,   RECOVERY_TRACE_FROM_UNSPEC,
    RECOVERY_TRACE_CONCRETE,         RECOVERY_TRACE_ADOPTED_RETAINED,
    RECOVERY_TRACE_CLOCK_RECOVERED,  RECOVERY_TRACE_CLOCK_TRUSTED,
    RECOVERY_TRACE_MARKER_READFAIL,  RECOVERY_TRACE_MARKER_BLANK,
    RECOVERY_TRACE_MARKER_INVALID,   RECOVERY_TRACE_MARKER_FULL};

/**
 * @brief Append a recovery-flags word as a fixed-width letter field.
 *
 * @details Each position is its letter when the bit is set and '-' when clear,
 *          so two words line up column-for-column when read side by side.
 *
 * @param[in,out] dst Current write pointer.
 * @param[in] end One-past-last writable byte.
 * @param[in] flags Recovery flags word to render.
 * @return Updated write pointer.
 */
static char *statusTraceAppendFlags(char *dst, char *end, uint32_t flags)
{
  for (size_t i = 0;
       i < sizeof(statusTraceFlagBits) / sizeof(statusTraceFlagBits[0]); i++)
    dst = statusDiagAppendChar(
        dst, end,
        (flags & statusTraceFlagBits[i]) ? statusTraceFlagLetters[i] : '-');
  return dst;
}

/**
 * @brief Report the retained boot reset-recovery trace in the status reply.
 *
 * @details Renders the BackupState recovery-trace words as a single line in
 *          Status.debug_message, for example:
 *
 *              rcv b=12 rc=5 st=7>2 mk=1/7 fl=E-V-C---b--- sn=EMV-CA--b---
 *              ch=4>2/0/0rc5 E-V-C---b--- h=72
 *
 *          where @c b is the boot count, @c rc the @ref t_resetCause recovery
 *          dispatched on, @c st the TagState pState held on entry to recovery
 *          and the one recovery resolved, and @c mk the number of marker-log
 *          entries the scan consumed with the reason of the last accepted
 *          marker.
 *
 *          @c fl is this boot's flags and @c sn the sticky OR over every boot,
 *          in the same column order: E entered recovery, M monitor attach,
 *          V retained state valid, U entered unspecified, C concrete state
 *          recovered, A retained state adopted, R clock recovered from the
 *          external RTC, T clockTrusted, then why the marker scan stopped --
 *          f read fault, b erased entry, i invalid marker, F log full.
 *
 *          @c ch is the full record of the boot that last changed the resolved
 *          state -- entry state, resolved state, markers consumed, last marker
 *          reason, reset cause, and that boot's flags. It is what an attach
 *          cannot overwrite, because an attach resolves the state the tag
 *          already holds and so changes nothing.
 *
 *          @c h is the resolved-state history, oldest digit first, pushed only
 *          on change. It is the field that survives the attach: a monitor
 *          attach is itself a reset, so @c fl always describes the healthy
 *          attach boot rather than the detached boot being investigated.
 *
 *          This exists because reset recovery decides the tag's state before
 *          any host can observe it, and its only other narration is
 *          debug_log_printf(), which shipped images compile out. A tag that
 *          reports FINISHED in its marker log but IDLE as its live state cannot
 *          be diagnosed without knowing which recovery branch ran.
 *
 * @pre  Status.debug_message must be otherwise unused for this reply.
 * @post ack.payload.status.debug_message holds a null-terminated trace line.
 */
static void statusRecoveryTraceWrite(void)
{
  char *dst = ack.payload.status.debug_message;
  char *end = dst + sizeof(ack.payload.status.debug_message) - 1U;
  const uint32_t flags = pState->recovery_flags;
  const uint32_t states = pState->recovery_states;
  const uint32_t history = pState->recovery_state_hist;
  bool started = false;

  dst = statusDiagAppendString(dst, end, "rcv b=");
  dst = statusDiagAppendU32(dst, end, pState->recovery_boots);
  dst = statusDiagAppendString(dst, end, " rc=");
  dst = statusDiagAppendU32(dst, end,
                            (flags & RECOVERY_TRACE_CAUSE_MASK) >>
                                RECOVERY_TRACE_CAUSE_SHIFT);
  dst = statusDiagAppendString(dst, end, " st=");
  dst = statusDiagAppendU32(dst, end, states & 0xFFU);
  dst = statusDiagAppendChar(dst, end, '>');
  dst = statusDiagAppendU32(dst, end, (states >> 8) & 0xFFU);
  dst = statusDiagAppendString(dst, end, " mk=");
  dst = statusDiagAppendU32(dst, end, (states >> 16) & 0xFFU);
  dst = statusDiagAppendChar(dst, end, '/');
  dst = statusDiagAppendU32(dst, end, (states >> 24) & 0xFFU);
  dst = statusDiagAppendString(dst, end, " fl=");
  dst = statusTraceAppendFlags(dst, end, flags);
  dst = statusDiagAppendString(dst, end, " w=");
  dst = statusDiagAppendU32(dst, end, pState->recovery_wipes);
  dst = statusDiagAppendString(dst, end, " sn=");
  dst = statusTraceAppendFlags(dst, end, pState->recovery_flags_seen);

  {
    const uint32_t chg = pState->recovery_change_flags;
    const uint32_t chs = pState->recovery_change_states;

    dst = statusDiagAppendString(dst, end, " ch=");
    dst = statusDiagAppendU32(dst, end, chs & 0xFFU);
    dst = statusDiagAppendChar(dst, end, '>');
    dst = statusDiagAppendU32(dst, end, (chs >> 8) & 0xFFU);
    dst = statusDiagAppendChar(dst, end, '/');
    dst = statusDiagAppendU32(dst, end, (chs >> 16) & 0xFFU);
    dst = statusDiagAppendChar(dst, end, '/');
    dst = statusDiagAppendU32(dst, end, (chs >> 24) & 0xFFU);
    dst = statusDiagAppendString(dst, end, "rc");
    dst = statusDiagAppendU32(dst, end,
                              (chg & RECOVERY_TRACE_CAUSE_MASK) >>
                                  RECOVERY_TRACE_CAUSE_SHIFT);
    dst = statusDiagAppendChar(dst, end, ' ');
    dst = statusTraceAppendFlags(dst, end, chg);
  }

  dst = statusDiagAppendString(dst, end, " h=");
  for (uint32_t shift = 4U * (RECOVERY_TRACE_HIST_DEPTH - 1U);; shift -= 4U)
  {
    const uint32_t nibble = (history >> shift) & 0xFU;
    if ((nibble != 0U) || started || (shift == 0U))
    {
      dst = statusDiagAppendU32(dst, end, nibble);
      started = true;
    }
    if (shift == 0U)
      break;
  }
  *dst = 0;
}
#endif

static int monitorReturn(int len)
{
  return len;
}

/** @name Monitor information strings
 * Static firmware/build strings returned to host tools during monitor discovery.
 * @{
 */
enum
{
  MONITOR_STR,
  BOARD_STR,
  REPO_STR,
  HASH_STR,
  BUILDTM_STR,
  SOURCE_STR,
  ARRAY_SIZE_STR
};

#define xstr(s) str(s)
#define str(s) #s

static const char *InfoStrings[ARRAY_SIZE_STR] = {
    [MONITOR_STR] = MAJOR_VERSION "." MINOR_VERSION,
    [BOARD_STR] = BOARD_NAME,
    [REPO_STR] = GIT_REPO,
    [HASH_STR] = VERSION_HASH,
    [BUILDTM_STR] = __DATE__ " : " __TIME__,
    [SOURCE_STR] = xstr(SOURCEDIR)};
/** @} */

/** @name Acknowledgement encoding
 * Encoding helpers keep every monitor response in the shared protobuf buffer
 * and convert nanopb failures into a best-effort error acknowledgement.
 * @{
 */
/**
 * @brief Encode the current Ack object into the shared monitor buffer.
 *
 * @return Encoded byte count, or 0 if encoding failed twice.
 */
int encode_ack(void)
{
  bool status;
  pb_ostream_t ostream = pb_ostream_from_buffer(ProtoBuf, protobuf_size);

  // encode message

  status = pb_encode(&ostream, Ack_fields, &ack);

  if (!status)
  {
    // in case of failure return nanopb error
    ack.err = Ack_Err_NANOPB;
    ack.which_payload = Ack_error_message_tag;
    strncpy(ack.payload.error_message, PB_GET_ERROR(&ostream), sizeof(ack.payload.error_message));
    // re-initialize the output stream
    ostream = pb_ostream_from_buffer(ProtoBuf, protobuf_size);
    status = pb_encode(&ostream, Ack_fields, &ack);
  }
  return status ? ostream.bytes_written : 0;
}

/**
 * @brief Build and encode an acknowledgement containing only an error code.
 *
 * @param[in] err Error code to place in the acknowledgement.
 * @return Encoded byte count.
 */
int errAck(Ack_Err err)
{
  ack.err = err;
  ack.which_payload = 0;
  return encode_ack();
}

/**
 * @brief Build and encode an error acknowledgement with a diagnostic string.
 *
 * @param[in] err Error code to place in the acknowledgement.
 * @param[in] message Null-terminated diagnostic for host tools.
 * @return Encoded byte count.
 */
static int errMessageAck(Ack_Err err, const char *message)
{
  ack.err = err;
  ack.which_payload = Ack_error_message_tag;
  strncpy(ack.payload.error_message, message,
          sizeof(ack.payload.error_message) - 1);
  ack.payload.error_message[sizeof(ack.payload.error_message) - 1] = 0;
  return encode_ack();
}
/** @} */

extern const unsigned char tag_default_config[];
extern const int tag_default_config_len;

/** @name Monitor message generators
 * Generators populate one Ack payload at a time from retained state, persistent
 * storage, or firmware metadata before handing it to encode_ack().
 * @{
 */
/**
 * @brief Generate the active or default configuration response.
 *
 * @return Encoded byte count.
 */
static int configAck(void)
{
  ack.err = Ack_OK;
  ack.which_payload = Ack_config_tag;
  if ((pState->state == IDLE) || (pState->state == TEST))
  {
    pb_istream_t istream = pb_istream_from_buffer(tag_default_config,
                                                  tag_default_config_len);
    pb_decode(&istream, Config_fields, &ack.payload.config);
  }
  else
  {
    readConfig(&ack.payload.config);
  }
  return encode_ack();
}

/**
 * @brief Generate a current status response for the host monitor.
 *
 * @return Encoded byte count.
 */
static int statusAck(void)
{

  int64_t epoch;
  uint32_t millis;

  monitorStatusMeasure(&status_vdd100, &status_temp10);

  ack.err = Ack_OK;
  ack.which_payload = Ack_status_tag;
  ack.payload.status.state = pState->state;
  ack.payload.status.internal_data_count = pState->pages;
  ack.payload.status.external_data_count = pState->external_blocks;
  ack.payload.status.test_status = pState->test_result;
  ack.payload.status.voltage = status_vdd100 * 0.01f;
  ack.payload.status.temperature = status_temp10 * 0.1f;
  ack.payload.status.sectors_erased = externalFlashSectorsErased();
  ack.payload.status.erase_sectors_total_plus_one =
      externalFlashSectorsToErasePlusOne();
  epoch = GetTimeUnixSec(&millis);
  epoch = epoch * 1000 + millis;
  ack.payload.status.millis = epoch;
#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG
  int len = debug_log_read((uint8_t *)ack.payload.status.debug_message,
                           sizeof(ack.payload.status.debug_message) - 1);
  ack.payload.status.debug_message[len] = 0;
#else
  /*
   * ack is reused between replies, so the field has to be cleared explicitly
   * rather than assumed empty: the diagnostic writers below only fill it when
   * the debug log left nothing, and a stale message would suppress them.
   */
  ack.payload.status.debug_message[0] = 0;
#endif
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  if (ack.payload.status.debug_message[0] == 0)
    statusDiagWrite();
#endif
#if TAG_RECOVERY_TRACE
  /*
   * Last resort for the debug-message field: the recovery trace is retained
   * state, so it stays reportable for the whole boot and does not need to win
   * against a live debug queue.
   */
  if (ack.payload.status.debug_message[0] == 0)
    statusRecoveryTraceWrite();
#endif

  return encode_ack();
}

/**
 * @brief Report whether the protobuf buffer contains an empty get_status Req.
 *
 * @details This checks the protobuf wire encoding for the generated
 *          Req_get_status field tag carrying an empty message. The fast path
 *          is only for this exact zero-length request; all other messages,
 *          including any future status request with fields, must stay on the
 *          cooperative monitor service path so normal protobuf decoding and
 *          permission checks apply.
 *
 * @note    This is intentionally a wire-format shortcut rather than a nanopb
 *          decode. It is safe only because the fast path handles one fixed,
 *          side-effect-free request whose encoded form is derived from the
 *          generated field tag and empty-message size.
 *
 * @param[in] len Number of request bytes in ProtoBuf.
 * @return true when the buffer is exactly Req.get_status, false otherwise.
 */
static bool monitor_is_fast_status_request(int len)
{
  const uint8_t get_status_key =
      (uint8_t)((Req_get_status_tag << 3) | PB_WT_STRING);

  return (len == 2) &&
         (ProtoBuf[0] == get_status_key) &&
         (ProtoBuf[1] == Empty_size);
}

/**
 * @brief Encode a cached status ACK without blocking on sensors or RTC.
 *
 * @details The STM32U3 monitor interrupt uses this while the main thread has
 *          marked @c pState->safe false. It reports retained runtime state,
 *          the last voltage/temperature values captured by the normal status
 *          path, and the last timestamp captured by the main loop, avoiding
 *          ADC reads, RTC access, debug-log draining, diagnostics, and storage
 *          queries that belong in thread context. The returned clock may
 *          therefore appear stale while a long operation is running, which is
 *          preferable to blocking the host monitor until that operation yields.
 *
 * @warning Intended only for the U3 IRQ fast path. Do not add hardware access,
 *          blocking calls, debug-log drains, or state-machine side effects.
 *
 * @param[in] len Number of request bytes in ProtoBuf.
 * @return Encoded acknowledgement byte count, or zero when @p len does not
 *         describe the fast-path status request.
 */
int monitor_fast_status_eval(int len)
{
  if (!monitor_is_fast_status_request(len))
    return 0;

  bzero(&ack, sizeof(ack));
  ack.err = Ack_OK;
  ack.which_payload = Ack_status_tag;
  ack.payload.status.state = pState->state;
  ack.payload.status.internal_data_count = pState->pages;
  ack.payload.status.external_data_count = pState->external_blocks;
  ack.payload.status.test_status = pState->test_result;
  ack.payload.status.voltage = status_vdd100 * 0.01f;
  ack.payload.status.temperature = status_temp10 * 0.1f;
  ack.payload.status.sectors_erased = externalFlashSectorsErased();
  ack.payload.status.erase_sectors_total_plus_one =
      externalFlashSectorsToErasePlusOne();
  ack.payload.status.millis =
      ((int64_t)timestamp * 1000) + (int64_t)timestamp_millis;
#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
  if (pState->state == STATE_UNSPECIFIED)
    statusDiagWriteFast();
#endif

  return encode_ack();
}

#define STR_COPY(src, dest) strncpy(dest, src, sizeof(dest))
/**
 * @brief Generate firmware, board, storage, and unique-ID information.
 *
 * @return Encoded byte count.
 */
static int infoAck(void)
{
  static const char *HEX = "0123456789ABCDEF";
  char buf[25];
  char *ptr = &buf[23];
  buf[24] = 0;

  ack.err = Ack_OK;
  ack.which_payload = Ack_info_tag;

  //STR_COPY(InfoStrings[MONITOR_STR], ack.payload.info.monitor);
  STR_COPY(InfoStrings[BOARD_STR], ack.payload.info.board_desc);
  for (int i = 0; i < 3; i++)
  {
    uint32_t data = ((uint32_t *)UID_BASE)[i];
    for (int j = 0; j < 8; j++)
    {
      *ptr-- = HEX[data & 15];
      data = data >> 4;
    }
  }
  STR_COPY(buf, ack.payload.info.uuid);
  ack.payload.info.intflashsz = *((uint16_t *)FLASHSIZE_BASE);
  ack.payload.info.extflashsz = externalFlashSize();

  ack.payload.info.tag_type = TAG_TYPE;
  STR_COPY(FIRMWARE_STRING, ack.payload.info.firmware);
  STR_COPY(InfoStrings[REPO_STR], ack.payload.info.gitrepo);
  STR_COPY(InfoStrings[HASH_STR], ack.payload.info.githash);
  STR_COPY(InfoStrings[BUILDTM_STR], ack.payload.info.build_time);
  STR_COPY(InfoStrings[SOURCE_STR], ack.payload.info.source_path);
#ifdef QTMONITOR_VERSION
  ack.payload.info.qtmonitor_min_version = QTMONITOR_VERSION;
#else
  ack.payload.info.qtmonitor_min_version = 1.5;
#endif
#if defined(SENSOR_CONSTANTS) && SENSOR_CONSTANTS
  ack.payload.info.accelconstant = ACCEL_CONSTANT;
  ack.payload.info.magconstant = MAG_CONSTANT;
#endif
  tagRtcRefreshClockCorrection();
  ack.payload.info.has_ppm_clock_error = true;
  ack.payload.info.ppm_clock_error = tagRtcClockErrorPpm();
  return encode_ack();
}

/**
 * @brief Generate one page of persistent system state log entries.
 *
 * @param[in] index First state-log entry to include.
 * @return Encoded byte count.
 */
static int system_logAck(int index)
{
  static const size_t max_count = sizeof(ack.payload.system_log.states) / sizeof(State);
  State *states = ack.payload.system_log.states;
  size_t count = 0;

  ack.err = Ack_Err_OK;
  ack.which_payload = Ack_system_log_tag;

  for (size_t i = index; (i < sEPOCH_SIZE) && (count < max_count); i++)
  {
    t_StateMarker marker;
    if (FLASH_Read_Checked(&sEpoch[i], &marker, sizeof(marker)))
    {
      break;
    }
    if (marker.epoch == -1)
    {
      break;
    }
    if ((marker.state <= STATE_UNSPECIFIED) ||
        (marker.state > _TagState_MAX) ||
        (marker.reason > _State_Event_MAX))
    {
      break;
    }
    states[count].has_status = true;
    states[count].status.millis = ((int64_t)marker.epoch) * 1000;
    states[count].status.state = marker.state;
    states[count].status.internal_data_count = marker.internal_pages;
    states[count].status.external_data_count = marker.external_pages;
    states[count].status.voltage = marker.vdd100 * 0.01f;
    states[count].status.temperature = marker.temp10 * 0.1f;
    states[count].transition_reason = marker.reason;
    states[count].status.test_status = 0;
    count++;
  }
  ack.payload.system_log.states_count = count;
  return encode_ack();
}

/**
 * @brief Report whether terminal-state data download is safe.
 *
 * Data-log export reads external flash pages whose contents may still be under
 * construction while acquisition is active. Restricting export to terminal
 * states keeps monitor reads out of the logging critical path.
 */
static bool monitor_data_log_allowed(void)
{
  return (pState->state == TagState_ABORTED) ||
         (pState->state == TagState_FINISHED) ||
         (pState->state == TagState_EXCEPTION);
}

/**
 * @brief Report whether the tag is actively running an acquisition.
 */
static bool monitor_acquisition_active(void)
{
  if ((pState->state == TagState_CONFIGURED) ||
      (pState->state == TagState_RUNNING))
  {
    return true;
  }
#if CONFIG_HAS_HIBERNATE
  if (pState->state == TagState_HIBERNATING)
  {
    return true;
  }
#endif
  return false;
}

/**
 * @brief Report whether the monitor may request a runtime stop.
 */
static bool monitor_stop_allowed(void)
{
  if (monitor_acquisition_active())
  {
    return true;
  }
  if (pState->state == TagState_EXCEPTION)
  {
    return true;
  }
#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  if (pState->state == TagState_CALIBRATE)
  {
    return true;
  }
#endif
  return false;
}

/**
 * @brief Check whether a decoded request is permitted in the current state.
 *
 * The main state machine still owns state transitions. This gate prevents the
 * monitor thread from acknowledging side-effecting requests that the main
 * thread would later ignore, and prevents direct monitor-context operations
 * such as log export or calibration sampling from racing active acquisition.
 */
static bool monitor_request_allowed(const Req *request)
{
  switch (request->which_payload)
  {
  case Req_get_info_tag:
  case Req_get_status_tag:
  case Req_get_config_tag:
    return true;

  case Req_erase_tag:
    return (pState->state == TagState_ABORTED) ||
           (pState->state == TagState_FINISHED);

  case Req_start_tag:
  case Req_test_tag:
  case Req_set_rtc_tag:
    return pState->state == TagState_IDLE;

  case Req_stop_tag:
    return monitor_stop_allowed();

  case Req_log_tag:
    if (request->payload.log.fmt == LogReq_INTERNAL_DATA)
    {
      return monitor_data_log_allowed();
    }
    return true;

#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  case Req_calibrate_tag:
    return pState->state == TagState_IDLE;
  case Req_caldata_tag:
    return pState->state == TagState_CALIBRATE;
#endif

#if defined(CALIBRATION_CONSTANTS) && CALIBRATION_CONSTANTS
  case Req_write_calibration_tag:
    return pState->state == TagState_IDLE;
  case Req_read_calibration_tag:
    return true;
#endif

  default:
    return true;
  }
}
/** @} */



/** @name Protocol request evaluation
 * Decode host requests, perform side effects that are safe in monitor context,
 * and return state-machine command work as a bitmask.
 * @{
 */
/**
 * @brief Evaluate one protobuf request from the shared monitor buffer.
 *
 * @param[in] len Number of request bytes present in ProtoBuf.
 * @param[out] work State-machine command bits requested by this packet.
 * @return Encoded acknowledgement byte count.
 */
int proto_eval(int len, uint32_t *work)
{
  int err;
  bool status;

  if (work)
    *work = 0;

  bzero(&ack, sizeof(ack));
  pb_istream_t istream = pb_istream_from_buffer(ProtoBuf, len);

  // decode request

  bzero(&req, sizeof(req));
  status = pb_decode(&istream, Req_fields, &req);
  if (!status)
  {
    return monitorReturn(errAck(Ack_Err_NANOPB));
  }

  if (!monitor_request_allowed(&req))
  {
    return monitorReturn(errMessageAck(Ack_Err_PERM,
                                       "Monitor request not permitted in current tag state"));
  }

  // Process requests in order of message fields

  switch (req.which_payload)
  {

    // Information Requests

  case Req_get_info_tag: // Get tag info
    return monitorReturn(infoAck());

  case Req_get_status_tag: // return tag state
    return monitorReturn(statusAck());

  case Req_get_config_tag: // get config
    return monitorReturn(configAck());

    // Control

  case Req_erase_tag: // erase
    if (work)
      *work |= MON_WORK_RESET;
    return monitorReturn(errAck(Ack_OK));

  case Req_start_tag: // start
  {
    if (writeConfig(&req.payload.start))
    {
      if (work)
        *work |= MON_WORK_START;
      return monitorReturn(errAck(Ack_OK));
    }
    else
    {
      const char *message = writeConfigErrorMessage();
      if (message && message[0])
      {
        ack.err = Ack_Err_NXIO;
        ack.which_payload = Ack_error_message_tag;
        strncpy(ack.payload.error_message, message,
                sizeof(ack.payload.error_message) - 1);
        ack.payload.error_message[sizeof(ack.payload.error_message) - 1] = 0;
        return monitorReturn(encode_ack());
      }
      return monitorReturn(errAck(Ack_Err_NXIO));
    }
  }

  case Req_stop_tag: // stop
    debug_log_printf("monitor: stop request accepted state=%d\r\n",
                     pState->state);
    if (work)
      *work |= MON_WORK_STOP;
    return monitorReturn(errAck(Ack_OK));

  case Req_test_tag: // self test
    test_to_run = req.payload.test;
    if (work)
      *work |= MON_WORK_SELFTEST;
    return monitorReturn(errAck(Ack_OK));
  case Req_set_rtc_tag: // Write RTC
    if ((err = SetTimeUnixSec(req.payload.set_rtc / 1000)))
    {
      return monitorReturn(errMessageAck(Ack_Err_NXIO,
                                         "RTC sync failed while writing tag clock"));
    }
    else
      return monitorReturn(errAck(Ack_OK));

    // request log

  case Req_log_tag:
    switch (req.payload.log.fmt)
    {
     case LogReq_INTERNAL_DATA:
        return monitorReturn(data_logAck(req.payload.log.index, &ack));
    case LogReq_SYSTEM_LOG:
      return monitorReturn(system_logAck(req.payload.log.index));
    default:
      return monitorReturn(errAck(Ack_Err_PERM));
    }
    // Unimplemented request

#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  case Req_calibrate_tag:
    if (work)
      *work |= MON_WORK_CALIBRATE;
    return monitorReturn(errAck(Ack_OK));
  case Req_caldata_tag:
    return monitorReturn(calibration_logAck(&ack));

#endif
#if defined(CALIBRATION_CONSTANTS) && CALIBRATION_CONSTANTS
  case Req_write_calibration_tag:
    return monitorReturn(write_calibration(&req.payload.write_calibration));
  case Req_read_calibration_tag:
    return monitorReturn(read_calibration(req.payload.read_calibration, &ack));
#endif
#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG
#endif
  default:
    return monitorReturn(errAck(Ack_Err_PERM));
  }
}
/** @} */
