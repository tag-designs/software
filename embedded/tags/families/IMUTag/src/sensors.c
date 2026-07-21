
/**
 * @file sensors.c
 * @brief IMUTagBreakout collection, live calibration, and calibration storage.
 * @author tag firmware authors
 * @date 2026-05-23
 *
 * Maintainer notes:
 * - This file is the IMUTag-family policy layer above descriptor-backed sensor
 *   drivers. Keep register sequencing and bus/session mechanics in the common
 *   sensor drivers; keep family choices such as rates, orientation, FIFO block
 *   shape, calibration mode, and recovery thresholds here.
 * - Normal collection is paced by the LSM6DSV16X triggered FIFO watermark.
 *   Each log superframe contains a full IMU block plus one auxiliary slot.
 *   Pressure and magnetometer devices free-run at lower rates and are sampled
 *   opportunistically after each complete IMU block.
 * - Auxiliary values intentionally do not reuse stale cache data in the frame
 *   when the current poll misses. A quiet NaN marks a missing pressure or
 *   magnetometer slot, while the latest raw pressure temperature remains
 *   cached separately for the environment header path in state_run.c.
 * - Live calibration is a separate monitor-driven mode. It streams individual
 *   magnetometer and accelerometer samples for host-side fitting and does not
 *   use the collection FIFO superframe path.
 * - User magnetometer calibration constants are stored as append-only records
 *   in the linker-provided .calibration flash section. Sensor factory trim
 *   data, such as BMM350 OTP compensation, belongs in the sensor driver and
 *   must not be mixed into this flash record format.
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

/**
 * @brief One flash record for host-provided magnetometer calibration.
 *
 * The first 32-bit word is the timestamp, which doubles as the erased-slot
 * sentinel because erased flash reads back as all ones. Alignment matches the
 * flash programming granularity of the active STM32 family.
 */
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

/** Calibration records in the linker-reserved flash section. */
sensor_constants_t calConstants[CONSTANT_CNT] __attribute__((section(".calibration")));

/**
 * @brief Report whether calibration flash contains at least one written slot.
 *
 * Empty slots are detected by the erased timestamp word. The host uses this as
 * a cheap presence check before requesting calibration constants by index.
 */
bool sensorsHaveCalibration(void)
{
  for (unsigned int index = 0; index < CONSTANT_CNT; index++)
  {
    if ((*((int32_t *)&calConstants[index])) != -1)
      return true;
  }
  return false;
}

/*
 * Runtime collection state. Keep these in normal zero-initialized RAM: the
 * collection and calibration entry points reset their own caches, and stale
 * NOINIT values would make missing auxiliary samples look valid after reset.
 */
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

/** Enable the IMU FIFO-watermark wake source used while collecting data. */
static void enable_data_collection_wake_event(void)
{
  extiClearGroup1(IMU_DATA_WAKE_EXTI_MASK);
  extiEnableGroup1(IMU_DATA_WAKE_EXTI_MASK,
                   EXTI_MODE_RISING_EDGE | IMU_DATA_WAKE_EXTI_ACTION);
}

/** Disable collection wake events before shutting down sensors. */
static void disable_data_collection_wake_event(void)
{
  extiEnableGroup1(IMU_DATA_WAKE_EXTI_MASK, EXTI_MODE_DISABLED);
  extiClearGroup1(IMU_DATA_WAKE_EXTI_MASK);
}

/**
 * @brief Rotate IMU-frame X/Y into the tag/log frame.
 *
 * The physical IMU mounting is shared by accel and gyro. Z is already aligned,
 * so this helper deliberately leaves it untouched.
 */
static inline void orient_imu_xyz(int16_t *x, int16_t *y, int16_t *z)
{
  int16_t raw_x = *x;
  int16_t raw_y = *y;

  *x = (int16_t)-raw_y;
  *y = raw_x;
  (void)z;
}

#if !defined(TAG_SENSOR_MAG_BMM350) || !TAG_SENSOR_MAG_BMM350
/** Decode the AK09940A little-endian signed 18-bit magnetic sample format. */
static inline int32_t decode_mag_18bit(uint8_t l, uint8_t m, uint8_t h)
{
  int32_t raw = ((int32_t)(h & 0x03U) << 16) | ((int32_t)m << 8) | l;
  if ((raw & 0x20000) != 0)
    raw |= (int32_t)0xFFFC0000;
  return raw;
}
#endif

/** Clamp wide raw sensor values to the 16-bit log/header representation. */
static inline int16_t clamp_i16(int32_t value)
{
  if (value > INT16_MAX)
    return INT16_MAX;
  if (value < INT16_MIN)
    return INT16_MIN;
  return (int16_t)value;
}

/**
 * @brief Return the quiet-NaN sentinel used for missing auxiliary samples.
 *
 * The log frame keeps one auxiliary sample per IMU block. When pressure or
 * magnetometer data is not ready for the current block, write NaN rather than
 * repeating an older value; host loaders can then distinguish "not sampled"
 * from a real zero or held value.
 */
static inline float missing_aux_sample(void)
{
  return __builtin_nanf("");
}

#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
/**
 * @brief Select the BMM350 free-running ODR for the configured IMU rate.
 *
 * The magnetometer is sampled once per IMU superframe, so it does not need to
 * match the IMU ODR. These rates keep fresh data available at the auxiliary
 * cadence without spending current on unnecessary magnetic conversions.
 */
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
/**
 * @brief Select the AK09940A free-running ODR for the configured IMU rate.
 *
 * Keep the rate high enough that the once-per-superframe auxiliary poll usually
 * finds a completed sample, while leaving margin for the older SPI magnetometer
 * path on low-power collection runs.
 */
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

/**
 * @brief Select the pressure free-running ODR for the configured IMU rate.
 *
 * Pressure is lower bandwidth than the IMU, but it still needs to be ready at
 * the auxiliary poll cadence so the superframe can carry coherent environment
 * updates when they are available.
 */
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

/** Reset cached auxiliary state at collection start. */
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

/** Reset the partial IMU FIFO block accumulator. */
static void reset_imu_block_cache(void)
{
  memset(block_samples, 0, sizeof(block_samples));
  block_sample_count = 0;
}

#if defined(TAG_RETAINED_RUN_DIAGNOSTICS) && TAG_RETAINED_RUN_DIAGNOSTICS
static inline void count_sampling_error(volatile uint32_t *counter)
{
  pState->sample_error_count++;
  (*counter)++;
}
#else
static inline void count_sampling_error(volatile uint32_t *counter)
{
  (void)counter;
}
#endif

/**
 * @brief Put the active magnetometer data-ready line in input mode.
 *
 * BMM350 boards carry the DRDY line in the descriptor. Legacy AK09940A boards
 * use the family-level LINE_MAG_TRG signal.
 */
static inline void init_mag_data_ready_line(void)
{
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  toInput(TAG_MAG_DEVICE->drdy);
#else
  toInput(LINE_MAG_TRG);
#endif
}

/** Return true when the active magnetometer has asserted data-ready. */
static inline bool mag_data_ready(void)
{
#if defined(TAG_SENSOR_MAG_BMM350) && TAG_SENSOR_MAG_BMM350
  return bmm350DataReady(TAG_MAG_DEVICE);
#else
  return palReadLine(LINE_MAG_TRG) == PAL_HIGH;
#endif
}

/** Put the LPS22HH data-ready line in input mode. */
static inline void init_pressure_data_ready_line(void)
{
  toInput(LINE_LPS_DRDY);
}

/** Return true when the pressure DRDY pin is asserted. */
static inline bool pressure_data_ready(void)
{
  return palReadLine(LINE_LPS_DRDY) == PAL_HIGH;
}

/** Return true when the IMU FIFO/wake line is asserted. */
static inline bool imu_data_ready(void)
{
  return palReadLine(LINE_WKUP1) == PAL_HIGH;
}

/**
 * @brief Check pressure readiness using the pin first, then status fallback.
 *
 * The fallback keeps collection resilient if the DRDY edge was missed or the
 * board route is unavailable during bring-up.
 */
static bool pressure_sample_ready(void)
{
  if (pressure_data_ready())
    return true;

  return lps22hh_data_ready_device(TAG_PRESSURE_DEVICE);
}

/**
 * @brief Configure the active magnetometer for collection.
 *
 * The BMM350 init path always performs its required reset/trim reload sequence,
 * so reset_first is meaningful only for the AK09940A path. A successful
 * configuration also clears the missed-sample recovery counter.
 */
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

/**
 * @brief Track missed magnetometer polls and reinitialize after repeated loss.
 *
 * Collection samples the magnetometer once per completed IMU superframe. A few
 * misses are normal when rates are close or an interrupt edge is lost; repeated
 * misses usually mean the continuous-mode state machine needs to be restarted.
 */
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
/**
 * @brief Read one AK09940A payload when data is ready.
 *
 * The ST2 byte is preserved through the burst so invalid reads can be rejected.
 * Data overrun is allowed for this free-running auxiliary stream because the
 * logger only needs the newest complete sample at the current superframe.
 */
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

/**
 * @brief Refresh the cached magnetometer values from the active device.
 *
 * BMM350 conversion to microtesla is owned by its native driver because factory
 * compensation is part of that device API. AK09940A conversion stays here to
 * bridge the legacy 18-bit payload into the shared auxiliary log fields.
 */
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

/**
 * @brief Refresh cached pressure and raw temperature values if data is ready.
 *
 * Pressure is converted to hPa immediately for the superframe. Raw temperature
 * remains in LPS22HH units because environment log metadata and host export
 * code already understand that representation.
 */
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
 * The active IMUTag page logger uses sampleDataCollection() instead. This
 * legacy hook is retained for older RUNNING code paths that expect a raw
 * single-sample API; clearing the destination is the compatibility behavior.
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
 * Initialization order matters: caches and DRDY lines are reset before sensors
 * start, auxiliary devices are put into continuous mode, then the IMU trigger
 * and FIFO watermark are enabled last so the first wake event sees all devices
 * in their collection configuration.
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

  /* Snapshot the configured IMU mode before deriving auxiliary rates from it. */
  trig_cfg.trig_odr = imu_odr;
  trig_cfg.xl_fs = xl_fs;
  trig_cfg.g_fs = g_fs;
  mag_rate = select_mag_rate(imu_odr);
  configured_mag_rate = mag_rate;
  pressure_rate = select_pressure_rate(imu_odr);

  /* Clear per-run state before any DRDY line can report old sensor data. */
  reset_environment_cache();
  reset_imu_block_cache();
  init_mag_data_ready_line();
  init_pressure_data_ready_line();

  /* Bring auxiliary sensors up first so the first IMU block can carry them. */
  if (!configure_mag_collection(true)) {
    ok = false;
  }

  if (lps22hh_config_continuous_device(TAG_PRESSURE_DEVICE, pressure_rate,
                                       LPS22HH_LPF_DISABLE) != 0) {
    debug_log_printf("IMUTag collection: LPS22HH init failed\r\n");
    ok = false;
  }

  /* Start the FIFO pacing source last, then enable the wake event for it. */
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
 * This routine may be called before a watermark is available; in that case it
 * returns false and leaves any partial FIFO block cached. It first verifies
 * the IMU FIFO/wake line so monitor-attached polling cannot trigger speculative
 * FIFO reads. Once a full block is assembled, IMU axes are rotated into log
 * orientation, auxiliary sensors are polled once, and missing auxiliary slots
 * are written as quiet NaNs.
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

  if (!imu_data_ready())
    return false;

  /* Check for a full FIFO watermark before draining any IMU samples. */
  fifo_words = lsm6dsv16x_read_fifo_status(TAG_IMU_DEVICE, &wtm, &ovr);
  if (ovr != 0U) {
    count_sampling_error(&pState->sample_fifo_overruns);
    debug_log_printf("IMUTag collection: IMU FIFO overrun\r\n");
  }
  if (fifo_words < IMU_FIFO_WATERMARK_WORDS) {
    if (wtm != 0U) {
      count_sampling_error(&pState->sample_fifo_watermark_shorts);
      debug_log_printf("IMUTag collection: FIFO watermark short, %u/%u words\r\n",
                       (unsigned)fifo_words,
                       (unsigned)IMU_FIFO_WATERMARK_WORDS);
    }
    return false;
  }

  /* Accumulate into a cached block in case the FIFO read returns short. */
  pairs = lsm6dsv16x_read_fifo(TAG_IMU_DEVICE,
                               &block_samples[block_sample_count],
                               IMU_FIFO_PAIRS_PER_SUPERFRAME - block_sample_count);
  if (pairs == 0U) {
    count_sampling_error(&pState->sample_fifo_empty_reads);
    debug_log_printf("IMUTag collection: FIFO read returned no pairs, %u words available\r\n",
                     (unsigned)fifo_words);
    return false;
  }
  block_sample_count += pairs;

  if (block_sample_count < IMU_FIFO_PAIRS_PER_SUPERFRAME) {
    count_sampling_error(&pState->sample_fifo_short_blocks);
    debug_log_printf("IMUTag collection: unrecoverable FIFO block short, %u/%u pairs\r\n",
                     (unsigned)block_sample_count,
                     (unsigned)IMU_FIFO_PAIRS_PER_SUPERFRAME);
    reset_imu_block_cache();
    return false;
  }

  /* A complete IMU block is ready; rebuild the destination frame from scratch. */
  memset(frame, 0, sizeof(*frame));

  for (uint16_t i = 0; i < IMU_FIFO_PAIRS_PER_SUPERFRAME; i++) {
    int16_t gx = block_samples[i].gyro_x;
    int16_t gy = block_samples[i].gyro_y;
    int16_t gz = block_samples[i].gyro_z;
    int16_t ax = block_samples[i].accel_x;
    int16_t ay = block_samples[i].accel_y;
    int16_t az = block_samples[i].accel_z;

    /* Store accel and gyro in the tag/log coordinate frame. */
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

  /*
   * Auxiliary sensors are deliberately polled after the IMU block is copied.
   * That keeps the FIFO service time short and makes the aux fields describe
   * the newest pressure/magnetic data available near the end of the block.
   */
  bool mag_sampled = update_latest_mag();
  bool pressure_sampled = update_latest_pressure();

  record_due_mag_sample(mag_sampled);

  /* Missing aux values are explicit NaNs, not stale cache repeats. */
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
 * Disable the EXTI wake source first so no collection event can arrive while
 * individual devices are being powered down.
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
 * The host calibration UI can accept sparse samples: a response may contain
 * only magnetometer, only accelerometer, both, or neither. The limited debug
 * logging avoids flooding the monitor while a user is waiting for DRDY edges.
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
 * Calibration uses a simpler setup than data collection: no gyro FIFO and no
 * pressure sensor. The PA4 trigger divider keeps accelerometer sampling active
 * for orientation feedback while the host gathers magnetometer points.
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
 * This mirrors initSensors() rather than deinitDataCollection(): only the
 * devices enabled for live calibration are touched.
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
 * Calibration is monitor-attached by design. Without an active monitor there is
 * no consumer for calibration_log ACKs, so the state immediately tears down the
 * live sensors and returns to idle.
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
 * The monitor protocol requests one calibration log entry at a time. Keep this
 * ACK shape in sync with host calibration tools that poll Req_caldata while the
 * tag is in CALIBRATE state.
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
 * Calibration records form a simple flash log. New constants are appended to
 * the first erased slot; when the page is full, the whole calibration page is
 * erased and writing starts at slot zero. The host treats a negative read index
 * as "latest", so append order is the only versioning stored here.
 *
 * @param[in] constants Host-provided calibration constants.
 * @return Encoded ACK length or error response length.
 */
int write_calibration(CalibrationConstants *constants){
  unsigned int index;


  if (!constants->has_magnetometer)
    return errAck(Ack_INVAL);

  /* Search for the first erased slot. */
  for (index = 0; index < CONSTANT_CNT; index++){
    if  ((*((int32_t *) &calConstants[index])) == -1)
      break;
  }

  /*
   * No empty slot means the calibration page is full. Erase the page and start
   * a new log at slot zero; older constants are intentionally discarded.
   */
  if (index >= CONSTANT_CNT)
  {
    chSysLock();
    FLASH_Unlock();
    FLASH_PageErase(((((uint32_t) calConstants)-0x8000000)) / 2048);
    FLASH_Lock();
    FLASH_Flush_Data_Cache();
    chSysUnlock();
    index = 0;
  }

  /*
   * Stage through NOINIT RAM so the flash programmer sees one aligned record
   * containing only the persisted timestamp and magnetometer constants.
   */
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
 * Negative indexes select the last written slot. Non-negative indexes expose
 * the raw flash-log slot numbering so host tools can inspect older records
 * while they still exist on the current page.
 *
 * @param[in] index Calibration slot to read, or negative for the latest slot.
 * @param[out] ack ACK message to populate.
 * @return Encoded ACK length or error response length.
 */
int read_calibration(int32_t index, Ack *ack){
  /* If index < 0, search backward for the latest written slot. */
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
