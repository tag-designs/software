#include "hal.h"
#include "custom.h"
#include "rtc_api.h"
#include "bmp5.h"
#include "lps.h"
#include "limits.h"
#include "sensor_io.h"

static const TagStSpiRegisterIO bmp5_spi = {
  .cs = LINE_STEVAL_CS,
  .read_mask = BMP5_SPI_RD_MASK,
  .write_mask = 0x00,
};

static const TagRegisterDevice bmp5_registers = {
  tagStSpiReadRegister,
  tagStSpiWriteRegister,
  &bmp5_spi,
};

static void bmp5_SetReg(enum BMP5_Reg reg, uint8_t *val, int num)
{
  (void)bmp5_registers.write_register(bmp5_registers.context, (uint8_t)reg,
                                      val, num);
}

static void bmp5_GetReg(enum BMP5_Reg reg, uint8_t *val, int num)
{
  (void)bmp5_registers.read_register(bmp5_registers.context, (uint8_t)reg,
                                     val, num);
}

float lpsPressure(int16_t pressure) {
  return ((uint16_t) pressure)*0.04f;
}

float lpsTemperature(int16_t temperature){
  return temperature/256.0f;
}

static bool power_up_check(void) {
  uint8_t nvm_status;
  uint8_t por_status;

  // check NVM

  bmp5_GetReg(BMP5_REG_STATUS, &nvm_status, 1);
  if (!((nvm_status & BMP5_INT_NVM_RDY) && (!(nvm_status & BMP5_INT_NVM_ERR))))
    return false;

  //  check if power-on complete

  bmp5_GetReg(BMP5_REG_INT_STATUS, &por_status,1);
  return por_status & BMP5_INT_ASSERTED_POR_SOFTRESET_COMPLETE;
}

bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
{
 
  uint8_t status = 0;
  uint8_t cmd;
  uint8_t chip_id;
  bool power_up;

  // default return values

  *pressure = 0;
  *temperature = SHRT_MIN;

  // power up

  lpsOn();  

  stopMilliseconds(true,3);  

  // read once to enable spi interface

  bmp5_GetReg(BMP5_REG_CHIP_ID, &chip_id, 1);

  for (int i = 0; i<4; i++) 
  {
      power_up = power_up_check();
      if (power_up)
        break;
      stopMilliseconds(true,3); 
  }

  if (!power_up)
    return false;

  // do any necessary initialization

  // Enter standby mode -- doesn't appear necessary

  //cmd = 1<<7;
  //bmp5_SetReg(BMP5_REG_ODR_CONFIG,&cmd,1);
  //stopMilliseconds(true,2);

  // Enable Pressure

  cmd = 1<< BMP5_PRESS_EN_POS;
  bmp5_SetReg( BMP5_REG_OSR_CONFIG, &cmd, 1);

  // Enable Interrupt -- not needed for software read of status register

  //cmd = 11; // Enabled, Latched, Active High, PushPull
  //bmp5_SetReg(BMP5_REG_INT_CONFIG,&cmd,1);

  // Set Interrupt Source

  cmd = 1; // data rdy interrupt
  bmp5_SetReg(BMP5_REG_INT_SOURCE,&cmd,1);

  // trigger sample -- set powermode to forced

  cmd = 2;
  bmp5_SetReg(BMP5_REG_ODR_CONFIG,&cmd,1);

  for (int i = 0; i < 5; i++){
    stopMilliseconds(true,5);
    bmp5_GetReg(BMP5_REG_INT_STATUS, &status,1);
    if (status & BMP5_INT_ASSERTED_DRDY)
      break;
  }

  // read data if datardy bit is set

  if (status & BMP5_INT_ASSERTED_DRDY) {
    bmp5_GetReg(BMP5_REG_TEMP_DATA_LSB,(uint8_t *) temperature, 2);
    bmp5_GetReg(BMP5_REG_PRESS_DATA_LSB, (uint8_t *) pressure, 2);
  }

  lpsOff();  // power down
  return status & BMP5_INT_ASSERTED_DRDY;
}

bool lpsTest(void)
{
  uint8_t chip_id;
  bool power_check;
  lpsOn();
  chThdSleepMilliseconds(3); 

  // read twice because first read just forces SPI 
  
  bmp5_GetReg(BMP5_REG_CHIP_ID, &chip_id, 1);
  bmp5_GetReg(BMP5_REG_CHIP_ID, &chip_id, 1);

  // do power on check
  
  power_check = power_up_check();

  lpsOff();
  return ((chip_id == BMP5_CHIP_ID_VALUE) && power_check);
}
