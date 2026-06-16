/***************************************************************************//**
 *   @file   ADXL367.h
 *   @brief  Descriptor-backed ADXL367 accelerometer register driver.
 *   @author tag firmware authors
 *   @date   2026-06-16
*******************************************************************************/

#ifndef __ADXL367_H__
#define __ADXL367_H__

#include "bus_device.h"

#include <stdbool.h>
#include <stdint.h>

/******************************************************************************/
/********************************* ADXL367 ************************************/
/******************************************************************************/

/* ADXL367 communication commands */
#define ADXL367_WRITE_REG               0x0A
#define ADXL367_READ_REG                0x0B
#define ADXL367_READ_FIFO               0x0D

/* Registers */
#define ADXL367_REG_DEVID_AD            0x00
#define ADXL367_REG_DEVID_MST           0x01
#define ADXL367_REG_PARTID              0x02
#define ADXL367_REG_REVID               0x03
#define ADXL367_REG_SERIAL_NUMBER_3     0x04
#define ADXL367_REG_SERIAL_NUMBER_2     0x05
#define ADXL367_REG_SERIAL_NUMBER_1     0x06
#define ADXL367_REG_SERIAL_NUMBER_0     0x07
#define ADXL367_REG_XDATA               0x08
#define ADXL367_REG_YDATA               0x09
#define ADXL367_REG_ZDATA               0x0A
#define ADXL367_REG_STATUS              0x0B
#define ADXL367_REG_FIFO_ENTRIES_L      0x0C
#define ADXL367_REG_FIFO_ENTRIES_H      0x0D
#define ADXL367_REG_XDATA_H             0x0E
#define ADXL367_REG_XDATA_L             0x0F
#define ADXL367_REG_YDATA_H             0x10
#define ADXL367_REG_YDATA_L             0x11
#define ADXL367_REG_ZDATA_H             0x12
#define ADXL367_REG_ZDATA_L             0x13
#define ADXL367_REG_TEMP_H              0x14
#define ADXL367_REG_TEMP_L              0x15
#define ADXL367_REG_EX_ADC_H            0x16
#define ADXL367_REG_EX_ADC_L            0x17
#define ADXL367_REG_I2C_FIFO_DATA       0x18
#define ADXL367_REG_SOFT_RESET          0x1F
#define ADXL367_REG_THRESH_ACT_H        0x20
#define ADXL367_REG_THRESH_ACT_L        0x21
#define ADXL367_REG_TIME_ACT            0x22
#define ADXL367_REG_THRESH_INACT_H      0x23
#define ADXL367_REG_THRESH_INACT_L      0x24
#define ADXL367_REG_TIME_INACT_H        0x25
#define ADXL367_REG_TIME_INACT_L        0x26
#define ADXL367_REG_ACT_INACT_CTL       0x27
#define ADXL367_REG_FIFO_CONTROL        0x28
#define ADXL367_REG_FIFO_SAMPLES        0x29
#define ADXL367_REG_INTMAP1_LWR         0x2A
#define ADXL367_REG_INTMAP2_LWR         0x2B
#define ADXL367_REG_FILTER_CTL          0x2C
#define ADXL367_REG_POWER_CTL           0x2D
#define ADXL367_REG_SELF_TEST           0x2E
#define ADXL367_REG_TAP_THRESH          0x2F
#define ADXL367_REG_TAP_DUR             0x30
#define ADXL367_REG_TAP_LATENT          0x31
#define ADXL367_REG_TAP_WINDOW          0x32
#define ADXL367_REG_X_OFFSET            0x33
#define ADXL367_REG_Y_OFFSET            0x34
#define ADXL367_REG_Z_OFFSET            0x35
#define ADXL367_REG_X_SENS              0x36
#define ADXL367_REG_Y_SENS              0x37
#define ADXL367_REG_Z_SENS              0x38
#define ADXL367_REG_TIMER_CTL           0x39
#define ADXL367_REG_INTMAP1_UPPER       0x3A
#define ADXL367_REG_INTMAP2_UPPER       0x3B
#define ADXL367_REG_ADC_CTL             0x3C
#define ADXL367_REG_TEMP_CTL            0x3D
#define ADXL367_REG_TEMP_ADC_OV_TH_H    0x3E
#define ADXL367_REG_TEMP_ADC_OV_TH_L    0x3F
#define ADXL367_REG_TEMP_ADC_UN_TH_H    0x40
#define ADXL367_REG_TEMP_ADC_UN_TH_L    0x41
#define ADXL367_REG_TEMP_ADC_TIMER      0x42
#define ADXL367_REG_AXIS_MASK           0x43
#define ADXL367_REG_STATUS_COPY         0x44
#define ADXL367_REG_STATUS_2            0x45

/* ADXL367_REG_STATUS definitions */
#define ADXL367_STATUS_ERR_USER_REGS    (1 << 7)
#define ADXL367_STATUS_AWAKE            (1 << 6)
#define ADXL367_STATUS_INACT            (1 << 5)
#define ADXL367_STATUS_ACT              (1 << 4)
#define ADXL367_STATUS_FIFO_OVERRUN     (1 << 3)
#define ADXL367_STATUS_FIFO_WATERMARK   (1 << 2)
#define ADXL367_STATUS_FIFO_RDY         (1 << 1)
#define ADXL367_STATUS_DATA_RDY         (1 << 0)

/* ADXL367_REG_ACT_INACT_CTL definitions */
#define ADXL367_ACT_INACT_CTL_LINKLOOP(x)   (((x) & 0x3) << 4)
#define ADXL367_ACT_INACT_CTL_INACT_EN(x)   (((x) & 0x3) << 2)
#define ADXL367_ACT_INACT_CTL_ACT_EN(x)     (((x) & 0x3) << 0)

/* ADXL367_ACT_INACT_CTL_LINKLOOP(x) options */
#define ADXL367_MODE_DEFAULT            0
#define ADXL367_MODE_LINK               1
#define ADXL367_MODE_LOOP               3

/* ADXL367 activity/inactivity enable options */
#define ADXL367_NO_ACTIVITY_DETECTION          0
#define ADXL367_ACTIVITY_ENABLE                1
#define ADXL367_REFERENCED_ACTIVITY_ENABLE     3
#define ADXL367_NO_INACTIVITY_DETECTION        0
#define ADXL367_INACTIVITY_ENABLE              1
#define ADXL367_REFERENCED_INACTIVITY_ENABLE   3

/* ADXL367_REG_FIFO_CONTROL */
#define ADXL367_FIFO_CONTROL_FIFO_CHANNEL(x)   (((x) & 0xF) << 3)
#define ADXL367_FIFO_CONTROL_FIFO_SAMPLES      (1 << 2)
#define ADXL367_FIFO_CONTROL_FIFO_MODE(x)      (((x) & 0x3) << 0)

/* ADXL367_FIFO_CONTROL_FIFO_CHANNEL(x) options */
#define ADXL367_ALL_AXIS                0x0
#define ADXL367_X_AXIS                  0x1
#define ADXL367_Y_AXIS                  0x2
#define ADXL367_Z_AXIS                  0x3
#define ADXL367_ALL_AXIS_TEMP           0x4
#define ADXL367_X_AXIS_TEMP             0x5
#define ADXL367_Y_AXIS_TEMP             0x6
#define ADXL367_Z_AXIS_TEMP             0x7
#define ADXL367_ALL_AXIS_EXT_ADC        0x8
#define ADXL367_X_AXIS_EXT_ADC          0x9
#define ADXL367_Y_AXIS_EXT_ADC          0xA
#define ADXL367_Z_AXIS_EXT_ADC          0xB

/* ADXL367_FIFO_CONTROL_FIFO_MODE(x) options */
#define ADXL367_FIFO_DISABLE            0
#define ADXL367_FIFO_OLDEST_SAVED       1
#define ADXL367_FIFO_STREAM             2
#define ADXL367_FIFO_TRIGGERED          3

/* ADXL367_REG_INTMAP1_LWR */
#define ADXL367_INTMAP1_INT_LOW         (1 << 7)
#define ADXL367_INTMAP1_AWAKE           (1 << 6)
#define ADXL367_INTMAP1_INACT           (1 << 5)
#define ADXL367_INTMAP1_ACT             (1 << 4)
#define ADXL367_INTMAP1_FIFO_OVERRUN    (1 << 3)
#define ADXL367_INTMAP1_FIFO_WATERMARK  (1 << 2)
#define ADXL367_INTMAP1_FIFO_READY      (1 << 1)
#define ADXL367_INTMAP1_DATA_READY      (1 << 0)

/* ADXL367_REG_INTMAP2_LWR */
#define ADXL367_INTMAP2_INT_LOW         (1 << 7)
#define ADXL367_INTMAP2_AWAKE           (1 << 6)
#define ADXL367_INTMAP2_INACT           (1 << 5)
#define ADXL367_INTMAP2_ACT             (1 << 4)
#define ADXL367_INTMAP2_FIFO_OVERRUN    (1 << 3)
#define ADXL367_INTMAP2_FIFO_WATERMARK  (1 << 2)
#define ADXL367_INTMAP2_FIFO_READY      (1 << 1)
#define ADXL367_INTMAP2_DATA_READY      (1 << 0)

/* ADXL367_REG_FILTER_CTL definitions */
#define ADXL367_FILTER_CTL_RANGE(x)     (((x) & 0x3) << 6)
#define ADXL367_FILTER_CTL_I2C_HS       (1 << 5)
#define ADXL367_FILTER_CTL_RES          (1 << 4)
#define ADXL367_FILTER_CTL_EXT_SAMPLE   (1 << 3)
#define ADXL367_FILTER_CTL_ODR(x)       (((x) & 0x7) << 0)

/* ADXL367_FILTER_CTL_RANGE(x) options */
#define ADXL367_RANGE_2G                0 /* +/-2 g */
#define ADXL367_RANGE_4G                1 /* +/-4 g */
#define ADXL367_RANGE_8G                2 /* +/-8 g */

/* ADXL367_FILTER_CTL_ODR(x) options */
#define ADXL367_ODR_12_5_HZ             0 /* 12.5 Hz */
#define ADXL367_ODR_25_HZ               1 /* 25 Hz */
#define ADXL367_ODR_50_HZ               2 /* 50 Hz */
#define ADXL367_ODR_100_HZ              3 /* 100 Hz */
#define ADXL367_ODR_200_HZ              4 /* 200 Hz */
#define ADXL367_ODR_400_HZ              5 /* 400 Hz */

/* Historical aliases used by older ADXL367 code. */
#define ADXL367_ODR_12P5HZ              ADXL367_ODR_12_5_HZ
#define ADXL367_ODR_25HZ                ADXL367_ODR_25_HZ
#define ADXL367_ODR_50HZ                ADXL367_ODR_50_HZ
#define ADXL367_ODR_100HZ               ADXL367_ODR_100_HZ
#define ADXL367_ODR_200HZ               ADXL367_ODR_200_HZ
#define ADXL367_ODR_400HZ               ADXL367_ODR_400_HZ

/* ADXL367_REG_POWER_CTL definitions */
#define ADXL367_POWER_CTL_RES           (1 << 7)
#define ADXL367_POWER_CTL_EXT_CLK       (1 << 6)
#define ADXL367_POWER_CTL_LOW_NOISE(x)  (((x) & 0x3) << 4)
#define ADXL367_POWER_CTL_WAKEUP        (1 << 3)
#define ADXL367_POWER_CTL_AUTOSLEEP     (1 << 2)
#define ADXL367_POWER_CTL_MEASURE(x)    (((x) & 0x3) << 0)

/* ADXL367_POWER_CTL_LOW_NOISE(x) options */
#define ADXL367_NOISE_MODE_NORMAL       0
#define ADXL367_NOISE_MODE_LOW          1
#define ADXL367_NOISE_MODE_ULTRALOW     2

/* ADXL367_POWER_CTL_MEASURE(x) options */
#define ADXL367_MEASURE_STANDBY         0
#define ADXL367_MEASURE_ON              2

/* Historical aliases used by older ADXL367 code. */
#define ADXL367_OP_STANDBY              ADXL367_MEASURE_STANDBY
#define ADXL367_OP_MEASURE              ADXL367_MEASURE_ON

/* ADXL367_REG_SELF_TEST */
#define ADXL367_SELF_TEST_ST_FORCE      (1 << 1)
#define ADXL367_SELF_TEST_ST            (1 << 0)

/* ADXL367 device information */
#define ADXL367_DEVICE_AD               0xAD
#define ADXL367_DEVICE_MST              0x1D
#define ADXL367_PART_ID                 0xF7

/* ADXL367 Reset settings */
#define ADXL367_RESET_KEY               0x52

/******************************************************************************/
/************************ Functions Declarations ******************************/
/******************************************************************************/

typedef struct {
  TagBusDevice bus;
} TagAdxl367Device;

static inline const TagSpiDevice *tagAdxl367SpiDevice(const TagAdxl367Device *device)
{
  return tagBusSpiDevice(&device->bus);
}

const TagAdxl367Device *tagAdxl367Device(void);

void ADXL367_DeviceBegin(const TagAdxl367Device *device);
void ADXL367_DeviceEnd(const TagAdxl367Device *device);
bool adxl367Test(const TagAdxl367Device *device);

char ADXL367_InitDevice(const TagAdxl367Device *device);
void ADXL367_SetRegisterValueDevice(const TagAdxl367Device *device,
                                    unsigned short registerValue,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber);
void ADXL367_GetRegisterValueDevice(const TagAdxl367Device *device,
                                    unsigned char *pReadData,
                                    unsigned char registerAddress,
                                    unsigned char bytesNumber);
void ADXL367_GetFifoValueDevice(const TagAdxl367Device *device,
                                unsigned char *pBuffer,
                                unsigned short bytesNumber);
void ADXL367_SoftwareResetDevice(const TagAdxl367Device *device);
void ADXL367_DeinitDevice(const TagAdxl367Device *device);
void ADXL367_SetPowerModeDevice(const TagAdxl367Device *device,
                                unsigned char pwrMode);
void ADXL367_SetRangeDevice(const TagAdxl367Device *device,
                            unsigned char gRange);
void ADXL367_SetOutputRateDevice(const TagAdxl367Device *device,
                                 unsigned char outRate);
void ADXL367_GetXyzDevice(const TagAdxl367Device *device, short *x, short *y,
                          short *z);
void ADXL367_GetGxyzDevice(const TagAdxl367Device *device, float *x, float *y,
                           float *z);
float ADXL367_ReadTemperatureDevice(const TagAdxl367Device *device);
void ADXL367_FifoSetupDevice(const TagAdxl367Device *device,
                             unsigned char mode, unsigned short waterMarkLvl,
                             unsigned char enTempRead);
void ADXL367_SetupActivityDetectionDevice(const TagAdxl367Device *device,
                                          unsigned char refOrAbs,
                                          unsigned short threshold,
                                          unsigned char time);
void ADXL367_SetupInactivityDetectionDevice(const TagAdxl367Device *device,
                                            unsigned char refOrAbs,
                                            unsigned short threshold,
                                            unsigned short time);

#endif /* __ADXL367_H__ */
