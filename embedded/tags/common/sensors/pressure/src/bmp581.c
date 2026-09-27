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
#include "rtc_api.h"

#define BMP581_PRESSURE_PA_PER_HPA 100.0f
#define BMP581_CENTI_C_PER_C 100.0f
#define BMP581_INIT_ATTEMPTS 12U
#define BMP581_INIT_RETRY_DELAY_US 2000U
#define BMP581_STATUS_CORE_READY 0x01U
/*
 * Data-sheet conversion time for bmp581_active_config's fixed oversampling
 * (OSR_T=2x: tconv_t ~1.1 ms; OSR_P=4x: tconv_p ~2.9 ms; BMP585 DS003 Table,
 * Sec. "Electrical Characteristics"), each +-5%, so ~4.0 ms nominal / ~4.2 ms
 * worst case. Waiting this long before the first poll means it almost
 * always finds DRDY already set, instead of the original loop's guaranteed-
 * to-miss immediate poll followed by one or two more 2 ms-spaced polls
 * before the conversion is actually done.
 */
#define BMP581_CONVERSION_SETTLE_MS 5U

/**
 * @brief Round a microsecond delay up to whole milliseconds for stopMilliseconds().
 *
 * @details All waits in this driver -- the Bosch SensorAPI's own delay_us()
 *          callback included -- used chThdSleepMicroseconds(), which blocks
 *          the calling thread but leaves the MCU in RUN mode for the whole
 *          wait. stopMilliseconds() is the same low-power STOP-mode sleep
 *          already used for comparable waits elsewhere in this same
 *          directory (lps27.c's power-up and ready-poll delays) and by
 *          other sensor/storage drivers; it only takes whole milliseconds,
 *          so a microsecond delay is rounded up (never down, so a wait is
 *          never shortened below what the data sheet requires).
 *
 * @param[in] period_us Requested delay in microseconds.
 * @return Equivalent delay in whole milliseconds, rounded up.
 */
static inline unsigned int bmp581_stop_ms(uint32_t period_us)
{
  return (unsigned int)((period_us + 999U) / 1000U);
}

static struct bmp5_osr_odr_press_config bmp581_active_config = {
  .osr_t = BMP5_OVERSAMPLING_2X,
  .osr_p = BMP5_OVERSAMPLING_4X,
  .press_en = BMP5_ENABLE,
  .odr = BMP5_ODR_50_HZ
};

static const bmp581_interrupt_config_t bmp581_continuous_interrupt_config =
    BMP581_INTERRUPT_PUSH_PULL_PULSED_ACTIVE_HIGH;

static const bmp581_interrupt_config_t bmp581_forced_interrupt_config =
    BMP581_INTERRUPT_OPEN_DRAIN_LATCHED_ACTIVE_LOW;

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
  stopMilliseconds(bmp581_stop_ms(period));
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
 * @brief Power the BMP581 rail and open its bus session.
 *
 * @param[in] device Pressure device descriptor.
 */
static void bmp581_begin_powered_session(const TagPressureDevice *device)
{
  tagBusPowerOn(&device->registers->bus);
  tagBusBegin(&device->registers->bus);
}

/**
 * @brief Close the BMP581 bus session without removing sensor power.
 *
 * @param[in] device Pressure device descriptor.
 */
static void bmp581_end_powered_session(const TagPressureDevice *device)
{
  tagBusEnd(&device->registers->bus);
}

/**
 * @brief Remove BMP581 power and run the tag-specific pin cleanup hook.
 *
 * @param[in] device Pressure device descriptor.
 */
static void bmp581_power_off(const TagPressureDevice *device)
{
  tagBusPowerOff(&device->registers->bus);
  tagPressureDeviceAfterPowerOff(device);
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

  stopMilliseconds(bmp581_stop_ms(BMP581_INIT_RETRY_DELAY_US));
  return ok;
}

/**
 * @brief Test whether a raw chip-id byte names a BMP5-family pressure sensor.
 *
 * @param[in] chip_id Register value read from BMP5_REG_CHIP_ID.
 * @return true when the value is one of the Bosch-defined BMP5 IDs.
 */
static bool bmp581_chip_id_valid(uint8_t chip_id)
{
  return chip_id == BMP5_CHIP_ID_PRIM || chip_id == BMP5_CHIP_ID_SEC;
}

/**
 * @brief Test whether Bosch init found the BMP581 stuck before NVM readiness.
 *
 * @details A STATUS value with the Bosch core-ready bit set but NVM-ready bit
 *          clear indicates that SPI communication is established, but the
 *          factory trim copy has not completed. Configuration writes are not
 *          attempted from this state; the caller uses it only to decide
 *          whether a soft reset recovery is worth trying.
 *
 * @param[in] rc Bosch SensorAPI result returned by bmp581_init_device().
 * @param[in] raw_chip_id Raw CHIP_ID register value from the same init attempt.
 * @param[in] status_last STATUS register value captured after the failure.
 * @return true when the BMP581 core is reachable, NVM is not ready, and no NVM
 *         error bits are asserted.
 */
static bool bmp581_needs_soft_reset_after_init(int8_t rc, uint8_t raw_chip_id,
                                               uint8_t status_last)
{
  uint8_t nvm_error_bits = BMP5_INT_NVM_ERR | BMP5_INT_NVM_CMD_ERR;

  return rc == BMP5_E_NVM_NOT_READY &&
         bmp581_chip_id_valid(raw_chip_id) &&
         ((status_last & BMP581_STATUS_CORE_READY) != 0U) &&
         ((status_last & nvm_error_bits) == 0U);
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
 * @param[out] status_last Last STATUS register byte observed after a failed
 *                         init attempt.
 * @return BMP5_OK on success, otherwise the final Bosch SensorAPI error.
 */
static int8_t bmp581_init_device(const TagPressureDevice *device,
                                 struct bmp5_dev *dev,
                                 uint8_t *raw_chip_id,
                                 bool *raw_read_ok,
                                 uint8_t *status_last)
{
  int8_t rc = BMP5_E_COM_FAIL;
  bool soft_reset_attempted = false;

  for (uint8_t attempt = 0U; attempt < BMP581_INIT_ATTEMPTS; attempt++) {
    *raw_chip_id = 0U;
    *status_last = 0U;
    *raw_read_ok = bmp581_select_spi(device, raw_chip_id);
    bmp581_prepare_dev(device, dev);
    rc = bmp5_init(dev);
    if (rc == BMP5_OK)
      return rc;

    (void)bmp581_spi_read(device, BMP5_REG_STATUS, status_last, 1U);

    if (!soft_reset_attempted &&
        bmp581_needs_soft_reset_after_init(rc, *raw_chip_id, *status_last)) {
      int8_t reset_rc = bmp5_soft_reset(dev);

      soft_reset_attempted = true;
      if (reset_rc != BMP5_OK) {
        debug_log_printf("BMP581: soft reset recovery failed status=0x%x"
                         " rc=%d reset_rc=%d intf=%d\r\n",
                         *status_last, rc, reset_rc, dev->intf_rslt);
      }
    }

    stopMilliseconds(bmp581_stop_ms(BMP581_INIT_RETRY_DELAY_US));
  }

  return rc;
}

/**
 * @brief Apply BMP581 pressure sampling, filter, and interrupt configuration.
 *
 * @param[in,out] dev Initialized Bosch SensorAPI device object.
 * @param[in] odr Output data-rate register encoding.
 * @param[in] interrupt_config Interrupt pin mode applied before DRDY enable.
 * @return BMP5_OK on success or a negative Bosch SensorAPI error.
 * @pre The sensor must be initialized and reachable through an active bus
 *      session.
 */
static int8_t
bmp581_apply_sampling_config(struct bmp5_dev *dev, bmp581_odr_t odr,
                             const bmp581_interrupt_config_t *interrupt_config)
{
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

  if (interrupt_config == NULL)
    interrupt_config = &bmp581_forced_interrupt_config;

  rc = bmp5_set_power_mode(BMP5_POWERMODE_STANDBY, dev);
  if (rc == BMP5_OK) {
    bmp581_active_config.odr = (uint8_t)odr;
    rc = bmp5_set_osr_odr_press_config(&bmp581_active_config, dev);
  }
  if (rc == BMP5_OK)
    rc = bmp5_set_iir_config(&iir_cfg, dev);
  if (rc == BMP5_OK)
    rc = bmp5_configure_interrupt(interrupt_config->mode,
                                  interrupt_config->polarity,
                                  interrupt_config->drive,
                                  BMP5_INTR_ENABLE,
                                  dev);
  if (rc == BMP5_OK)
    rc = bmp5_int_source_select(&int_src, dev);

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
  uint8_t status_last = 0U;
  bool raw_read_ok = false;

  tagPressureDeviceBegin(device);
  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok,
                          &status_last);
  tagPressureDeviceEnd(device);

  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: probe raw_ok=%u raw_id=0x%x status=0x%x"
                     " rc=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, status_last, rc);
  }

  return rc == BMP5_OK;
}

int bmp581_set_idle_device(const TagPressureDevice *device)
{
  struct bmp5_dev dev;
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  uint8_t status_last = 0U;
  bool raw_read_ok = false;

  tagPressureDeviceBegin(device);
  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok,
                          &status_last);
  if (rc == BMP5_OK)
    rc = bmp5_set_power_mode(BMP5_POWERMODE_STANDBY, &dev);
  tagPressureDeviceEnd(device);

  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: idle raw_ok=%u raw_id=0x%x status=0x%x"
                     " rc=%d chip=0x%x intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, status_last, rc,
                     dev.chip_id, dev.intf_rslt);
  }

  return rc;
}

int bmp581_config_continuous_device(const TagPressureDevice *device,
                                    bmp581_odr_t odr)
{
  struct bmp5_dev dev;
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  uint8_t status_last = 0U;
  bool raw_read_ok = false;

  tagPressureDeviceBegin(device);

  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok,
                          &status_last);
  if (rc == BMP5_OK)
    rc = bmp581_apply_sampling_config(&dev, odr,
                                      &bmp581_continuous_interrupt_config);
  if (rc == BMP5_OK)
    rc = bmp5_set_power_mode(BMP5_POWERMODE_CONTINUOUS, &dev);
  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: config raw_ok=%u raw_id=0x%x status=0x%x"
                     " rc=%d chip=0x%x intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, status_last, rc,
                     dev.chip_id, dev.intf_rslt);
  }

  tagPressureDeviceEnd(device);
  return rc;
}

int bmp581_config_forced_device(const TagPressureDevice *device,
                                bmp581_odr_t odr,
                                const bmp581_interrupt_config_t *interrupt_config)
{
  struct bmp5_dev dev;
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  uint8_t status_last = 0U;
  bool raw_read_ok = false;

  bmp581_begin_powered_session(device);

  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok,
                          &status_last);
  if (rc == BMP5_OK)
    rc = bmp581_apply_sampling_config(&dev, odr, interrupt_config);
  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: forced config raw_ok=%u raw_id=0x%x"
                     " status=0x%x rc=%d chip=0x%x intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, status_last, rc,
                     dev.chip_id, dev.intf_rslt);
  }

  bmp581_end_powered_session(device);
  if (rc != BMP5_OK)
    bmp581_power_off(device);

  return rc;
}

/**
 * @brief Issue up to two single-byte SPI register writes without a
 *        preceding read.
 *
 * @details Per the data sheet (BMP585 DS003 Sec. 5.5.2 "SPI Write
 *          Operation"): "for each write byte the address has to be sent
 *          over separately" -- a genuine burst write on this chip is a
 *          sequence of (address, data) pairs within one chip-select
 *          assertion, not one address followed by streamed data bytes (that
 *          format is burst-*read*-only; the data sheet's read section has no
 *          equivalent warning). bmp5_set_regs()'s own byte-at-a-time loop
 *          reflects this by re-sending the (incremented) address with every
 *          byte -- correctly, just as two fully separate transactions
 *          instead of one held transaction. This does the same two
 *          transactions bmp5_set_regs() would, skipping only its own
 *          preceding bmp5_get_regs() call, for registers whose target value
 *          is fully known up front (no read-modify-write needed).
 *
 * @param[in,out] dev Bosch SensorAPI device object.
 * @param[in] reg_addr First register address (SPI write, no read mask).
 * @param[in] data Bytes to write, one per consecutive register address.
 * @param[in] len Number of bytes/registers (1 or 2).
 * @return BMP5_OK on success or BMP5_E_COM_FAIL on a transport failure.
 */
static int8_t bmp581_write_bytes(struct bmp5_dev *dev, uint8_t reg_addr,
                                 const uint8_t *data, uint32_t len)
{
  uint32_t idx;

  for (idx = 0U; idx < len; idx++) {
    dev->intf_rslt = dev->write((uint8_t)(reg_addr + idx), &data[idx], 1,
                                dev->intf_ptr);
    if (dev->intf_rslt != BMP5_INTF_RET_SUCCESS)
      return BMP5_E_COM_FAIL;
  }
  return BMP5_OK;
}

/**
 * @brief Fast-path equivalent of bmp581_apply_sampling_config().
 *
 * @details Same register targets as bmp581_apply_sampling_config(), reached
 *          in 5 transactions instead of 20: OSR_CONFIG (0x36) and the
 *          adjacent ODR_CONFIG (0x37) as two single-byte writes with no
 *          preceding reads (oversampling/pressure-enable computed directly;
 *          deep-standby-disable, standby power mode, and ODR computed
 *          directly instead of read-modify-written); a discard read of
 *          INT_STATUS (0x27) to clear the power-on-reset interrupt latch,
 *          matching bmp5_configure_interrupt()'s own clear-before-enable
 *          step; then INT_CONFIG (0x14) and the adjacent INT_SOURCE (0x15)
 *          as two more single-byte writes (pin mode/polarity/drive/enable,
 *          then data-ready source-select). DSP_CONFIG/DSP_IIR (IIR filter,
 *          0x30-0x31) is not written at all: its power-on reset value
 *          already selects filter-bypass for both temperature and pressure,
 *          which is the only configuration this driver ever uses. See
 *          bmp581_config_forced_fast_device()'s precondition -- this only
 *          produces the intended state from a true power-on reset.
 *
 * @param[in,out] dev Initialized Bosch SensorAPI device object.
 * @param[in] odr Output data-rate register encoding.
 * @param[in] interrupt_config Interrupt pin mode applied before DRDY enable.
 * @return BMP5_OK on success or a negative Bosch SensorAPI error.
 */
static int8_t
bmp581_apply_sampling_config_fast(struct bmp5_dev *dev, bmp581_odr_t odr,
                                  const bmp581_interrupt_config_t *interrupt_config)
{
  uint8_t osr_odr[2] = { 0U, 0U };
  uint8_t int_regs[2] = { 0U, 0U };
  uint8_t discard_status = 0U;
  int8_t rc;

  if (interrupt_config == NULL)
    interrupt_config = &bmp581_forced_interrupt_config;

  bmp581_active_config.odr = (uint8_t)odr;

  osr_odr[0] = BMP5_SET_BITS_POS_0(osr_odr[0], BMP5_TEMP_OS,
                                   bmp581_active_config.osr_t);
  osr_odr[0] = BMP5_SET_BITSLICE(osr_odr[0], BMP5_PRESS_OS,
                                 bmp581_active_config.osr_p);
  osr_odr[0] = BMP5_SET_BITSLICE(osr_odr[0], BMP5_PRESS_EN,
                                 bmp581_active_config.press_en);
  osr_odr[1] = BMP5_SET_BITSLICE(osr_odr[1], BMP5_DEEP_DISABLE,
                                 BMP5_DEEP_DISABLED);
  osr_odr[1] = BMP5_SET_BITS_POS_0(osr_odr[1], BMP5_POWERMODE,
                                   BMP5_POWERMODE_STANDBY);
  osr_odr[1] = BMP5_SET_BITSLICE(osr_odr[1], BMP5_ODR, (uint8_t)odr);

  rc = bmp581_write_bytes(dev, BMP5_REG_OSR_CONFIG, osr_odr, 2);
  if (rc != BMP5_OK)
    return rc;

  /* t_standby: time for the standby transition just requested above to
   * actually complete, per the data sheet -- same delay
   * bmp5_set_power_mode() applies after its own standby-mode write. */
  dev->delay_us(BMP5_DELAY_US_STANDBY, dev->intf_ptr);

  rc = bmp5_get_regs(BMP5_REG_INT_STATUS, &discard_status, 1, dev);
  if (rc != BMP5_OK)
    return rc;

  int_regs[0] = BMP5_SET_BITS_POS_0(int_regs[0], BMP5_INT_MODE,
                                    interrupt_config->mode);
  int_regs[0] = BMP5_SET_BITSLICE(int_regs[0], BMP5_INT_POL,
                                  interrupt_config->polarity);
  int_regs[0] = BMP5_SET_BITSLICE(int_regs[0], BMP5_INT_OD,
                                  interrupt_config->drive);
  int_regs[0] = BMP5_SET_BITSLICE(int_regs[0], BMP5_INT_EN, BMP5_INTR_ENABLE);
  /* pad_int_drv (bit 4): not exposed by any struct this driver sets, in the
   * generic path or here -- bmp5_configure_interrupt() only ever preserves
   * it via its own read-before-write. Its power-on reset value is 1; keep
   * that explicitly since this path has no prior read to preserve it from. */
  int_regs[0] |= (uint8_t)(1U << 4);
  int_regs[1] = BMP5_SET_BITS_POS_0(int_regs[1], BMP5_INT_DRDY_EN,
                                    BMP5_ENABLE);

  return bmp581_write_bytes(dev, BMP5_REG_INT_CONFIG, int_regs, 2);
}

/* Public API contract documented in bmp581.h. */
int bmp581_config_forced_fast_device(const TagPressureDevice *device,
                                     bmp581_odr_t odr,
                                     const bmp581_interrupt_config_t *interrupt_config)
{
  struct bmp5_dev dev;
  int8_t rc;
  uint8_t raw_chip_id = 0U;
  uint8_t status_last = 0U;
  bool raw_read_ok = false;

  bmp581_begin_powered_session(device);

  rc = bmp581_init_device(device, &dev, &raw_chip_id, &raw_read_ok,
                          &status_last);
  if (rc == BMP5_OK)
    rc = bmp581_apply_sampling_config_fast(&dev, odr, interrupt_config);
  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: fast forced config raw_ok=%u raw_id=0x%x"
                     " status=0x%x rc=%d chip=0x%x intf=%d\r\n",
                     raw_read_ok ? 1U : 0U, raw_chip_id, status_last, rc,
                     dev.chip_id, dev.intf_rslt);
  }

  bmp581_end_powered_session(device);
  if (rc != BMP5_OK)
    bmp581_power_off(device);

  return rc;
}

int bmp581_trigger_forced_device(const TagPressureDevice *device)
{
  struct bmp5_dev dev;
  int8_t rc;

  bmp581_begin_powered_session(device);
  bmp581_prepare_dev(device, &dev);
  rc = bmp5_set_power_mode(BMP5_POWERMODE_FORCED, &dev);
  bmp581_end_powered_session(device);

  if (rc != BMP5_OK) {
    debug_log_printf("BMP581: forced trigger rc=%d intf=%d\r\n",
                     rc, dev.intf_rslt);
  }

  return rc;
}

int bmp581_clear_interrupt_status_device(const TagPressureDevice *device,
                                         uint8_t *int_status)
{
  struct bmp5_dev dev;
  int8_t rc;

  if (int_status == NULL)
    return BMP5_E_NULL_PTR;

  bmp581_begin_powered_session(device);
  bmp581_prepare_dev(device, &dev);
  rc = bmp5_get_interrupt_status(int_status, &dev);
  bmp581_end_powered_session(device);

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

int bmp581_read_pressure_temp_powered_device(const TagPressureDevice *device,
                                             float *pressure_hpa,
                                             int16_t *temperature_centi_c)
{
  struct bmp5_dev dev;
  struct bmp5_sensor_data sensor_data;
  int8_t rc;

  if (pressure_hpa == NULL || temperature_centi_c == NULL)
    return BMP5_E_NULL_PTR;

  bmp581_begin_powered_session(device);
  bmp581_prepare_dev(device, &dev);
  rc = bmp5_get_sensor_data(&sensor_data, &bmp581_active_config, &dev);
  bmp581_end_powered_session(device);

  if (rc == BMP5_OK) {
    *pressure_hpa = sensor_data.pressure / BMP581_PRESSURE_PA_PER_HPA;
    *temperature_centi_c = bmp581_centi_c(sensor_data.temperature);
  }

  return rc;
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

int bmp581_sample_forced_blocking_device(const TagPressureDevice *device,
                                         uint32_t timeout_us,
                                         float *pressure_hpa,
                                         int16_t *temperature_centi_c)
{
  uint32_t remaining_us = timeout_us;
  uint32_t settle_us = BMP581_CONVERSION_SETTLE_MS * 1000U;
  int rc = bmp581_trigger_forced_device(device);

  if (rc != BMP5_OK)
    return rc;

  if (settle_us > remaining_us)
    settle_us = remaining_us;
  stopMilliseconds(bmp581_stop_ms(settle_us));
  remaining_us -= settle_us;

  do {
    uint8_t int_status = 0U;

    rc = bmp581_clear_interrupt_status_device(device, &int_status);
    if (rc != BMP5_OK)
      return rc;
    if ((int_status & BMP5_INT_ASSERTED_DRDY) != 0U) {
      return bmp581_read_pressure_temp_powered_device(device, pressure_hpa,
                                                      temperature_centi_c);
    }

    if (remaining_us == 0U)
      break;

    if (remaining_us > BMP581_INIT_RETRY_DELAY_US) {
      stopMilliseconds(bmp581_stop_ms(BMP581_INIT_RETRY_DELAY_US));
      remaining_us -= BMP581_INIT_RETRY_DELAY_US;
    } else {
      stopMilliseconds(bmp581_stop_ms(remaining_us));
      remaining_us = 0U;
    }
  } while (true);

  return BMP581_E_DATA_READY_TIMEOUT;
}
