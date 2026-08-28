/**
 * @file bmp581.c
 * @brief Descriptor-backed Bosch BMP581 pressure sensor driver.
 * @author tag firmware authors
 * @date 2026-08-28
 */

#include "bmp581.h"

#include "bmp5.h"
#include "custom.h"
#include "debug_log.h"
#include "hal.h"

#define BMP581_PRESSURE_PA_PER_HPA 100.0f
#define BMP581_CENTI_C_PER_C 100.0f
#define BMP581_INIT_ATTEMPTS 3U
#define BMP581_INIT_RETRY_DELAY_US 2000U

static struct bmp5_osr_odr_press_config bmp581_active_config = {
  .osr_t = BMP5_OVERSAMPLING_2X,
  .osr_p = BMP5_OVERSAMPLING_4X,
  .press_en = BMP5_ENABLE,
  .odr = BMP5_ODR_50_HZ
};

/**
 * @brief Convert a tag register-bus result into a Bosch SensorAPI result.
 *
 * @param[in] result Project register helper result.
 * @return BMP5_INTF_RET_SUCCESS on success, otherwise -1.
 */
static BMP5_INTF_RET_TYPE bmp581_bus_result(int result)
{
  return result == MSG_OK ? BMP5_INTF_RET_SUCCESS : (BMP5_INTF_RET_TYPE)-1;
}

/**
 * @brief Read BMP581 registers using the sensor's SPI command framing.
 *
 * @details A BMP581 SPI read clocks one command byte followed by dummy
 *          transmit bytes. The MISO byte captured during the command phase is
 *          discarded; bytes captured while sending dummy data are returned to
 *          the caller.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] reg Register address, with or without BMP5_SPI_RD_MASK applied.
 * @param[out] data Destination buffer.
 * @param[in] len Number of register bytes to read.
 * @return MSG_OK on success or MSG_RESET for an unsupported bus or transfer
 *         failure.
 */
static int bmp581_spi_read(const TagPressureDevice *device, uint8_t reg,
                           uint8_t *data, uint32_t len)
{
  const TagRegisterDevice *registers = device->registers;
  const TagSpiDevice *spi;
  uint8_t command = (uint8_t)(reg | BMP5_SPI_RD_MASK);
  bool ok;

  if (registers->bus.kind != TAG_BUS_SPI)
    return MSG_RESET;

  spi = tagBusSpiDevice(&registers->bus);
  tagSpiSelect(spi);
  ok = tagSpiPolledSend(spi, &command, 1U) &&
       tagSpiPolledReceive(spi, data, len);
  tagSpiDeselect(spi);

  return ok ? MSG_OK : MSG_RESET;
}

/**
 * @brief Bosch SensorAPI read callback backed by BMP581 SPI framing.
 *
 * @param[in] reg_addr Sensor register address.
 * @param[out] read_data Destination buffer.
 * @param[in] len Number of bytes to read.
 * @param[in] intf_ptr Opaque TagPressureDevice pointer.
 * @return Bosch interface result.
 */
static BMP5_INTF_RET_TYPE bmp581_bus_read(uint8_t reg_addr,
                                          uint8_t *read_data,
                                          uint32_t len,
                                          void *intf_ptr)
{
  const TagPressureDevice *device = (const TagPressureDevice *)intf_ptr;
  return bmp581_bus_result(bmp581_spi_read(device, reg_addr, read_data, len));
}

/**
 * @brief Bosch SensorAPI write callback backed by the tag register helper.
 *
 * @param[in] reg_addr Sensor register address.
 * @param[in] write_data Source buffer.
 * @param[in] len Number of bytes to write.
 * @param[in] intf_ptr Opaque TagPressureDevice pointer.
 * @return Bosch interface result.
 */
static BMP5_INTF_RET_TYPE bmp581_bus_write(uint8_t reg_addr,
                                           const uint8_t *write_data,
                                           uint32_t len,
                                           void *intf_ptr)
{
  const TagPressureDevice *device = (const TagPressureDevice *)intf_ptr;
  return bmp581_bus_result(tagRegisterWrite(device->registers, reg_addr,
                                            write_data, len));
}

/**
 * @brief Bosch SensorAPI microsecond delay callback.
 *
 * @param[in] period Delay in microseconds.
 * @param[in] intf_ptr Unused callback context.
 */
static void bmp581_delay_us(uint32_t period, void *intf_ptr)
{
  (void)intf_ptr;
  chThdSleepMicroseconds(period);
}

/**
 * @brief Initialize a Bosch device wrapper around the tag descriptor.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] dev Bosch SensorAPI device object.
 */
static void bmp581_prepare_dev(const TagPressureDevice *device,
                               struct bmp5_dev *dev)
{
  dev->chip_id = 0U;
  dev->intf_ptr = (void *)device;
  dev->read = bmp581_bus_read;
  dev->write = bmp581_bus_write;
  dev->delay_us = bmp581_delay_us;
  dev->intf_rslt = BMP5_INTF_RET_SUCCESS;
  dev->intf = BMP5_SPI_INTF;
}

/**
 * @brief Read one BMP581 register using a literal SPI command transaction.
 *
 * @details Reuses the BMP581-specific SPI read helper so the reset
 *          interface-selection read exactly matches the Bosch SPI framing:
 *          one command byte followed by dummy clocks while CS is asserted.
 *
 * @param[in] device Pressure device descriptor.
 * @param[in] reg Register address without protocol read mask.
 * @param[out] value Register value captured from MISO.
 * @return true when the complete SPI transaction was transferred.
 */
static bool bmp581_raw_spi_read(const TagPressureDevice *device, uint8_t reg,
                                uint8_t *value)
{
  return bmp581_spi_read(device, reg, value, 1U) == MSG_OK;
}

/**
 * @brief Issue the BMP581 SPI-selection dummy read required after reset.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] chip_id Raw chip-id byte returned by the dummy access.
 * @return true when the SPI transaction completed.
 */
static bool bmp581_select_spi(const TagPressureDevice *device, uint8_t *chip_id)
{
  bool ok = bmp581_raw_spi_read(device, BMP5_REG_CHIP_ID, chip_id);

  chThdSleepMicroseconds(BMP581_INIT_RETRY_DELAY_US);
  return ok;
}

/**
 * @brief Initialize the Bosch SensorAPI device with reset-time retries.
 *
 * @details BMP581 starts in I2C/I3C mode after reset and switches to SPI only
 *          after a complete CS-low SPI transaction. The first read is invalid
 *          by design, and the NVM-ready check can also race very early boot, so
 *          the probe performs the selection read and retries Bosch init a few
 *          times before reporting failure.
 *
 * @param[in] device Pressure device descriptor.
 * @param[out] dev Bosch SensorAPI device object.
 * @param[out] raw_chip_id Last raw chip-id byte observed before init.
 * @param[out] raw_read_ok true when the raw SPI transaction completed.
 * @return BMP5_OK on success, otherwise the final Bosch SensorAPI error.
 */
static int8_t bmp581_init_device(const TagPressureDevice *device,
                                 struct bmp5_dev *dev,
                                 uint8_t *raw_chip_id,
                                 bool *raw_read_ok)
{
  int8_t rc = BMP5_E_COM_FAIL;

  for (uint8_t attempt = 0U; attempt < BMP581_INIT_ATTEMPTS; attempt++) {
    *raw_chip_id = 0U;
    *raw_read_ok = bmp581_select_spi(device, raw_chip_id);
    bmp581_prepare_dev(device, dev);
    rc = bmp5_init(dev);
    if (rc == BMP5_OK)
      return rc;

    chThdSleepMicroseconds(BMP581_INIT_RETRY_DELAY_US);
  }

  return rc;
}

/**
 * @brief Saturating conversion from float degrees Celsius to centi-Celsius.
 *
 * @param[in] temperature_c Temperature in degrees Celsius.
 * @return Saturated centi-degree Celsius representation.
 */
static int16_t bmp581_centi_c(float temperature_c)
{
  float scaled = temperature_c * BMP581_CENTI_C_PER_C;

  if (scaled > 32767.0f)
    return INT16_MAX;
  if (scaled < -32768.0f)
    return INT16_MIN;
  if (scaled >= 0.0f)
    return (int16_t)(scaled + 0.5f);
  return (int16_t)(scaled - 0.5f);
}

bool bmp581_check_who_am_i_device(const TagPressureDevice *device)
{
  struct bmp5_dev dev;
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  bool raw_read_ok = false;

  tagPressureDeviceBegin(device);
  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok);
  tagPressureDeviceEnd(device);

  if ((rc != BMP5_OK) ||
      ((dev.chip_id != BMP5_CHIP_ID_PRIM) &&
       (dev.chip_id != BMP5_CHIP_ID_SEC))) {
    debug_log_printf("BMP581: probe raw_ok=%u raw_id=0x%x rc=%d chip=0x%x"
                     " intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, rc, dev.chip_id,
                     dev.intf_rslt);
  }

  return (rc == BMP5_OK) &&
         ((dev.chip_id == BMP5_CHIP_ID_PRIM) ||
          (dev.chip_id == BMP5_CHIP_ID_SEC));
}

int bmp581_set_idle_device(const TagPressureDevice *device)
{
  struct bmp5_dev dev;
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  bool raw_read_ok = false;

  tagPressureDeviceBegin(device);
  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok);
  if (rc == BMP5_OK)
    rc = bmp5_set_power_mode(BMP5_POWERMODE_STANDBY, &dev);
  tagPressureDeviceEnd(device);

  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: idle raw_ok=%u raw_id=0x%x rc=%d chip=0x%x"
                     " intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, rc, dev.chip_id,
                     dev.intf_rslt);
  }

  return rc;
}

int bmp581_config_continuous_device(const TagPressureDevice *device,
                                    bmp581_odr_t odr)
{
  struct bmp5_dev dev;
  struct bmp5_iir_config iir_cfg = {
    .set_iir_t = BMP5_IIR_FILTER_BYPASS,
    .set_iir_p = BMP5_IIR_FILTER_BYPASS,
    .shdw_set_iir_t = BMP5_DISABLE,
    .shdw_set_iir_p = BMP5_DISABLE,
    .iir_flush_forced_en = BMP5_DISABLE
  };
  struct bmp5_int_source_select int_src = {
    .drdy_en = BMP5_ENABLE,
    .fifo_full_en = BMP5_DISABLE,
    .fifo_thres_en = BMP5_DISABLE,
    .oor_press_en = BMP5_DISABLE
  };
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  bool raw_read_ok = false;

  tagPressureDeviceBegin(device);

  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok);
  if (rc == BMP5_OK)
    rc = bmp5_set_power_mode(BMP5_POWERMODE_STANDBY, &dev);
  if (rc == BMP5_OK) {
    bmp581_active_config.odr = (uint8_t)odr;
    rc = bmp5_set_osr_odr_press_config(&bmp581_active_config, &dev);
  }
  if (rc == BMP5_OK)
    rc = bmp5_set_iir_config(&iir_cfg, &dev);
  if (rc == BMP5_OK)
    rc = bmp5_configure_interrupt(BMP5_PULSED, BMP5_ACTIVE_HIGH,
                                  BMP5_INTR_PUSH_PULL, BMP5_INTR_ENABLE,
                                  &dev);
  if (rc == BMP5_OK)
    rc = bmp5_int_source_select(&int_src, &dev);
  if (rc == BMP5_OK)
    rc = bmp5_set_power_mode(BMP5_POWERMODE_CONTINUOUS, &dev);
  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: config raw_ok=%u raw_id=0x%x rc=%d chip=0x%x"
                     " intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, rc, dev.chip_id,
                     dev.intf_rslt);
  }

  tagPressureDeviceEnd(device);
  return rc;
}

bool bmp581_data_ready_device(const TagPressureDevice *device)
{
  struct bmp5_dev dev;
  uint8_t int_status = 0U;
  int8_t rc;

  tagPressureDeviceBegin(device);
  bmp581_prepare_dev(device, &dev);
  rc = bmp5_get_interrupt_status(&int_status, &dev);
  tagPressureDeviceEnd(device);

  return (rc == BMP5_OK) && ((int_status & BMP5_INT_ASSERTED_DRDY) != 0U);
}

int bmp581_read_pressure_temp_device(const TagPressureDevice *device,
                                     float *pressure_hpa,
                                     int16_t *temperature_centi_c)
{
  struct bmp5_dev dev;
  struct bmp5_sensor_data sensor_data;
  int8_t rc;

  tagPressureDeviceBegin(device);
  bmp581_prepare_dev(device, &dev);
  rc = bmp5_get_sensor_data(&sensor_data, &bmp581_active_config, &dev);
  tagPressureDeviceEnd(device);

  if (rc == BMP5_OK) {
    *pressure_hpa = sensor_data.pressure / BMP581_PRESSURE_PA_PER_HPA;
    *temperature_centi_c = bmp581_centi_c(sensor_data.temperature);
  }

  return rc;
}
