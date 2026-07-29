/**
 * @file rtc_device.h
 * @brief Descriptor-backed RTC register bus and transaction helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_RTC_DEVICE_H
#define TAG_RTC_DEVICE_H

#include "sensor_io.h"

#include <stdint.h>

/** @name RTC device descriptor
 * RTC device descriptor.
 *
 * The RV3028/RV3032/RV8803 drivers own chip register sequences. This
 * descriptor supplies the tag-specific I2C register bus and the power/bus
 * callbacks needed before and after each RTC transaction.
 * @{
 */
/**
 * @typedef TagRtcPower
 * @brief Optional RTC power/session callback.
 *
 * Implementations normally assert or deassert a board-specific RTC power rail
 * or bus switch. The callback takes no arguments because the active descriptor
 * already owns the callback binding.
 */
typedef void (*TagRtcPower)(void);

/**
 * @struct TagRtcDevice
 * @brief Board binding for one RTC register device.
 *
 * @details The descriptor supplies the I2C register transport plus optional
 *          power callbacks used to bracket every RTC transaction. Chip drivers
 *          consume this descriptor through tagRtcReadRegister() and
 *          tagRtcWriteRegister() rather than touching the bus directly.
 */
typedef struct {
  const TagI2cDevice *registers; ///< I2C register device for the RTC chip.
  TagRtcPower device_on;         ///< Optional callback before register access.
  TagRtcPower device_off;        ///< Optional callback after register access.
} TagRtcDevice;
/** @} */

/** @name RTC descriptor binding
 * Tag/family code may override the weak descriptor binding when RTC wiring is
 * not the default.
 * @{
 */
/**
 * @brief Return the configured RTC device descriptor.
 *
 * @return Immutable RTC device descriptor for the current tag.
 */
const TagRtcDevice *tagRtcDevice(void);

/**
 * @brief Initialize runtime state for the configured RTC bus binding.
 */
void tagRtcDeviceRuntimeInit(void);
/** @} */

/** @name RTC transaction helpers
 * Helpers bracket chip register access with the descriptor's power/session
 * callbacks and adapt RTC register IO to sensor_io I2C helpers.
 * @{
 */
/**
 * @brief Begin an RTC transaction by enabling the device/bus binding.
 *
 * @param[in] device RTC device descriptor.
 */
void tagRtcDeviceBegin(const TagRtcDevice *device);

/**
 * @brief End an RTC transaction by disabling the device/bus binding.
 *
 * @param[in] device RTC device descriptor.
 */
void tagRtcDeviceEnd(const TagRtcDevice *device);

/**
 * @brief Read consecutive RTC registers.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] reg First register address.
 * @param[out] buf Destination buffer for register bytes.
 * @param[in] len Number of bytes to read.
 * @return MSG_OK on success or a bus error.
 */
int tagRtcReadRegister(const TagRtcDevice *device, uint8_t reg, uint8_t *buf,
                       uint32_t len);

/**
 * @brief Write consecutive RTC registers.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] reg First register address.
 * @param[in] buf Source buffer for register bytes.
 * @param[in] len Number of bytes to write.
 * @return MSG_OK on success or a bus error.
 */
int tagRtcWriteRegister(const TagRtcDevice *device, uint8_t reg,
                        const uint8_t *buf, uint32_t len);
/** @} */

#endif
