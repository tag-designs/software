/**
 * @file rtc_device.c
 * @brief Default RTC descriptor binding and register transaction helpers.
 * @author tag firmware authors
 * @date 2026-05-23
 */

#include "rtc_device.h"

#include "custom.h"
#include "debug_log.h"

#ifndef TAG_RTC_TRANSFER_DIAGNOSTICS
#define TAG_RTC_TRANSFER_DIAGNOSTICS 0
#endif

#if defined(TAG_RTC_RV3028)
#include "power.h"
#include "rv3028.h"

#define RTC_TIMEOUT 100

#if !defined(TAG_RTC_I2C_DELAY_CYCLES)
#if defined(STM32U3XX)
#define TAG_RTC_I2C_DELAY_CYCLES 64U
#else
#define TAG_RTC_I2C_DELAY_CYCLES 1U
#endif
#endif

/**
 * @brief Software-I2C delay callback used by the default RTC I2C binding.
 */
static void delay(void)
{
#if TAG_RTC_I2C_DELAY_CYCLES == 0U
  return;
#elif TAG_RTC_I2C_DELAY_CYCLES == 1U
  __NOP();
#else
  for (uint32_t i = 0; i < TAG_RTC_I2C_DELAY_CYCLES; i++)
  {
    __NOP();
  }
#endif
}

static const TagSoftI2cConfig rtc_i2c_config = {
    .delay = delay,
#if defined(SWAP_I2C) && SWAP_I2C
    .sda = LINE_RTC_SCL,
    .scl = LINE_RTC_SDA,
#else
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
#endif
};

static TagSoftI2cDriver I2CRTC;
static binary_semaphore_t I2CRTCmutex;

const TagI2cController __attribute__((weak)) tagRtcI2cController = {
    .backend = TAG_I2C_BACKEND_SOFTWARE,
    .mutex = &I2CRTCmutex,
    .driver.software = &I2CRTC,
};

/** @name Default RV3028 device binding
 * Default RV3028 device binding.
 *
 * The RTC driver itself is parameterized by TagRtcDevice. Most tags use the
 * shared software-I2C RTC bus owned by common power code, so the common module
 * provides a weak descriptor here. A tag with unusual RTC wiring can override
 * tagRtcDevice() locally without carrying a private copy of the RV3028
 * register driver.
 * @{
 */
static const TagI2cDevice rtc_i2c = {
    .controller = &tagRtcI2cController,
    .config.software = &rtc_i2c_config,
    .sda = LINE_RTC_SDA,
    .scl = LINE_RTC_SCL,
    .pwr = TAG_NO_LINE,
    .address = RV3028_ADR,
    .timeout = RTC_TIMEOUT,
    .sleep_policy = TAG_I2C_SLEEP_PULLUP,
};

static const TagRtcDevice rtc_device = {
    .registers = &rtc_i2c,
    .device_on = rtcOn,
    .device_off = rtcOff,
};

/**
 * @brief Return the default RV3028 RTC descriptor.
 *
 * @return Immutable RTC descriptor for the default software-I2C RV3028 binding.
 */
const TagRtcDevice *__attribute__((weak)) tagRtcDevice(void)
{
  return &rtc_device;
}

/**
 * @brief Initialize runtime state for the default RV3028 RTC bus binding.
 */
void __attribute__((weak)) tagRtcDeviceRuntimeInit(void)
{
  chBSemObjectInit(&I2CRTCmutex, false);
  tagI2cControllerObjectInit(rtc_i2c.controller);
}
/** @} */
#endif

/** @name RTC transaction helpers
 * Descriptor-based helpers used by chip drivers to bracket register access with
 * the tag's RTC power/bus policy.
 * @{
 */
/**
 * @brief Begin an RTC transaction by enabling the device/bus binding.
 *
 * @param[in] device RTC device descriptor.
 */
void tagRtcDeviceBegin(const TagRtcDevice *device)
{
  if (device->device_on)
    device->device_on();
}

/**
 * @brief End an RTC transaction by disabling the device/bus binding.
 *
 * @param[in] device RTC device descriptor.
 */
void tagRtcDeviceEnd(const TagRtcDevice *device)
{
  if (device->device_off)
    device->device_off();
}

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
                       uint32_t len)
{
#if TAG_RTC_TRANSFER_DIAGNOSTICS
  const TagI2cDevice *registers = device->registers;
  int ret = tagI2cReadRegister(registers, reg, buf, len);

  if (ret != MSG_OK)
  {
    debug_log_printf(
        "rv3028 i2c read reg=0x%02x len=%lu ret=%d sda=%u scl=%u\r\n",
        reg, (unsigned long)len, ret,
        (unsigned)palReadLine(registers->sda),
        (unsigned)palReadLine(registers->scl));
  }

  return ret;
#else
  return tagI2cReadRegister(device->registers, reg, buf, len);
#endif
}

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
                        const uint8_t *buf, uint32_t len)
{
#if TAG_RTC_TRANSFER_DIAGNOSTICS
  const TagI2cDevice *registers = device->registers;
  int ret = tagI2cWriteRegister(registers, reg, buf, len);

  if (ret != MSG_OK)
  {
    debug_log_printf(
        "rv3028 i2c write reg=0x%02x len=%lu ret=%d sda=%u scl=%u\r\n",
        reg, (unsigned long)len, ret,
        (unsigned)palReadLine(registers->sda),
        (unsigned)palReadLine(registers->scl));
  }

  return ret;
#else
  return tagI2cWriteRegister(device->registers, reg, buf, len);
#endif
}
/** @} */
