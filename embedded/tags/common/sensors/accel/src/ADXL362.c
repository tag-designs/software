/**
 * @file ADXL362.c
 * @brief Descriptor-backed ADXL362 accelerometer register driver.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "ADXL362.h"

#include <stddef.h>
#include <stdint.h>

static uint8_t selected_range = 2;

/** @name ADXL362 bus lifecycle
 * Helpers bracket ADXL362 register access with the descriptor's bus power and
 * transaction session.
 * @{
 */
/**
 * @brief Power and begin the bus session for an ADXL362 device.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_DeviceBegin(const TagAdxl362Device *device)
{
  tagBusPowerOn(&device->bus);
  tagBusBegin(&device->bus);
}

/**
 * @brief End the bus session and power down an ADXL362 device.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_DeviceEnd(const TagAdxl362Device *device)
{
  tagBusEnd(&device->bus);
  tagBusPowerOff(&device->bus);
}
/** @} */

/** @name ADXL362 transfer helpers
 * Low-level SPI helpers keep chip select asserted across command and data
 * phases required by the ADXL362 register protocol.
 * @{
 */
/**
 * @brief Write a command/payload buffer to the ADXL362.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] buffer Bytes to transmit.
 * @param[in] length Number of bytes to transmit.
 */
static void adxl362Write(const TagAdxl362Device *device,
                         const uint8_t *buffer,
                         size_t length)
{
  const TagSpiDevice *spi = tagAdxl362SpiDevice(device);

  tagSpiSelect(spi);
  tagSpiWrite(spi, buffer, length);
  tagSpiDeselect(spi);
}

/**
 * @brief Write a command then read response bytes under one chip-select.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] write_buffer Command bytes to transmit.
 * @param[in] write_length Number of command bytes.
 * @param[out] read_buffer Destination buffer.
 * @param[in] read_length Number of bytes to read.
 */
static void adxl362WriteThenRead(const TagAdxl362Device *device,
                                 const uint8_t *write_buffer,
                                 size_t write_length,
                                 uint8_t *read_buffer,
                                 size_t read_length)
{
  const TagSpiDevice *spi = tagAdxl362SpiDevice(device);

  tagSpiSelect(spi);
  tagSpiWrite(spi, write_buffer, write_length);
  tagSpiRead(spi, read_buffer, read_length);
  tagSpiDeselect(spi);
}
/** @} */

/** @name ADXL362 register driver API
 * Descriptor-backed ADXL362 operations used by tag/family accelerometer code.
 * @{
 */
/**
 * @brief Verify ADXL362 identity and initialize driver state.
 *
 * @param[in] device ADXL362 device descriptor.
 * @return 0 on success or -1 on identity mismatch.
 */
char ADXL362_InitDevice(const TagAdxl362Device *device)
{
  unsigned char regValue = 0;

  ADXL362_GetRegisterValueDevice(device, &regValue, ADXL362_REG_PARTID, 1);
  if (regValue != ADXL362_PART_ID) {
    return -1;
  }

  selected_range = 2;
  return 0;
}

/**
 * @brief Write one or two bytes to an ADXL362 register.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] registerValue Value to write, least-significant byte first.
 * @param[in] registerAddress Register address.
 * @param[in] bytesNumber Number of register bytes to write.
 */
void ADXL362_SetRegisterValueDevice(const TagAdxl362Device *device,
                                    unsigned short registerValue,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber)
{
  uint8_t buffer[4];

  if ((bytesNumber == 0) || (bytesNumber > 2)) {
    return;
  }

  buffer[0] = ADXL362_WRITE_REG;
  buffer[1] = registerAddress;
  buffer[2] = (uint8_t)(registerValue & 0x00ff);
  buffer[3] = (uint8_t)(registerValue >> 8);

  adxl362Write(device, buffer, bytesNumber + 2);
}

/**
 * @brief Read one or more consecutive ADXL362 registers.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] pReadData Destination buffer.
 * @param[in] registerAddress First register address.
 * @param[in] bytesNumber Number of bytes to read.
 */
void ADXL362_GetRegisterValueDevice(const TagAdxl362Device *device,
                                    unsigned char *pReadData,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber)
{
  uint8_t buffer[2];

  if (bytesNumber == 0) {
    return;
  }

  buffer[0] = ADXL362_READ_REG;
  buffer[1] = registerAddress;

  adxl362WriteThenRead(device, buffer, sizeof(buffer), pReadData, bytesNumber);
}

/**
 * @brief Read bytes from the ADXL362 FIFO.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] pBuffer Destination buffer.
 * @param[in] bytesNumber Number of FIFO bytes to read.
 */
void ADXL362_GetFifoValueDevice(const TagAdxl362Device *device,
                                unsigned char *pBuffer,
                                unsigned short bytesNumber)
{
  uint8_t command = ADXL362_READ_FIFO;

  if (bytesNumber == 0) {
    return;
  }

  adxl362WriteThenRead(device, &command, sizeof(command), pBuffer, bytesNumber);
}

/**
 * @brief Reset the ADXL362 through its software reset register.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_SoftwareResetDevice(const TagAdxl362Device *device)
{
  ADXL362_SetRegisterValueDevice(device, ADXL362_RESET_KEY,
                                 ADXL362_REG_SOFT_RESET, 1);
}

/**
 * @brief Reset and power down an ADXL362 device.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_DeinitDevice(const TagAdxl362Device *device)
{
  ADXL362_DeviceBegin(device);
  ADXL362_SoftwareResetDevice(device);
  ADXL362_DeviceEnd(device);
}

/**
 * @brief Place the ADXL362 into standby or measurement mode.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] pwrMode Nonzero to measure, zero for standby.
 */
void ADXL362_SetPowerModeDevice(const TagAdxl362Device *device,
                                unsigned char pwrMode)
{
  unsigned char power_ctl = 0;

  ADXL362_GetRegisterValueDevice(device, &power_ctl, ADXL362_REG_POWER_CTL, 1);
  power_ctl &= (unsigned char)~ADXL362_POWER_CTL_MEASURE(0x3);
  if (pwrMode) {
    power_ctl |= ADXL362_POWER_CTL_MEASURE(ADXL362_MEASURE_ON);
  }
  ADXL362_SetRegisterValueDevice(device, power_ctl, ADXL362_REG_POWER_CTL, 1);
}

/**
 * @brief Select the ADXL362 measurement range.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] gRange ADXL362 range selector.
 */
void ADXL362_SetRangeDevice(const TagAdxl362Device *device,
                            unsigned char gRange)
{
  unsigned char filter_ctl = 0;

  ADXL362_GetRegisterValueDevice(device, &filter_ctl, ADXL362_REG_FILTER_CTL, 1);
  filter_ctl &= (unsigned char)~ADXL362_FILTER_CTL_RANGE(0x3);
  filter_ctl |= ADXL362_FILTER_CTL_RANGE(gRange);
  ADXL362_SetRegisterValueDevice(device, filter_ctl, ADXL362_REG_FILTER_CTL, 1);
  selected_range = (uint8_t)((1U << gRange) * 2U);
}

/**
 * @brief Select the ADXL362 output data rate.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] outRate ADXL362 ODR selector.
 */
void ADXL362_SetOutputRateDevice(const TagAdxl362Device *device,
                                 unsigned char outRate)
{
  unsigned char filter_ctl = 0;

  ADXL362_GetRegisterValueDevice(device, &filter_ctl, ADXL362_REG_FILTER_CTL, 1);
  filter_ctl &= (unsigned char)~ADXL362_FILTER_CTL_ODR(0x7);
  filter_ctl |= ADXL362_FILTER_CTL_ODR(outRate);
  ADXL362_SetRegisterValueDevice(device, filter_ctl, ADXL362_REG_FILTER_CTL, 1);
}

/**
 * @brief Read raw X/Y/Z acceleration.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] x Raw X sample.
 * @param[out] y Raw Y sample.
 * @param[out] z Raw Z sample.
 */
void ADXL362_GetXyzDevice(const TagAdxl362Device *device, short *x, short *y,
                          short *z)
{
  unsigned char xyzValues[6] = {0, 0, 0, 0, 0, 0};

  ADXL362_GetRegisterValueDevice(device, xyzValues, ADXL362_REG_XDATA_L, 6);
  *x = (short)(((unsigned short)xyzValues[1] << 8) | xyzValues[0]);
  *y = (short)(((unsigned short)xyzValues[3] << 8) | xyzValues[2]);
  *z = (short)(((unsigned short)xyzValues[5] << 8) | xyzValues[4]);
}

#if defined(INCLUDE_FLOAT) && INCLUDE_FLOAT
/**
 * @brief Read acceleration and convert to g.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] x X acceleration in g.
 * @param[out] y Y acceleration in g.
 * @param[out] z Z acceleration in g.
 */
void ADXL362_GetGxyzDevice(const TagAdxl362Device *device, float *x, float *y,
                           float *z)
{
  short raw_x = 0;
  short raw_y = 0;
  short raw_z = 0;
  float lsb_per_g = 1000.0f / ((float)selected_range / 2.0f);

  ADXL362_GetXyzDevice(device, &raw_x, &raw_y, &raw_z);
  *x = (float)raw_x / lsb_per_g;
  *y = (float)raw_y / lsb_per_g;
  *z = (float)raw_z / lsb_per_g;
}

/**
 * @brief Read and convert the ADXL362 temperature channel.
 *
 * @param[in] device ADXL362 device descriptor.
 * @return Temperature in degrees Celsius.
 */
float ADXL362_ReadTemperatureDevice(const TagAdxl362Device *device)
{
  unsigned char rawTempData[2] = {0, 0};
  short signedTemp = 0;

  ADXL362_GetRegisterValueDevice(device, rawTempData, ADXL362_REG_TEMP_L, 2);
  signedTemp = (short)(((unsigned short)rawTempData[1] << 8) | rawTempData[0]);

  return (float)signedTemp * 0.065f;
}
#endif

/**
 * @brief Configure ADXL362 FIFO mode and watermark.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] mode FIFO mode selector.
 * @param[in] waterMarkLvl FIFO watermark level.
 * @param[in] enTempRead Nonzero to include temperature samples.
 */
void ADXL362_FifoSetupDevice(const TagAdxl362Device *device,
                             unsigned char mode,
                             unsigned short waterMarkLvl,
                             unsigned char enTempRead)
{
  unsigned char fifo_ctl = 0;

  fifo_ctl = ADXL362_FIFO_CTL_FIFO_MODE(mode) |
             (enTempRead ? ADXL362_FIFO_CTL_FIFO_TEMP : 0) |
             (((waterMarkLvl >> 8) & 0x01) ? ADXL362_FIFO_CTL_AH : 0);

  ADXL362_SetRegisterValueDevice(device, fifo_ctl, ADXL362_REG_FIFO_CTL, 1);
  ADXL362_SetRegisterValueDevice(device, waterMarkLvl,
                                 ADXL362_REG_FIFO_SAMPLES, 2);
}

/**
 * @brief Configure ADXL362 activity detection.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] refOrAbs Nonzero for referenced mode, zero for absolute mode.
 * @param[in] threshold Activity threshold.
 * @param[in] time Activity time count.
 */
void ADXL362_SetupActivityDetectionDevice(const TagAdxl362Device *device,
                                          unsigned char refOrAbs,
                                          unsigned short threshold,
                                          unsigned char time)
{
  unsigned char act_inact_ctl = 0;

  ADXL362_SetRegisterValueDevice(device, threshold & 0x07ff,
                                 ADXL362_REG_THRESH_ACT_L, 2);
  ADXL362_SetRegisterValueDevice(device, time, ADXL362_REG_TIME_ACT, 1);

  ADXL362_GetRegisterValueDevice(device, &act_inact_ctl,
                                 ADXL362_REG_ACT_INACT_CTL, 1);
  act_inact_ctl &= (unsigned char)~ADXL362_ACT_INACT_CTL_ACT_REF;
  act_inact_ctl |= ADXL362_ACT_INACT_CTL_ACT_EN;
  if (refOrAbs) {
    act_inact_ctl |= ADXL362_ACT_INACT_CTL_ACT_REF;
  }
  ADXL362_SetRegisterValueDevice(device, act_inact_ctl,
                                 ADXL362_REG_ACT_INACT_CTL, 1);
}

/**
 * @brief Configure ADXL362 inactivity detection.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] refOrAbs Nonzero for referenced mode, zero for absolute mode.
 * @param[in] threshold Inactivity threshold.
 * @param[in] time Inactivity time count.
 */
void ADXL362_SetupInactivityDetectionDevice(const TagAdxl362Device *device,
                                            unsigned char refOrAbs,
                                            unsigned short threshold,
                                            unsigned short time)
{
  unsigned char act_inact_ctl = 0;

  ADXL362_SetRegisterValueDevice(device, threshold & 0x07ff,
                                 ADXL362_REG_THRESH_INACT_L, 2);
  ADXL362_SetRegisterValueDevice(device, time, ADXL362_REG_TIME_INACT_L, 2);

  ADXL362_GetRegisterValueDevice(device, &act_inact_ctl,
                                 ADXL362_REG_ACT_INACT_CTL, 1);
  act_inact_ctl &= (unsigned char)~ADXL362_ACT_INACT_CTL_INACT_REF;
  act_inact_ctl |= ADXL362_ACT_INACT_CTL_INACT_EN;
  if (refOrAbs) {
    act_inact_ctl |= ADXL362_ACT_INACT_CTL_INACT_REF;
  }
  ADXL362_SetRegisterValueDevice(device, act_inact_ctl,
                                 ADXL362_REG_ACT_INACT_CTL, 1);
}
/** @} */
