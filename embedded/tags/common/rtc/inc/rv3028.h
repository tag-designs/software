/**
 * @file rv3028.h
 * @brief Micro Crystal RV3028 RTC register definitions and driver API.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#ifndef RV3028_H
#define RV3028_H

#include "rtc_device.h"

#include <stdbool.h>
#include <stdint.h>

/** @name RV3028 registers and bit definitions
 * Register addresses and status/control bits used by the RV3028 driver.
 * @{
 */
#define RV3028_ADR (0x52)

#define BIT(x) (1<<(x))

enum RV3028Reg {
    RV3028_SEC,  // 0x00
    RV3028_MIN,  // 0x01
    RV3028_HOUR, // 0x02
    RV3028_WDAY, // 0x03
    RV3028_DAY,  // 0x04
    RV3028_MONTH, // 0x05
    RV3028_YEAR, // 0x06
    RV3028_ALARM_MIN, // 0x07
    RV3028_ALARM_HOUR, // 0x08
    RV3028_ALARM_DAY, // 0x09
    RV3028_STATUS = 0x0E, // 0x0E
    RV3028_CTRL1 = 0x0F,
    RV3028_CTRL2 = 0x10, // 0x10
    RV3028_EVT_CTRL = 0x13, // 0x13
    RV3028_TS_COUNT = 0x14,// 0x14
    RV3028_TS_SEC = 0x15, // 0x15
    RV3028_UNIX_TIME0 = 0x1B,  // 0x1B
    RV3028_RAM1 = 0x1F,        // 0x1F
    RV3028_EEPROM_ADDR = 0x25, // 0x25
    RV3028_EEPROM_DATA = 0x26, // 0x26
    RV3028_EEPROM_CMD =0x27, // 0x27
// EEPROM RAM mirror
    RV3028_EEPROM_PWENABLE = 0x30,
    RV3028_CLKOUT = 0x35,
    RV3028_OFFSET = 0x36,
    RV3028_BACKUP = 0x37
};


#define RV3028_STATUS_PORF BIT(0)
#define RV3028_STATUS_EVF BIT(1)
#define RV3028_STATUS_AF BIT(2)
#define RV3028_STATUS_TF BIT(3)
#define RV3028_STATUS_UF BIT(4)
#define RV3028_STATUS_BSF BIT(5)
#define RV3028_STATUS_CLKF BIT(6)
#define RV3028_STATUS_EEBUSY BIT(7)
#define RV3028_CTRL1_EERD BIT(3)
#define RV3028_CTRL1_WADA BIT(5)
#define RV3028_CTRL2_RESET BIT(0)
#define RV3028_CTRL2_12_24 BIT(1)
#define RV3028_CTRL2_EIE BIT(2)
#define RV3028_CTRL2_AIE BIT(3)
#define RV3028_CTRL2_TIE BIT(4)
#define RV3028_CTRL2_UIE BIT(5)
#define RV3028_CTRL2_TSE BIT(7)
#define RV3028_EVT_CTRL_TSR BIT(2)

#define RV3028_EEPROM_CMD_WRITE		0x21
#define RV3028_EEPROM_CMD_READ		0x22
#define RV3028_EEPROM_CMD_REFRESH   0x12
/** @} */

/** @name RV3028 driver API
 * Descriptor-based accessors used by the generic tag RTC wrapper.
 * @{
 */
/**
 * @brief Read one or more RV3028 registers.
 *
 * @param[in] device RTC device descriptor that owns the register bus.
 * @param[in] reg First RV3028 register to read.
 * @param[out] val Destination buffer for register bytes.
 * @param[in] num Number of bytes to read.
 * @return MSG_OK on success or a bus error.
 */
int rv3028GetReg(const TagRtcDevice *device, enum RV3028Reg reg,
                 uint8_t *val, int num);

/**
 * @brief Initialize the RV3028 oscillator output and STM32 RTC dividers.
 *
 * @param[in] device RTC device descriptor.
 * @return true when the RTC configuration is usable.
 */
bool rv3028Init(const TagRtcDevice *device);

/**
 * @brief Write date/time to the RV3028.
 *
 * @param[in] device RTC device descriptor.
 * @param[in] tm RTC date/time structure to write.
 * @return MSG_OK on success or a driver-specific error.
 */
msg_t rv3028SetDateTime(const TagRtcDevice *device, RTCDateTime *tm);

/**
 * @brief Read date/time from the RV3028.
 *
 * @param[in] device RTC device descriptor.
 * @param[out] tm RTC date/time structure to populate.
 * @return MSG_OK on success or a driver-specific error.
 */
msg_t rv3028GetDateTime(const TagRtcDevice *device, RTCDateTime *tm);
/** @} */

#endif
