#include "hal.h"
#include "custom.h"
#include "app.h"
//#include "ais2dw12_reg.h"

typedef enum
{
  AIS2DW12_OUT_T_L = 0x0DU,
  AIS2DW12_OUT_T_H = 0x0EU,
  AIS2DW12_WHO_AM_I = 0x0FU,
  AIS2DW12_CTRL1 = 0x20U,
  AIS2DW12_CTRL2 = 0x21U,
  AIS2DW12_CTRL3 = 0x22U,
  AIS2DW12_CTRL4_INT1 = 0x23U,
  AIS2DW12_CTRL5_INT2 = 0x24U,
  AIS2DW12_CTRL6 = 0x25U,
  AIS2DW12_OUT_T = 0x26U,
  AIS2DW12_STATUS = 0x27U,
  AIS2DW12_OUT_X_L = 0x28U,
  AIS2DW12_OUT_X_H = 0x29U,
  AIS2DW12_OUT_Y_L = 0x2AU,
  AIS2DW12_OUT_Y_H = 0x2BU,
  AIS2DW12_OUT_Z_L = 0x2CU,
  AIS2DW12_OUT_Z_H = 0x2DU,
  AIS2DW12_FIFO_CTRL = 0x2EU,
  AIS2DW12_FIFO_SAMPLES = 0x2FU,
  AIS2DW12_SIXD_THS = 0x30U,
  AIS2DW12_WAKE_UP_THS = 0x34U,
  AIS2DW12_WAKE_UP_DUR = 0x35U,
  AIS2DW12_FREE_FALL = 0x36U,
  AIS2DW12_STATUS_DUP = 0x37U,
  AIS2DW12_WAKE_UP_SRC = 0x38U,
  AIS2DW12_SIXD_SRC = 0x3AU,
  AIS2DW12_ALL_INT_SRC = 0x3BU,
  AIS2DW12_X_OFS_USR = 0x3CU,
  AIS2DW12_Y_OFS_USR = 0x3DU,
  AIS2DW12_Z_OFS_USR = 0x3EU,
  AIS2DW12_CTRL7 = 0x3FU
} ais2dw12_reg;

#define AIS2DW12_ID            0x44U

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

static int32_t ais2dw12_write(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  unsigned char buffer = ((uint8_t)reg);
  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(1, &buffer);
  spiSendPolled(len, bufp);
  palSetLine(LINE_ACCEL_CS);
  return 0;
}

static int32_t ais2dw12_read(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  unsigned char buffer = 0x80 | reg;
  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(1, &buffer);
  spiReceivePolled(len, bufp);
  palSetLine(LINE_ACCEL_CS);
  return 0;
}

static int32_t ais2_reg_write(uint8_t regnum, uint8_t val)
{
  return ais2dw12_write(regnum, &val, 1);
}

static void ais2_init(bool lpf)
{
  // set block data update, automatic register increment, turn off cs pullup, turn off i2c interface
  ais2_reg_write(AIS2DW12_CTRL2, (15 << 1));
  // Set full scale, lpf, filter odr/20
  if (lpf)
  {
    // lpf output
    ais2_reg_write(AIS2DW12_CTRL6, (3 << 6));
  }
  else
  {
    // hpf output
    ais2_reg_write(AIS2DW12_CTRL6, (3 << 6) | (1 << 3));
  }
  // set odr (25hz), operating mode (continuous), power mode 4
  ais2_reg_write(AIS2DW12_CTRL1, (3 << 4) | 3);
}

static void ais2_deinit(void)
{
  // soft reset
  ais2_reg_write(AIS2DW12_CTRL2, (1 << 6));
}

static int16_t accelbuf[32 * 3] NOINIT;

void ais_sample(int samples, int16_t *rms, int16_t orientation[3])
{
  float sum;
  const int odr = 25;

  accelSpiOn();
  ais2_init(true);

  // let filter start up

  //SPI1->CR1 &= ~SPI_CR1_SPE;
  stopMilliseconds(true,12 * 1000 / odr);
  //SPI1->CR1 |= SPI_CR1_SPE;
  // read low-pass samples
  ais2dw12_read(AIS2DW12_OUT_X_L, (uint8_t *)orientation, 6);
  // switch filter path
  ais2_reg_write(AIS2DW12_CTRL6, (2 << 6) | (1 << 3));

  // switch to fifo mode and then sleep
  ais2_reg_write(AIS2DW12_FIFO_CTRL, 6 << 5 | 31);

  //SPI1->CR1 &= ~SPI_CR1_SPE;
  stopMilliseconds(true,(samples + 5) * 1000 / odr);
  //SPI1->CR1 |= SPI_CR1_SPE;

  // read samples and compute sum of squares

  ais2dw12_read(AIS2DW12_OUT_X_L, (uint8_t *)accelbuf, (samples+3) * 6);
  ais2_deinit();
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

bool ais2_test(void) {
  uint8_t val;
  accelSpiOn();
  ais2dw12_read(AIS2DW12_WHO_AM_I,&val, 1);
  accelSpiOff();
  return val == AIS2DW12_ID;
}