/**
 * @file lis2du12.c
 * @brief CompassTag LIS2DU12 accelerometer register driver.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "lis2du12.h"

typedef enum
{
  LIS2DU12_IF_PU_CTRL    = 0x0CU,
  LIS2DU12_IF_CTRL       = 0x0EU,

  LIS2DU12_CTRL1         = 0x10U,
  LIS2DU12_CTRL2         = 0x11U,
  LIS2DU12_CTRL3         = 0x12U,
  LIS2DU12_CTRL4         = 0x13U,
  LIS2DU12_CTRL5         = 0x14U,
  LIS2DU12_FIFO_CTRL     = 0x15U,
  LIS2DU12_FIFO_WTM      = 0x16U,
  LIS2DU12_INTERRUPT_CFG = 0x17U,
  LIS2DU12_TAP_THS_X     = 0x18U,
  LIS2DU12_TAP_THS_Y     = 0x19U,
  LIS2DU12_TAP_THS_Z     = 0x1AU,
  LIS2DU12_INT_DUR       = 0x1BU,
  LIS2DU12_WAKE_UP_THS   = 0x1CU,
  LIS2DU12_WAKE_UP_DUR   = 0x1DU,
  LIS2DU12_FREE_FALL     = 0x1EU,
  LIS2DU12_MD1_CFG       = 0x1FU,

  LIS2DU12_MD2_CFG       = 0x20U,
  LIS2DU12_WAKE_UP_SRC   = 0x21U,
  LIS2DU12_TAP_SRC       = 0x22U,
  LIS2DU12_SIXD_SRC      = 0x23U,

  LIS2DU12_ALL_INT_SRC   = 0x24U,
  LIS2DU12_STATUS        = 0x25U,
  LIS2DU12_FIFO_STATUS1  = 0x26U,
  LIS2DU12_FIFO_STATUS2  = 0x27U,

  LIS2DU12_OUT_X_L       = 0x28U,
  LIS2DU12_OUT_X_H       = 0x29U,
  LIS2DU12_OUT_Y_L       = 0x2AU,
  LIS2DU12_OUT_Y_H       = 0x2BU,
  LIS2DU12_OUT_Z_L       = 0x2CU,
  LIS2DU12_OUT_Z_H       = 0x2DU,
  LIS2DU12_OUT_T_L       = 0x2EU,
  LIS2DU12_OUT_T_H       = 0x2FU,

  LIS2DU12_TEMP_OUT_L    = 0x30U,
  LIS2DU12_TEMP_OUT_H    = 0x32U,
  LIS2DU12_WHO_AM_I      = 0x43U,
  LIS2DU12_ST_SIGN       = 0x58U
  
} LIS2DU12_reg;

#define LIS2DU12_ID            0x45U

#define CTRL1_SOFT_RESET (1<<5)
#define CTRL1_IF_ADD_INC (1<<4)
#define CTRL1_WU_EN (7)
#define CTRL4_BDU (1<<5)
#define CTRL5_1_6HZ (1<<4)
/*
 * ODR=0011 is "6 Hz in ultralow-power mode" (Table 34); ODR=0100 is "6 Hz in
 * normal mode". Normal mode's antialiasing filter runs regardless of ODR, so
 * its current is roughly constant no matter how slow the ODR is -- ULP mode
 * is the one whose current actually scales down with ODR. This wake-up
 * config wants the ULP variant; a prior commit (f407466) silently switched
 * it to normal mode, which is why the resting current between samples
 * doesn't drop the way the datasheet suggests it should.
 */
#define CTRL5_6HZ_3HZ_ULP ((3<<4)|(3<<2))
#define CTRL5_POWER_DOWN (0)
#define INT_CFG_SLEEP_STATUS_ON_INT (1<<3)
#define INT_CFG_ENABLE (1)
#define MD1_CFG_INT1_SLEEP_CHANGE (1<<7)
#define MD1_CFG_WKUP (1<<5)
#define MD1_CFG_WU_DUR_X4 (1<<1)
#define MD2_CFG_INT2_SLEEP_CHANGE (1<<7)

/*
 * Wake-up threshold/duration configuration, in physical units instead of
 * hand-computed register bit patterns.
 *
 * Threshold: WAKE_UP_THS.WK_THS[5:0] (Table 53/54), with WAKE_THS_W=0 (fixed
 * by this driver): 1 LSB = full_scale_g / 64 g. 6 bits -> 0..63 LSB.
 *
 * Duration: WAKE_UP_DUR.WAKE_DUR[1:0] (Table 55/56) is interpreted two ways
 * depending on MD1_CFG.WU_DUR_X4 (Table 59/60):
 *   WU_DUR_X4=0: WAKE_DUR value IS the ODR-time count, 0..3.
 *   WU_DUR_X4=1: WAKE_DUR selects one of {3, 7, 11, 15} ODR-times.
 * This driver picks whichever of the two representations lands closest to
 * the requested duration.
 */
typedef struct {
  float threshold_mg;    /**< Wake-up threshold, in mg. */
  float duration_s;      /**< Minimum time above threshold to wake, in seconds. */
  uint8_t full_scale_g;  /**< Configured full scale (2/4/8/16); must match CTRL5 FS[1:0]. */
  float odr_hz;          /**< Configured ODR the duration is measured against. */
} LIS2DU12_WakeConfig;

/** LIS2DU12_WAKE_UP_THS.WK_THS is 6 bits wide. */
#define LIS2DU12_WAKE_THS_MAX 63U
/** LIS2DU12_WAKE_UP_DUR.WAKE_DUR is 2 bits wide either way it's interpreted. */
#define LIS2DU12_WAKE_DUR_MAX 3U
/** WU_DUR_X4 alternate table (Table 56): WAKE_DUR -> ODR-time count. */
static const uint8_t kWakeDurX4OdrTimes[4] = {3U, 7U, 11U, 15U};

/*
 * Default wake-up sensitivity for ACCEL_WAKEUP_MODE, as build-time constants
 * in physical units. Must match CTRL5_6HZ_3HZ_ULP's FS[1:0]/ODR[3:0]
 * selection (+-2g, 6 Hz) below.
 *
 * duration_s = 7/6 s: 7 ODR-times at 6 Hz -- this is the debounce the
 * original hand-written register value (WAKE_UP_DUR_7ODR) was named for but
 * did not actually produce; MD1_CFG never set WU_DUR_X4, so it silently
 * configured 1 ODR-time (~167 ms) instead of the intended ~1.17 s. That
 * mismatch is very likely why wake sensitivity felt too twitchy.
 *
 * TODO: this belongs in host/target protobuf configuration eventually, so a
 * deployment can set it without a firmware rebuild. For now it is a
 * build-time default.
 */
static const LIS2DU12_WakeConfig kDefaultWakeConfig = {
  .threshold_mg = 125.0f,
  .duration_s = 7.0f / 6.0f,
  .full_scale_g = 2,
  .odr_hz = 6.0f,
};

/**
 * @brief Acquire the register bus for an LIS2DU12 transaction group.
 *
 * @param[in] device Register-device descriptor.
 */
static void lis2du12DeviceBegin(const TagRegisterDevice *device)
{
  tagBusBegin(&device->bus);
}

/**
 * @brief Release the register bus after an LIS2DU12 transaction group.
 *
 * @param[in] device Register-device descriptor.
 */
static void lis2du12DeviceEnd(const TagRegisterDevice *device)
{
  tagBusEnd(&device->bus);
}

/**
 * @brief Write one LIS2DU12 register byte.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] reg Register address.
 * @param[in] val Value to write.
 */
static void LIS2DU12_write_byte(const TagRegisterDevice *device, uint8_t reg,
                                uint8_t val)
{
  (void)tagRegisterWrite(device, reg, &val, 1);
}

/**
 * @brief Read one or more LIS2DU12 register bytes.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] reg First register address.
 * @param[out] bufp Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return Register transport status.
 */
static int32_t LIS2DU12_read(const TagRegisterDevice *device, uint8_t reg,
                             uint8_t *bufp, uint16_t len)
{
  return tagRegisterRead(device, reg, bufp, len);
}


/*
void lis2du12_init(bool lpf)
{
  accelSpiOn();
  // set block data update, automatic register increment, turn off cs pullup, turn off i2c interface
  LIS2DU12_write(LIS2DU12_CTRL2, (15 << 1));
  // Set full scale, lpf, filter odr/20
  if (lpf)
  {
    // lpf output
    LIS2DU12_write(LIS2DU12_CTRL6, (3 << 6));
  }
  else
  {
    // hpf output
    LIS2DU12_write(LIS2DU12_CTRL6, (3 << 6) | (1 << 3));
  }
  // set odr (25hz), operating mode (continuous), power mode 2 //4
  LIS2DU12_write(LIS2DU12_CTRL1, (3 << 4) | 1); //3
  accelSpiOff();
}
  */

/**
 * @brief Power down and reset an LIS2DU12 descriptor.
 *
 * @param[in] device Register-device descriptor.
 */
void lis2du12Deinit(const TagRegisterDevice *device)
{
  // soft reset
  lis2du12DeviceBegin(device);
  LIS2DU12_write_byte(device, LIS2DU12_CTRL5, (0));  /* power down */
  LIS2DU12_write_byte(device, LIS2DU12_CTRL1,0x20U); /* reset */
  lis2du12DeviceEnd(device);
}

/**
 * @brief Configure the LIS2DU12 wake-up threshold, duration, and INT1 routing.
 *
 * @details Writes WAKE_UP_THS, WAKE_UP_DUR, and MD1_CFG from physical units
 *          (@p config) instead of hand-computed register bit patterns, so
 *          the intended sensitivity is legible at the call site and checked
 *          against the datasheet by this one conversion instead of being
 *          re-derived by hand at every write site. Always routes the wake
 *          event to INT1 (MD1_CFG.INT1_WU). Does not touch CTRL1/CTRL4/CTRL5
 *          or INTERRUPT_CFG; the caller is responsible for those and for
 *          keeping @c full_scale_g/@c odr_hz in @p config consistent with
 *          CTRL5's actual FS[1:0]/ODR[3:0] selection.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] config Threshold/duration in physical units.
 */
static void lis2du12ConfigureWake(const TagRegisterDevice *device,
                                  const LIS2DU12_WakeConfig *config)
{
  float lsb_mg = (config->full_scale_g * 1000.0f) / 64.0f;
  int32_t ths_raw = (int32_t)((config->threshold_mg / lsb_mg) + 0.5f);
  if (ths_raw < 0)
    ths_raw = 0;
  if (ths_raw > (int32_t)LIS2DU12_WAKE_THS_MAX)
    ths_raw = LIS2DU12_WAKE_THS_MAX;

  int32_t target_odr_times = (int32_t)((config->duration_s * config->odr_hz) + 0.5f);
  if (target_odr_times < 0)
    target_odr_times = 0;

  bool use_x4;
  uint8_t dur_raw;
  if (target_odr_times <= (int32_t)LIS2DU12_WAKE_DUR_MAX)
  {
    /* Direct 0..3 ODR-time representation covers this duration exactly. */
    use_x4 = false;
    dur_raw = (uint8_t)target_odr_times;
  }
  else
  {
    /* Otherwise pick the closest of the {3,7,11,15} ODR-time alternatives. */
    use_x4 = true;
    dur_raw = 0;
    int32_t best_diff = -1;
    for (uint8_t i = 0; i < 4; i++)
    {
      int32_t diff = target_odr_times - (int32_t)kWakeDurX4OdrTimes[i];
      if (diff < 0)
        diff = -diff;
      if ((best_diff < 0) || (diff < best_diff))
      {
        best_diff = diff;
        dur_raw = i;
      }
    }
  }

  LIS2DU12_write_byte(device, LIS2DU12_WAKE_UP_THS, (uint8_t)ths_raw);
  LIS2DU12_write_byte(device, LIS2DU12_WAKE_UP_DUR, dur_raw);
  LIS2DU12_write_byte(device, LIS2DU12_MD1_CFG,
                      MD1_CFG_WKUP | (use_x4 ? MD1_CFG_WU_DUR_X4 : 0));
}

/**
 * @brief Configure an LIS2DU12 descriptor for wakeup or sampling.
 *
 * @param[in] device Register-device descriptor.
 * @param[in] mode Desired accelerometer mode.
 */
void lis2du12Init(const TagRegisterDevice *device, lis2du12mode_t mode)
{
  /* send sleep state on pin, so activity bit is reversed */
  lis2du12DeviceBegin(device);

  switch (mode) {
  
    case ACCEL_WAKEUP_MODE: 
      LIS2DU12_write_byte(device, LIS2DU12_CTRL5, CTRL5_POWER_DOWN);  /* power down */
      LIS2DU12_write_byte(device, LIS2DU12_CTRL1, CTRL1_SOFT_RESET); // Software reset
      LIS2DU12_write_byte(device, LIS2DU12_CTRL1, CTRL1_IF_ADD_INC | CTRL1_WU_EN); // ADD_INC, Wkup x,y,z
      //LIS2DU12_write_byte(LIS2DU12_CTRL2, 0x0U);  // Make sure CTRL2 is reset
      //LIS2DU12_write_byte(LIS2DU12_CTRL3, 0x0U);  // Make sure CTRL3 is reset
      LIS2DU12_write_byte(device, LIS2DU12_CTRL4, CTRL4_BDU); // was A0, now block data update
      LIS2DU12_write_byte(device, LIS2DU12_INTERRUPT_CFG,INT_CFG_ENABLE); // Sleep status on interrupt
      lis2du12ConfigureWake(device, &kDefaultWakeConfig);
      LIS2DU12_write_byte(device, LIS2DU12_CTRL5, CTRL5_6HZ_3HZ_ULP); // ODR = 6hz ultralow-power, BW = 3hz
      break;
    case ACCEL_SAMPLE_50HZ_MODE:
      LIS2DU12_write_byte(device, LIS2DU12_CTRL1, 0x10U); // ADD_INC
      LIS2DU12_write_byte(device, LIS2DU12_CTRL4, 0x20U); // Block data update
      LIS2DU12_write_byte(device, LIS2DU12_CTRL5, 0x74U); // ODR = 50hz, BW = 12.5hz
      break;
    case ACCEL_SAMPLE_100HZ_MODE:
      LIS2DU12_write_byte(device, LIS2DU12_CTRL1, 0x10U); // ADD_INC
      LIS2DU12_write_byte(device, LIS2DU12_CTRL4, 0x20U); // Block data update
      LIS2DU12_write_byte(device, LIS2DU12_CTRL5, 0x84U); // ODR = 100hz, BW = 25hz
      break;
  }

  lis2du12DeviceEnd(device);
}

/**
 * @brief Read one LIS2DU12 sample when data is ready.
 *
 * @param[in] device Register-device descriptor.
 * @param[out] data Destination for six raw sample bytes.
 * @return true when a fresh sample was read.
 */
bool lis2du12Sample(const TagRegisterDevice *device, uint8_t *data)
{
  uint8_t status;
  bool res = false;
  lis2du12DeviceBegin(device);
  LIS2DU12_read(device, LIS2DU12_STATUS, &status, 1);
  if (status & 1){
    LIS2DU12_read(device, LIS2DU12_OUT_X_L,data,6);
    res = true;
  }

  lis2du12DeviceEnd(device);
  return res;
}

/*

static int16_t accelbuf[32 * 3] NOINIT;

void lis2_sample(int samples, int16_t *rms, int16_t orientation[3])
{
  float sum;
  const int odr = 25;

  accelSpiOn();
  lis2_init(true);

  // let filter start up

  //SPI1->CR1 &= ~SPI_CR1_SPE;
  stopMilliseconds(12 * 1000 / odr);
  //SPI1->CR1 |= SPI_CR1_SPE;
  // read low-pass samples
  LIS2DU12_read(LIS2DU12_OUT_X_L, (uint8_t *)orientation, 6);
  // switch filter path
  LIS2DU12_write(LIS2DU12_CTRL6, (3 << 6) | (1 << 3));

  // switch to fifo mode and then sleep
  LIS2DU12_write(LIS2DU12_FIFO_CTRL, 6 << 5 | 31);

  //SPI1->CR1 &= ~SPI_CR1_SPE;
  stopMilliseconds((samples + 5) * 1000 / odr);
  //SPI1->CR1 |= SPI_CR1_SPE;

  // read samples and compute sum of squares

  LIS2DU12_read(LIS2DU12_OUT_X_L, (uint8_t *)accelbuf, (samples+3) * 6);
  lis2_deinit();
  accelSpiOff();

  // must discard first three samples

  sum = 0;
  for (int i = 9; i < (samples+3) * 3; i++)
  {
    float val = accelbuf[i];
    sum += val*val;
  }

  sum = sqrt(sum / (samples * 3));
  *rms = sum;
}
  */

/**
 * @brief Check the LIS2DU12 identity register.
 *
 * @param[in] device Register-device descriptor.
 * @return true when the expected identity is present.
 */
bool lis2du12Test(const TagRegisterDevice *device) {
  uint8_t val;
  lis2du12DeviceBegin(device);
  LIS2DU12_read(device, LIS2DU12_WHO_AM_I,&val, 1);
  lis2du12DeviceEnd(device);
  return val == LIS2DU12_ID;
}

/**
 * @brief Reset an LIS2DU12 descriptor.
 *
 * @param[in] device Register-device descriptor.
 */
void lis2du12Reset(const TagRegisterDevice *device)
{
  lis2du12Deinit(device);
}

/**
 * @brief Initialize the default family accelerometer descriptor.
 *
 * @param[in] mode Desired accelerometer mode.
 */
void accelInit(lis2du12mode_t mode)
{
  lis2du12Init(tagLis2du12Device(), mode);
}

/**
 * @brief Deinitialize the default family accelerometer descriptor.
 */
void accelDeinit(void)
{
  lis2du12Deinit(tagLis2du12Device());
}

/**
 * @brief Reset the default family accelerometer descriptor.
 */
void accelReset(void)
{
  lis2du12Reset(tagLis2du12Device());
}

/**
 * @brief Read a sample from the default family accelerometer descriptor.
 *
 * @param[out] data Destination for six raw sample bytes.
 * @return true when a fresh sample was read.
 */
bool accelSample(uint8_t *data)
{
  return lis2du12Sample(tagLis2du12Device(), data);
}

/**
 * @brief Test the default family accelerometer descriptor.
 *
 * @return true when the expected identity is present.
 */
bool accelTest(void)
{
  return lis2du12Test(tagLis2du12Device());
}
