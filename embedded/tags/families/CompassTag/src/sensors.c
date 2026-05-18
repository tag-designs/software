
#include <tag.pb.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "core_types.h"
#include "flash_internal.h"
#include "persistent.h"

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

bool sensorSample(RawSensorData *data){
  bool ok = true;
  uint8_t buf[11];
  struct {
      int16_t x;
      int16_t y;
      int16_t z;
  } accel_data;
  const TagMagDevice *mag;
  const TagLis2du12Device *accel;
  int mx = 0;
  int my = 0;
  int mz = 0;

  memset(data,0,sizeof(*data));

  mag = tagAk09940aDevice();
  ak09940aInit(mag, MAG_SAMPLE_SINGLE_MODE);
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

  accel = tagLis2du12Device();
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


bool sensorCalibrationSample(SensorData *sensors)
{
    uint8_t buf[11];
    struct {
        int16_t x;
        int16_t y;
        int16_t z;
    } accel_data;
    const TagLis2du12Device *accel;

    int x = 0;
    int y = 0;
    int z = 0;
    //int t = 0;
   
    if (ak09940aSample(tagAk09940aDevice(), false, buf))
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

    accel = tagLis2du12Device();
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

bool initSensors(void){
    lis2du12Init(tagLis2du12Device(), ACCEL_SAMPLE_100HZ_MODE);
    ak09940aInit(tagAk09940aDevice(), MAG_SAMPLE_100HZ_MODE);
    return true;
}

bool deinitSensors(void) {
    ak09940aDeviceEnd(tagAk09940aDevice());
    lis2du12Deinit(tagLis2du12Device());
    return true;
}

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

int calibration_logAck(Ack *ack){
  CalibrationLog *data = &ack->payload.calibration_log;
  ack->err = Ack_OK;
  ack->which_payload = Ack_calibration_log_tag;
  data->data_count = 1;
  sensorCalibrationSample(&data->data[0]);
  return encode_ack();
}

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
