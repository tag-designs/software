
#include "hal.h"
#include "app.h"
#include "lps27hhw.h"
#include "lps.h"
#include "limits.h"

#define LPS27HW_ADR (0x5C)

#if defined(LPS_I2C) && defined(USE_LPS27)
//extern const I2CConfig i2cfg1;
#define LPS27_TIMEOUT 100

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

int lps27_GetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  return i2cMasterTransmitTimeout(&I2CD1, LPS27HW_ADR, &reg, 1, val, num,
                                  LPS27_TIMEOUT);
}

int lps27_SetReg(enum LPS27_Reg reg, unsigned char *val, int num)
{
  return I2C1_MemWrite(LPS27HW_ADR, reg, val, num, LPS27_TIMEOUT);
}

#endif

#if defined(LPS_SPI) && defined(USE_LPS27)
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

void lps27_SetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = ((uint8_t)reg);

  palClearLine(LINE_STEVAL_CS);
  spiSendPolled(1, &buffer);
  spiSendPolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

void lps27_GetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = 0x80 | ((uint8_t)reg);
  palClearLine(LINE_STEVAL_CS);
  spiSendPolled(1, &buffer);
  spiReceivePolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

#endif

static inline void sleep(int ms) {
#if defined(LPS_SPI)
 //    SPI1->CR1 &= ~SPI_CR1_SPE;
 stopMilliseconds(true,ms);
#else
 stopMilliseconds(false,ms);
#endif
}

#if defined(USE_LPS27)

float lpsPressure(int16_t pressure) {
  return pressure/32.0f;
}

float lpsTemperature(int16_t temperature){
  return temperature/100.0f;
}


bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
{
  {
    // buffer to capture pressure/temperature;
    /*
    struct
    {
      uint32_t pressure;
      int16_t temperature;
    } buf;
    */
    uint8_t buf[5];
    uint8_t tmp;
    lpsOn();
    sleep(10);
    uint8_t cmd;;
#if defined(LPS_LOW_POWER)
    cmd = LPS27_CTRL_REG2_ONE_SHOT |
          LPS27_CTRL_REG2_IF_ADD_INC;
#else
    cmd = LPS27_CTRL_REG2_ONE_SHOT |
          LPS27_CTRL_REG2_LOW_NOISE_EN |
          LPS27_CTRL_REG2_IF_ADD_INC;
#endif
    lps27_SetReg(LPS27_CTRL_REG2, &cmd, 1);
     
    // wait for data

#if defined(LPS_LOW_POWER)
     sleep(25);
#else
     sleep(35);
#endif

    lps27_GetReg(LPS27_STATUS, &tmp, 1);
    lps27_GetReg(LPS27_PRESS_OUT_XL, buf, 5);
    lpsOff();

    *pressure = ((int32_t) ((buf[2] << 24) | (buf[1]<<16) | buf[0] << 8))>>15;
    // keep as much accuracy as feasible
    *temperature = (int16_t) ((buf[4]<<8) | ((buf[3])));
    if (!(tmp & 1))
      *pressure = 0;
    if (!(tmp & 2))
      *temperature = SHRT_MIN;
    return (tmp & 3) == 3;
  }
  return false;
}

bool lpsTest(void)
{
  uint8_t who;
  lpsOn();
  chThdSleepMilliseconds(10);
  lps27_GetReg(LPS27_WHO_AM_I, &who, 1);
  lpsOff();
  return who == LPS27_WHO_AM_I_VALUE;
}
#endif