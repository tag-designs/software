#include "hal.h"
#include "custom.h"
#include "bmp5.h"
#include "lps.h"
#include "lps22hh.h"
#include "lps33hw.h"
#include "power.h"
#include "rtc_api.h"
#include "sensor_io.h"

#include <limits.h>

/*
 * Compatibility shim for the historical `lps*` pressure API.
 *
 * The concrete pressure drivers are intentionally parameterized by
 * TagPressureDevice and no longer choose their own bus or power policy. This
 * file is the single place where a tag's selected pressure sensor, transport,
 * and legacy API names are connected.
 */

#if defined(TAG_SENSOR_PRESSURE_LPS27) || defined(USE_LPS27)
#define LPS_SHIM_LPS27 1
#elif defined(TAG_SENSOR_PRESSURE_LPS22HH) || defined(USE_LPS22HH)
#define LPS_SHIM_LPS22HH 1
#elif defined(USE_LPS22)
#define LPS_SHIM_LPS22 1
#elif defined(USE_LPS33)
#define LPS_SHIM_LPS33 1
#elif defined(USE_BMP)
#define LPS_SHIM_BMP5 1
#else
#error "No pressure sensor selected for lps.c"
#endif

#if defined(LPS_SHIM_LPS27)
#define LPS27HW_ADR (0x5C)

#if defined(LPS_I2C)
#define LPS27_TIMEOUT 100
static const TagI2cDevice lps_i2c = {
  .controller = &tagI2c1DefaultController,
  .address = LPS27HW_ADR,
  .timeout = LPS27_TIMEOUT,
  .sleep_policy = TAG_I2C_SLEEP_CUSTOM,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagI2cBusOps,
  .device = &lps_i2c,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagI2cReadRegister,
  .write_register = tagI2cWriteRegister,
  .context = &lps_i2c,
  .bus = &lps_register_bus,
};
#elif defined(LPS_SPI)
static const TagSpiDevice lps_spi_device = {
  .controller = &tagSpi1DefaultController,
  .config = &tagSpiDefaultConfig,
  .cs = LINE_STEVAL_CS,
  .sck = LINE_STEVAL_SCK,
  .miso = LINE_STEVAL_MISO,
  .mosi = LINE_STEVAL_MOSI,
  .pwr = LINE_STEVAL_PWR,
  .dummy = 0xff,
  .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagSpiBusOps,
  .device = &lps_spi_device,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagStSpiReadRegisterDevice,
  .write_register = tagStSpiWriteRegisterDevice,
  .bus = &lps_register_bus,
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#elif defined(LPS_USART)
static const TagUsartDevice lps_usart_device = {
  .controller = &tagUsart2SyncController,
  .config = &tagUsart2SyncDefaultConfig,
  .cs = LINE_LPS_CS,
  .sck = LINE_LPS_SCK,
  .tx = LINE_LPS_TX,
  .rx = LINE_LPS_RX,
  .pwr = LINE_LPS_PWR,
  .dummy = 0xff,
  .sleep_policy = TAG_USART_SLEEP_FLOAT,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagUsartBusOps,
  .device = &lps_usart_device,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagStUsartReadRegisterDevice,
  .write_register = tagStUsartWriteRegisterDevice,
  .bus = &lps_register_bus,
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#else
#error "LPS27 selected without LPS_I2C, LPS_SPI, or LPS_USART"
#endif

#elif defined(LPS_SHIM_LPS22HH)

static const TagSpiDevice lps_spi_device = {
  .controller = &tagSpi1DefaultController,
  .config = &tagSpiDefaultConfig,
  .cs = LINE_LPS_CS,
  .sck = LINE_LPS_SCK,
  .miso = LINE_LPS_MISO,
  .mosi = LINE_LPS_MOSI,
  .pwr = TAG_NO_LINE,
  .dummy = 0xff,
  .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagSpiBusOps,
  .device = &lps_spi_device,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagStSpiReadRegisterDevice,
  .write_register = tagStSpiWriteRegisterDevice,
  .bus = &lps_register_bus,
  .read_mask = 0x80,
  .write_mask = 0x00,
};

#elif defined(LPS_SHIM_LPS22)
#define LPS22HW_ADR (0x5C)

#if defined(LPS_I2C)
#define LPS22_TIMEOUT 100
static const TagI2cDevice lps_i2c = {
  .controller = &tagI2c1DefaultController,
  .address = LPS22HW_ADR,
  .timeout = LPS22_TIMEOUT,
  .sleep_policy = TAG_I2C_SLEEP_CUSTOM,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagI2cBusOps,
  .device = &lps_i2c,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagI2cReadRegister,
  .write_register = tagI2cWriteRegister,
  .context = &lps_i2c,
  .bus = &lps_register_bus,
};
#elif defined(LPS_SPI)
static const TagSpiDevice lps_spi_device = {
  .controller = &tagSpi1DefaultController,
  .config = &tagSpiDefaultConfig,
  .cs = LINE_STEVAL_CS,
  .sck = LINE_STEVAL_SCK,
  .miso = LINE_STEVAL_MISO,
  .mosi = LINE_STEVAL_MOSI,
  .pwr = LINE_STEVAL_PWR,
  .dummy = 0xff,
  .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagSpiBusOps,
  .device = &lps_spi_device,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagStSpiReadRegisterDevice,
  .write_register = tagStSpiWriteRegisterDevice,
  .bus = &lps_register_bus,
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#else
#error "LPS22 selected without LPS_I2C or LPS_SPI"
#endif

#elif defined(LPS_SHIM_LPS33)
#define LPS33HW_ADR (0x5C)

#if defined(LPS_I2C)
#define LPS33_TIMEOUT 100
static const TagI2cDevice lps_i2c = {
  .controller = &tagI2c1DefaultController,
  .address = LPS33HW_ADR,
  .timeout = LPS33_TIMEOUT,
  .sleep_policy = TAG_I2C_SLEEP_CUSTOM,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagI2cBusOps,
  .device = &lps_i2c,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagI2cReadRegister,
  .write_register = tagI2cWriteRegister,
  .context = &lps_i2c,
  .bus = &lps_register_bus,
};
#elif defined(LPS_SPI)
static const TagSpiDevice lps_spi_device = {
  .controller = &tagSpi1DefaultController,
  .config = &tagSpiDefaultConfig,
  .cs = LINE_STEVAL_CS,
  .sck = LINE_STEVAL_SCK,
  .miso = LINE_STEVAL_MISO,
  .mosi = LINE_STEVAL_MOSI,
  .pwr = LINE_STEVAL_PWR,
  .dummy = 0xff,
  .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagSpiBusOps,
  .device = &lps_spi_device,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagStSpiReadRegisterDevice,
  .write_register = tagStSpiWriteRegisterDevice,
  .bus = &lps_register_bus,
  .read_mask = 0x80,
  .write_mask = 0x00,
};
#else
#error "LPS33 selected without LPS_I2C or LPS_SPI"
#endif

#elif defined(LPS_SHIM_BMP5)

static const TagSpiDevice lps_spi_device = {
  .controller = &tagSpi1DefaultController,
  .config = &tagSpiDefaultConfig,
  .cs = LINE_STEVAL_CS,
  .sck = LINE_STEVAL_SCK,
  .miso = LINE_STEVAL_MISO,
  .mosi = LINE_STEVAL_MOSI,
  .pwr = LINE_STEVAL_PWR,
  .dummy = 0xff,
  .sleep_policy = TAG_SPI_SLEEP_FLOAT,
};
static const TagBusDevice lps_register_bus = {
  .ops = &tagSpiBusOps,
  .device = &lps_spi_device,
};
static const TagRegisterDevice lps_registers = {
  .read_register = tagStSpiReadRegisterDevice,
  .write_register = tagStSpiWriteRegisterDevice,
  .bus = &lps_register_bus,
  .read_mask = BMP5_SPI_RD_MASK,
  .write_mask = 0x00,
};

#endif

static void lps_default_sleep(int ms)
{
  /*
   * stopMilliseconds() now suspends active bus controllers internally. The
   * boolean is retained only for source compatibility, so the pressure shim no
   * longer needs to know which transport is active.
   */
  stopMilliseconds(false, ms);
}

static const TagPressureDevice lps_device = {
  .registers = &lps_registers,
  .sleep_ms = lps_default_sleep,
};

void tagPressureDeviceBegin(const TagPressureDevice *device)
{
  tagBusPowerOn(device->registers->bus);
  tagRegisterBusBegin(device->registers);
}

void tagPressureDeviceEnd(const TagPressureDevice *device)
{
  tagRegisterBusEnd(device->registers);
  tagBusPowerOff(device->registers->bus);
}

void lpsInit(void) {}

bool lpsGetPressureTemp(int16_t *pressure, int16_t *temperature)
{
#if defined(LPS_SHIM_LPS27)
  return lps27GetPressureTemp(&lps_device, pressure, temperature);
#elif defined(LPS_SHIM_LPS22)
  return lps22GetPressureTemp(&lps_device, pressure, temperature);
#elif defined(LPS_SHIM_LPS33)
  return lps33GetPressureTemp(&lps_device, pressure, temperature);
#elif defined(LPS_SHIM_BMP5)
  return bmp5GetPressureTemp(&lps_device, pressure, temperature);
#elif defined(LPS_SHIM_LPS22HH)
  int32_t raw_pressure = 0;
  int32_t raw_temperature = 0;
  if (lps22hh_read_raw_device(&lps_device, &raw_pressure,
                              &raw_temperature) != 0) {
    *pressure = SHRT_MIN;
    *temperature = SHRT_MIN;
    return false;
  }

  /*
   * The historical API stores pressure in units of 1/16 hPa and temperature
   * in units of 1/100 C. LPS22HH raw pressure is 1/4096 hPa and raw
   * temperature is already 1/100 C.
   */
  *pressure = (int16_t)(raw_pressure / 256);
  *temperature = (int16_t)raw_temperature;
  return true;
#endif
}

bool lpsTest(void)
{
#if defined(LPS_SHIM_LPS27)
  return lps27Test(&lps_device);
#elif defined(LPS_SHIM_LPS22)
  return lps22Test(&lps_device);
#elif defined(LPS_SHIM_LPS33)
  return lps33Test(&lps_device);
#elif defined(LPS_SHIM_BMP5)
  return bmp5Test(&lps_device);
#elif defined(LPS_SHIM_LPS22HH)
  return lps22hh_check_who_am_i_device(&lps_device);
#endif
}

float lpsPressure(int16_t pressure)
{
#if defined(LPS_SHIM_LPS27)
  return lps27Pressure(pressure);
#elif defined(LPS_SHIM_LPS22)
  return lps22Pressure(pressure);
#elif defined(LPS_SHIM_LPS33)
  return lps33Pressure(pressure);
#elif defined(LPS_SHIM_BMP5)
  return bmp5Pressure(pressure);
#elif defined(LPS_SHIM_LPS22HH)
  return pressure / 16.0f;
#endif
}

float lpsTemperature(int16_t temperature)
{
#if defined(LPS_SHIM_LPS27)
  return lps27Temperature(temperature);
#elif defined(LPS_SHIM_LPS22)
  return lps22Temperature(temperature);
#elif defined(LPS_SHIM_LPS33)
  return lps33Temperature(temperature);
#elif defined(LPS_SHIM_BMP5)
  return bmp5Temperature(temperature);
#elif defined(LPS_SHIM_LPS22HH)
  return temperature / 100.0f;
#endif
}

#if defined(LPS_SHIM_LPS22HH)
bool lps22hh_check_who_am_i(void)
{
  return lps22hh_check_who_am_i_device(&lps_device);
}

int lps22hh_set_idle(void)
{
  return lps22hh_set_idle_device(&lps_device);
}

int lps22hh_config_continuous(lps22hh_odr_t odr, lps22hh_lpf_t lpf)
{
  return lps22hh_config_continuous_device(&lps_device, odr, lpf);
}

int lps22hh_config_triggered(lps22hh_lpf_t lpf)
{
  return lps22hh_config_triggered_device(&lps_device, lpf);
}

int lps22hh_trigger_one_shot(void)
{
  return lps22hh_trigger_one_shot_device(&lps_device);
}

bool lps22hh_data_ready(void)
{
  return lps22hh_data_ready_device(&lps_device);
}

int lps22hh_read_raw(int32_t *pressure, int32_t *temperature)
{
  return lps22hh_read_raw_device(&lps_device, pressure, temperature);
}
#endif

#if defined(LPS_SHIM_LPS33)
bool GetPressureTemp(int16_t *pressure, int16_t *temperature)
{
  return lpsGetPressureTemp(pressure, temperature);
}

void lps33_SetReg(enum LPS33_Reg reg, unsigned char *val, int num)
{
  (void)tagRegisterWrite(lps_device.registers, (uint8_t)reg, val, num);
}

void lps33_GetReg(enum LPS33_Reg reg, uint8_t *val, int num)
{
  (void)tagRegisterRead(lps_device.registers, (uint8_t)reg, val, num);
}
#endif
