#include "hal.h"
#include "custom.h"
#include "power.h"
#include "sensor_io.h"
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

#define CTRL1_SOFT_RESET (1<<5)
#define CTRL1_IF_ADD_INC (1<<4)
#define CTRL1_WU_EN (7)
#define CTRL4_BDU (1<<5)
#define CTRL5_1_6HZ (1<<4)
#define CTRL5_6HZ_3HZ ((4<<4)|(3<<2))
#define CTRL5_POWER_DOWN (0)
#define INT_CFG_SLEEP_STATUS_ON_INT (1<<3)
#define INT_CFG_ENABLE (1)
#define MD1_CFG_INT1_SLEEP_CHANGE (1<<7)
#define MD1_CFG_WKUP (1<<5)
#define MD2_CFG_INT2_SLEEP_CHANGE (1<<7)
#define WAKE_UP_DUR_7ODR (1<<5)
#define WAKE_UP_THS_0_5G (1<<4)

/* 
Wakeup threshold

each bit is (1/64)*2 g = 1/32 g
0.5 g = 0x10


*/


#if defined(ACCEL_USART)
static const TagUsartBus lis2du12_usart_bus = {
    .usart = USART2,
    .cs = LINE_ACCEL_CS,
    .dummy = 0xff,
};

static const TagStUsartRegisterBus lis2du12_register_bus = {
    .bus = &lis2du12_usart_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus lis2du12_registers = {
    .read_register = tagStUsartReadRegister,
    .write_register = tagStUsartWriteRegister,
    .context = &lis2du12_register_bus,
};
#elif defined(ACCEL_USE_SPI)
static const TagSpiBus lis2du12_spi_bus = {
    .spi = SPI1,
    .cs = LINE_ACCEL_CS,
    .dummy = 0xff,
};

static const TagStSpiRegisterBus lis2du12_register_bus = {
    .bus = &lis2du12_spi_bus,
    .read_mask = 0x80,
    .write_mask = 0x00,
};

static const TagRegisterBus lis2du12_registers = {
    .read_register = tagStSpiReadRegister,
    .write_register = tagStSpiWriteRegister,
    .context = &lis2du12_register_bus,
};
#else
#error "LIS2DU12 requires ACCEL_USART or ACCEL_USE_SPI"
#endif

static void LIS2DU12_write_byte(uint8_t reg, uint8_t val)
{
  /*
   * The wakeup setup is a sequence of single-register writes. Keep those byte
   * writes as one selected transaction, matching the original CompassTag
   * driver exactly; some synchronous-USART parts are fussier about transaction
   * shape than their SPI-like command format suggests.
   */
  uint8_t buffer[] = {reg, val};

#if defined(ACCEL_USART)
  tagUsartBusWrite(lis2du12_register_bus.bus, buffer, sizeof(buffer));
#elif defined(ACCEL_USE_SPI)
  tagSpiBusWrite(lis2du12_register_bus.bus, buffer, sizeof(buffer));
#endif
}

static int32_t LIS2DU12_read(uint8_t reg, uint8_t *bufp, uint16_t len)
{
  return tagRegisterRead(&lis2du12_registers, reg, bufp, len);
}


/*
void lis2du12_init(bool lpf)
{
  accelSpiOn();
  // set block data update, automatic register increment, turn off cs pullup, turn off i2c interface
  LIS2DU12_write(LIS2DU12_CTRL2, (15 << 1));
  // Set full scale, lpf, filter odr/20
  if (lpf)
  {
    // lpf output
    LIS2DU12_write(LIS2DU12_CTRL6, (3 << 6));
  }
  else
  {
    // hpf output
    LIS2DU12_write(LIS2DU12_CTRL6, (3 << 6) | (1 << 3));
  }
  // set odr (25hz), operating mode (continuous), power mode 2 //4
  LIS2DU12_write(LIS2DU12_CTRL1, (3 << 4) | 1); //3
  accelSpiOff();
}
  */

void accelDeinit(void)
{
  // soft reset
  accelOn();
  LIS2DU12_write_byte(LIS2DU12_CTRL5, (0));  /* power down */
  LIS2DU12_write_byte(LIS2DU12_CTRL1,0x20U); /* reset */
  accelOff();
}

/*
 *  Notes on wakeup duration and threshold
 *
 *   Default case, each bit of threshold (2g range) = 2/64 = 0.03g
 *                                                    max_value of thresh is 127 = 2g
 *   bittag has threshold of 0.5g  which is 16*0.03.
 *   not clear we want that level, try 8*0.03
 *   here we need wakeup threshold of 0x10
 * 
 *  
*/

void accelInit(lis2du12mode_t mode)
{
  /* send sleep state on pin, so activity bit is reversed */
  accelOn();

  switch (mode) {
  
    case ACCEL_WAKEUP_MODE: 
      LIS2DU12_write_byte(LIS2DU12_CTRL5, CTRL5_POWER_DOWN);  /* power down */
      LIS2DU12_write_byte(LIS2DU12_CTRL1, CTRL1_SOFT_RESET); // Software reset
      LIS2DU12_write_byte(LIS2DU12_CTRL1, CTRL1_IF_ADD_INC | CTRL1_WU_EN); // ADD_INC, Wkup x,y,z
      //LIS2DU12_write_byte(LIS2DU12_CTRL2, 0x0U);  // Make sure CTRL2 is reset
      //LIS2DU12_write_byte(LIS2DU12_CTRL3, 0x0U);  // Make sure CTRL3 is reset
      LIS2DU12_write_byte(LIS2DU12_CTRL4, CTRL4_BDU); // was A0, now block data update
      LIS2DU12_write_byte(LIS2DU12_INTERRUPT_CFG,INT_CFG_ENABLE); // Sleep status on interrupt
      LIS2DU12_write_byte(LIS2DU12_WAKE_UP_DUR, WAKE_UP_DUR_7ODR); // Wakeup duration = 7 sample times, Sleep duration = 16 samples times
      LIS2DU12_write_byte(LIS2DU12_WAKE_UP_THS,0x4);//WAKE_UP_THS_0_5G);  // was 42
      LIS2DU12_write_byte(LIS2DU12_MD1_CFG,MD1_CFG_WKUP); // Wakeup event on INT1 pin, 0x22U changes wake_up_dur interpretation
      LIS2DU12_write_byte(LIS2DU12_CTRL5, CTRL5_6HZ_3HZ); // ODR = 6hz, BW = 3hz -- was 3C which is ultralow power mode
      break;
    case ACCEL_SAMPLE_50HZ_MODE:
      LIS2DU12_write_byte(LIS2DU12_CTRL1, 0x10U); // ADD_INC
      LIS2DU12_write_byte(LIS2DU12_CTRL4, 0x20U); // Block data update
      LIS2DU12_write_byte(LIS2DU12_CTRL5, 0x74U); // ODR = 50hz, BW = 12.5hz
      break;
    case ACCEL_SAMPLE_100HZ_MODE:
      LIS2DU12_write_byte(LIS2DU12_CTRL1, 0x10U); // ADD_INC
      LIS2DU12_write_byte(LIS2DU12_CTRL4, 0x20U); // Block data update
      LIS2DU12_write_byte(LIS2DU12_CTRL5, 0x84U); // ODR = 100hz, BW = 25hz
      break;
  }

  accelOff();
}

bool accelSample(uint8_t *data)
{
  uint8_t status;
  bool res = false;
  accelOn();
  LIS2DU12_read(LIS2DU12_STATUS, &status, 1);
  if (status & 1){
    LIS2DU12_read(LIS2DU12_OUT_X_L,data,6);
    res = true;
  }

  accelOff();
  return res;
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
  LIS2DU12_write(LIS2DU12_CTRL6, (3 << 6) | (1 << 3));

  // switch to fifo mode and then sleep
  LIS2DU12_write(LIS2DU12_FIFO_CTRL, 6 << 5 | 31);

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

bool accelTest(void) {
  uint8_t val;
  accelOn();
  LIS2DU12_read(LIS2DU12_WHO_AM_I,&val, 1);
  accelOff();
  return val == LIS2DU12_ID;
}
