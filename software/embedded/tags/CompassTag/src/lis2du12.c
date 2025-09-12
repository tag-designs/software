#include "hal.h"
#include "custom.h"
#include "app.h"
#include "math.h"
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

#define CTRL1_IF_ADD_INC (1<<4)
#define CTRL1_WU_EN (7)
#define CTRL4_BDU (1<<5)
#define CTRL5_1_6HZ (1<<4)
#define INTERRUPT_CFG_SLEEP_STATUS_ON_INT (1<<3)
#define INTERRUPT_CFG_ENABLE (1)
#define MD2_CFG_INT2_SLEEP_CHANGE (1<<7)


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

static int32_t LIS2DU12_write(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  unsigned char buffer = ((uint8_t)reg);
  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(1, &buffer);
  spiSendPolled(len, bufp);
  palSetLine(LINE_ACCEL_CS);
  return 0;
}

static int32_t LIS2DU12_read(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  unsigned char buffer = 0x80 | reg;
  palClearLine(LINE_ACCEL_CS);
  spiSendPolled(1, &buffer);
  spiReceivePolled(len, bufp);
  palSetLine(LINE_ACCEL_CS);
  return 0;
}

static inline int32_t lis2_reg_write(uint8_t regnum, uint8_t val)
{
  return LIS2DU12_write(regnum, &val, 1);
}
/*
void lis2du12_init(bool lpf)
{
  accelSpiOn();
  // set block data update, automatic register increment, turn off cs pullup, turn off i2c interface
  lis2_reg_write(LIS2DU12_CTRL2, (15 << 1));
  // Set full scale, lpf, filter odr/20
  if (lpf)
  {
    // lpf output
    lis2_reg_write(LIS2DU12_CTRL6, (3 << 6));
  }
  else
  {
    // hpf output
    lis2_reg_write(LIS2DU12_CTRL6, (3 << 6) | (1 << 3));
  }
  // set odr (25hz), operating mode (continuous), power mode 2 //4
  lis2_reg_write(LIS2DU12_CTRL1, (3 << 4) | 1); //3
  accelSpiOff();
}
  */

void lis2du12_deinit(void)
{
  // soft reset
  accelSpiOn();
  lis2_reg_write(LIS2DU12_CTRL5, (0));  /* power down */
  lis2_reg_write(LIS2DU12_CTRL1,0x20U); /* reset */
  accelSpiOff();
}

void lis2du12_init(void)
{
  /* send sleep state on pin, so activity bit is reversed */
  accelSpiOn();
  lis2_reg_write(LIS2DU12_CTRL1, 0x17U);
  lis2_reg_write(LIS2DU12_CTRL4, 0xA0U);
  lis2_reg_write(LIS2DU12_INTERRUPT_CFG,0x1U); /* was 0x09???*/
  lis2_reg_write(LIS2DU12_WAKE_UP_DUR, 0x42U);
  lis2_reg_write(LIS2DU12_WAKE_UP_THS,0x42U);
  lis2_reg_write(LIS2DU12_MD1_CFG,0x20U); /* was 0x80U*/
  lis2_reg_write(LIS2DU12_CTRL5, 0x20U);
  accelSpiOff();
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
  stopMilliseconds(true,12 * 1000 / odr);
  //SPI1->CR1 |= SPI_CR1_SPE;
  // read low-pass samples
  LIS2DU12_read(LIS2DU12_OUT_X_L, (uint8_t *)orientation, 6);
  // switch filter path
  lis2_reg_write(LIS2DU12_CTRL6, (3 << 6) | (1 << 3));

  // switch to fifo mode and then sleep
  lis2_reg_write(LIS2DU12_FIFO_CTRL, 6 << 5 | 31);

  //SPI1->CR1 &= ~SPI_CR1_SPE;
  stopMilliseconds(true,(samples + 5) * 1000 / odr);
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

bool lis2du12_test(void) {
  uint8_t val;
  accelSpiOn();
  LIS2DU12_read(LIS2DU12_WHO_AM_I,&val, 1);
  accelSpiOff();
  return val == LIS2DU12_ID;
}