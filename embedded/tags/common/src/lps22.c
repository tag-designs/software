#include "hal.h"
#include "app.h"
#include "lps22hhw.h"
#include "lps.h"
#include "limits.h"

#define LPS22HW_ADR (0x5C)

#if defined(LPS_I2C) && defined(USE_LPS22)
//extern const I2CConfig i2cfg1;
#define LPS22_TIMEOUT 100

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

int lps22_GetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  return i2cMasterTransmitTimeout(&I2CD1, LPS22HW_ADR, &reg, 1, val, num,
                                  LPS22_TIMEOUT);
}

int lps22_SetReg(enum LPS22_Reg reg, unsigned char *val, int num)
{
  return I2C1_MemWrite(LPS22HW_ADR, reg, val, num, LPS22_TIMEOUT);
}

#endif

#if defined(LPS_SPI) && defined(USE_LPS22)
// needs to be re-written to use three wire format
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

void lps22_SetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = ((uint8_t)reg);

  palClearLine(LINE_STEVAL_CS);
  spiSendPolled(1, &buffer);
  spiSendPolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

void lps22_GetReg(enum LPS22_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = 0x80 | ((uint8_t)reg);
  palClearLine(LINE_STEVAL_CS);
  spiSendPolled(1, &buffer);
  spiReceivePolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

#endif

#if defined(USE_LPS22)
bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
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
#if defined(LPS_SPI)
    // SPI1->CR1 &= ~SPI_CR1_SPE;
#endif
    stopMilliseconds(5);
    //chThdSleepMilliseconds(true,2);
#if defined(LPS_SPI)
    // SPI1->CR1 |= SPI_CR1_SPE;
#endif
    uint8_t cmd = LPS22_CTRL_REG3_DRDY;
    lps22_SetReg(LPS22_CTRL_REG3, &cmd, 1);
    //palEnableLineEvent(LINE_INT1,PAL_EVENT_MODE_RISING_EDGE);
#ifdef LPS_LOW_POWER
    cmd = LPS22_CTRL_REG2_ONE_SHOT |
          LPS22_CTRL_REG2_IF_ADD_INC;
#else
    cmd = LPS22_CTRL_REG2_ONE_SHOT |
          LPS22_CTRL_REG2_LOW_NOISE_EN |
          LPS22_CTRL_REG2_IF_ADD_INC;
#endif
    // start one-shot conversion
#if defined(LPS_SPI)
     SPI1->CR1 |= SPI_CR1_SPE;
#endif
     lps22_SetReg(LPS22_CTRL_REG2, &cmd, 1);
     
    // wait for data
    for (int i = 0; i < 6; i++)
    {
#if defined(LPS_SPI)
      //SPI1->CR1 &= ~SPI_CR1_SPE;
#endif
      stopMilliseconds(true,5);
      //chThdSleepMilliseconds(5);
#if defined(LPS_SPI)
      //SPI1->CR1 |= SPI_CR1_SPE;
#endif
      lps22_GetReg(LPS22_STATUS, &tmp, 1);
      if ((tmp & 3) == 3)
        break;
    }
    // read pressure/temperature
    lps22_GetReg(LPS22_PRESS_OUT_XL, &((uint8_t *)&buf)[1], 5);
    *pressure = buf.pressure >> 16;
    *temperature = buf.temperature;
    lpsOff();
    if (!(tmp & 1))
      *pressure = 0;
    if (!(tmp & 2))
      *temperature = SHRT_MIN;
    return (tmp & 3) == 3;
  }
  return false;
}

void lpsInit(void){}

bool lpsTest(void)
{
  uint8_t who;
  lpsOn();
  chThdSleepMilliseconds(10);
  lps22_GetReg(LPS22_WHO_AM_I, &who, 1);
  lpsOff();
  return who == LPS22_WHO_AM_I_VALUE;
}
#endif