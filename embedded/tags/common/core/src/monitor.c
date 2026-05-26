/**
 * @file monitor.c
 * @brief Protobuf monitor request evaluation and acknowledgement generation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "monitor.h"
#include <tag.pb.h>

#include "adc.h"
#include "config.h"
#include "core_events.h"
#include "core_sync.h"
#include "core_types.h"
#include "custom.h"
#include "debug_log.h"
#include "persistent.h"
#include "sensor_calibration.h"
#include "test_support.h"
#include "timekeeping.h"
#include "version.h"

#include <pb_decode.h>
#include <pb_encode.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

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
  uint16_t vdd100 = 0;
  int16_t temp10 = 0;
  adcVDD(&vdd100, &temp10);

  ack.err = Ack_OK;
  ack.which_payload = Ack_status_tag;
  ack.payload.status.state = pState->state;
  ack.payload.status.internal_data_count = pState->pages;
  ack.payload.status.external_data_count = pState->external_blocks;
  ack.payload.status.test_status = pState->test_result;
  ack.payload.status.voltage = vdd100 * 0.01f;
  ack.payload.status.temperature = temp10 * 0.1f;
  ack.payload.status.sectors_erased = externalFlashSectorsErased();
  epoch = GetTimeUnixSec(&millis);
  epoch = epoch * 1000 + millis;
  ack.payload.status.millis = epoch;
#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG
   int len = debug_log_read((uint8_t *)ack.payload.status.debug_message,
                            sizeof(ack.payload.status.debug_message) - 1);
   ack.payload.status.debug_message[len] = 0;
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
    if (sEpoch[i].epoch == -1)
    {
      break;
    }
    states[count].has_status = true;
    states[count].status.millis = ((int64_t)sEpoch[i].epoch) * 1000;
    states[count].status.state = sEpoch[i].state;
    states[count].status.internal_data_count = sEpoch[i].internal_pages;
    states[count].status.external_data_count = sEpoch[i].external_pages;
    states[count].status.voltage = sEpoch[i].vdd100 * 0.01f;
    states[count].status.temperature = sEpoch[i].temp10 * 0.1f;
    states[count].transition_reason = sEpoch[i].reason;
    states[count].status.test_status = 0;
    count++;
  }
  ack.payload.system_log.states_count = count;
  return encode_ack();
}
/** @} */



/** @name Protocol request evaluation
 * Decode host requests, perform side effects that are safe in monitor context,
 * and signal the main thread for state-machine commands.
 * @{
 */
/**
 * @brief Evaluate one protobuf request from the shared monitor buffer.
 *
 * @param[in] len Number of request bytes present in ProtoBuf.
 * @return Encoded acknowledgement byte count.
 */
int proto_eval(int len)
{
  int err;
  bool status;
  bzero(&ack, sizeof(ack));
  pb_istream_t istream = pb_istream_from_buffer(ProtoBuf, len);

  // decode request

  bzero(&req, sizeof(req));
  status = pb_decode(&istream, Req_fields, &req);
  if (!status)
    return errAck(Ack_Err_NANOPB);

  // local state variables

  // Process requests in order of message fields

  switch (req.which_payload)
  {

    // Information Requests

  case Req_get_info_tag: // Get tag info
    return infoAck();

  case Req_get_status_tag: // return tag state
    return statusAck();

  case Req_get_config_tag: // get config
    return configAck();

    // Control

  case Req_erase_tag: // erase
    chEvtSignal(tpMain, EVT_RESET);
    return errAck(Ack_OK);

  case Req_start_tag: // start
  {
    if (writeConfig(&req.payload.start))
    {
      chEvtSignal(tpMain, EVT_START);
      return errAck(Ack_OK);
    }
    else
    {
      return errAck(Ack_Err_NXIO);
    }
  }

  case Req_stop_tag: // stop
    chEvtSignal(tpMain, EVT_STOP);
    return errAck(Ack_OK);

  case Req_test_tag: // self test
    test_to_run = req.payload.test;
    chEvtSignal(tpMain, EVT_SELFTEST);
    return errAck(Ack_OK);
  case Req_set_rtc_tag: // Write RTC
    if ((err = SetTimeUnixSec(req.payload.set_rtc / 1000)))
      return errAck(Ack_Err_NXIO);
    else
      return errAck(Ack_OK);

    // request log

  case Req_log_tag:
    switch (req.payload.log.fmt)
    {
     case LogReq_INTERNAL_DATA:
        return data_logAck(req.payload.log.index, &ack);
    case LogReq_SYSTEM_LOG:
      return system_logAck(req.payload.log.index);
    default:
      return errAck(Ack_Err_PERM);
    }
    // Unimplemented request

#if defined(SENSOR_CALIBRATION) && SENSOR_CALIBRATION
  case Req_calibrate_tag:
    chEvtSignal(tpMain, EVT_CALIBRATE);
    return errAck(Ack_OK);
  case Req_caldata_tag:
    return  calibration_logAck(&ack);

#endif
#if defined(CALIBRATION_CONSTANTS) && CALIBRATION_CONSTANTS
  case Req_write_calibration_tag:
    return write_calibration(&req.payload.write_calibration);
  case Req_read_calibration_tag:
    return read_calibration(req.payload.read_calibration, &ack);
#endif
#if defined(TAG_DEBUG_LOG) && TAG_DEBUG_LOG
#endif
  default:
    return errAck(Ack_Err_PERM);
  }
}
/** @} */
