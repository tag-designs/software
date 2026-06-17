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

static void monitorStatusMeasure(uint16_t *vdd100, int16_t *temp10)
{
  adcVDD(vdd100, temp10);
#if defined(TAG_STATUS_FIXED_VDD100)
  *vdd100 = TAG_STATUS_FIXED_VDD100;
#endif
}

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

  if (!monitor_acquisition_active())
  {
    monitorStatusMeasure(&status_vdd100, &status_temp10);
  }
#if defined(TAG_STATUS_FIXED_VDD100)
  else
  {
    status_vdd100 = TAG_STATUS_FIXED_VDD100;
  }
#endif

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
    t_StateMarker marker;
    if (FLASH_Read_Checked(&sEpoch[i], &marker, sizeof(marker)))
    {
      break;
    }
    if (marker.epoch == -1)
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
         (pState->state == TagState_FINISHED);
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
    return monitorReturn(errAck(Ack_Err_PERM));
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
      return monitorReturn(errAck(Ack_Err_NXIO));
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
