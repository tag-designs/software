/***************************************************************************//**
 *   @file   ADXL362.h
 *   @brief  Header file of ADXL362 Driver.
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

#ifndef __ADXL362_ORIGINAL_H__
#define __ADXL362_ORIGINAL_H__

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
//#include "Communication.h"
#include "spi_bus.h"

#include <stdbool.h>

/******************************************************************************/
/********************************* ADXL362 ************************************/
/******************************************************************************/

//#define ADXL362_SLAVE_ID    1

/* ADXL362 communication commands */
#define ADXL362_WRITE_REG           0x0A
#define ADXL362_READ_REG            0x0B
#define ADXL362_WRITE_FIFO          0x0D

/* Registers */
#define ADXL362_REG_DEVID_AD            0x00
#define ADXL362_REG_DEVID_MST           0x01
#define ADXL362_REG_PARTID              0x02
#define ADXL362_REG_REVID               0x03
#define ADXL362_REG_XDATA               0x08
#define ADXL362_REG_YDATA               0x09
#define ADXL362_REG_ZDATA               0x0A
#define ADXL362_REG_STATUS              0x0B
#define ADXL362_REG_FIFO_L              0x0C
#define ADXL362_REG_FIFO_H              0x0D
#define ADXL362_REG_XDATA_L             0x0E
#define ADXL362_REG_XDATA_H             0x0F
#define ADXL362_REG_YDATA_L             0x10
#define ADXL362_REG_YDATA_H             0x11
#define ADXL362_REG_ZDATA_L             0x12
#define ADXL362_REG_ZDATA_H             0x13
#define ADXL362_REG_TEMP_L              0x14
#define ADXL362_REG_TEMP_H              0x15
#define ADXL362_REG_SOFT_RESET          0x1F
#define ADXL362_REG_THRESH_ACT_L        0x20
#define ADXL362_REG_THRESH_ACT_H        0x21
#define ADXL362_REG_TIME_ACT            0x22
#define ADXL362_REG_THRESH_INACT_L      0x23
#define ADXL362_REG_THRESH_INACT_H      0x24
#define ADXL362_REG_TIME_INACT_L        0x25
#define ADXL362_REG_TIME_INACT_H        0x26
#define ADXL362_REG_ACT_INACT_CTL       0x27
#define ADXL362_REG_FIFO_CTL            0x28
#define ADXL362_REG_FIFO_SAMPLES        0x29
#define ADXL362_REG_INTMAP1             0x2A
#define ADXL362_REG_INTMAP2             0x2B
#define ADXL362_REG_FILTER_CTL          0x2C
#define ADXL362_REG_POWER_CTL           0x2D
#define ADXL362_REG_SELF_TEST           0x2E

/* ADXL362_REG_STATUS definitions */
#define ADXL362_STATUS_ERR_USER_REGS        (1 << 7)
#define ADXL362_STATUS_AWAKE                (1 << 6)
#define ADXL362_STATUS_INACT                (1 << 5)
#define ADXL362_STATUS_ACT                  (1 << 4)
#define ADXL362_STATUS_FIFO_OVERRUN         (1 << 3)
#define ADXL362_STATUS_FIFO_WATERMARK       (1 << 2)
#define ADXL362_STATUS_FIFO_RDY             (1 << 1)
#define ADXL362_STATUS_DATA_RDY             (1 << 0)

/* ADXL362_REG_ACT_INACT_CTL definitions */
#define ADXL362_ACT_INACT_CTL_LINKLOOP(x)   (((x) & 0x3) << 4)
#define ADXL362_ACT_INACT_CTL_INACT_REF     (1 << 3)
#define ADXL362_ACT_INACT_CTL_INACT_EN      (1 << 2)
#define ADXL362_ACT_INACT_CTL_ACT_REF       (1 << 1)
#define ADXL362_ACT_INACT_CTL_ACT_EN        (1 << 0)

/* ADXL362_ACT_INACT_CTL_LINKLOOP(x) options */
#define ADXL362_MODE_DEFAULT        0
#define ADXL362_MODE_LINK           1
#define ADXL362_MODE_LOOP           3

/* ADXL362_REG_FIFO_CTL */
#define ADXL362_FIFO_CTL_AH                 (1 << 3)
#define ADXL362_FIFO_CTL_FIFO_TEMP          (1 << 2)
#define ADXL362_FIFO_CTL_FIFO_MODE(x)       (((x) & 0x3) << 0)

/* ADXL362_FIFO_CTL_FIFO_MODE(x) options */
#define ADXL362_FIFO_DISABLE              0
#define ADXL362_FIFO_OLDEST_SAVED         1
#define ADXL362_FIFO_STREAM               2
#define ADXL362_FIFO_TRIGGERED            3

/* ADXL362_REG_INTMAP1 */
#define ADXL362_INTMAP1_INT_LOW             (1 << 7)
#define ADXL362_INTMAP1_AWAKE               (1 << 6)
#define ADXL362_INTMAP1_INACT               (1 << 5)
#define ADXL362_INTMAP1_ACT                 (1 << 4)
#define ADXL362_INTMAP1_FIFO_OVERRUN        (1 << 3)
#define ADXL362_INTMAP1_FIFO_WATERMARK      (1 << 2)
#define ADXL362_INTMAP1_FIFO_READY          (1 << 1)
#define ADXL362_INTMAP1_DATA_READY          (1 << 0)

/* ADXL362_REG_INTMAP2 definitions */
#define ADXL362_INTMAP2_INT_LOW             (1 << 7)
#define ADXL362_INTMAP2_AWAKE               (1 << 6)
#define ADXL362_INTMAP2_INACT               (1 << 5)
#define ADXL362_INTMAP2_ACT                 (1 << 4)
#define ADXL362_INTMAP2_FIFO_OVERRUN        (1 << 3)
#define ADXL362_INTMAP2_FIFO_WATERMARK      (1 << 2)
#define ADXL362_INTMAP2_FIFO_READY          (1 << 1)
#define ADXL362_INTMAP2_DATA_READY          (1 << 0)

/* ADXL362_REG_FILTER_CTL definitions */
#define ADXL362_FILTER_CTL_RANGE(x)         (((x) & 0x3) << 6)
#define ADXL362_FILTER_CTL_RES              (1 << 5)
#define ADXL362_FILTER_CTL_HALF_BW          (1 << 4)
#define ADXL362_FILTER_CTL_EXT_SAMPLE       (1 << 3)
#define ADXL362_FILTER_CTL_ODR(x)           (((x) & 0x7) << 0)

/* ADXL362_FILTER_CTL_RANGE(x) options */
#define ADXL362_RANGE_2G                0 /* +/-2 g */
#define ADXL362_RANGE_4G                1 /* +/-4 g */
#define ADXL362_RANGE_8G                2 /* +/-8 g */

/* ADXL362_FILTER_CTL_ODR(x) options */
#define ADXL362_ODR_12_5_HZ             0 /* 12.5 Hz */
#define ADXL362_ODR_25_HZ               1 /* 25 Hz */
#define ADXL362_ODR_50_HZ               2 /* 50 Hz */
#define ADXL362_ODR_100_HZ              3 /* 100 Hz */
#define ADXL362_ODR_200_HZ              4 /* 200 Hz */
#define ADXL362_ODR_400_HZ              5 /* 400 Hz */

/* ADXL362_REG_POWER_CTL definitions */
#define ADXL362_POWER_CTL_RES               (1 << 7)
#define ADXL362_POWER_CTL_EXT_CLK           (1 << 6)
#define ADXL362_POWER_CTL_LOW_NOISE(x)      (((x) & 0x3) << 4)
#define ADXL362_POWER_CTL_WAKEUP            (1 << 3)
#define ADXL362_POWER_CTL_AUTOSLEEP         (1 << 2)
#define ADXL362_POWER_CTL_MEASURE(x)        (((x) & 0x3) << 0)

/* ADXL362_POWER_CTL_LOW_NOISE(x) options */
#define ADXL362_NOISE_MODE_NORMAL           0
#define ADXL362_NOISE_MODE_LOW              1
#define ADXL362_NOISE_MODE_ULTRALOW         2

/* ADXL362_POWER_CTL_MEASURE(x) options */
#define ADXL362_MEASURE_STANDBY         0
#define ADXL362_MEASURE_ON              2

/* ADXL362_REG_SELF_TEST */
#define ADXL362_SELF_TEST_ST            (1 << 0)

/* ADXL362 device information */
#define ADXL362_DEVICE_AD               0xAD
#define ADXL362_DEVICE_MST              0x1D
#define ADXL362_PART_ID                 0xF2

/* ADXL362 Reset settings */
#define ADXL362_RESET_KEY               0x52

/******************************************************************************/
/************************ Functions Declarations ******************************/
/******************************************************************************/

/** @name Preserved ADXL362 device binding
 * Legacy driver descriptor retained for reference builds.
 * @{
 */
/**
 * @brief Delay callback used by the preserved ADXL362 driver.
 *
 * @param[in] ms Delay in milliseconds.
 */
typedef void (*TagAdxl362Sleep)(int ms);

typedef struct {
  const TagSpiDevice *spi;
  TagAdxl362Sleep sleep_ms;
} TagAdxl362Device;

/**
 * @brief Return the built-in default ADXL362 descriptor.
 *
 * @return Default ADXL362 descriptor.
 */
const TagAdxl362Device *ADXL362_DefaultDevice(void);
/**
 * @brief Return the tag-selected ADXL362 descriptor.
 *
 * @return Tag-specific descriptor, or the weak default when not overridden.
 */
const TagAdxl362Device *tagAdxl362Device(void);

/**
 * @brief Power and acquire the SPI bus for one ADXL362 transaction group.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_DeviceBegin(const TagAdxl362Device *device);
/**
 * @brief Release and power down the SPI bus after an ADXL362 transaction group.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_DeviceEnd(const TagAdxl362Device *device);
/**
 * @brief Check whether an ADXL362 descriptor identifies the expected part.
 *
 * @param[in] device ADXL362 descriptor.
 * @return true when the part ID is present.
 */
bool adxl362Test(const TagAdxl362Device *device);
/** @} */

/** @name Preserved ADXL362 register and sample API
 * Original Analog Devices style API plus descriptor-backed variants.
 * @{
 */
/**
 * @brief Initialize the default ADXL362.
 *
 * @return 0 on success, -1 when the part is not detected.
 */
char ADXL362_Init(void);
/**
 * @brief Initialize an ADXL362 descriptor and verify its identity.
 *
 * @param[in] device ADXL362 descriptor.
 * @return 0 on success, -1 when the part is not detected.
 */
char ADXL362_InitDevice(const TagAdxl362Device *device);

/**
 * @brief Write one or two bytes to a default ADXL362 register.
 *
 * @param[in] registerValue Value to write.
 * @param[in] registerAddress Register address.
 * @param[in] bytesNumber Number of value bytes to write.
 */
void ADXL362_SetRegisterValue(unsigned short registerValue,
                              unsigned char  registerAddress,
                              unsigned char  bytesNumber);
/**
 * @brief Write one or two bytes to an ADXL362 descriptor register.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] registerValue Value to write.
 * @param[in] registerAddress Register address.
 * @param[in] bytesNumber Number of value bytes to write.
 */
void ADXL362_SetRegisterValueDevice(const TagAdxl362Device *device,
                                    unsigned short registerValue,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber);

/**
 * @brief Read registers from the default ADXL362.
 *
 * @param[out] pReadData Destination buffer.
 * @param[in] registerAddress First register address.
 * @param[in] bytesNumber Number of bytes to read.
 */
void ADXL362_GetRegisterValue(unsigned char *pReadData,
                              unsigned char  registerAddress,
                              unsigned char  bytesNumber);
/**
 * @brief Read registers from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] pReadData Destination buffer.
 * @param[in] registerAddress First register address.
 * @param[in] bytesNumber Number of bytes to read.
 */
void ADXL362_GetRegisterValueDevice(const TagAdxl362Device *device,
                                    unsigned char *pReadData,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber);

/**
 * @brief Read FIFO bytes from the default ADXL362.
 *
 * @param[out] pBuffer Destination buffer.
 * @param[in] bytesNumber Number of FIFO bytes to read.
 */
void ADXL362_GetFifoValue(unsigned char *pBuffer, unsigned short bytesNumber);
/**
 * @brief Read FIFO bytes from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] pBuffer Destination buffer.
 * @param[in] bytesNumber Number of FIFO bytes to read.
 */
void ADXL362_GetFifoValueDevice(const TagAdxl362Device *device,
                                unsigned char *pBuffer,
                                unsigned short bytesNumber);

/**
 * @brief Reset the default ADXL362 through its software-reset register.
 */
void ADXL362_SoftwareReset(void);
/**
 * @brief Reset an ADXL362 descriptor through its software-reset register.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_SoftwareResetDevice(const TagAdxl362Device *device);
/**
 * @brief Deinitialize the default ADXL362 into its reset state.
 */
void ADXL362_Deinit(void);
/**
 * @brief Deinitialize an ADXL362 descriptor into its reset state.
 *
 * @param[in] device ADXL362 descriptor.
 */
void ADXL362_DeinitDevice(const TagAdxl362Device *device);

/**
 * @brief Set standby or measure mode on the default ADXL362.
 *
 * @param[in] pwrMode 0 for standby, nonzero for measurement.
 */
void ADXL362_SetPowerMode(unsigned char pwrMode);
/**
 * @brief Set standby or measure mode on an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] pwrMode 0 for standby, nonzero for measurement.
 */
void ADXL362_SetPowerModeDevice(const TagAdxl362Device *device,
                                unsigned char pwrMode);

/**
 * @brief Select the measurement range on the default ADXL362.
 *
 * @param[in] gRange ADXL362_RANGE_* selector.
 */
void ADXL362_SetRange(unsigned char gRange);
/**
 * @brief Select the measurement range on an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] gRange ADXL362_RANGE_* selector.
 */
void ADXL362_SetRangeDevice(const TagAdxl362Device *device,
                            unsigned char gRange);

/**
 * @brief Select the output data rate on the default ADXL362.
 *
 * @param[in] outRate ADXL362_ODR_* selector.
 */
void ADXL362_SetOutputRate(unsigned char outRate);
/**
 * @brief Select the output data rate on an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] outRate ADXL362_ODR_* selector.
 */
void ADXL362_SetOutputRateDevice(const TagAdxl362Device *device,
                                 unsigned char outRate);

/**
 * @brief Read raw X/Y/Z samples from the default ADXL362.
 *
 * @param[out] x X-axis raw sample.
 * @param[out] y Y-axis raw sample.
 * @param[out] z Z-axis raw sample.
 */
void ADXL362_GetXyz(short *x, short *y, short *z);
/**
 * @brief Read raw X/Y/Z samples from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] x X-axis raw sample.
 * @param[out] y Y-axis raw sample.
 * @param[out] z Z-axis raw sample.
 */
void ADXL362_GetXyzDevice(const TagAdxl362Device *device, short *x, short *y,
                          short *z);

/**
 * @brief Read acceleration in g from the default ADXL362.
 *
 * @param[out] x X-axis acceleration in g.
 * @param[out] y Y-axis acceleration in g.
 * @param[out] z Z-axis acceleration in g.
 */
void ADXL362_GetGxyz(float* x, float* y, float* z);
/**
 * @brief Read acceleration in g from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[out] x X-axis acceleration in g.
 * @param[out] y Y-axis acceleration in g.
 * @param[out] z Z-axis acceleration in g.
 */
void ADXL362_GetGxyzDevice(const TagAdxl362Device *device, float *x, float *y,
                           float *z);

/**
 * @brief Read temperature from the default ADXL362.
 *
 * @return Temperature in degrees Celsius.
 */
float ADXL362_ReadTemperature(void);
/**
 * @brief Read temperature from an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @return Temperature in degrees Celsius.
 */
float ADXL362_ReadTemperatureDevice(const TagAdxl362Device *device);

/**
 * @brief Configure FIFO mode on the default ADXL362.
 *
 * @param[in] mode ADXL362_FIFO_* mode selector.
 * @param[in] waterMarkLvl FIFO watermark sample count.
 * @param[in] enTempRead Nonzero to include temperature samples in FIFO.
 */
void ADXL362_FifoSetup(unsigned char  mode,
                       unsigned short waterMarkLvl,
                       unsigned char  enTempRead);
/**
 * @brief Configure FIFO mode on an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] mode ADXL362_FIFO_* mode selector.
 * @param[in] waterMarkLvl FIFO watermark sample count.
 * @param[in] enTempRead Nonzero to include temperature samples in FIFO.
 */
void ADXL362_FifoSetupDevice(const TagAdxl362Device *device,
                             unsigned char mode, unsigned short waterMarkLvl,
                             unsigned char enTempRead);

/**
 * @brief Configure activity detection on the default ADXL362.
 *
 * @param[in] refOrAbs 0 for absolute mode, nonzero for referenced mode.
 * @param[in] threshold Activity threshold.
 * @param[in] time Activity timer value in output-data-rate ticks.
 */
void ADXL362_SetupActivityDetection(unsigned char  refOrAbs,
                                    unsigned short threshold,
                                    unsigned char  time);
/**
 * @brief Configure activity detection on an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] refOrAbs 0 for absolute mode, nonzero for referenced mode.
 * @param[in] threshold Activity threshold.
 * @param[in] time Activity timer value in output-data-rate ticks.
 */
void ADXL362_SetupActivityDetectionDevice(const TagAdxl362Device *device,
                                          unsigned char refOrAbs,
                                          unsigned short threshold,
                                          unsigned char time);

/**
 * @brief Configure inactivity detection on the default ADXL362.
 *
 * @param[in] refOrAbs 0 for absolute mode, nonzero for referenced mode.
 * @param[in] threshold Inactivity threshold.
 * @param[in] time Inactivity timer value in output-data-rate ticks.
 */
void ADXL362_SetupInactivityDetection(unsigned char  refOrAbs,
                                      unsigned short threshold,
                                      unsigned short time);
/**
 * @brief Configure inactivity detection on an ADXL362 descriptor.
 *
 * @param[in] device ADXL362 descriptor.
 * @param[in] refOrAbs 0 for absolute mode, nonzero for referenced mode.
 * @param[in] threshold Inactivity threshold.
 * @param[in] time Inactivity timer value in output-data-rate ticks.
 */
void ADXL362_SetupInactivityDetectionDevice(const TagAdxl362Device *device,
                                            unsigned char refOrAbs,
                                            unsigned short threshold,
                                            unsigned short time);
/** @} */

#endif /* __ADXL362_ORIGINAL_H__ */
