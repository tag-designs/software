
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
#include "debug_log.h"
#include "devices.h"
#include "flash_internal.h"
#include "persistent.h"

#include "ak09940a.h"
#include "lsm6dsv16x.h"
#include "sensors.h"


#define IMU_ACCEL_MG_PER_LSB_4G 0.122f

typedef struct {
    int32_t timestamp;
    CalibrationConstants_MagConstants constants;
} sensor_constants_t __attribute__((aligned(8))); 

sensor_constants_t constants_tmp NOINIT;



#define CONSTANT_CNT (2048/sizeof(sensor_constants_t))

// calibration constants in reserved flash section

sensor_constants_t calConstants[CONSTANT_CNT] __attribute__((section(".calibration")));

static unsigned int empty_calibration_sample_logs;

static int32_t decode_mag_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
  if ((raw & 0x20000) != 0)
    raw |= (int32_t)0xFFFC0000;
  return raw;
}

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
  return true;
}


/**
 * @brief Populate a live calibration sample.
 *
 * Magnetometer samples are reported in microtesla. Accelerometer samples are
 * reported in mg using the calibration-mode +/-4 g scale configured by
 * initSensors().
 *
 * @param[out] sensors Protobuf sensor-data payload to populate.
 * @return true after attempting the calibration sample.
 */
bool sensorCalibrationSample(SensorData *sensors)
{
    uint8_t buf[11];
    int16_t ax = 0;
    int16_t ay = 0;
    int16_t az = 0;

    memset(sensors, 0, sizeof(*sensors));

    ak09940aDeviceBegin(TAG_MAG_DEVICE);
    if (ak09940aSample(TAG_MAG_DEVICE, false, buf))
    {
      int32_t mx_raw = decode_mag_18bit(buf[0], buf[1], buf[2]);
      int32_t my_raw = decode_mag_18bit(buf[3], buf[4], buf[5]);
      int32_t mz_raw = decode_mag_18bit(buf[6], buf[7], buf[8]);

      sensors->has_mag = true;
      ak09940_convert_to_uT(mx_raw, my_raw, mz_raw,
                            &sensors->mag.mx,
                            &sensors->mag.my,
                            &sensors->mag.mz);
    }
    ak09940aDeviceEnd(TAG_MAG_DEVICE);

    if (lsm6dsv16x_read_accel(TAG_IMU_DEVICE, &ax, &ay, &az) == 0)
    {
        sensors->has_accel = true;

        // fix sensor orientation to match compass tag

        sensors->accel.ax = -ay * IMU_ACCEL_MG_PER_LSB_4G;
        sensors->accel.ay = ax * IMU_ACCEL_MG_PER_LSB_4G;
        sensors->accel.az = az * IMU_ACCEL_MG_PER_LSB_4G;
    }

    if (!sensors->has_mag && !sensors->has_accel &&
        empty_calibration_sample_logs < 4U)
    {
      debug_log_printf("IMUTag calibration: sample had no mag or accel data\r\n");
      empty_calibration_sample_logs++;
    }

    return true;
}

/**
 * @brief Start magnetometer and IMU accelerometer for live calibration.
 *
 * @return true when the calibration sensor configuration completes.
 */
bool initSensors(void){
    lsm6dsv16x_accel_cfg_t accel_cfg = {
        .odr = LSM6DSV16X_XL_ODR_120Hz,
    };
    lsm6dsv16x_range_cfg_t range_cfg = {
        .xl_fs = LSM6DSV16X_XL_FS_4G,
        .g_fs = LSM6DSV16X_G_FS_2000DPS,
    };

    ak09940aDeviceBegin(TAG_MAG_DEVICE);
    if (ak09940aInitContinuous(TAG_MAG_DEVICE, AK09940_RATE_100HZ,
                               AK09940_DRIVE_LOW_NOISE_2,
                               AK09940_TEMP_DISABLED) != MSG_OK)
    {
      debug_log_printf("IMUTag calibration: AK09940A continuous init failed\r\n");
    }
    ak09940aDeviceEnd(TAG_MAG_DEVICE);

    lsm6dsv16x_init_accel_only(TAG_IMU_DEVICE, &accel_cfg);
    lsm6dsv16x_set_ranges(TAG_IMU_DEVICE, &range_cfg);
    empty_calibration_sample_logs = 0;
    debug_log_printf("IMUTag calibration: sensors initialized\r\n");
    return true;
}

/**
 * @brief Stop calibration sensors and return them to low-power state.
 *
 * @return true when shutdown commands complete.
 */
bool deinitSensors(void) {
    ak09940aDeviceBegin(TAG_MAG_DEVICE);
    (void)ak09940aInitPowerDown(TAG_MAG_DEVICE);
    ak09940aDeviceEnd(TAG_MAG_DEVICE);
    lsm6dsv16x_init_shutdown(TAG_IMU_DEVICE);
    debug_log_printf("IMUTag calibration: sensors deinitialized\r\n");
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

  if (t == T_EXIT)
  {
    deinitSensors();
    pState->state = TagState_IDLE;
    return SHUTDOWN;
  }

  if (t == T_INIT)
  {
    initSensors();
    pState->state = TagState_CALIBRATE;
  }
  if (MONCONNECTED)
    return SHUTDOWN;
  else
  {
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
 * @brief Accept calibration constants from the host.
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
 * @brief Read calibration constants into a host ACK.
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
