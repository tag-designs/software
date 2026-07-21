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
 * @file tag_soft_i2c.c
 * @brief Project-namespaced software I2C backend copied from ChibiOS fallback.
 * @author tag firmware authors
 * @date 2026-07-18
 */

#include "tag_soft_i2c.h"

#define CHECK_ERROR(msg)                                                     \
  do                                                                         \
  {                                                                          \
    if ((msg) < (msg_t)0)                                                    \
    {                                                                        \
      palSetLine(driver->config->sda);                                       \
      palSetLine(driver->config->scl);                                       \
      driver->state = I2C_LOCKED;                                            \
      return MSG_TIMEOUT;                                                    \
    }                                                                        \
  } while (false)

static msg_t tagSoftI2cWriteStop(TagSoftI2cDriver *driver);

static inline void tagSoftI2cDelay(TagSoftI2cDriver *driver)
{
  if (driver->config->delay)
  {
    driver->config->delay();
  }
  else
  {
    __NOP();
  }
}

static inline msg_t tagSoftI2cCheckArbitration(TagSoftI2cDriver *driver)
{
  if (palReadLine(driver->config->sda) == PAL_LOW)
  {
    driver->errors |= I2C_ARBITRATION_LOST;
    return MSG_RESET;
  }

  return MSG_OK;
}

static inline msg_t tagSoftI2cCheckTimeout(TagSoftI2cDriver *driver)
{
  if ((driver->start != driver->end) &&
      (!osalTimeIsInRangeX(osalOsGetSystemTimeX(), driver->start,
                           driver->end)))
  {
    tagSoftI2cWriteStop(driver);
    return MSG_TIMEOUT;
  }

  return MSG_OK;
}

static msg_t tagSoftI2cWaitClock(TagSoftI2cDriver *driver)
{
  while (palReadLine(driver->config->scl) == PAL_LOW)
  {
    if ((driver->start != driver->end) &&
        (!osalTimeIsInRangeX(osalOsGetSystemTimeX(), driver->start,
                             driver->end)))
    {
      return MSG_TIMEOUT;
    }
    tagSoftI2cDelay(driver);
  }

  return MSG_OK;
}

static inline msg_t tagSoftI2cWriteStart(TagSoftI2cDriver *driver)
{
  CHECK_ERROR(tagSoftI2cCheckArbitration(driver));

  palClearLine(driver->config->sda);
  tagSoftI2cDelay(driver);
  palClearLine(driver->config->scl);

  return MSG_OK;
}

static msg_t tagSoftI2cWriteRestart(TagSoftI2cDriver *driver)
{
  palSetLine(driver->config->sda);
  tagSoftI2cDelay(driver);
  palSetLine(driver->config->scl);

  CHECK_ERROR(tagSoftI2cWaitClock(driver));

  tagSoftI2cDelay(driver);
  tagSoftI2cWriteStart(driver);

  return MSG_OK;
}

static msg_t tagSoftI2cWriteStop(TagSoftI2cDriver *driver)
{
  palClearLine(driver->config->sda);
  tagSoftI2cDelay(driver);
  palSetLine(driver->config->scl);

  CHECK_ERROR(tagSoftI2cWaitClock(driver));

  tagSoftI2cDelay(driver);
  palSetLine(driver->config->sda);
  tagSoftI2cDelay(driver);

  CHECK_ERROR(tagSoftI2cCheckArbitration(driver));

  tagSoftI2cDelay(driver);

  return MSG_OK;
}

static msg_t tagSoftI2cWriteBit(TagSoftI2cDriver *driver, unsigned bit)
{
  palWriteLine(driver->config->sda, bit);
  tagSoftI2cDelay(driver);
  palSetLine(driver->config->scl);
  tagSoftI2cDelay(driver);

  CHECK_ERROR(tagSoftI2cWaitClock(driver));

  if (bit == PAL_HIGH)
  {
    CHECK_ERROR(tagSoftI2cCheckArbitration(driver));
  }

  palClearLine(driver->config->scl);

  return MSG_OK;
}

static msg_t tagSoftI2cReadBit(TagSoftI2cDriver *driver)
{
  msg_t bit;

  palSetLine(driver->config->sda);
  tagSoftI2cDelay(driver);
  palSetLine(driver->config->scl);

  CHECK_ERROR(tagSoftI2cWaitClock(driver));

  tagSoftI2cDelay(driver);
  bit = palReadLine(driver->config->sda);
  palClearLine(driver->config->scl);

  return bit;
}

static msg_t tagSoftI2cWriteByte(TagSoftI2cDriver *driver, uint8_t byte)
{
  msg_t msg;

  CHECK_ERROR(tagSoftI2cCheckTimeout(driver));

  for (uint8_t mask = 0x80U; mask > 0U; mask >>= 1U)
  {
    CHECK_ERROR(tagSoftI2cWriteBit(driver, (byte & mask) != 0U));
  }

  msg = tagSoftI2cReadBit(driver);
  CHECK_ERROR(msg);

  if (msg == PAL_HIGH)
  {
    driver->errors |= I2C_ACK_FAILURE;
    return MSG_RESET;
  }

  return MSG_OK;
}

static msg_t tagSoftI2cReadByte(TagSoftI2cDriver *driver, unsigned nack)
{
  msg_t byte = 0;

  CHECK_ERROR(tagSoftI2cCheckTimeout(driver));

  for (unsigned i = 0; i < 8U; i++)
  {
    msg_t msg = tagSoftI2cReadBit(driver);
    CHECK_ERROR(msg);
    byte = (byte << 1U) | msg;
  }

  CHECK_ERROR(tagSoftI2cWriteBit(driver, nack));

  return byte;
}

static msg_t tagSoftI2cWriteHeader(TagSoftI2cDriver *driver, i2caddr_t addr,
                                   bool read)
{
  if (driver->config->addr10)
  {
    uint8_t b1 = (uint8_t)(0xF0U | ((addr >> 8U) << 1U));
    uint8_t b2 = (uint8_t)(addr & 255U);

    if (read)
    {
      b1 |= 1U;
    }
    CHECK_ERROR(tagSoftI2cWriteByte(driver, b1));
    CHECK_ERROR(tagSoftI2cWriteByte(driver, b2));
  }
  else
  {
    CHECK_ERROR(tagSoftI2cWriteByte(driver,
                                    (uint8_t)((addr << 1U) | (read ? 1U : 0U))));
  }

  return MSG_OK;
}

void tagSoftI2cObjectInit(TagSoftI2cDriver *driver)
{
  osalDbgCheck(driver != NULL);

  driver->state = I2C_STOP;
  driver->config = NULL;
  driver->errors = I2C_NO_ERROR;
  driver->start = 0;
  driver->end = 0;
}

msg_t tagSoftI2cStart(TagSoftI2cDriver *driver,
                      const TagSoftI2cConfig *config)
{
  osalDbgCheck((driver != NULL) && (config != NULL));

  osalDbgAssert((driver->state == I2C_STOP) ||
                    (driver->state == I2C_READY) ||
                    (driver->state == I2C_LOCKED),
                "invalid state");
  driver->config = config;
  driver->state = I2C_READY;

  return MSG_OK;
}

void tagSoftI2cStop(TagSoftI2cDriver *driver)
{
  osalDbgCheck(driver != NULL);

  osalDbgAssert((driver->state == I2C_STOP) ||
                    (driver->state == I2C_READY) ||
                    (driver->state == I2C_LOCKED),
                "invalid state");
  driver->config = NULL;
  driver->state = I2C_STOP;
}

void tagSoftI2cBusClear(const TagSoftI2cConfig *config)
{
  osalDbgCheck(config != NULL);

  palSetLine(config->sda);
  palSetLine(config->scl);
  if (config->delay) {
    config->delay();
  }

  for (unsigned i = 0; (i < 9U) && (palReadLine(config->sda) == PAL_LOW); i++) {
    palClearLine(config->scl);
    if (config->delay) {
      config->delay();
    }
    palSetLine(config->scl);
    if (config->delay) {
      config->delay();
    }
  }

  palClearLine(config->sda);
  if (config->delay) {
    config->delay();
  }
  palSetLine(config->scl);
  if (config->delay) {
    config->delay();
  }
  palSetLine(config->sda);
  if (config->delay) {
    config->delay();
  }
}

i2cflags_t tagSoftI2cGetErrors(TagSoftI2cDriver *driver)
{
  osalDbgCheck(driver != NULL);

  return driver->errors;
}

msg_t tagSoftI2cMasterTransmitTimeout(TagSoftI2cDriver *driver,
                                      i2caddr_t addr,
                                      const uint8_t *txbuf, size_t txbytes,
                                      uint8_t *rxbuf, size_t rxbytes,
                                      sysinterval_t timeout)
{
  msg_t result;

  osalDbgCheck((driver != NULL) &&
               (txbytes > 0U) && (txbuf != NULL) &&
               ((rxbytes == 0U) || ((rxbytes > 0U) && (rxbuf != NULL))) &&
               (timeout != TIME_IMMEDIATE));
  osalDbgAssert(driver->state == I2C_READY, "not ready");

  driver->errors = I2C_NO_ERROR;
  driver->state = I2C_ACTIVE_TX;
  driver->start = osalOsGetSystemTimeX();
  driver->end = driver->start;
  if (timeout != TIME_INFINITE)
  {
    driver->end = osalTimeAddX(driver->start, timeout);
  }

  CHECK_ERROR(tagSoftI2cWriteStart(driver));
  CHECK_ERROR(tagSoftI2cWriteHeader(driver, addr, false));

  do
  {
    CHECK_ERROR(tagSoftI2cWriteByte(driver, *txbuf++));
  } while (--txbytes);

  if (rxbytes > 0U)
  {
    CHECK_ERROR(tagSoftI2cWriteRestart(driver));
    CHECK_ERROR(tagSoftI2cWriteHeader(driver, addr, true));

    do
    {
      msg_t msg = tagSoftI2cReadByte(driver, rxbytes > 1U ? 0U : 1U);
      CHECK_ERROR(msg);
      *rxbuf++ = (uint8_t)msg;
    } while (--rxbytes);
  }

  result = tagSoftI2cWriteStop(driver);
  driver->state = (result == MSG_TIMEOUT) ? I2C_LOCKED : I2C_READY;

  return result;
}
