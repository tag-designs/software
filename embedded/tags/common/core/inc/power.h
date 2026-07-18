/**
 * @file power.h
 * @brief Common power, standby-pin, and legacy power hook declarations.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef TAG_CORE_POWER_H
#define TAG_CORE_POWER_H

#include "hal.h"
#include "i2c_bus.h"
#include "spi_bus.h"
#include "usart_bus.h"

#include <stdbool.h>
#include <stdint.h>

#define TAG_NO_LINE ((ioline_t)0)

/** @name Shared low-power helpers
 * Utilities used by bus/device layers and Stop2 entry to validate optional
 * board lines, suspend active buses, and configure standby pull state.
 * @{
 */
/**
 * @brief Test whether an optional board line is populated.
 *
 * @param[in] line PAL line value to validate.
 * @return true when line names a real board signal.
 */
bool tagLineIsValid(ioline_t line);

/**
 * @brief Suspend active SPI and USART buses before Stop2 entry.
 */
void tagDisableActiveBusesForStop(void);

/**
 * @brief Resume buses that were suspended for Stop2 entry.
 */
void tagEnableActiveBusesAfterStop(void);

/**
 * @brief Enable the STM32 standby pull-up for a valid board line.
 *
 * @param[in] line PAL line to bias during standby.
 */
void tagEnableStandbyPullup(ioline_t line);

/**
 * @brief Enable the STM32 standby pull-down for a valid board line.
 *
 * @param[in] line PAL line to bias during standby.
 */
void tagEnableStandbyPulldown(ioline_t line);

/**
 * @brief Clear retained STM32 standby pull-up and pull-down selections.
 */
void tagClearStandbyPulls(void);

/**
 * @brief Give tag devices a chance to prepare for standby.
 */
void tagPrepareDevicesForStandby(void);
/** @} */

/** @name RTC power hooks
 * Legacy-compatible names for bringing the shared RTC I2C bus up and down.
 * @{
 */
/**
 * @brief I2C controller used by the current shared RTC bus binding.
 */
extern const TagI2cController tagRtcI2cController;

/**
 * @brief Initialize power/RTC bus runtime state.
 */
void tagPowerInit(void);

/**
 * @brief Power and begin the shared RTC bus session.
 */
void rtcOn(void);

/**
 * @brief End the shared RTC bus session and remove device power.
 */
void rtcOff(void);
/** @} */

/** @name Legacy device power hooks
 * Declarations retained for older tag-local code while newer devices use the
 * compile-time bus/device descriptors.
 * @{
 */
/**
 * @brief Legacy SPI1 power-on hook implemented by older tags.
 */
void spi1On(void);

/**
 * @brief Legacy SPI1 power-off hook implemented by older tags.
 */
void spi1Off(void);

/**
 * @brief Legacy accelerometer SPI power-on hook implemented by older tags.
 */
void accelSpiOn(void);

/**
 * @brief Legacy accelerometer SPI power-off hook implemented by older tags.
 */
void accelSpiOff(void);

/**
 * @brief Configure the legacy accelerometer FIFO.
 *
 * @param[in] entries Requested FIFO watermark or entry count.
 * @return Tag-specific status code.
 */
int accelConfigureFIFO(int entries);
extern uint8_t adxl_status;

/**
 * @brief Legacy accelerometer power-on hook implemented by older tags.
 */
void accelOn(void);

/**
 * @brief Legacy accelerometer power-off hook implemented by older tags.
 */
void accelOff(void);
/** @} */

#endif
