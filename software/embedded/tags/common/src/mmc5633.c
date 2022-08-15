#include "hal.h"
#include "app.h"
#include "mmc5633.h"

extern const I2CConfig i2cfg_mmc5633;
#define TIMEOUT 100

static int I2C1_MemWrite(uint8_t reg, unsigned char *buffer,
                         uint16_t size, uint32_t timeout)
{
  uint8_t txbuf[10];
  txbuf[0] = reg;
  for (int i = 0; i < 8 && i < size; i++)
  {
    txbuf[i + 1] = buffer[i];
  }
  return i2cMasterTransmitTimeout(&I2CD1, MMC5633_ADDR, txbuf, size + 1, 0, 0,
                                  timeout);
}

static int mmc5633_GetReg(enum MMC5633_Reg reg, uint8_t *val, int num)
{
  return i2cMasterTransmitTimeout(&I2CD1, MMC5633_ADDR, &reg, 1, val, num,
                                  TIMEOUT);
}

static int mmc5633_SetReg(enum MMC5633_Reg reg, unsigned char *val, int num)
{
  return I2C1_MemWrite(reg, val, num, TIMEOUT);
}

int mmc5633_set(void)
{
  uint8_t val = MMC5633_CTRL0_DO_SET;
  return mmc5633_SetReg(MMC5633_CTRL0, &val, 1);
}

int mmc5633_reset(void)
{
  uint8_t val = MMC5633_CTRL0_DO_RESET;
  return mmc5633_SetReg(MMC5633_CTRL0, &val, 1);
}

static mmc5633_one_sample(int32_t xyz[3], int delay){
  uint8_t buf[9];
  
  uint8_t val = MMC5633_CTRL0_TAKE_M_MEAS;
  mmc5633_SetReg(MMC5633_CTRL0, &val, 1);
  stopMilliseconds(false,delay);
  mmc5633_GetReg(MMC5633_XOUT0,buf,9);
  xyz[0] = (buf[0]<<12)|(buf[1]<<4)|(buf[6]>>4);
  xyz[1] = (buf[2]<<12)|(buf[3]<<4)|(buf[7]>>4);
  xyz[2] = (buf[4]<<12)|(buf[5]<<4)|(buf[8]>>4);
  return 0;
}

static const int msdelay[] = {
  7,4,2,2
};

int mmc5633_sample(int16_t xyz[3], int samples)
{
  uint32_t abc[3];
  uint32_t offset[3];
  uint32_t sum[3];
  uint8_t bw = MMC5633_CTRL1_ST_BW1;

  int i,j;
  
  
  mmc5633_SetReg(MMC5633_CTRL1, &bw, 1);
  mmc5633_reset();
  stopMilliseconds(false,2);
 
  mmc5633_one_sample(offset, msdelay[bw]);
  stopMilliseconds(false,2);
  mmc5633_set();
  stopMilliseconds(false,2);
  mmc5633_one_sample(abc,msdelay[bw]);
  stopMilliseconds(false,2);
  for (i = 0; i < 3; i++) {
    offset[i] = (offset[i]+abc[i])/2;
  }

  for (i = 0; i < 3; i++) {
    sum[i] = abc[i];
  }
  
  // accumulate samples

  for (i = 1; i < samples; i++) {
      //mmc5633_set();
      mmc5633_one_sample(abc,msdelay[bw]);
      stopMilliseconds(false,2);
      for (j = 0; j < 3; j++) {
        sum[j] += abc[j];
      }
  }

  // compute average and remove offset
  
  for (int i = 0; i < 3; i++) {
    xyz[i] = sum[i]/samples - offset[i];
  }
  return 0;
}

int mmc5633_init(void)
{
}

int mmc5633_get_pid(uint8_t *pid)
{
  return mmc5633_GetReg(MMC5633_PID, pid, 1);
}

bool mmc5633_test(void)
{
  uint8_t val = 0;
  magOn();
  mmc5633_get_pid(&val);
  magOff();
  return val == MMC5633_PID_VAL;
}