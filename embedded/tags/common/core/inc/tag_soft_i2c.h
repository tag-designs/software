/*
    ChibiOS - Copyright (C) 2006-2026 Giovanni Di Sirio.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file tag_soft_i2c.h
 * @brief Project-namespaced software I2C backend copied from ChibiOS fallback.
 * @author tag firmware authors
 * @date 2026-07-18
 */

#ifndef TAG_CORE_TAG_SOFT_I2C_H
#define TAG_CORE_TAG_SOFT_I2C_H

#include "hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @name Software I2C backend
 * Namespaced copy of the ChibiOS fallback I2C transaction engine.
 * @{
 */

/**
 * @brief Delay callback used to time software-I2C bit transitions.
 */
typedef void (*TagSoftI2cDelay)(void);

/**
 * @brief Software-I2C bus pin and timing configuration.
 */
typedef struct {
  bool addr10;              ///< true when 10-bit addressing is enabled.
  ioline_t scl;             ///< Clock line driven by the software backend.
  ioline_t sda;             ///< Data line driven/read by the software backend.
  TagSoftI2cDelay delay;    ///< Bit-timing delay callback.
} TagSoftI2cConfig;

/**
 * @brief Runtime state for one software-I2C controller instance.
 */
typedef struct {
  i2cstate_t state;              ///< ChibiOS-compatible driver state.
  const TagSoftI2cConfig *config;///< Active configuration while started.
  i2cflags_t errors;             ///< Sticky ChibiOS-compatible error flags.
  systime_t start;               ///< Timestamp captured at transaction start.
  systime_t end;                 ///< Timestamp captured at transaction end.
} TagSoftI2cDriver;

/**
 * @brief Initialize a software-I2C driver object before first use.
 *
 * @param[out] driver Driver object to initialize.
 */
void tagSoftI2cObjectInit(TagSoftI2cDriver *driver);

/**
 * @brief Start a software-I2C driver with a pin/timing configuration.
 *
 * @param[in,out] driver Driver object to start.
 * @param[in] config Bus pin and delay configuration.
 * @return MSG_OK on success.
 */
msg_t tagSoftI2cStart(TagSoftI2cDriver *driver,
                      const TagSoftI2cConfig *config);

/**
 * @brief Stop a software-I2C driver and release its active configuration.
 *
 * @param[in,out] driver Driver object to stop.
 */
void tagSoftI2cStop(TagSoftI2cDriver *driver);

/**
 * @brief Attempt to release a stuck software-I2C bus by toggling SCL.
 *
 * @param[in] config Bus pin configuration to clear.
 */
void tagSoftI2cBusClear(const TagSoftI2cConfig *config);

/**
 * @brief Return and clear the driver's accumulated I2C error flags.
 *
 * @param[in,out] driver Driver object to query.
 * @return ChibiOS-compatible I2C error flags.
 */
i2cflags_t tagSoftI2cGetErrors(TagSoftI2cDriver *driver);

/**
 * @brief Perform one software-I2C write/read transaction with timeout.
 *
 * @param[in,out] driver Started software-I2C driver.
 * @param[in] addr I2C target address.
 * @param[in] txbuf Optional transmit buffer.
 * @param[in] txbytes Number of transmit bytes.
 * @param[out] rxbuf Optional receive buffer.
 * @param[in] rxbytes Number of receive bytes.
 * @param[in] timeout ChibiOS timeout interval.
 * @return MSG_OK on success, MSG_TIMEOUT on timeout, or MSG_RESET on bus error.
 */
msg_t tagSoftI2cMasterTransmitTimeout(TagSoftI2cDriver *driver,
                                      i2caddr_t addr,
                                      const uint8_t *txbuf, size_t txbytes,
                                      uint8_t *rxbuf, size_t rxbytes,
                                      sysinterval_t timeout);

/** @} */

#endif
