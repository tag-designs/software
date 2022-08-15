
#include "hal.h"
#include "app.h"
#include "lps33hw.h"
#include "lps.h"
#include "limits.h"

#define LPS33HW_ADR (0x5C)

#ifdef LPS_I2C
extern const I2CConfig i2cfg1;
#define LPS33_TIMEOUT 100

static int I2C1_MemWrite(uint8_t device, uint8_t reg, unsigned char *buffer,
                         uint16_t size, uint32_t timeout)
{
  uint8_t txbuf[10];
  txbuf[0] = reg;
  for (int i = 0; i < 8 && i < size; i++)
  {
    txbuf[i + 1] = buffer[i];
  }
  return i2cMasterTransmitTimeout(&I2CD1, device, txbuf, size + 1, 0, 0,
                                  timeout);
}

int lps33_GetReg(enum LPS33_Reg reg, uint8_t *val, int num)
{
  return i2cMasterTransmitTimeout(&I2CD1, LPS33HW_ADR, &reg, 1, val, num,
                                  LPS33_TIMEOUT);
}

int lps33_SetReg(enum LPS33_Reg reg, unsigned char *val, int num)
{
  return I2C1_MemWrite(LPS33HW_ADR, reg, val, num, LPS33_TIMEOUT);
}

#endif

#ifdef LPS_SPI
static inline void spiSendPolled(uint32_t n, uint8_t *buf)
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

static inline void spiReceivePolled(uint32_t n, uint8_t *buf)
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

void lps33_SetReg(enum LPS33_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = ((uint8_t)reg);

  palClearLine(LINE_STEVAL_CS);
  spiSendPolled(1, &buffer);
  spiSendPolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

void lps33_GetReg(enum LPS33_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = 0x80 | ((uint8_t)reg);
  palClearLine(LINE_STEVAL_CS);
  spiSendPolled(1, &buffer);
  spiReceivePolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

#endif

#if defined(USE_LPS33)
bool GetPressureTemp(int16_t *pressure, int16_t *temperature)
{
  {

    // buffer to capture pressure/temperature;
    struct
    {
      int32_t pressure;
      int16_t temperature;
    } buf;
    uint8_t tmp;
    lpsOn();
    // wait for power to stabilize
    chThdSleepMilliseconds(10);
#ifdef LPS_LOW_POWER
    /* enable low power converion
         read/write to avoid modifying 
         reserved bit */
    lps33_GetReg(LPS33_RES_CONF, &tmp, 1);
    tmp |= LPS33_RES_CONF_LC_EN;
    lps33_SetReg(LPS33_RES_CONF, &tmp, 1);
#endif
    // start one-shot conversion
    uint8_t cmd = LPS33_CTRL_REG2_ONE_SHOT |
                  LPS33_CTRL_REG2_IF_ADD_INC;
    lps33_SetReg(LPS33_CTRL_REG2, &cmd, 1);
    // wait for data
    chThdSleepMilliseconds(20);
    lps33_GetReg(LPS33_STATUS, &tmp, 1);

    if ((tmp & 3) == 3)
    {
      // read pressure/temperature
      lps33_GetReg(LPS33_PRESS_OUT_XL, &((uint8_t *)&buf)[1], 5);
      *pressure = buf.pressure >> 16;
      *temperature = buf.temperature;
    }
    else
    {
      *pressure = 0;
      *temperature = SHRT_MIN;
    }
    lpsOff();

    return (tmp & 3) == 3;
  }
  return false;
}

bool lpsTest(void)
{
  uint8_t who;
  lpsOn();
  chThdSleepMilliseconds(10);
  lps33_GetReg(LPS33_WHO_AM_I, &who, 1);
  lpsOff();
  return who == LPS33_WHO_AM_I_VALUE;
}
#endif