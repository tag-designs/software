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

typedef void (*TagSoftI2cDelay)(void);

typedef struct {
  bool addr10;
  ioline_t scl;
  ioline_t sda;
  TagSoftI2cDelay delay;
} TagSoftI2cConfig;

typedef struct {
  i2cstate_t state;
  const TagSoftI2cConfig *config;
  i2cflags_t errors;
  systime_t start;
  systime_t end;
} TagSoftI2cDriver;

void tagSoftI2cObjectInit(TagSoftI2cDriver *driver);
msg_t tagSoftI2cStart(TagSoftI2cDriver *driver,
                      const TagSoftI2cConfig *config);
void tagSoftI2cStop(TagSoftI2cDriver *driver);
i2cflags_t tagSoftI2cGetErrors(TagSoftI2cDriver *driver);
msg_t tagSoftI2cMasterTransmitTimeout(TagSoftI2cDriver *driver,
                                      i2caddr_t addr,
                                      const uint8_t *txbuf, size_t txbytes,
                                      uint8_t *rxbuf, size_t rxbytes,
                                      sysinterval_t timeout);

/** @} */

#endif
