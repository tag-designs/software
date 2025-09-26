#include <stdint.h>


#include "hal.h"
#include "app.h"
#include "limits.h"
#include "ak09940a.h"


typedef struct {
    uint8_t HXL;
    uint8_t HXM;
    uint8_t HXH;
    uint8_t HYL;
    uint8_t HYM;
    uint8_t HYH;
    uint8_t HZL;
    uint8_t HZM;
    uint8_t HZH;
    uint8_t TMPS;
    uint8_t ST1;
} ak09940a_data;


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
    *spidr = 0x00;
    while ((SPI1->SR & SPI_SR_RXNE) == 0)
      ;
    *buf++ = *spidr;
  }
}


static void AK09940A_SetReg(enum AK09940A_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = ((uint8_t)reg);

  palClearLine(LINE_MAG_CS);
  SendPolled(1, &buffer);
  SendPolled(num, val);
  palSetLine(LINE_MAG_CS);
}

static void AK09940A_GetReg(enum AK09940A_Reg reg, uint8_t *val, int num)
{
  unsigned char buffer = 0x80 | ((uint8_t)reg);
  palClearLine(LINE_MAG_CS);
  SendPolled(1, &buffer);
  ReceivePolled(num, val);
  palSetLine(LINE_MAG_CS);
}


// Single measurement
/*
    1) Set CNTL3 power mode and single measurement mode
    2) Poll ST for datardy
    3) Read all data include ST1 to clear
*/

bool magSample(bool single, uint8_t *xyz){
  uint8_t raw[11];
  uint8_t status = 0;
  uint8_t command = AK09940A_CNTL3_SINGLE_MEASURE |
                    AK09940A_CNTL3_LN2;  // Single measurement, low nose 2

 
  if (single)                 
        AK09940A_SetReg(AK09940A_CNTL3,&command,1);
  for (int i = 0; i < 2; i++) { 
    AK09940A_GetReg(AK09940A_ST,&status,1);
    if ((status&1)==1){
      AK09940A_GetReg(AK09940A_HXL,raw,11);
      memcpy(xyz,raw,10);
      return true;
    }
    stopMilliseconds(true,4);
  }
  return false;
}

void magInit(uint8_t mode){
  uint8_t command = mode | AK09940A_CNTL3_LN2;
  magOn();
  stopMilliseconds(true,1); 
  if (mode > AK09940A_CNTL3_SINGLE_MEASURE)
    AK09940A_SetReg(AK09940A_CNTL3,&command,1);

}
// Self test
/* 
    1) Set power down
    2) Set Low noise drive 2 (3), self test mode
    3) monitory data ready
    4) read data
  
*/

bool magTest(void){
  uint8_t wia[2];
  magOn();
  chThdSleepMilliseconds(1);
  AK09940A_GetReg(AK09940A_WIA1, wia, 2);
  magOff();
  return ((wia[0] == AK09940A_COMPANY_ID) && (wia[1] == AK09940A_PRODUCT_ID));
}