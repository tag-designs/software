
/**
 * @file sensors.c
 * @brief IMUTagBreakout sensor sampling and calibration storage stubs.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <tag.pb.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "core_types.h"
#include "flash_internal.h"
#include "persistent.h"

#include "lis2du12.h"
#include "ak09940a.h"
#include "sensors.h"


void magOn(void);
void magOff(void);

typedef struct {
    int32_t timestamp;
    CalibrationConstants_MagConstants constants;
} sensor_constants_t __attribute__((aligned(8))); 

sensor_constants_t constants_tmp NOINIT;



#define CONSTANT_CNT (2048/sizeof(sensor_constants_t))

// calibration constants in reserved flash section

sensor_constants_t calConstants[CONSTANT_CNT] __attribute__((section(".calibration")));

/**
 * @brief Sample raw sensor data for the data log.
 *
 * This revived target currently leaves the legacy sensor bodies disabled while
 * the descriptor-backed shared drivers are wired back in.
 *
 * @param[out] data Raw sensor destination.
 * @return true after clearing the destination.
 */
bool sensorSample(RawSensorData *data){
  memset(data,0,sizeof(*data));
/*
  bool ok = true;
  uint8_t buf[11];
  magInit(MAG_SAMPLE_SINGLE_MODE);
  if (magSample(true,buf)) {

      // keep only 16 bits
      data->mx = ((int) (buf[2]<<30)|(buf[1]<<22) | (buf[0] << 14))>>16;
      data->my = ((int) (buf[5]<<30)|(buf[4]<<22) | (buf[3] << 14))>>16;
      data->mz = ((int) (buf[8]<<30)|(buf[7]<<22) | (buf[6] << 14))>>16;
  } else {
    ok = false;
  }
  magOff();
  */

/*
  struct {
      int16_t x;
      int16_t y;
      int16_t z;
  } accel_data;
 if (accelSample((uint8_t *) &accel_data))
    {
        data->ax = (accel_data.x/16);
        data->ay = (accel_data.y/16);
        data->az = (accel_data.z/16);
    } else {
      ok = false;
    }*/
    return true;
}


/**
 * @brief Populate a live calibration sample.
 *
 * The legacy sampling body is intentionally disabled for this target until the
 * descriptor-backed sensors are fully restored.
 *
 * @param[out] sensors Protobuf sensor-data payload to populate.
 * @return true after attempting the calibration sample.
 */
bool sensorCalibrationSample(SensorData *sensors)
{
    memset(sensors, 0, sizeof(*sensors));
   /*
    uint8_t buf[11];
    struct {
        int16_t x;
        int16_t y;
        int16_t z;
    } accel_data;

    int x = 0;
    int y = 0;
    int z = 0;
    //int t = 0;

    if (magSample(false,buf))
    {
      // keep all 18 bits
      sensors->has_mag = true;
      x = ((int) (buf[2]<<30)|(buf[1]<<22) | (buf[0] << 14))>>14;
      y = ((int) (buf[5]<<30)|(buf[4]<<22) | (buf[3] << 14))>>14;
      z = ((int) (buf[8]<<30)|(buf[7]<<22) | (buf[6] << 14))>>14;
     
      sensors->mag.mx = x * 0.01f;
      sensors->mag.my = y * 0.01f;
      sensors->mag.mz = z * 0.01f;
    }

    if (accelSample((uint8_t *) &accel_data))
    {
        sensors->has_accel = true;
        
        sensors->accel.ax = (accel_data.x/16) * 0.976f;
        sensors->accel.ay = (accel_data.y/16) * 0.976f;
        sensors->accel.az = (accel_data.z/16) * 0.976f;
    }
        */

    return true;
}
/*
bool initSensors(void){
    accelInit(ACCEL_SAMPLE_100HZ_MODE);
    magInit(MAG_SAMPLE_100HZ_MODE);
    return true;
}

bool deinitSensors(void) {
    magOff();
    accelDeinit();
    return true;
}*/

/**
 * @brief Handle the live sensor-calibration state.
 *
 * @param[in] t State transition phase.
 * @param[in] reason Reason for entering or continuing calibration.
 * @return Requested low-power mode after handling calibration.
 */
enum Sleep Calibrating(enum StateTrans t, State_Event reason)
{
  (void)reason;

  if (t == T_INIT)
  {
  //  initSensors();
    // start sensors
    pState->state = TagState_CALIBRATE;
  }
  if (MONCONNECTED)
    return SHUTDOWN;
  else
  {
    // shutdown sensors
 //   deinitSensors();
    pState->state = TagState_IDLE;
    return SHUTDOWN;
  }
}


extern int encode_ack(void);
extern int errAck(Ack_Err err);

/**
 * @brief Populate and encode a live calibration sample ACK.
 *
 * @param[out] ack ACK message to fill.
 * @return Encoded ACK length.
 */
int calibration_logAck(Ack *ack){
  CalibrationLog *data = &ack->payload.calibration_log;
  ack->err = Ack_OK;
  ack->which_payload = Ack_calibration_log_tag;
  data->data_count = 1;
  sensorCalibrationSample(&data->data[0]);
  return encode_ack();
}

/**
 * @brief Accept calibration constants from the host.
 *
 * The flash write body is disabled with the legacy sensor path, but validation
 * is retained so the monitor sees the same command shape.
 *
 * @param[in] constants Host-provided calibration constants.
 * @return Encoded ACK length or error response length.
 */
int write_calibration(CalibrationConstants *constants){
  unsigned int index;

  
  if (!constants->has_magnetometer)
    return errAck(Ack_INVAL);

  // search for first empty slot

  for (index = 0; index < CONSTANT_CNT; index++){
    if  ((*((int32_t *) &calConstants[index])) == -1)
      break;
  }/*

  // if all slots are free, erase current constants
  if (index >= CONSTANT_CNT)
  {
    // erase block
    chSysLock();
    FLASH_Unlock();
    FLASH_PageErase(((((uint32_t) calConstants)-0x8000000)) / 2048);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
    index = 0;
  }

  // write constants

  memcpy(&constants_tmp.constants, 
         &(constants->magnetometer), 
        sizeof(constants->magnetometer));
  constants_tmp.timestamp = constants->timestamp;

  chSysLock();
  FLASH_Unlock();
  FLASH_Program_Array((uint32_t *) &calConstants[index], 
                      (uint32_t *) &(constants_tmp),
                      sizeof(constants_tmp)/4);
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();*/
  return errAck(Ack_OK);
}

/**
 * @brief Read calibration constants into a host ACK.
 *
 * The legacy read body is disabled for this target.
 *
 * @param[in] index Calibration slot to read.
 * @param[out] ack ACK message to populate.
 * @return Undefined until the legacy calibration read path is restored.
 */
int read_calibration(int32_t index, Ack *ack){
  (void)index;
  (void)ack;
  // if index < 0, search for last written
 /* if (index < 0) {
    for (index = (int32_t) CONSTANT_CNT - 1 ; index > -1 ; index--){
      if ((*((int32_t *) &calConstants[index])) != -1)
        break;
    }
  }
  if ((index < 0) || (index >= (int32_t) CONSTANT_CNT ) || ((*((int32_t *) &calConstants[index])) == -1))
    return errAck(Ack_NODATA);
  ack->err = Ack_OK;
  ack->which_payload = Ack_calibration_constants_tag;
  ack->payload.calibration_constants.has_magnetometer = true;
  memcpy(&ack->payload.calibration_constants.magnetometer, &calConstants[index].constants,  
      sizeof(CalibrationConstants_MagConstants));
  ack->payload.calibration_constants.timestamp = calConstants[index].timestamp;
  return encode_ack();
  */
  return errAck(Ack_NODATA);
}

