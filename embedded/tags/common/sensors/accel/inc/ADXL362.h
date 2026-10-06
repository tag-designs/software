/**
 * @file    ADXL362.h
 * @brief   ADXL362 accelerometer register map and descriptor-backed driver API.
 * @author  tag firmware authors
 * @date    2026-10-06
 *
 * @details The register map below is written from the ADXL362 data sheet
 *          (Analog Devices): its SPI command set, register map table, and
 *          per-register bit descriptions. Macro names follow the register and
 *          field names the data sheet uses, prefixed ADXL362_, and are
 *          unchanged from the names the tag firmware has always used.
 *
 *          The ADXL362 talks SPI only. Every transaction starts with a
 *          command byte (ADXL362_WRITE_REG, ADXL362_READ_REG or
 *          ADXL362_READ_FIFO); register transactions follow it with a start
 *          address and auto-increment through consecutive registers.
 *
 *          Multi-byte quantities are little-endian: the _L register holds the
 *          low byte and the _H register the high bits.
 */

#ifndef ADXL362_H
#define ADXL362_H

#include "bus_device.h"

#include <stdbool.h>

/** @name SPI command bytes
 * The first byte of every SPI transaction.
 * @{
 */
#define ADXL362_WRITE_REG           0x0A  /**< Write registers from an address. */
#define ADXL362_READ_REG            0x0B  /**< Read registers from an address. */
#define ADXL362_WRITE_FIFO          0x0D  /**< FIFO access command (the FIFO is read-only). */
#define ADXL362_READ_FIFO           ADXL362_WRITE_FIFO  /**< Read from the FIFO. */
/** @} */

/** @name Register addresses
 * @{
 */
#define ADXL362_REG_DEVID_AD            0x00  /**< Analog Devices ID, reads ADXL362_DEVICE_AD. */
#define ADXL362_REG_DEVID_MST           0x01  /**< MEMS ID, reads ADXL362_DEVICE_MST. */
#define ADXL362_REG_PARTID              0x02  /**< Part ID, reads ADXL362_PART_ID. */
#define ADXL362_REG_REVID               0x03  /**< Silicon revision. */
#define ADXL362_REG_XDATA               0x08  /**< X axis, 8 most significant bits. */
#define ADXL362_REG_YDATA               0x09  /**< Y axis, 8 most significant bits. */
#define ADXL362_REG_ZDATA               0x0A  /**< Z axis, 8 most significant bits. */
#define ADXL362_REG_STATUS              0x0B  /**< Status flags; see ADXL362_STATUS_*. */
#define ADXL362_REG_FIFO_L              0x0C  /**< FIFO entry count, low byte. */
#define ADXL362_REG_FIFO_H              0x0D  /**< FIFO entry count, high bits. */
#define ADXL362_REG_XDATA_L             0x0E  /**< X axis, 12-bit sample, low byte. */
#define ADXL362_REG_XDATA_H             0x0F  /**< X axis, 12-bit sample, high bits. */
#define ADXL362_REG_YDATA_L             0x10  /**< Y axis, 12-bit sample, low byte. */
#define ADXL362_REG_YDATA_H             0x11  /**< Y axis, 12-bit sample, high bits. */
#define ADXL362_REG_ZDATA_L             0x12  /**< Z axis, 12-bit sample, low byte. */
#define ADXL362_REG_ZDATA_H             0x13  /**< Z axis, 12-bit sample, high bits. */
#define ADXL362_REG_TEMP_L              0x14  /**< Temperature, low byte. */
#define ADXL362_REG_TEMP_H              0x15  /**< Temperature, high bits. */
#define ADXL362_REG_SOFT_RESET          0x1F  /**< Write ADXL362_RESET_KEY to reset. */
#define ADXL362_REG_THRESH_ACT_L        0x20  /**< Activity threshold, low byte. */
#define ADXL362_REG_THRESH_ACT_H        0x21  /**< Activity threshold, high bits. */
#define ADXL362_REG_TIME_ACT            0x22  /**< Activity time, in samples. */
#define ADXL362_REG_THRESH_INACT_L      0x23  /**< Inactivity threshold, low byte. */
#define ADXL362_REG_THRESH_INACT_H      0x24  /**< Inactivity threshold, high bits. */
#define ADXL362_REG_TIME_INACT_L        0x25  /**< Inactivity time, low byte. */
#define ADXL362_REG_TIME_INACT_H        0x26  /**< Inactivity time, high byte. */
#define ADXL362_REG_ACT_INACT_CTL       0x27  /**< Activity/inactivity control; see ADXL362_ACT_INACT_CTL_*. */
#define ADXL362_REG_FIFO_CTL            0x28  /**< FIFO control; see ADXL362_FIFO_CTL_*. */
#define ADXL362_REG_FIFO_SAMPLES        0x29  /**< FIFO watermark, low 8 bits. */
#define ADXL362_REG_INTMAP1             0x2A  /**< INT1 routing; see ADXL362_INTMAP1_*. */
#define ADXL362_REG_INTMAP2             0x2B  /**< INT2 routing; see ADXL362_INTMAP2_*. */
#define ADXL362_REG_FILTER_CTL          0x2C  /**< Range, bandwidth and data rate; see ADXL362_FILTER_CTL_*. */
#define ADXL362_REG_POWER_CTL           0x2D  /**< Power and measurement mode; see ADXL362_POWER_CTL_*. */
#define ADXL362_REG_SELF_TEST           0x2E  /**< Self test; see ADXL362_SELF_TEST_ST. */
/** @} */

/** @name STATUS register bits
 * @{
 */
#define ADXL362_STATUS_ERR_USER_REGS        (1 << 7)  /**< Configuration registers corrupted (SEU). */
#define ADXL362_STATUS_AWAKE                (1 << 6)  /**< Awake (activity seen, not since inactive). */
#define ADXL362_STATUS_INACT                (1 << 5)  /**< Inactivity detected. */
#define ADXL362_STATUS_ACT                  (1 << 4)  /**< Activity detected. */
#define ADXL362_STATUS_FIFO_OVERRUN         (1 << 3)  /**< FIFO overran; samples were lost. */
#define ADXL362_STATUS_FIFO_WATERMARK       (1 << 2)  /**< FIFO holds at least the watermark. */
#define ADXL362_STATUS_FIFO_RDY             (1 << 1)  /**< FIFO holds at least one sample. */
#define ADXL362_STATUS_DATA_RDY             (1 << 0)  /**< New sample available. */
/** @} */

/** @name ACT_INACT_CTL register fields
 * @{
 */
#define ADXL362_ACT_INACT_CTL_LINKLOOP(x)   (((x) & 0x3) << 4)  /**< Linking mode, bits 5:4; ADXL362_MODE_*. */
#define ADXL362_ACT_INACT_CTL_INACT_REF     (1 << 3)  /**< Inactivity referenced (not absolute). */
#define ADXL362_ACT_INACT_CTL_INACT_EN      (1 << 2)  /**< Inactivity detection enabled. */
#define ADXL362_ACT_INACT_CTL_ACT_REF       (1 << 1)  /**< Activity referenced (not absolute). */
#define ADXL362_ACT_INACT_CTL_ACT_EN        (1 << 0)  /**< Activity detection enabled. */
/** @} */

/** @name Linking modes for ADXL362_ACT_INACT_CTL_LINKLOOP()
 * @{
 */
#define ADXL362_MODE_DEFAULT        0  /**< Activity and inactivity independent. */
#define ADXL362_MODE_LINK           1  /**< Linked: each must be acknowledged. */
#define ADXL362_MODE_LOOP           3  /**< Loop: linked and self-acknowledging. */
/** @} */

/** @name FIFO_CTL register fields
 * @{
 */
#define ADXL362_FIFO_CTL_AH                 (1 << 3)  /**< Watermark bit 8 (above half). */
#define ADXL362_FIFO_CTL_FIFO_TEMP          (1 << 2)  /**< Store temperature with each sample. */
#define ADXL362_FIFO_CTL_FIFO_MODE(x)       (((x) & 0x3) << 0)  /**< FIFO mode, bits 1:0; ADXL362_FIFO_*. */
/** @} */

/** @name FIFO modes for ADXL362_FIFO_CTL_FIFO_MODE()
 * @{
 */
#define ADXL362_FIFO_DISABLE              0  /**< FIFO off. */
#define ADXL362_FIFO_OLDEST_SAVED         1  /**< Keep the oldest samples; stop when full. */
#define ADXL362_FIFO_STREAM               2  /**< Keep the newest samples; overwrite when full. */
#define ADXL362_FIFO_TRIGGERED            3  /**< Keep samples around an INT trigger. */
/** @} */

/** @name INTMAP1 register bits
 * Each set bit routes that status condition to the INT1 pin.
 * @{
 */
#define ADXL362_INTMAP1_INT_LOW             (1 << 7)  /**< INT1 active low. */
#define ADXL362_INTMAP1_AWAKE               (1 << 6)  /**< Awake state. */
#define ADXL362_INTMAP1_INACT               (1 << 5)  /**< Inactivity. */
#define ADXL362_INTMAP1_ACT                 (1 << 4)  /**< Activity. */
#define ADXL362_INTMAP1_FIFO_OVERRUN        (1 << 3)  /**< FIFO overrun. */
#define ADXL362_INTMAP1_FIFO_WATERMARK      (1 << 2)  /**< FIFO watermark. */
#define ADXL362_INTMAP1_FIFO_READY          (1 << 1)  /**< FIFO ready. */
#define ADXL362_INTMAP1_DATA_READY          (1 << 0)  /**< Data ready. */
/** @} */

/** @name INTMAP2 register bits
 * Each set bit routes that status condition to the INT2 pin.
 * @{
 */
#define ADXL362_INTMAP2_INT_LOW             (1 << 7)  /**< INT2 active low. */
#define ADXL362_INTMAP2_AWAKE               (1 << 6)  /**< Awake state. */
#define ADXL362_INTMAP2_INACT               (1 << 5)  /**< Inactivity. */
#define ADXL362_INTMAP2_ACT                 (1 << 4)  /**< Activity. */
#define ADXL362_INTMAP2_FIFO_OVERRUN        (1 << 3)  /**< FIFO overrun. */
#define ADXL362_INTMAP2_FIFO_WATERMARK      (1 << 2)  /**< FIFO watermark. */
#define ADXL362_INTMAP2_FIFO_READY          (1 << 1)  /**< FIFO ready. */
#define ADXL362_INTMAP2_DATA_READY          (1 << 0)  /**< Data ready. */
/** @} */

/** @name FILTER_CTL register fields
 * @{
 */
#define ADXL362_FILTER_CTL_RANGE(x)         (((x) & 0x3) << 6)  /**< Measurement range, bits 7:6; ADXL362_RANGE_*. */
#define ADXL362_FILTER_CTL_RES              (1 << 5)  /**< Reserved. */
#define ADXL362_FILTER_CTL_HALF_BW          (1 << 4)  /**< Anti-alias bandwidth ODR/4 instead of ODR/2. */
#define ADXL362_FILTER_CTL_EXT_SAMPLE       (1 << 3)  /**< Sample on the INT2 pin. */
#define ADXL362_FILTER_CTL_ODR(x)           (((x) & 0x7) << 0)  /**< Output data rate, bits 2:0; ADXL362_ODR_*. */
/** @} */

/** @name Measurement ranges for ADXL362_FILTER_CTL_RANGE()
 * @{
 */
#define ADXL362_RANGE_2G                0  /**< +/-2 g. */
#define ADXL362_RANGE_4G                1  /**< +/-4 g. */
#define ADXL362_RANGE_8G                2  /**< +/-8 g. */
/** @} */

/** @name Output data rates for ADXL362_FILTER_CTL_ODR()
 * @{
 */
#define ADXL362_ODR_12_5_HZ             0  /**< 12.5 Hz. */
#define ADXL362_ODR_25_HZ               1  /**< 25 Hz. */
#define ADXL362_ODR_50_HZ               2  /**< 50 Hz. */
#define ADXL362_ODR_100_HZ              3  /**< 100 Hz. */
#define ADXL362_ODR_200_HZ              4  /**< 200 Hz. */
#define ADXL362_ODR_400_HZ              5  /**< 400 Hz. */
/** @} */

/** @name POWER_CTL register fields
 * @{
 */
#define ADXL362_POWER_CTL_RES               (1 << 7)  /**< Reserved. */
#define ADXL362_POWER_CTL_EXT_CLK           (1 << 6)  /**< Clock from the INT1 pin. */
#define ADXL362_POWER_CTL_LOW_NOISE(x)      (((x) & 0x3) << 4)  /**< Noise mode, bits 5:4; ADXL362_NOISE_MODE_*. */
#define ADXL362_POWER_CTL_WAKEUP            (1 << 3)  /**< Wake-up (very low power) mode. */
#define ADXL362_POWER_CTL_AUTOSLEEP         (1 << 2)  /**< Autosleep after inactivity. */
#define ADXL362_POWER_CTL_MEASURE(x)        (((x) & 0x3) << 0)  /**< Measurement mode, bits 1:0; ADXL362_MEASURE_*. */
/** @} */

/** @name Noise modes for ADXL362_POWER_CTL_LOW_NOISE()
 * @{
 */
#define ADXL362_NOISE_MODE_NORMAL           0  /**< Normal operation. */
#define ADXL362_NOISE_MODE_LOW              1  /**< Low noise. */
#define ADXL362_NOISE_MODE_ULTRALOW         2  /**< Ultralow noise. */
/** @} */

/** @name Measurement modes for ADXL362_POWER_CTL_MEASURE()
 * @{
 */
#define ADXL362_MEASURE_STANDBY         0  /**< Standby. */
#define ADXL362_MEASURE_ON              2  /**< Measuring. */
/** @} */

/** @name SELF_TEST register bits
 * @{
 */
#define ADXL362_SELF_TEST_ST            (1 << 0)  /**< Apply the self-test force. */
/** @} */

/** @name Identity and reset values
 * @{
 */
#define ADXL362_DEVICE_AD               0xAD  /**< Expected DEVID_AD. */
#define ADXL362_DEVICE_MST              0x1D  /**< Expected DEVID_MST. */
#define ADXL362_PART_ID                 0xF2  /**< Expected PARTID. */
#define ADXL362_RESET_KEY               0x52  /**< SOFT_RESET value that resets the part ('R'). */
/** @} */

/**
 * @struct TagAdxl362Device
 * @brief Board binding for one ADXL362 accelerometer instance.
 */
typedef struct {
  TagBusDevice bus; ///< Physical bus binding and low-power policy.
} TagAdxl362Device;

/** @name ADXL362 descriptor helpers
 * Helpers that bind the ADXL362 register protocol to the compile-time tag bus
 * descriptor.
 * @{
 */
/**
 * @brief Return the SPI descriptor embedded in an ADXL362 device.
 *
 * @param[in] device ADXL362 device descriptor.
 * @return SPI device descriptor used for ADXL362 transfers.
 */
static inline const TagSpiDevice *tagAdxl362SpiDevice(const TagAdxl362Device *device)
{
  return tagBusSpiDevice(&device->bus);
}

/**
 * @brief Return the configured ADXL362 device descriptor.
 *
 * @return Immutable ADXL362 device descriptor for the current tag.
 */
const TagAdxl362Device *tagAdxl362Device(void);

/**
 * @brief Power and begin the bus session for an ADXL362 device.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_DeviceBegin(const TagAdxl362Device *device);

/**
 * @brief End the bus session and power down an ADXL362 device.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_DeviceEnd(const TagAdxl362Device *device);

/**
 * @brief Run the ADXL362 identity and self-test sequence.
 *
 * @param[in] device ADXL362 device descriptor.
 * @return true when identity and self-test pass.
 */
bool adxl362Test(const TagAdxl362Device *device);
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
char ADXL362_InitDevice(const TagAdxl362Device *device);

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
                                    unsigned char bytesNumber);

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
                                    unsigned char bytesNumber);

/**
 * @brief Read bytes from the ADXL362 FIFO.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] pBuffer Destination buffer.
 * @param[in] bytesNumber Number of FIFO bytes to read.
 */
void ADXL362_GetFifoValueDevice(const TagAdxl362Device *device,
                                unsigned char *pBuffer,
                                unsigned short bytesNumber);

/**
 * @brief Reset the ADXL362 through its software reset register.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_SoftwareResetDevice(const TagAdxl362Device *device);

/**
 * @brief Put an ADXL362 device into standby and clear wake interrupt routing.
 *
 * @param[in] device ADXL362 device descriptor.
 */
void ADXL362_DeinitDevice(const TagAdxl362Device *device);

/**
 * @brief Place the ADXL362 into standby or measurement mode.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] pwrMode Nonzero to measure, zero for standby.
 */
void ADXL362_SetPowerModeDevice(const TagAdxl362Device *device,
                                unsigned char pwrMode);

/**
 * @brief Select the ADXL362 measurement range.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] gRange ADXL362 range selector.
 */
void ADXL362_SetRangeDevice(const TagAdxl362Device *device,
                            unsigned char gRange);

/**
 * @brief Select the ADXL362 output data rate.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] outRate ADXL362 ODR selector.
 */
void ADXL362_SetOutputRateDevice(const TagAdxl362Device *device,
                                 unsigned char outRate);

/**
 * @brief Read raw X/Y/Z acceleration.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] x Raw X sample.
 * @param[out] y Raw Y sample.
 * @param[out] z Raw Z sample.
 */
void ADXL362_GetXyzDevice(const TagAdxl362Device *device, short *x, short *y,
                          short *z);

/**
 * @brief Read acceleration and convert to g.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[out] x X acceleration in g.
 * @param[out] y Y acceleration in g.
 * @param[out] z Z acceleration in g.
 */
void ADXL362_GetGxyzDevice(const TagAdxl362Device *device, float *x, float *y,
                           float *z);

/**
 * @brief Read and convert the ADXL362 temperature channel.
 *
 * @param[in] device ADXL362 device descriptor.
 * @return Temperature in degrees Celsius.
 */
float ADXL362_ReadTemperatureDevice(const TagAdxl362Device *device);

/**
 * @brief Configure ADXL362 FIFO mode and watermark.
 *
 * @param[in] device ADXL362 device descriptor.
 * @param[in] mode FIFO mode selector.
 * @param[in] waterMarkLvl FIFO watermark level.
 * @param[in] enTempRead Nonzero to include temperature samples.
 */
void ADXL362_FifoSetupDevice(const TagAdxl362Device *device,
                             unsigned char mode, unsigned short waterMarkLvl,
                             unsigned char enTempRead);

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
                                          unsigned char time);

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
                                            unsigned short time);
/** @} */

#endif /* ADXL362_H */
