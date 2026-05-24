
/**
 * @file sensors.c
 * @brief CompassTag sensor sampling, orientation, and calibration storage.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <tag.pb.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "core_types.h"
#include "devices.h"
#include "flash_internal.h"
#include "persistent.h"
#include "timekeeping.h"

#include "lis2du12.h"
#include "ak09940a.h"
#include "custom.h"
#include "sensors.h"

/*
 * Shared CompassTag sensor/calibration path.
 *
 * The three active CompassTag variants share the magnetometer, accelerometer,
 * calibration flash layout, and monitor ACK handling. The production tag and
 * breakout boards mount the sensors in different orientations, so axis
 * orientation is still selected by the variant custom.h. Currently
 * CompassTagAT25 defines COMPASS_TAG to select the production-tag transform;
 * breakout variants leave it unset.
 */

typedef struct {
    int32_t timestamp;
    CalibrationConstants_MagConstants constants;
} sensor_constants_t __attribute__((aligned(8))); 

sensor_constants_t constants_tmp NOINIT;



#define CONSTANT_CNT (2048/sizeof(sensor_constants_t))

// calibration constants in reserved flash section

sensor_constants_t calConstants[CONSTANT_CNT] __attribute__((section(".calibration")));

/**
 * @brief Apply the variant-specific magnetometer axis orientation.
 *
 * @param[in,out] x X-axis magnetic sample.
 * @param[in,out] y Y-axis magnetic sample.
 * @param[in,out] z Z-axis magnetic sample.
 */
static void orient_mag_values(int *x, int *y, int *z)
{
#ifdef COMPASS_TAG
  int tmp = -*y;
  *y = -*x;
  *x = tmp;
  *z = -*z;
#else
  (void)x;
  (void)y;
  (void)z;
#endif
}

/**
 * @brief Apply the variant-specific accelerometer axis orientation.
 *
 * @param[in,out] x X-axis accelerometer sample.
 * @param[in,out] y Y-axis accelerometer sample.
 */
static void orient_accel_raw(int16_t *x, int16_t *y)
{
#ifdef COMPASS_TAG
  *x = -*x;
  *y = -*y;
#else
  (void)x;
  (void)y;
#endif
}

/**
 * @brief Sample raw accelerometer and magnetometer data for the data log.
 *
 * @param[out] data Raw sensor destination.
 * @return true when all enabled sensors sampled successfully.
 */
bool sensorSample(RawSensorData *data){
  bool ok = true;
  uint8_t buf[11];
  struct {
      int16_t x;
      int16_t y;
      int16_t z;
  } accel_data;
  const TagRegisterDevice *mag;
  const TagRegisterDevice *accel;
  int mx = 0;
  int my = 0;
  int mz = 0;

  memset(data,0,sizeof(*data));

  mag = TAG_MAG_DEVICE;
  ak09940aDeviceBegin(mag);
  stopMilliseconds(1);
  tagCompassMagResetRelease();
  if (ak09940aSample(mag, true, buf)) {

      // keep only 16 bits
      mx = ((int) (buf[2]<<30)|(buf[1]<<22) | (buf[0] << 14))>>16;
      my = ((int) (buf[5]<<30)|(buf[4]<<22) | (buf[3] << 14))>>16;
      mz = ((int) (buf[8]<<30)|(buf[7]<<22) | (buf[6] << 14))>>16;
      orient_mag_values(&mx, &my, &mz);
      data->mx = mx;
      data->my = my;
      data->mz = mz;
  } else {
    ok = false;
  }
  ak09940aDeviceEnd(mag);
  tagCompassMagResetAssert();

  accel = TAG_ACCEL_DEVICE;
  if (lis2du12Sample(accel, (uint8_t *) &accel_data))
    {
        orient_accel_raw(&accel_data.x, &accel_data.y);
        data->ax = (accel_data.x/16);
        data->ay = (accel_data.y/16);
        data->az = (accel_data.z/16);
    } else {
      ok = false;
    }
    return ok;
}


/**
 * @brief Sample sensors in engineering units for live calibration.
 *
 * @param[out] sensors Protobuf sensor-data payload to populate.
 * @return true after attempting the calibration sample.
 */
bool sensorCalibrationSample(SensorData *sensors)
{
    uint8_t buf[11];
    struct {
        int16_t x;
        int16_t y;
        int16_t z;
    } accel_data;
    const TagRegisterDevice *accel;

    int x = 0;
    int y = 0;
    int z = 0;
    //int t = 0;
   
    if (ak09940aSample(TAG_MAG_DEVICE, false, buf))
    {
      // keep all 18 bits
      sensors->has_mag = true;
      x = ((int) (buf[2]<<30)|(buf[1]<<22) | (buf[0] << 14))>>14;
      y = ((int) (buf[5]<<30)|(buf[4]<<22) | (buf[3] << 14))>>14;
      z = ((int) (buf[8]<<30)|(buf[7]<<22) | (buf[6] << 14))>>14;

      orient_mag_values(&x, &y, &z);
     
      sensors->mag.mx = x * 0.01f;
      sensors->mag.my = y * 0.01f;
      sensors->mag.mz = z * 0.01f;
    }

    accel = TAG_ACCEL_DEVICE;
    if (lis2du12Sample(accel, (uint8_t *) &accel_data))
    {
        sensors->has_accel = true;

        orient_accel_raw(&accel_data.x, &accel_data.y);
        
        sensors->accel.ax = (accel_data.x/16) * 0.976f;
        sensors->accel.ay = (accel_data.y/16) * 0.976f;
        sensors->accel.az = (accel_data.z/16) * 0.976f;
    }

    return true;
}

/**
 * @brief Start sensors in continuous mode for calibration streaming.
 *
 * @return true when sensor startup commands have been issued.
 */
bool initSensors(void){
    lis2du12Init(TAG_ACCEL_DEVICE, ACCEL_SAMPLE_100HZ_MODE);
    ak09940aDeviceBegin(TAG_MAG_DEVICE);
    stopMilliseconds(1);
    tagCompassMagResetRelease();
    ak09940aInitContinuous(TAG_MAG_DEVICE, AK09940_RATE_100HZ,
                           AK09940_DRIVE_LOW_NOISE_2,
                           AK09940_TEMP_DISABLED);
    return true;
}

/**
 * @brief Stop calibration sensors and return them to low-power state.
 *
 * @return true when shutdown commands have been issued.
 */
bool deinitSensors(void) {
    ak09940aDeviceEnd(TAG_MAG_DEVICE);
    tagCompassMagResetAssert();
    lis2du12Deinit(TAG_ACCEL_DEVICE);
    return true;
}

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
    initSensors();
    // start sensors
    pState->state = TagState_CALIBRATE;
  }
  if (MONCONNECTED)
    return SHUTDOWN;
  else
  {
    // shutdown sensors
    deinitSensors();
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
 * @brief Store magnetometer calibration constants in reserved flash.
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
  }

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
  chSysUnlock();
  return errAck(Ack_OK);
}

/**
 * @brief Read magnetometer calibration constants from reserved flash.
 *
 * @param[in] index Calibration slot to read, or negative for the latest slot.
 * @param[out] ack ACK message to populate.
 * @return Encoded ACK length or error response length.
 */
int read_calibration(int32_t index, Ack *ack){
  // if index < 0, search for last written
  if (index < 0) {
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
}
