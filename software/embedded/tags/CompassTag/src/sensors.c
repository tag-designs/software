
#include <tag.pb.h>
#include <stdint.h>
#include <strings.h>
#include "hal.h"
#include "monitor.h"
#include "app.h"
#include "persistent.h"
#include "config.h"

#include "lis2du12.h"
#include "ak09940a.h"


void magOn(void);
void magOff(void);

// calibration constants on page boundary

CalibrationConstants_MagConstants calConstants[42] __attribute__((aligned (2048)));


bool sensorSample(SensorData *sensors)
{
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
      sensors->has_mag = true;
      x = ((int) (buf[2]<<30)|(buf[1]<<22) | (buf[0] << 14))>>14;
      y = ((int) (buf[5]<<30)|(buf[4]<<22) | (buf[3] << 14))>>14;
      z = ((int) (buf[8]<<30)|(buf[7]<<22) | (buf[6] << 14))>>14;
      //t = ((int)(buf[9]));

      // orientation for compass proto board

      sensors->mag.mx = -x * 0.01f;
      sensors->mag.my = -y * 0.01f;
      sensors->mag.mz = z * 0.01f;
    }
    //sensors->mag.temperature = 30.0f - t/1.7f;

    if (accelSample((uint8_t *) &accel_data))
    {
        sensors->has_accel = true;
        sensors->accel.ay = (accel_data.x/16) * 0.976f;
        sensors->accel.ax = -(accel_data.y/16) * 0.976f;
        sensors->accel.az = (accel_data.z/16) * 0.976f;
    }

    return true;
}
bool initSensors(void){
    accelInit(SAMPLE_100HZ);
    magInit(AK09940A_CNTL3_100HZ);
    return true;
}
bool deinitSensors(void) {
    magOff();
    accelDeinit();
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
  sensorSample(&data->data[0]);
  return encode_ack();
}

int write_calibration(CalibrationConstants *constants){
  if (!constants->has_magnetometer)
    return errAck(Ack_INVAL);
  if ((*((int32_t *) &calConstants[0])) != -1)
  {
    // erase block
    chSysLock();
    FLASH_Unlock();
    FLASH_PageErase((uint32_t) calConstants);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
  }

  // write constants

  chSysLock();
  FLASH_Unlock();
  FLASH_Program_Array((uint32_t *) calConstants, 
                      (uint32_t *) &(constants->magnetometer),
                      sizeof(CalibrationConstants_MagConstants)/4);
  FLASH_Lock();
  FLASH_Flush_Data_Cache();
  chSysUnlock();
  return errAck(Ack_OK);
}

int read_calibration(Ack *ack){
  if ((*((int32_t *) &calConstants[0])) == -1)
    return errAck(Ack_NODATA);
  ack->err = Ack_OK;
  ack->which_payload = Ack_calibration_constants_tag;
  ack->payload.calibration_constants.has_magnetometer = true;
  memcpy(&ack->payload.calibration_constants.magnetometer, calConstants,  sizeof(CalibrationConstants_MagConstants));
  return encode_ack();
}




