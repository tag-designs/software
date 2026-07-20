
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

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
#include "bmm350_tag.h"
#else
#include "ak09940a.h"
#endif
#include "lsm6dsv16x.h"
#include "lps22hh.h"
#include "sensors.h"


#define IMU_ACCEL_MG_PER_LSB_4G 0.122f
#define IMU_FIFO_PAIRS_PER_SUPERFRAME IMUTAG_IMU_SAMPLES_PER_SUPERFRAME
#define IMU_FIFO_WORDS_PER_PAIR 2U
#define IMU_FIFO_SUPERFRAME_WORDS \
  (IMU_FIFO_PAIRS_PER_SUPERFRAME * IMU_FIFO_WORDS_PER_PAIR)
#define IMU_FIFO_WATERMARK_WORDS IMU_FIFO_SUPERFRAME_WORDS
#define IMU_DATA_WAKE_EXTI_MASK (1U << PAL_PAD(LINE_WKUP1))
#if defined(TAG_STOP1_WAKE_USES_INTERRUPT) && TAG_STOP1_WAKE_USES_INTERRUPT
#define IMU_DATA_WAKE_EXTI_ACTION EXTI_MODE_ACTION_INTERRUPT
#else
#define IMU_DATA_WAKE_EXTI_ACTION EXTI_MODE_ACTION_EVENT
#endif
#define MAG_MISSED_RECOVERY_THRESHOLD 16U

typedef struct {
    int32_t timestamp;
    CalibrationConstants_MagConstants constants;
#if defined(IMUTAG_STM32U3_FLASH) && IMUTAG_STM32U3_FLASH
    uint64_t flash_padding;
} sensor_constants_t __attribute__((aligned(16)));
#else
} sensor_constants_t __attribute__((aligned(8)));
#endif

sensor_constants_t constants_tmp NOINIT;



#define CONSTANT_CNT (2048/sizeof(sensor_constants_t))

// calibration constants in reserved flash section

sensor_constants_t calConstants[CONSTANT_CNT] __attribute__((section(".calibration")));

bool sensorsHaveCalibration(void)
{
  for (unsigned int index = 0; index < CONSTANT_CNT; index++)
  {
    if ((*((int32_t *)&calConstants[index])) != -1)
      return true;
  }
  return false;
}

// check if these variables can be noinit

static unsigned int empty_calibration_sample_logs;
static float latest_pressure;
static int16_t latest_rawtemp;
static float latest_mx;
static float latest_my;
static float latest_mz;
static bool have_pressure_sample;
static bool have_mag_sample;
static lsm6dsv16x_sample_t block_samples[IMU_FIFO_PAIRS_PER_SUPERFRAME];
static uint16_t block_sample_count;
static uint16_t consecutive_mag_misses;
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
static bmm350_rate_t configured_mag_rate;
#else
static ak09940_rate_t configured_mag_rate;
#endif

static void enable_data_collection_wake_event(void)
{
  extiClearGroup1(IMU_DATA_WAKE_EXTI_MASK);
  extiEnableGroup1(IMU_DATA_WAKE_EXTI_MASK,
                   EXTI_MODE_RISING_EDGE | IMU_DATA_WAKE_EXTI_ACTION);
}

static void disable_data_collection_wake_event(void)
{
  extiEnableGroup1(IMU_DATA_WAKE_EXTI_MASK, EXTI_MODE_DISABLED);
  extiClearGroup1(IMU_DATA_WAKE_EXTI_MASK);
}

static inline void orient_imu_xyz(int16_t *x, int16_t *y, int16_t *z)
{
  int16_t raw_x = *x;
  int16_t raw_y = *y;

  *x = (int16_t)-raw_y;
  *y = raw_x;
  (void)z;
}

#if !defined(TAG_SENSOR_MAG_BMM350) || !TAG_SENSOR_MAG_BMM350
static int32_t decode_mag_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
  if ((raw & 0x20000) != 0)
    raw |= (int32_t)0xFFFC0000;
  return raw;
}
#endif

static int16_t clamp_i16(int32_t value)
{
  if (value > INT16_MAX)
    return INT16_MAX;
  if (value < INT16_MIN)
    return INT16_MIN;
  return (int16_t)value;
}

static float missing_aux_sample(void)
{
  union {
    uint32_t raw;
    float value;
  } missing = {0x7fc00000U};

  return missing.value;
}

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
static bmm350_rate_t select_mag_rate(lsm6dsv16x_trig_odr_t imu_odr)
{
  switch (imu_odr) {
  case LSM6DSV16X_TRIG_ODR_50HZ:
  case LSM6DSV16X_TRIG_ODR_100HZ:
    return BMM350_RATE_12_5HZ;
  case LSM6DSV16X_TRIG_ODR_200HZ:
    return BMM350_RATE_25HZ;
  case LSM6DSV16X_TRIG_ODR_400HZ:
    return BMM350_RATE_50HZ;
  case LSM6DSV16X_TRIG_ODR_800HZ:
  case LSM6DSV16X_TRIG_ODR_1600HZ:
    return BMM350_RATE_100HZ;
  default:
    return BMM350_RATE_50HZ;
  }
}
#else
static ak09940_rate_t select_mag_rate(lsm6dsv16x_trig_odr_t imu_odr)
{
  switch (imu_odr) {
  case LSM6DSV16X_TRIG_ODR_50HZ:
  case LSM6DSV16X_TRIG_ODR_100HZ:
    return AK09940_RATE_10HZ;
  case LSM6DSV16X_TRIG_ODR_200HZ:
    return AK09940_RATE_20HZ;
  case LSM6DSV16X_TRIG_ODR_400HZ:
    return AK09940_RATE_50HZ;
  case LSM6DSV16X_TRIG_ODR_800HZ:
    return AK09940_RATE_100HZ;
  case LSM6DSV16X_TRIG_ODR_1600HZ:
    return AK09940_RATE_200HZ;
  default:
    return AK09940_RATE_50HZ;
  }
}
#endif

static lps22hh_odr_t select_pressure_rate(lsm6dsv16x_trig_odr_t imu_odr)
{
  switch (imu_odr) {
  case LSM6DSV16X_TRIG_ODR_50HZ:
    return LPS22HH_ODR_10HZ;
  case LSM6DSV16X_TRIG_ODR_100HZ:
    return LPS22HH_ODR_25HZ;
  case LSM6DSV16X_TRIG_ODR_200HZ:
    return LPS22HH_ODR_25HZ;
  case LSM6DSV16X_TRIG_ODR_400HZ:
    return LPS22HH_ODR_50HZ;
  case LSM6DSV16X_TRIG_ODR_800HZ:
    return LPS22HH_ODR_100HZ;
  case LSM6DSV16X_TRIG_ODR_1600HZ:
    return LPS22HH_ODR_200HZ;
  default:
    return LPS22HH_ODR_50HZ;
  }
}

static void reset_environment_cache(void)
{
  latest_pressure = missing_aux_sample();
  latest_rawtemp = 0;
  latest_mx = missing_aux_sample();
  latest_my = missing_aux_sample();
  latest_mz = missing_aux_sample();
  have_pressure_sample = false;
  have_mag_sample = false;
  consecutive_mag_misses = 0U;
}

static void reset_imu_block_cache(void)
{
  memset(block_samples, 0, sizeof(block_samples));
  block_sample_count = 0;
}

static void init_mag_data_ready_line(void)
{
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  toInput(TAG_MAG_DEVICE->drdy);
#else
  toInput(LINE_MAG_TRG);
#endif
}

static bool mag_data_ready(void)
{
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  return bmm350DataReady(TAG_MAG_DEVICE);
#else
  return palReadLine(LINE_MAG_TRG) == PAL_HIGH;
#endif
}

static void init_pressure_data_ready_line(void)
{
  toInput(LINE_LPS_DRDY);
}

static bool pressure_data_ready(void)
{
  return palReadLine(LINE_LPS_DRDY) == PAL_HIGH;
}

static bool pressure_sample_ready(void)
{
  if (pressure_data_ready())
    return true;

  return lps22hh_data_ready_device(TAG_PRESSURE_DEVICE);
}

static bool configure_mag_collection(bool reset_first)
{
  bool ok = true;

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  (void)reset_first;
  bmm350DeviceBegin(TAG_MAG_DEVICE);
  if (bmm350InitContinuous(TAG_MAG_DEVICE, configured_mag_rate,
                           BMM350_PERF_LOW_NOISE) != MSG_OK) {
    debug_log_printf("IMUTag collection: BMM350 init failed\r\n");
    ok = false;
  }
  bmm350DeviceEnd(TAG_MAG_DEVICE);
#else
  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  if (reset_first && (ak09940aReset(TAG_MAG_DEVICE) != MSG_OK)) {
    debug_log_printf("IMUTag collection: AK09940A reset failed\r\n");
    ok = false;
  }
  if (ok && ak09940aInitContinuous(TAG_MAG_DEVICE, configured_mag_rate,
                                   AK09940_DRIVE_LOW_NOISE_2,
                                   AK09940_TEMP_DISABLED) != MSG_OK) {
    debug_log_printf("IMUTag collection: AK09940A init failed\r\n");
    ok = false;
  }
  ak09940aDeviceEnd(TAG_MAG_DEVICE);
#endif

  if (ok)
    consecutive_mag_misses = 0U;
  return ok;
}

static void record_due_mag_sample(bool sampled)
{
  if (sampled) {
    consecutive_mag_misses = 0U;
    return;
  }

  if (consecutive_mag_misses < MAG_MISSED_RECOVERY_THRESHOLD)
    consecutive_mag_misses++;

  if (consecutive_mag_misses >= MAG_MISSED_RECOVERY_THRESHOLD) {
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
    debug_log_printf("IMUTag collection: recovering BMM350 after %u misses\r\n",
                     (unsigned)consecutive_mag_misses);
#else
    debug_log_printf("IMUTag collection: recovering AK09940A after %u misses\r\n",
                     (unsigned)consecutive_mag_misses);
#endif
    (void)configure_mag_collection(true);
    consecutive_mag_misses = 0U;
  }
}

#if !defined(TAG_SENSOR_MAG_BMM350) || !TAG_SENSOR_MAG_BMM350
static bool read_mag_payload_if_ready(uint8_t *buf)
{
  bool ready;
  bool ok;

  ready = mag_data_ready();

  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  if (!ready)
    ready = ak09940aDataReady(TAG_MAG_DEVICE, true);
  ok = ready &&
       (tagRegisterRead(TAG_MAG_DEVICE, AK09940A_HXL, buf, 11U) == MSG_OK);
  ak09940aDeviceEnd(TAG_MAG_DEVICE);

  if (!ok)
    return false;

  /*
   * The magnetometer is free-running faster than the log block rate, so DOR
   * is expected when an older sample was skipped. For this path we only need
   * the latest complete environmental sample; reject invalid FIFO reads, but
   * accept overrun-marked continuous-mode data.
   */
  return (buf[10] & AK09940A_ST2_INV_MSK) == 0;
}
#endif

static bool update_latest_mag(void)
{
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  bool ready;
  bool ok;

  ready = mag_data_ready();
  bmm350DeviceBegin(TAG_MAG_DEVICE);
  ok = ready &&
       (bmm350ReadMagUT(TAG_MAG_DEVICE, &latest_mx, &latest_my,
                        &latest_mz) == MSG_OK);
  bmm350DeviceEnd(TAG_MAG_DEVICE);

  if (!ok)
    return false;

  have_mag_sample = true;
  return true;
#else
  uint8_t buf[11];

  if (!read_mag_payload_if_ready(buf))
    return false;

  ak09940_convert_to_uT(decode_mag_18bit(buf[0], buf[1], buf[2]),
                        decode_mag_18bit(buf[3], buf[4], buf[5]),
                        decode_mag_18bit(buf[6], buf[7], buf[8]),
                        &latest_mx, &latest_my, &latest_mz);

  have_mag_sample = true;
  return true;
#endif
}

static bool update_latest_pressure(void)
{
  int32_t pressure;
  int32_t temperature;
  int rc;

  if (!pressure_sample_ready())
    return false;

  rc = lps22hh_read_raw_device(TAG_PRESSURE_DEVICE, &pressure, &temperature);
  if (rc == 0) {
    latest_pressure = pressure * (float)IMUTAG_PRESSURE_HPA_PER_LSB;
    latest_rawtemp = clamp_i16(temperature);
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
 * The IMU FIFO watermark is the superframe pacing source. Magnetometer and
 * pressure run freely at rates selected to stay above the 1:10 auxiliary
 * polling cadence used by the page log.
 *
 * @return true when the configured collection mode was accepted.
 */
bool initDataCollection(void)
{
  lsm6dsv16x_trig_odr_t imu_odr;
  lsm6dsv16x_xl_fs_t xl_fs;
  lsm6dsv16x_g_fs_t g_fs;
  lsm6dsv16x_trig_mode_cfg_t trig_cfg;
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  bmm350_rate_t mag_rate;
#else
  ak09940_rate_t mag_rate;
#endif
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
  configured_mag_rate = mag_rate;
  pressure_rate = select_pressure_rate(imu_odr);
  reset_environment_cache();
  reset_imu_block_cache();
  init_mag_data_ready_line();
  init_pressure_data_ready_line();

  if (!configure_mag_collection(true)) {
    ok = false;
  }

  if (lps22hh_config_continuous_device(TAG_PRESSURE_DEVICE, pressure_rate,
                                       LPS22HH_LPF_DISABLE) != 0) {
    debug_log_printf("IMUTag collection: LPS22HH init failed\r\n");
    ok = false;
  }

  lsm6dsv16x_init_accel_gyro_triggered(TAG_IMU_DEVICE, &trig_cfg, NULL);
  lsm6dsv16x_set_fifo_watermark(TAG_IMU_DEVICE, IMU_FIFO_WATERMARK_WORDS);
  enable_data_collection_wake_event();

  debug_log_printf("IMUTag collection: IMU %u Hz, FIFO watermark %u words\r\n",
                   (unsigned)imu_odr,
                   (unsigned)IMU_FIFO_WATERMARK_WORDS);
  return ok;
}

/**
 * @brief Fill one log superframe from the IMU FIFO and environment sensors.
 *
 * @param[out] frame Destination log superframe.
 * @return true when one complete accel/gyro and auxiliary superframe was copied.
 */
bool sampleDataCollection(t_ImuTagSuperFrame *frame)
{
  uint8_t wtm = 0;
  uint8_t ovr = 0;
  uint16_t fifo_words;
  uint16_t pairs;

  if (frame == NULL)
    return false;

  fifo_words = lsm6dsv16x_read_fifo_status(TAG_IMU_DEVICE, &wtm, &ovr);
  if (ovr != 0U) {
    debug_log_printf("IMUTag collection: IMU FIFO overrun\r\n");
  }
  if (fifo_words < IMU_FIFO_WATERMARK_WORDS) {
    return false;
  }

  pairs = lsm6dsv16x_read_fifo(TAG_IMU_DEVICE,
                               &block_samples[block_sample_count],
                               IMU_FIFO_PAIRS_PER_SUPERFRAME - block_sample_count);
  if (pairs == 0U) {
    return false;
  }
  block_sample_count += pairs;

  if (block_sample_count < IMU_FIFO_PAIRS_PER_SUPERFRAME) {
    return false;
  }

  memset(frame, 0, sizeof(*frame));

  for (uint16_t i = 0; i < IMU_FIFO_PAIRS_PER_SUPERFRAME; i++) {
    int16_t gx = block_samples[i].gyro_x;
    int16_t gy = block_samples[i].gyro_y;
    int16_t gz = block_samples[i].gyro_z;
    int16_t ax = block_samples[i].accel_x;
    int16_t ay = block_samples[i].accel_y;
    int16_t az = block_samples[i].accel_z;

    orient_imu_xyz(&gx, &gy, &gz);
    orient_imu_xyz(&ax, &ay, &az);

    frame->imu[i].ax = ax;
    frame->imu[i].ay = ay;
    frame->imu[i].az = az;
    frame->imu[i].gx = gx;
    frame->imu[i].gy = gy;
    frame->imu[i].gz = gz;
  }
  reset_imu_block_cache();

  bool mag_sampled = update_latest_mag();
  bool pressure_sampled = update_latest_pressure();

  record_due_mag_sample(mag_sampled);

  frame->aux.pressure = pressure_sampled ? latest_pressure : missing_aux_sample();
  frame->aux.mag_x = mag_sampled ? latest_mx : missing_aux_sample();
  frame->aux.mag_y = mag_sampled ? latest_my : missing_aux_sample();
  frame->aux.mag_z = mag_sampled ? latest_mz : missing_aux_sample();

  return true;
}

/**
 * @brief Return the latest raw pressure-sensor temperature.
 *
 * The cache is refreshed by sampleDataCollection(), which reads pressure and
 * temperature as one coherent LPS22HH burst. The LPS22HH reports temperature
 * in hundredths of a degree C; conversion is deferred to the log export path.
 *
 * @param[out] rawtemp Latest cached raw temperature in 0.01 C units.
 * @return true once at least one pressure/temperature sample has been read.
 */
bool latestDataCollectionRawTemp(int16_t *rawtemp)
{
  if (rawtemp == NULL || !have_pressure_sample)
    return false;

  *rawtemp = latest_rawtemp;
  return true;
}

/**
 * @brief Stop collection sensors and return them to low-power state.
 *
 * @return true when shutdown commands have been issued.
 */
bool deinitDataCollection(void)
{
  disable_data_collection_wake_event();

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  bmm350DeviceBegin(TAG_MAG_DEVICE);
  (void)bmm350InitPowerDown(TAG_MAG_DEVICE);
  bmm350DeviceEnd(TAG_MAG_DEVICE);
#else
  ak09940aDeviceBegin(TAG_MAG_DEVICE);
  (void)ak09940aInitPowerDown(TAG_MAG_DEVICE);
  ak09940aDeviceEnd(TAG_MAG_DEVICE);
#endif

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
#if !defined(TAG_SENSOR_MAG_BMM350) || !TAG_SENSOR_MAG_BMM350
    uint8_t buf[11];
#endif
    int16_t ax = 0;
    int16_t ay = 0;
    int16_t az = 0;

    memset(sensors, 0, sizeof(*sensors));

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
    if (mag_data_ready())
    {
      bmm350DeviceBegin(TAG_MAG_DEVICE);
      if (bmm350ReadMagUT(TAG_MAG_DEVICE, &sensors->mag.mx,
                          &sensors->mag.my,
                          &sensors->mag.mz) == MSG_OK)
      {
        sensors->has_mag = true;
      }
      bmm350DeviceEnd(TAG_MAG_DEVICE);
    }
#else
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
#endif

    if (lsm6dsv16x_read_accel(TAG_IMU_DEVICE, &ax, &ay, &az) == 0)
    {
        sensors->has_accel = true;
        orient_imu_xyz(&ax, &ay, &az);

        sensors->accel.ax = ax * IMU_ACCEL_MG_PER_LSB_4G;
        sensors->accel.ay = ay * IMU_ACCEL_MG_PER_LSB_4G;
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

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
    bmm350DeviceBegin(TAG_MAG_DEVICE);
    if (bmm350InitContinuous(TAG_MAG_DEVICE, BMM350_RATE_100HZ,
                             BMM350_PERF_LOW_NOISE) != MSG_OK)
    {
      debug_log_printf("IMUTag calibration: BMM350 continuous init failed\r\n");
    }
    bmm350DeviceEnd(TAG_MAG_DEVICE);
#else
    ak09940aDeviceBegin(TAG_MAG_DEVICE);
    if (ak09940aInitContinuous(TAG_MAG_DEVICE, AK09940_RATE_100HZ,
                               AK09940_DRIVE_LOW_NOISE_2,
                               AK09940_TEMP_DISABLED) != MSG_OK)
    {
      debug_log_printf("IMUTag calibration: AK09940A continuous init failed\r\n");
    }
    ak09940aDeviceEnd(TAG_MAG_DEVICE);
#endif
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
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
    bmm350DeviceBegin(TAG_MAG_DEVICE);
    (void)bmm350InitPowerDown(TAG_MAG_DEVICE);
    bmm350DeviceEnd(TAG_MAG_DEVICE);
#else
    ak09940aDeviceBegin(TAG_MAG_DEVICE);
    (void)ak09940aInitPowerDown(TAG_MAG_DEVICE);
    ak09940aDeviceEnd(TAG_MAG_DEVICE);
#endif
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
