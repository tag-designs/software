/***************************************************************************//**
 *   @file   ADXL362.c
 *   @brief  Implementation of ADXL362 Driver.
 *   @author DNechita(Dan.Nechita@analog.com)
 *   @date   2026-05-23
********************************************************************************
 * Copyright 2012(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
********************************************************************************
 *   SVN Revision: $WCREV$
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include "adxl362-original.h"
#include "ch.h"
#include "hal.h"
#include "board.h"
#include "custom.h"
#include "power.h"

/******************************************************************************/
/************************* Variables Declarations *****************************/
/******************************************************************************/
char selectedRange = 0;

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

static const TagSpiDevice adxl362_spi = {
  TAG_SPI1_DEVICE_DEFAULTS,
  .cs = LINE_ACCEL_CS,
  .sck = LINE_ACCEL_SCK,
  .miso = LINE_ACCEL_MISO,
  .mosi = LINE_ACCEL_MOSI,
  .pwr = TAG_NO_LINE,
  .dummy = 0xff,
  .sleep_policy = TAG_SPI_SLEEP_SAFE_IDLE,
};

static const TagAdxl362Device adxl362_default_device = {
  .spi = &adxl362_spi,
  .sleep_ms = 0,
};

/**
 * @brief Return the preserved driver's built-in ADXL362 descriptor.
 *
 * @return Default ADXL362 descriptor.
 */
const TagAdxl362Device *ADXL362_DefaultDevice(void)
{
  return &adxl362_default_device;
}

/**
 * @brief Return the tag-selected descriptor for the preserved ADXL362 API.
 *
 * @return Tag-specific descriptor, or the weak default when not overridden.
 */
const TagAdxl362Device *__attribute__((weak)) tagAdxl362Device(void)
{
  return ADXL362_DefaultDevice();
}

/**
 * @brief Power and acquire the ADXL362 SPI bus for a grouped transaction.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_DeviceBegin(const TagAdxl362Device *device)
{
  tagSpiDevicePowerOn(device->spi);
  tagSpiBusBegin(device->spi);
}

/**
 * @brief Release and power down the ADXL362 SPI bus.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_DeviceEnd(const TagAdxl362Device *device)
{
  tagSpiBusEnd(device->spi);
  tagSpiDevicePowerOff(device->spi);
}

/***************************************************************************//**
 * @brief Initializes communication with the device and checks if the part is
 *        present by reading the device id.
 *
 * @return  0 - the initialization was successful and the device is present;
 *         -1 - an error occurred.
*******************************************************************************/
char ADXL362_Init(void)
{
  return ADXL362_InitDevice(&adxl362_default_device);
}

/**
 * @brief Initialize an ADXL362 descriptor and check the part ID.
 *
 * @param[in] device ADXL362 descriptor.
 * @return 0 on success, -1 when the expected part ID is absent.
 */
char ADXL362_InitDevice(const TagAdxl362Device *device)
{
    unsigned char regValue = 0;
    char          status   = -1;

    //    status = SPI_Init(0, 4000000, 0, 1);
    ADXL362_GetRegisterValueDevice(device, &regValue, ADXL362_REG_PARTID, 1);
    if((regValue != ADXL362_PART_ID))
    {
        status = -1;
    }
    selectedRange = 2; // Measurement Range: +/- 2g (reset default).

    return status;
}

/***************************************************************************//**
 * @brief Writes data into a register.
 *
 * @param registerValue   - Data value to write.
 * @param registerAddress - Address of the register.
 * @param bytesNumber     - Number of bytes. Accepted values: 0 - 1.
 *
 * @return None.
*******************************************************************************/
void ADXL362_SetRegisterValue(unsigned short registerValue,
                              unsigned char  registerAddress,
                              unsigned char  bytesNumber)
{
  ADXL362_SetRegisterValueDevice(&adxl362_default_device, registerValue,
                                 registerAddress, bytesNumber);
}

/**
 * @brief Write one or two value bytes to an ADXL362 register.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] registerValue Value to write.
 * @param[in] registerAddress Register address.
 * @param[in] bytesNumber Number of value bytes to write.
 */
void ADXL362_SetRegisterValueDevice(const TagAdxl362Device *device,
                                    unsigned short registerValue,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber)
{
  unsigned char buffer[4];

  buffer[0] = ADXL362_WRITE_REG;
  buffer[1] = registerAddress;
  buffer[2] = (registerValue & 0x00FF);
  buffer[3] = (registerValue >> 8);
  
  tagSpiBusWrite(device->spi, buffer, bytesNumber + 2);
}

/***************************************************************************//**
 * @brief Performs a burst read of a specified number of registers.
 *
 * @param pReadData       - The read values are stored in this buffer.
 * @param registerAddress - The start address of the burst read.
 * @param bytesNumber     - Number of bytes to read.
 *
 * @return None.
*******************************************************************************/
void ADXL362_GetRegisterValue(unsigned char* pReadData,
                              unsigned char  registerAddress,
                              unsigned char  bytesNumber)
{
    ADXL362_GetRegisterValueDevice(&adxl362_default_device, pReadData,
                                   registerAddress, bytesNumber);
}

/**
 * @brief Burst-read registers from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] pReadData Destination buffer.
 * @param[in] registerAddress First register address.
 * @param[in] bytesNumber Number of bytes to read.
 */
void ADXL362_GetRegisterValueDevice(const TagAdxl362Device *device,
                                    unsigned char *pReadData,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber)
{
    unsigned char buffer[2];
    
    buffer[0] = ADXL362_READ_REG;
    buffer[1] = registerAddress;

    tagSpiSelect(device->spi);
    tagSpiWrite(device->spi, buffer, 2);
    tagSpiRead(device->spi, pReadData, bytesNumber);
    tagSpiDeselect(device->spi);
}

/***************************************************************************//**
 * @brief Reads multiple bytes from the device's FIFO buffer.
 *
 * @param pBuffer     - Stores the read bytes.
 * @param bytesNumber - Number of bytes to read.
 *
 * @return None.
*******************************************************************************/
void ADXL362_GetFifoValue(unsigned char* pBuffer, unsigned short bytesNumber)
{
  ADXL362_GetFifoValueDevice(&adxl362_default_device, pBuffer, bytesNumber);
}

/**
 * @brief Read bytes from an ADXL362 descriptor FIFO.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] pBuffer Destination buffer.
 * @param[in] bytesNumber Number of FIFO bytes to read.
 */
void ADXL362_GetFifoValueDevice(const TagAdxl362Device *device,
                                unsigned char *pBuffer,
                                unsigned short bytesNumber)
{
  unsigned char  buffer[1];

  buffer[0] = ADXL362_WRITE_FIFO;
  tagSpiSelect(device->spi);
  tagSpiWrite(device->spi, buffer, 1);
  tagSpiRead(device->spi, pBuffer, bytesNumber);
  tagSpiDeselect(device->spi);
}

/***************************************************************************//**
 * @brief Resets the device via SPI communication bus.
 *
 * @return None.
*******************************************************************************/
void ADXL362_SoftwareReset(void)
{
    ADXL362_SoftwareResetDevice(&adxl362_default_device);
}

/**
 * @brief Reset an ADXL362 descriptor through the software-reset register.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_SoftwareResetDevice(const TagAdxl362Device *device)
{
    ADXL362_SetRegisterValueDevice(device, ADXL362_RESET_KEY,
                                   ADXL362_REG_SOFT_RESET, 1);
}

/**
 * @brief Reset the default ADXL362 and leave it deinitialized.
 */
void ADXL362_Deinit(void)
{
    ADXL362_DeinitDevice(&adxl362_default_device);
}

/**
 * @brief Reset an ADXL362 descriptor and release its bus.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_DeinitDevice(const TagAdxl362Device *device)
{
    ADXL362_DeviceBegin(device);
    ADXL362_SoftwareResetDevice(device);
    ADXL362_DeviceEnd(device);
}

/***************************************************************************//**
 * @brief Places the device into standby/measure mode.
 *
 * @param pwrMode - Power mode.
 *                  Example: 0 - standby mode.
 *                  1 - measure mode.
 *
 * @return None.
*******************************************************************************/
void ADXL362_SetPowerMode(unsigned char pwrMode)
{
    ADXL362_SetPowerModeDevice(&adxl362_default_device, pwrMode);
}

/**
 * @brief Place an ADXL362 descriptor in standby or measurement mode.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] pwrMode 0 for standby, nonzero for measurement.
 */
void ADXL362_SetPowerModeDevice(const TagAdxl362Device *device,
                                unsigned char pwrMode)
{
    unsigned char oldPowerCtl = 0;
    unsigned char newPowerCtl = 0;

    ADXL362_GetRegisterValueDevice(device, &oldPowerCtl,
                                   ADXL362_REG_POWER_CTL, 1);
    newPowerCtl = oldPowerCtl & ~ADXL362_POWER_CTL_MEASURE(0x3);
    newPowerCtl = newPowerCtl |
                  (pwrMode * ADXL362_POWER_CTL_MEASURE(ADXL362_MEASURE_ON));
    ADXL362_SetRegisterValueDevice(device, newPowerCtl,
                                   ADXL362_REG_POWER_CTL, 1);
}

/***************************************************************************//**
 * @brief Selects the measurement range.
 *
 * @param gRange - Range option.
 *                  Example: ADXL362_RANGE_2G  -  +-2 g
 *                           ADXL362_RANGE_4G  -  +-4 g
 *                           ADXL362_RANGE_8G  -  +-8 g
 *                           
 * @return None.
*******************************************************************************/
void ADXL362_SetRange(unsigned char gRange)
{
    ADXL362_SetRangeDevice(&adxl362_default_device, gRange);
}

/**
 * @brief Select the measurement range for an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] gRange ADXL362_RANGE_* selector.
 */
void ADXL362_SetRangeDevice(const TagAdxl362Device *device,
                            unsigned char gRange)
{
    unsigned char oldFilterCtl = 0;
    unsigned char newFilterCtl = 0;

    ADXL362_GetRegisterValueDevice(device, &oldFilterCtl,
                                   ADXL362_REG_FILTER_CTL, 1);
    newFilterCtl = oldFilterCtl & ~ADXL362_FILTER_CTL_RANGE(0x3);
    newFilterCtl = newFilterCtl | ADXL362_FILTER_CTL_RANGE(gRange);
    ADXL362_SetRegisterValueDevice(device, newFilterCtl,
                                   ADXL362_REG_FILTER_CTL, 1);
    selectedRange = (1 << gRange) * 2;
}

/***************************************************************************//**
 * @brief Selects the Output Data Rate of the device.
 *
 * @param outRate - Output Data Rate option.
 *                  Example: ADXL362_ODR_12_5_HZ  -  12.5Hz
 *                           ADXL362_ODR_25_HZ    -  25Hz
 *                           ADXL362_ODR_50_HZ    -  50Hz
 *                           ADXL362_ODR_100_HZ   -  100Hz
 *                           ADXL362_ODR_200_HZ   -  200Hz
 *                           ADXL362_ODR_400_HZ   -  400Hz
 *
 * @return None.
*******************************************************************************/
void ADXL362_SetOutputRate(unsigned char outRate)
{
    ADXL362_SetOutputRateDevice(&adxl362_default_device, outRate);
}

/**
 * @brief Select the output data rate for an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] outRate ADXL362_ODR_* selector.
 */
void ADXL362_SetOutputRateDevice(const TagAdxl362Device *device,
                                 unsigned char outRate)
{
    unsigned char oldFilterCtl = 0;
    unsigned char newFilterCtl = 0;

    ADXL362_GetRegisterValueDevice(device, &oldFilterCtl,
                                   ADXL362_REG_FILTER_CTL, 1);
    newFilterCtl = oldFilterCtl & ~ADXL362_FILTER_CTL_ODR(0x7);
    newFilterCtl = newFilterCtl | ADXL362_FILTER_CTL_ODR(outRate);
    ADXL362_SetRegisterValueDevice(device, newFilterCtl,
                                   ADXL362_REG_FILTER_CTL, 1);
}

/***************************************************************************//**
 * @brief Reads the 3-axis raw data from the accelerometer.
 *
 * @param x - Stores the X-axis data(as two's complement).
 * @param y - Stores the Y-axis data(as two's complement).
 * @param z - Stores the Z-axis data(as two's complement).
 *
 * @return None.
*******************************************************************************/
void ADXL362_GetXyz(short* x, short* y, short* z)
{
    ADXL362_GetXyzDevice(&adxl362_default_device, x, y, z);
}

/**
 * @brief Read raw X/Y/Z samples from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] x X-axis raw sample.
 * @param[out] y Y-axis raw sample.
 * @param[out] z Z-axis raw sample.
 */
void ADXL362_GetXyzDevice(const TagAdxl362Device *device, short *x, short *y,
                          short *z)
{
    unsigned char xyzValues[6] = {0, 0, 0, 0, 0, 0};

    ADXL362_GetRegisterValueDevice(device, xyzValues, ADXL362_REG_XDATA_L, 6);
    *x = ((short)xyzValues[1] << 8) + xyzValues[0];
    *y = ((short)xyzValues[3] << 8) + xyzValues[2];
    *z = ((short)xyzValues[5] << 8) + xyzValues[4];
}

/***************************************************************************//**
 * @brief Reads the 3-axis raw data from the accelerometer and converts it to g.
 *
 * @param x - Stores the X-axis data.
 * @param y - Stores the Y-axis data.
 * @param z - Stores the Z-axis data.
 *
 * @return None.
*******************************************************************************/

#if defined(INCLUDE_FLOAT) && INCLUDE_FLOAT
void ADXL362_GetGxyz(float* x, float* y, float* z)
{
    ADXL362_GetGxyzDevice(&adxl362_default_device, x, y, z);
}

/**
 * @brief Read X/Y/Z samples from an ADXL362 descriptor and convert them to g.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] x X-axis acceleration in g.
 * @param[out] y Y-axis acceleration in g.
 * @param[out] z Z-axis acceleration in g.
 */
void ADXL362_GetGxyzDevice(const TagAdxl362Device *device, float *x, float *y,
                           float *z)
{
    unsigned char xyzValues[6] = {0, 0, 0, 0, 0, 0};

    ADXL362_GetRegisterValueDevice(device, xyzValues, ADXL362_REG_XDATA_L, 6);
    *x = ((short)xyzValues[1] << 8) + xyzValues[0];
    *x /= (1000 / (selectedRange / 2));
    *y = ((short)xyzValues[3] << 8) + xyzValues[2];
    *y /= (1000 / (selectedRange / 2));
    *z = ((short)xyzValues[5] << 8) + xyzValues[4];
    *z /= (1000 / (selectedRange / 2));
    }

/***************************************************************************//**
 * @brief Reads the temperature of the device.
 *
 * @return tempCelsius - The value of the temperature(degrees Celsius).
*******************************************************************************/

float ADXL362_ReadTemperature(void)
{
    return ADXL362_ReadTemperatureDevice(&adxl362_default_device);
}

/**
 * @brief Read temperature from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @return Temperature in degrees Celsius.
 */
float ADXL362_ReadTemperatureDevice(const TagAdxl362Device *device)
{
    unsigned char rawTempData[2] = {0, 0};
    short         signedTemp     = 0;
    float         tempCelsius    = 0;

    ADXL362_GetRegisterValueDevice(device, rawTempData, ADXL362_REG_TEMP_L, 2);
    signedTemp = (short)(rawTempData[1] << 8) + rawTempData[0];
    tempCelsius = (float)signedTemp * 0.065f;
    
    return tempCelsius;
}
#endif
/***************************************************************************//**
 * @brief Configures the FIFO feature.
 *
 * @param mode         - Mode selection.
 *                       Example: ADXL362_FIFO_DISABLE      -  FIFO is disabled.
 *                                ADXL362_FIFO_OLDEST_SAVED -  Oldest saved mode.
 *                                ADXL362_FIFO_STREAM       -  Stream mode.
 *                                ADXL362_FIFO_TRIGGERED    -  Triggered mode.
 * @param waterMarkLvl - Specifies the number of samples to store in the FIFO.
 * @param enTempRead   - Store Temperature Data to FIFO.
 *                       Example: 1 - temperature data is stored in the FIFO
 *                                    together with x-, y- and x-axis data.
 *                                0 - temperature data is skipped.
 *
 * @return None.
*******************************************************************************/
void ADXL362_FifoSetup(unsigned char  mode,
                       unsigned short waterMarkLvl,
                       unsigned char  enTempRead)
{
    ADXL362_FifoSetupDevice(&adxl362_default_device, mode, waterMarkLvl,
                            enTempRead);
}

/**
 * @brief Configure FIFO mode for an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] mode ADXL362_FIFO_* mode selector.
 * @param[in] waterMarkLvl FIFO watermark sample count.
 * @param[in] enTempRead Nonzero to include temperature samples in FIFO.
 */
void ADXL362_FifoSetupDevice(const TagAdxl362Device *device,
                             unsigned char mode, unsigned short waterMarkLvl,
                             unsigned char enTempRead)
{
    unsigned char writeVal = 0;

    writeVal = ADXL362_FIFO_CTL_FIFO_MODE(mode) |
               (enTempRead * ADXL362_FIFO_CTL_FIFO_TEMP) |
               ADXL362_FIFO_CTL_AH;
    ADXL362_SetRegisterValueDevice(device, writeVal, ADXL362_REG_FIFO_CTL, 1);
    ADXL362_SetRegisterValueDevice(device, waterMarkLvl,
                                   ADXL362_REG_FIFO_SAMPLES, 2);
}

/***************************************************************************//**
 * @brief Configures activity detection.
 *
 * @param refOrAbs  - Referenced/Absolute Activity Select.
 *                    Example: 0 - absolute mode.
 *                             1 - referenced mode.
 * @param threshold - 11-bit unsigned value that the adxl362 samples are
 *                    compared to.
 * @param time      - 8-bit value written to the activity timer register. The 
 *                    amount of time (in seconds) is: time / ODR, where ODR - is 
 *                    the output data rate.
 *
 * @return None.
*******************************************************************************/
void ADXL362_SetupActivityDetection(unsigned char  refOrAbs,
                                    unsigned short threshold,
                                    unsigned char  time)
{
    ADXL362_SetupActivityDetectionDevice(&adxl362_default_device, refOrAbs,
                                         threshold, time);
}

/**
 * @brief Configure activity detection for an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] refOrAbs 0 for absolute mode, nonzero for referenced mode.
 * @param[in] threshold Activity threshold.
 * @param[in] time Activity timer in output-data-rate ticks.
 */
void ADXL362_SetupActivityDetectionDevice(const TagAdxl362Device *device,
                                          unsigned char refOrAbs,
                                          unsigned short threshold,
                                          unsigned char time)
{
    unsigned char oldActInactReg = 0;
    unsigned char newActInactReg = 0;

    /* Configure motion threshold and activity timer. */
    ADXL362_SetRegisterValueDevice(device, (threshold & 0x7FF),
                                   ADXL362_REG_THRESH_ACT_L, 2);
    ADXL362_SetRegisterValueDevice(device, time, ADXL362_REG_TIME_ACT, 1);
    /* Enable activity interrupt and select a referenced or absolute
       configuration. */
    ADXL362_GetRegisterValueDevice(device, &oldActInactReg,
                                   ADXL362_REG_ACT_INACT_CTL, 1);
    newActInactReg = oldActInactReg & ~ADXL362_ACT_INACT_CTL_ACT_REF;
    newActInactReg |= ADXL362_ACT_INACT_CTL_ACT_EN |
                     (refOrAbs * ADXL362_ACT_INACT_CTL_ACT_REF);
    ADXL362_SetRegisterValueDevice(device, newActInactReg,
                                   ADXL362_REG_ACT_INACT_CTL, 1);
}

/***************************************************************************//**
 * @brief Configures inactivity detection.
 *
 * @param refOrAbs  - Referenced/Absolute Inactivity Select.
 *                    Example: 0 - absolute mode.
 *                             1 - referenced mode.
 * @param threshold - 11-bit unsigned value that the adxl362 samples are
 *                    compared to.
 * @param time      - 16-bit value written to the inactivity timer register. The 
 *                    amount of time (in seconds) is: time / ODR, where ODR - is  
 *                    the output data rate.
 *
 * @return None.
*******************************************************************************/
void ADXL362_SetupInactivityDetection(unsigned char  refOrAbs,
                                      unsigned short threshold,
                                      unsigned short time)
{
    ADXL362_SetupInactivityDetectionDevice(&adxl362_default_device, refOrAbs,
                                           threshold, time);
}

/**
 * @brief Configure inactivity detection for an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] refOrAbs 0 for absolute mode, nonzero for referenced mode.
 * @param[in] threshold Inactivity threshold.
 * @param[in] time Inactivity timer in output-data-rate ticks.
 */
void ADXL362_SetupInactivityDetectionDevice(const TagAdxl362Device *device,
                                            unsigned char refOrAbs,
                                            unsigned short threshold,
                                            unsigned short time)
{
    unsigned char oldActInactReg = 0;
    unsigned char newActInactReg = 0;
    
    /* Configure motion threshold and inactivity timer. */
    ADXL362_SetRegisterValueDevice(device, (threshold & 0x7FF),
                                   ADXL362_REG_THRESH_INACT_L, 2);
    ADXL362_SetRegisterValueDevice(device, time, ADXL362_REG_TIME_INACT_L, 2);
    /* Enable inactivity interrupt and select a referenced or absolute
       configuration. */
    ADXL362_GetRegisterValueDevice(device, &oldActInactReg,
                                   ADXL362_REG_ACT_INACT_CTL, 1);
    newActInactReg = oldActInactReg & ~ADXL362_ACT_INACT_CTL_INACT_REF;
    newActInactReg |= ADXL362_ACT_INACT_CTL_INACT_EN |
                     (refOrAbs * ADXL362_ACT_INACT_CTL_INACT_REF);
    ADXL362_SetRegisterValueDevice(device, newActInactReg,
                                   ADXL362_REG_ACT_INACT_CTL, 1);
}
