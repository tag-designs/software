/**
 * @file lis2dtw12.c
 * @brief Legacy LIS2DTW12 accelerometer sampling and self-test implementation.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "hal.h"
#include "custom.h"
#include "core_types.h"
#include "power.h"
#include "rtc_api.h"
#include "math.h"

typedef enum
{
  LIS2DTW12_OUT_T_L = 0x0DU,
  LIS2DTW12_OUT_T_H = 0x0EU,
  LIS2DTW12_WHO_AM_I = 0x0FU,
  LIS2DTW12_CTRL1 = 0x20U,
  LIS2DTW12_CTRL2 = 0x21U,
  LIS2DTW12_CTRL3 = 0x22U,
  LIS2DTW12_CTRL4_INT1 = 0x23U,
  LIS2DTW12_CTRL5_INT2 = 0x24U,
  LIS2DTW12_CTRL6 = 0x25U,
  LIS2DTW12_STATUS = 0x27U,
  LIS2DTW12_OUT_X_L = 0x28U,
  LIS2DTW12_OUT_X_H = 0x29U,
  LIS2DTW12_OUT_Y_L = 0x2AU,
  LIS2DTW12_OUT_Y_H = 0x2BU,
  LIS2DTW12_OUT_Z_L = 0x2CU,
  LIS2DTW12_OUT_Z_H = 0x2DU,
  LIS2DTW12_FIFO_CTRL = 0x2EU,
  LIS2DTW12_FIFO_SAMPLES = 0x2FU,
  LIS2DTW12_INT_DUR = 0x33U,
  LIS2DTW12_STATUS_DUP = 0x37U,
  LIS2DTW12_WAKE_UP_SRC = 0x38U,
  LIS2DTW12_SIXD_SRC = 0x3AU,
  LIS2DTW12_ALL_INT_SRC = 0x3BU,
  LIS2DTW12_X_OFS_USR = 0x3CU,
  LIS2DTW12_Y_OFS_USR = 0x3DU,
  LIS2DTW12_Z_OFS_USR = 0x3EU,
  LIS2DTW12_CTRL7 = 0x3FU
} LIS2DTW12_reg;

#define LIS2DTW12_ID            0x44U

/** @name Legacy SPI transfer helpers
 * LIS2DTW12 legacy code talks directly to SPI1 and LINE_ACCEL_CS; these helpers
 * keep that register pacing localized.
 * @{
 */
/**
 * @brief Send bytes over the legacy SPI1 accelerometer bus.
 *
 * @param[in] n Number of bytes to send.
 * @param[in] buf Source bytes.
 */
static void spiSendPolled(uint32_t n, uint8_t *buf)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
  while (n--)
  {
    *spidr = *buf++;
    while ((SPI1->SR & SPI_SR_RXNE) == 0)
      ;
    *spidr;
  }
}

/**
 * @brief Receive bytes over the legacy SPI1 accelerometer bus.
 *
 * @param[in] n Number of bytes to receive.
 * @param[out] buf Destination buffer.
 */
static void spiReceivePolled(uint32_t n, uint8_t *buf)
{
  volatile uint8_t *spidr = (volatile uint8_t *)&SPI1->DR;
  while (n--)
  {
    *spidr = 0xff;
    while ((SPI1->SR & SPI_SR_RXNE) == 0)
      ;
    *buf++ = *spidr;
  }
}

/**
 * @brief Write one or more LIS2DTW12 registers.
 *
 * @param[in] reg First register to write.
 * @param[in] bufp Source buffer.
 * @param[in] len Number of bytes to write.
 * @return 0 on success.
 */
static int32_t lis2dtw12_write(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  unsigned char buffer = ((uint8_t)reg);
  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(1, &buffer);
  spiSendPolled(len, bufp);
  palSetLine(LINE_ACCEL_CS);
  return 0;
}

/**
 * @brief Read one or more LIS2DTW12 registers.
 *
 * @param[in] reg First register to read.
 * @param[out] bufp Destination buffer.
 * @param[in] len Number of bytes to read.
 * @return 0 on success.
 */
static int32_t lis2dtw12_read(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  unsigned char buffer = 0x80 | reg;
  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(1, &buffer);
  spiReceivePolled(len, bufp);
  palSetLine(LINE_ACCEL_CS);
  return 0;
}

/**
 * @brief Write one byte to a LIS2DTW12 register.
 *
 * @param[in] regnum Register address.
 * @param[in] val Value to write.
 * @return 0 on success.
 */
static int32_t lis2_reg_write(uint8_t regnum, uint8_t val)
{
  return lis2dtw12_write(regnum, &val, 1);
}
/** @} */

/** @name LIS2DTW12 setup and sampling
 * Legacy setup helpers used by the simple RMS sampling path.
 * @{
 */
/**
 * @brief Configure LIS2DTW12 for low-pass or high-pass sampling.
 *
 * @param[in] lpf true for low-pass output, false for high-pass output.
 */
static void lis2_init(bool lpf)
{
  // set block data update, automatic register increment, turn off cs pullup, turn off i2c interface
  lis2_reg_write(LIS2DTW12_CTRL2, (15 << 1));
  // Set full scale, lpf, filter odr/20
  if (lpf)
  {
    // lpf output
    lis2_reg_write(LIS2DTW12_CTRL6, (3 << 6));
  }
  else
  {
    // hpf output
    lis2_reg_write(LIS2DTW12_CTRL6, (3 << 6) | (1 << 3));
  }
  // set odr (25hz), operating mode (continuous), power mode 2 //4
  lis2_reg_write(LIS2DTW12_CTRL1, (3 << 4) | 1); //3
}

/**
 * @brief Soft-reset the LIS2DTW12 after sampling.
 */
static void lis2_deinit(void)
{
  // soft reset
  lis2_reg_write(LIS2DTW12_CTRL2, (1 << 6));
}

static int16_t accelbuf[32 * 3] NOINIT;

/**
 * @brief Capture LIS2DTW12 samples and compute RMS motion.
 *
 * @param[in] samples Number of samples to include in RMS calculation.
 * @param[out] rms RMS acceleration estimate.
 * @param[out] orientation Three-axis low-pass orientation sample.
 */
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
  lis2dtw12_read(LIS2DTW12_OUT_X_L, (uint8_t *)orientation, 6);
  // switch filter path
  lis2_reg_write(LIS2DTW12_CTRL6, (3 << 6) | (1 << 3));

  // switch to fifo mode and then sleep
  lis2_reg_write(LIS2DTW12_FIFO_CTRL, 6 << 5 | 31);

  //SPI1->CR1 &= ~SPI_CR1_SPE;
  stopMilliseconds((samples + 5) * 1000 / odr);
  //SPI1->CR1 |= SPI_CR1_SPE;

  // read samples and compute sum of squares

  lis2dtw12_read(LIS2DTW12_OUT_X_L, (uint8_t *)accelbuf, (samples+3) * 6);
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

/**
 * @brief Read the LIS2DTW12 identity register.
 *
 * @return true when the expected device ID is present.
 */
bool lis2_test(void) {
  uint8_t val;
  accelSpiOn();
  lis2dtw12_read(LIS2DTW12_WHO_AM_I,&val, 1);
  accelSpiOff();
  return val == LIS2DTW12_ID;
}
/** @} */
