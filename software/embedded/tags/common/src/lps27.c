
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
static inline void SendPolled(uint32_t n, uint8_t *buf)
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

static inline void ReceivePolled(uint32_t n, uint8_t *buf)
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

#endif

#if defined(LPS_USART) && defined(USE_LPS27)
static inline void SendPolled(uint32_t n, uint8_t *buf)
{
  volatile uint8_t *tdr = (volatile uint8_t *)&USART2->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&USART2->RDR;
  while (n--)
  {
    *tdr = *buf++;
    while ((USART2->ISR & USART_ISR_RXNE) == 0)
      ;
    *rdr;
  }
}

static inline void ReceivePolled(uint32_t n, uint8_t *buf)
{
  volatile uint8_t *tdr = (volatile uint8_t *)&USART2->TDR;
  volatile uint8_t *rdr = (volatile uint8_t *)&USART2->RDR;
  while (n--)
  {
    *tdr = 0xff;
     while ((USART2->ISR & USART_ISR_RXNE) == 0)
      ;
    *buf++ = *rdr;
  }
}
#endif

void lps27_SetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = ((uint8_t)reg);

  palClearLine(LINE_STEVAL_CS);
  SendPolled(1, &buffer);
  SendPolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

void lps27_GetReg(enum LPS27_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = 0x80 | ((uint8_t)reg);
  palClearLine(LINE_STEVAL_CS);
  SendPolled(1, &buffer);
  ReceivePolled(num, val);
  palSetLine(LINE_STEVAL_CS);
}

#

static inline void sleepMS(int ms) {
#if defined(LPS_SPI)
 //    SPI1->CR1 &= ~SPI_CR1_SPE;
 stopMilliseconds(true,ms);
#else
 stopMilliseconds(false,ms);
#endif
}

#if defined(USE_LPS27)

float lpsPressure(int16_t pressure) {
  return pressure/16.0f;
}

float lpsTemperature(int16_t temperature){
  return temperature/100.0f;
}


bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
{
 
  uint8_t status;
  uint8_t cmd[2];

  // default return values

  *pressure = SHRT_MIN;
  *temperature = SHRT_MIN;

  lpsOn();      // powered through gpio
  sleepMS(10);  // 10ms power up

  // set BDU and configure one shot

  cmd[0] = LPS27_CTRL_REG1_BDU;
  cmd[1] = LPS27_CTRL_REG2_ONE_SHOT |
           LPS27_CTRL_REG2_LOW_NOISE_EN |
           LPS27_CTRL_REG2_IF_ADD_INC;

  // write CTRL_REG1 and CTRL_REG2 to start one shot
  
  lps27_SetReg(LPS27_CTRL_REG1, cmd, 2);
    
  // wait for data

  for (int i = 0; i < 6; i++) {
    sleepMS(15);
    lps27_GetReg(LPS27_STATUS, &status, 1);
    if ((status & 3) == 3)
      break;
  }
  
  if (status == 3) // don't capture if overrun
  {
    lps27_GetReg(LPS27_PRESS_OUT_L, (uint8_t *) pressure, 2);
    lps27_GetReg(LPS27_TEMP_OUT_L, (uint8_t *) temperature, 2);
  }
  lpsOff();
  return status == 3;
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