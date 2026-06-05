
/**
 * @file sensors.c
 * @brief IMUTagBreakout sensor sampling and calibration storage stubs.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include <tag.pb.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "config.h"
#include "core_types.h"
#include "datalog.h"
#include "debug_log.h"
#include "devices.h"
#include "flash_internal.h"
#include "gpio_utils.h"
#include "persistent.h"

#include "ak09940a.h"
#include "lsm6dsv16x.h"
#include "lps22hh.h"
#include "sensors.h"


#define IMU_ACCEL_MG_PER_LSB_4G 0.122f
#define IMU_FIFO_PAIRS_PER_BLOCK 10U
#define IMU_FIFO_WORDS_PER_PAIR 2U
#define IMU_FIFO_BLOCK_WORDS \
  (IMU_FIFO_PAIRS_PER_BLOCK * IMU_FIFO_WORDS_PER_PAIR)
#define IMU_FIFO_WATERMARK_WORDS (IMU_FIFO_BLOCK_WORDS + 2U)

typedef struct {
    int32_t timestamp;
    CalibrationConstants_MagConstants constants;
} sensor_constants_t __attribute__((aligned(8))); 

sensor_constants_t constants_tmp NOINIT;



#define CONSTANT_CNT (2048/sizeof(sensor_constants_t))

// calibration constants in reserved flash section

sensor_constants_t calConstants[CONSTANT_CNT] __attribute__((section(".calibration")));

// check if these variables can be noinit

static unsigned int empty_calibration_sample_logs;
static int16_t latest_pressure;
static int16_t latest_temp10;
static int16_t latest_mx;
static int16_t latest_my;
static int16_t latest_mz;
static bool have_pressure_sample;
static bool have_mag_sample;
static lsm6dsv16x_sample_t block_samples[IMU_FIFO_PAIRS_PER_BLOCK];
static uint16_t block_sample_count;

static int32_t decode_mag_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
  if ((raw & 0x20000) != 0)
    raw |= (int32_t)0xFFFC0000;
  return raw;
}

static int16_t clamp_i16(int32_t value)
{
  if (value > INT16_MAX)
    return INT16_MAX;
  if (value < INT16_MIN)
    return INT16_MIN;
  return (int16_t)value;
}

static int16_t compact_mag_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  return clamp_i16(decode_mag_18bit(l, m, h) >> 2);
}

static int16_t compact_pressure_raw(int32_t pressure)
{
  return clamp_i16(pressure >> 8);
}

static int16_t compact_temperature_raw(int32_t temperature)
{
  return clamp_i16(temperature / 10);
}

static ak09940_rate_t select_mag_rate(lsm6dsv16x_trig_odr_t imu_odr)
{
  switch (imu_odr) {
  case LSM6DSV16X_TRIG_ODR_50HZ:
    return AK09940_RATE_10HZ;
  case LSM6DSV16X_TRIG_ODR_100HZ:
    return AK09940_RATE_20HZ;
  case LSM6DSV16X_TRIG_ODR_200HZ:
  case LSM6DSV16X_TRIG_ODR_400HZ:
    return AK09940_RATE_50HZ;
  case LSM6DSV16X_TRIG_ODR_800HZ:
    return AK09940_RATE_100HZ;
  default:
    return AK09940_RATE_200HZ;
  }
}

static lps22hh_odr_t select_pressure_rate(lsm6dsv16x_trig_odr_t imu_odr)
{
  switch (imu_odr) {
  case LSM6DSV16X_TRIG_ODR_50HZ:
    return LPS22HH_ODR_10HZ;
  case LSM6DSV16X_TRIG_ODR_100HZ:
  case LSM6DSV16X_TRIG_ODR_200HZ:
    return LPS22HH_ODR_25HZ;
  case LSM6DSV16X_TRIG_ODR_400HZ:
    return LPS22HH_ODR_50HZ;
  case LSM6DSV16X_TRIG_ODR_800HZ:
    return LPS22HH_ODR_100HZ;
  default:
    return LPS22HH_ODR_200HZ;
  }
}

static void reset_environment_cache(void)
{
  latest_pressure = 0;
  latest_temp10 = 0;
  latest_mx = 0;
  latest_my = 0;
  latest_mz = 0;
  have_pressure_sample = false;
  have_mag_sample = false;
}

static void reset_imu_block_cache(void)
{
  memset(block_samples, 0, sizeof(block_samples));
  block_sample_count = 0;
}

static void init_mag_data_ready_line(void)
{
  toInput(LINE_MAG_TRG);
}

static bool mag_data_ready(void)
{
  return palReadLine(LINE_MAG_TRG) == PAL_HIGH;
}

static void init_pressure_data_ready_line(void)
{
  toInput(LINE_LPS_DRDY);
}

static bool pressure_data_ready(void)
{
  return palReadLine(LINE_LPS_DRDY) == PAL_HIGH;
}

static bool read_mag_payload_if_ready(uint8_t *buf)
{
  bool ok;

  if (!mag_data_ready())
    return false;

  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  ok = (tagRegisterRead(TAG_MAG_DEVICE, AK09940A_HXL, buf, 11U) == MSG_OK);
  ak09940aDeviceEnd(TAG_MAG_DEVICE);

  if (!ok)
    return false;
  return (buf[10] & (AK09940A_ST2_INV_MSK | AK09940A_ST2_DOR_MSK)) == 0;
}

static bool update_latest_mag(void)
{
  uint8_t buf[11];

  if (!read_mag_payload_if_ready(buf))
    return false;

  latest_mx = compact_mag_18bit(buf[0], buf[1], buf[2]);
  latest_my = compact_mag_18bit(buf[3], buf[4], buf[5]);
  latest_mz = compact_mag_18bit(buf[6], buf[7], buf[8]);
  have_mag_sample = true;
  return true;
}

static bool update_latest_pressure(void)
{
  int32_t pressure;
  int32_t temperature;
  int rc;

  if (!pressure_data_ready())
    return false;

  rc = lps22hh_read_raw_device(TAG_PRESSURE_DEVICE, &pressure, &temperature);
  if (rc == 0) {
    latest_pressure = compact_pressure_raw(pressure);
    latest_temp10 = compact_temperature_raw(temperature);
    have_pressure_sample = true;
    return true;
  }
  return false;
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
 * @brief Configure continuous collection sensors.
 *
 * The IMU FIFO watermark is the block pacing source. Magnetometer and pressure
 * run freely at the lowest useful supported rate above one sample per ten IMU
 * samples, and sampleDataCollection() reads their latest values per block.
 *
 * @return true when the configured collection mode was accepted.
 */
bool initDataCollection(void)
{
  lsm6dsv16x_trig_odr_t imu_odr;
  lsm6dsv16x_xl_fs_t xl_fs;
  lsm6dsv16x_g_fs_t g_fs;
  lsm6dsv16x_trig_mode_cfg_t trig_cfg;
  ak09940_rate_t mag_rate;
  lps22hh_odr_t pressure_rate;
  bool ok = true;

  if (!get_lsm_config(&imu_odr, &xl_fs, &g_fs)) {
    debug_log_printf("IMUTag collection: invalid LSM6 config\r\n");
    return false;
  }

  trig_cfg.trig_odr = imu_odr;
  trig_cfg.xl_fs = xl_fs;
  trig_cfg.g_fs = g_fs;
  mag_rate = select_mag_rate(imu_odr);
  pressure_rate = select_pressure_rate(imu_odr);
  reset_environment_cache();
  reset_imu_block_cache();
  init_mag_data_ready_line();
  init_pressure_data_ready_line();

  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  if (ak09940aInitContinuous(TAG_MAG_DEVICE, mag_rate,
                             AK09940_DRIVE_LOW_NOISE_2,
                             AK09940_TEMP_DISABLED) != MSG_OK) {
    debug_log_printf("IMUTag collection: AK09940A init failed\r\n");
    ok = false;
  }
  ak09940aDeviceEnd(TAG_MAG_DEVICE);

  if (lps22hh_config_continuous_device(TAG_PRESSURE_DEVICE, pressure_rate,
                                       LPS22HH_LPF_DISABLE) != 0) {
    debug_log_printf("IMUTag collection: LPS22HH init failed\r\n");
    ok = false;
  }

  lsm6dsv16x_init_accel_gyro_triggered(TAG_IMU_DEVICE, &trig_cfg, NULL);
  lsm6dsv16x_set_fifo_watermark(TAG_IMU_DEVICE, IMU_FIFO_WATERMARK_WORDS);

  debug_log_printf("IMUTag collection: IMU %u Hz, FIFO watermark %u words\r\n",
                   (unsigned)imu_odr,
                   (unsigned)IMU_FIFO_WATERMARK_WORDS);
  return ok;
}

/**
 * @brief Fill one log block from the IMU FIFO and latest environment samples.
 *
 * @param[out] data Destination log block.
 * @return true when ten accel/gyro pairs were copied into the block.
 */
bool sampleDataCollection(t_DataLog *data)
{
  lsm6dsv16x_sample_t samples[IMU_FIFO_PAIRS_PER_BLOCK];
  uint8_t wtm = 0;
  uint8_t ovr = 0;
  uint16_t fifo_words;
  uint16_t pairs;

  if (data == NULL)
    return false;

  memset(data, 0, sizeof(*data));

  fifo_words = lsm6dsv16x_read_fifo_status(TAG_IMU_DEVICE, &wtm, &ovr);
  if (ovr != 0U) {
    debug_log_printf("IMUTag collection: IMU FIFO overrun\r\n");
  }
  if (fifo_words < IMU_FIFO_WATERMARK_WORDS) {
    return false;
  }

  pairs = lsm6dsv16x_read_fifo(TAG_IMU_DEVICE, samples,
                               IMU_FIFO_PAIRS_PER_BLOCK - block_sample_count);
  if (pairs == 0U) {
    return false;
  }

  for (uint16_t i = 0; i < pairs; i++) {
    block_samples[block_sample_count++] = samples[i];
  }

  if (block_sample_count < IMU_FIFO_PAIRS_PER_BLOCK) {
    return false;
  }

  for (uint16_t i = 0; i < IMU_FIFO_PAIRS_PER_BLOCK; i++) {
    data->raw_data[i].gx = block_samples[i].gyro_x;
    data->raw_data[i].gy = block_samples[i].gyro_y;
    data->raw_data[i].gz = block_samples[i].gyro_z;
    data->raw_data[i].ax = block_samples[i].accel_x;
    data->raw_data[i].ay = block_samples[i].accel_y;
    data->raw_data[i].az = block_samples[i].accel_z;
  }
  reset_imu_block_cache();

  (void)update_latest_mag();
  (void)update_latest_pressure();

  if (have_mag_sample) {
    data->mx = latest_mx;
    data->my = latest_my;
    data->mz = latest_mz;
  }
  if (have_pressure_sample) {
    data->pressure = latest_pressure;
  }

  return true;
}

/**
 * @brief Return the latest pressure-sensor temperature in tenths of a degree C.
 *
 * The cache is refreshed by sampleDataCollection(), which reads pressure and
 * temperature as one coherent LPS22HH burst.
 *
 * @param[out] temp10 Latest cached temperature in 0.1 C units.
 * @return true once at least one pressure/temperature sample has been read.
 */
bool latestDataCollectionTemp10(int16_t *temp10)
{
  if (temp10 == NULL || !have_pressure_sample)
    return false;

  *temp10 = latest_temp10;
  return true;
}

/**
 * @brief Stop collection sensors and return them to low-power state.
 *
 * @return true when shutdown commands have been issued.
 */
bool deinitDataCollection(void)
{
  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  (void)ak09940aInitPowerDown(TAG_MAG_DEVICE);
  ak09940aDeviceEnd(TAG_MAG_DEVICE);

  (void)lps22hh_set_idle_device(TAG_PRESSURE_DEVICE);
  lsm6dsv16x_init_shutdown(TAG_IMU_DEVICE);
  debug_log_printf("IMUTag collection: sensors deinitialized\r\n");
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

    if (read_mag_payload_if_ready(buf))
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
    init_mag_data_ready_line();

    lsm6dsv16x_init_accel_only(TAG_IMU_DEVICE, &accel_cfg);
    lsm6dsv16x_set_ranges(TAG_IMU_DEVICE, &range_cfg);
    tagImuTagSetTrigger(8U);
    empty_calibration_sample_logs = 0;
    debug_log_printf("IMUTag calibration: sensors initialized, PA4 trigger divider 8\r\n");
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
